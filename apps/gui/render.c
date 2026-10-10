/* 3D cube rendering (with layer-turn animation) and the 2D facelet net. */
#include <math.h>

#include "gui.h"
#include "raymath.h"
#include "rlgl.h"

#define FOVY 32.0f
#define BODY_SIZE 0.96f
#define STICKER_OFFSET 0.483f
#define STICKER_HALF 0.41f

static Vector3 g_pos[NUM_FACELETS], g_normal[NUM_FACELETS];
static int g_ipos[NUM_FACELETS][3];
static bool g_geom_ready;

static void init_geometry(void) {
    if (g_geom_ready) return;
    for (int i = 0; i < NUM_FACELETS; i++) {
        int p[3], n[3];
        facelet_geometry(i, p, n);
        for (int k = 0; k < 3; k++) g_ipos[i][k] = p[k];
        g_pos[i] = (Vector3){(float)p[0], (float)p[1], (float)p[2]};
        g_normal[i] = (Vector3){(float)n[0], (float)n[1], (float)n[2]};
    }
    g_geom_ready = true;
}

/* Vertical field of view that keeps the cube inside narrow views too. */
static float view_fovy(Rectangle view) {
    float aspect = view.width / view.height;
    if (aspect >= 1) return FOVY;
    return 2.0f * atanf(tanf(FOVY * 0.5f * DEG2RAD) / aspect) * RAD2DEG;
}

static Camera3D make_camera(const App *a, Rectangle view) {
    Camera3D cam = {0};
    cam.position = (Vector3){a->dist * cosf(a->pitch) * sinf(a->yaw), a->dist * sinf(a->pitch),
                             a->dist * cosf(a->pitch) * cosf(a->yaw)};
    cam.target = (Vector3){0, 0, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = view_fovy(view);
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

/* The four corners of a sticker, in a consistent order. */
static void sticker_quad(int i, Vector3 out[4], float half) {
    Vector3 n = g_normal[i];
    Vector3 c = Vector3Add(g_pos[i], Vector3Scale(n, STICKER_OFFSET));
    /* tangents: the two axes the normal is not along */
    Vector3 u = fabsf(n.x) > 0.5f ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
    Vector3 v = Vector3CrossProduct(n, u);
    u = Vector3Scale(u, half);
    v = Vector3Scale(v, half);
    out[0] = Vector3Subtract(Vector3Subtract(c, u), v);
    out[1] = Vector3Subtract(Vector3Add(c, u), v);
    out[2] = Vector3Add(Vector3Add(c, u), v);
    out[3] = Vector3Add(Vector3Subtract(c, u), v);
}

static float ease(float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

static void push_layer_rotation(const App *a, const int pos[3]) {
    rlPushMatrix();
    if (!a->animating || !move_affects(a->anim.m, pos)) return;
    int axis, mask, quarter;
    move_geometry(a->anim.m, &axis, &mask, &quarter);
    float angle = 90.0f * (float)quarter * ease(a->anim_t);
    rlRotatef(angle, axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
}

static void draw_sticker(int i, Color col) {
    Vector3 q[4];
    sticker_quad(i, q, STICKER_HALF);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    for (int k = 0; k < 4; k++) rlVertex3f(q[k].x, q[k].y, q[k].z);
    rlEnd();
}

static void draw_sticker_outline(int i, Color col) {
    Vector3 q[4];
    sticker_quad(i, q, STICKER_HALF + 0.03f);
    Vector3 lift = Vector3Scale(g_normal[i], 0.004f);
    for (int k = 0; k < 4; k++) DrawLine3D(Vector3Add(q[k], lift), Vector3Add(q[(k + 1) % 4], lift), col);
}

int render_pick_sticker(App *a, Rectangle view, Vector2 mouse) {
    init_geometry();
    if (!CheckCollisionPointRec(mouse, view)) return -1;
    Camera3D cam = make_camera(a, view);
    Ray ray = GetScreenToWorldRayEx((Vector2){mouse.x - view.x, mouse.y - view.y}, cam, (int)view.width,
                                    (int)view.height);
    int best = -1;
    float best_d = 1e9f;
    for (int i = 0; i < NUM_FACELETS; i++) {
        Vector3 q[4];
        sticker_quad(i, q, 0.5f);
        RayCollision hit = GetRayCollisionQuad(ray, q[0], q[1], q[2], q[3]);
        if (hit.hit && hit.distance < best_d) best_d = hit.distance, best = i;
    }
    return best;
}

void render_cube3d(App *a, Rectangle view) {
    init_geometry();
    Camera3D cam = make_camera(a, view);
    float scale = (float)GetRenderWidth() / (float)GetScreenWidth();
    int screen_h = GetRenderHeight();

    /* a soft vignette behind the cube */
    DrawCircleGradient((int)(view.x + view.width / 2), (int)(view.y + view.height / 2),
                       fminf(view.width, view.height) * 0.55f, (Color){44, 48, 60, 255}, UI_BG);

    /* BeginMode3D always uses the whole window; set up a viewport for the view rectangle */
    rlDrawRenderBatchActive();
    rlViewport((int)(view.x * scale), (int)(screen_h - (view.y + view.height) * scale), (int)(view.width * scale),
               (int)(view.height * scale));
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    double top = RL_CULL_DISTANCE_NEAR * tan(cam.fovy * 0.5 * DEG2RAD);
    double right = top * (double)view.width / (double)view.height;
    rlFrustum(-right, right, -top, top, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    Matrix look = MatrixLookAt(cam.position, cam.target, cam.up);
    rlMultMatrixf(MatrixToFloat(look));
    rlEnableDepthTest();

    /* cubie bodies */
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            for (int z = -1; z <= 1; z++) {
                int p[3] = {x, y, z};
                push_layer_rotation(a, p);
                DrawCube((Vector3){(float)x, (float)y, (float)z}, BODY_SIZE, BODY_SIZE, BODY_SIZE,
                         (Color){16, 16, 18, 255});
                rlPopMatrix();
            }

    /* stickers */
    int hover = a->edit_mode ? render_pick_sticker(a, view, GetMousePosition()) : -1;
    if (hover >= 0 && hover % 9 != 4) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    rlDisableBackfaceCulling();
    for (int i = 0; i < NUM_FACELETS; i++) {
        push_layer_rotation(a, g_ipos[i]);
        Color col = STICKER_COLORS[a->cube.f[i]];
        if (i == hover && i % 9 != 4) col = ColorBrightness(col, 0.25f);
        draw_sticker(i, col);
        rlPopMatrix();
    }
    rlEnableBackfaceCulling();
    if (hover >= 0) draw_sticker_outline(hover, hover % 9 == 4 ? UI_DIM : UI_ACCENT);

    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlViewport(0, 0, GetRenderWidth(), screen_h);
    rlDisableDepthTest();

    /* face letters next to the visible centers */
    static const char *const LETTERS[NUM_FACES] = {"U", "R", "F", "D", "L", "B"};
    for (int f = 0; f < NUM_FACES; f++) {
        int i = f * 9 + 4;
        Vector3 out = Vector3Add(g_pos[i], Vector3Scale(g_normal[i], 1.25f));
        Vector3 to_cam = Vector3Subtract(cam.position, g_pos[i]);
        float facing = Vector3DotProduct(Vector3Normalize(to_cam), g_normal[i]);
        if (facing < 0.15f) continue;
        Vector2 sp = GetWorldToScreenEx(out, cam, (int)view.width, (int)view.height);
        Color c = Fade(UI_DIM, fminf(1.0f, (facing - 0.15f) * 3));
        float w = ui_text_width(a, LETTERS[f], 22);
        ui_text(a, LETTERS[f], view.x + sp.x - w / 2, view.y + sp.y - 11, 22, c);
    }
}

void render_net(App *a, Rectangle area, int *hover_out) {
    static const int ORIGIN[NUM_FACES][2] = {{3, 0}, {6, 3}, {3, 3}, {3, 6}, {0, 3}, {9, 3}}; /* U R F D L B */
    float cell = fminf(area.width / 12.0f, area.height / 9.0f);
    float ox = area.x + (area.width - cell * 12) / 2, oy = area.y + (area.height - cell * 9) / 2;
    Vector2 mouse = GetMousePosition();
    int hover = -1;
    for (int i = 0; i < NUM_FACELETS; i++) {
        int f = i / 9, r = i % 9 / 3, c = i % 3;
        Rectangle rc = {ox + (ORIGIN[f][0] + c) * cell, oy + (ORIGIN[f][1] + r) * cell, cell, cell};
        Rectangle inner = {rc.x + 1, rc.y + 1, rc.width - 2, rc.height - 2};
        bool over = CheckCollisionPointRec(mouse, rc);
        if (over) hover = i;
        Color col = STICKER_COLORS[a->cube.f[i]];
        DrawRectangleRec(rc, (Color){12, 12, 14, 255});
        DrawRectangleRounded(inner, 0.25f, 4, (a->edit_mode && over && r * 3 + c != 4) ? ColorBrightness(col, 0.25f) : col);
    }
    if (a->edit_mode && hover >= 0 && hover % 9 != 4) {
        a->cursor = MOUSE_CURSOR_POINTING_HAND;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->cube.f[hover] = (uint8_t)a->paint_color;
    }
    if (hover_out) *hover_out = hover;
}
