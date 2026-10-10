/* 3D cube rendering (with layer-turn animation) and the 2D facelet net. */
#include <math.h>

#include "gui.h"
#include "raymath.h"
#include "rlgl.h"

#define FOVY 32.0f
#define BODY_SIZE 0.96f
#define STICKER_OFFSET 0.483f
#define STICKER_HALF 0.41f
#define BODY_BEVEL 0.07f   /* flat face half-extent is BODY_SIZE/2 - BODY_BEVEL, matching STICKER_HALF */
#define BODY_ARC_STEPS 4   /* segments per half bevel arc, per face */
#define STICKER_CORNER 0.09f
#define STICKER_CORNER_STEPS 5
#define BODY_COLOR ((Color){30, 30, 34, 255})

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


/* ---- matte lit meshes: one beveled body and one rounded sticker, shared by every piece ---- */

static const char *const LIT_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragNormal = mat3(matNormal) * vertexNormal;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

/* Diffuse only: ambient + a key light + a weaker fill from the other side. Directions are in world space. */
static const char *const LIT_FS =
    "#version 330\n"
    "in vec3 fragNormal;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "const vec3 KEY = normalize(vec3(0.35, 0.8, 0.5));\n"
    "const vec3 FILL = normalize(vec3(-0.7, -0.3, -0.4));\n"
    "void main() {\n"
    "    vec3 n = normalize(fragNormal);\n"
    "    float light = 0.62 + 0.45 * max(dot(n, KEY), 0.0) + 0.25 * max(dot(n, FILL), 0.0);\n"
    "    finalColor = vec4(colDiffuse.rgb * min(light, 1.0), colDiffuse.a);\n"
    "}\n";

static Shader g_shader;
static Material g_material;
static Mesh g_body_mesh, g_sticker_mesh;
static bool g_lit_ready;

static Mesh build_body_mesh(void) {
    const float h = BODY_SIZE * 0.5f, r = BODY_BEVEL, flat = h - r;
    const int k = BODY_ARC_STEPS, n = 2 * k + 2; /* samples per axis: low arc, then high arc */
    float coord[2 * BODY_ARC_STEPS + 2];
    for (int i = 0; i <= k; i++) {
        coord[i] = -h + r * (float)i / (float)k;
        coord[k + 1 + i] = flat + r * (float)i / (float)k;
    }
    /* outward normal and tangents of each face, with u x v = normal so triangles wind counter-clockwise from outside */
    static const float FACES[6][3][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    Mesh m = {0};
    m.vertexCount = 6 * n * n;
    m.triangleCount = 6 * (n - 1) * (n - 1) * 2;
    m.vertices = MemAlloc((unsigned)m.vertexCount * 3 * sizeof(float));
    m.normals = MemAlloc((unsigned)m.vertexCount * 3 * sizeof(float));
    m.indices = MemAlloc((unsigned)m.triangleCount * 3 * sizeof(unsigned short));
    int vi = 0, ii = 0;
    for (int f = 0; f < 6; f++) {
        const float *fn = FACES[f][0], *fu = FACES[f][1], *fv = FACES[f][2];
        int base = vi;
        for (int a = 0; a < n; a++)
            for (int b = 0; b < n; b++) {
                float p[3], inner[3], d[3], len2 = 0;
                for (int c = 0; c < 3; c++) {
                    p[c] = fn[c] * h + fu[c] * coord[a] + fv[c] * coord[b];
                    inner[c] = fminf(fmaxf(p[c], -flat), flat);
                    d[c] = p[c] - inner[c];
                    len2 += d[c] * d[c];
                }
                float inv = 1.0f / sqrtf(len2);
                for (int c = 0; c < 3; c++) {
                    m.normals[vi * 3 + c] = d[c] * inv;
                    m.vertices[vi * 3 + c] = inner[c] + d[c] * inv * r;
                }
                vi++;
            }
        for (int a = 0; a + 1 < n; a++)
            for (int b = 0; b + 1 < n; b++) {
                unsigned short i0 = (unsigned short)(base + a * n + b), i1 = (unsigned short)(i0 + n);
                unsigned short quad[6] = {i0, i1, (unsigned short)(i1 + 1), i0, (unsigned short)(i1 + 1), (unsigned short)(i0 + 1)};
                for (int t = 0; t < 6; t++) m.indices[ii++] = quad[t];
            }
    }
    UploadMesh(&m, false);
    return m;
}

/* A rounded square in the local XY plane facing +Z, built as a fan from its center. */
static Mesh build_sticker_mesh(void) {
    const int per_corner = STICKER_CORNER_STEPS + 1, ring = 4 * per_corner;
    Mesh m = {0};
    m.vertexCount = 1 + ring;
    m.triangleCount = ring;
    m.vertices = MemAlloc((unsigned)m.vertexCount * 3 * sizeof(float));
    m.normals = MemAlloc((unsigned)m.vertexCount * 3 * sizeof(float));
    m.indices = MemAlloc((unsigned)m.triangleCount * 3 * sizeof(unsigned short));
    for (int v = 0; v < m.vertexCount; v++) m.normals[v * 3 + 2] = 1.0f;
    const float inset = STICKER_HALF - STICKER_CORNER;
    for (int c = 0; c < 4; c++) {
        float cx = (c == 0 || c == 3) ? inset : -inset, cy = c < 2 ? inset : -inset;
        for (int s = 0; s < per_corner; s++) {
            float ang = ((float)c + (float)s / (float)STICKER_CORNER_STEPS) * (PI * 0.5f);
            int v = 1 + c * per_corner + s;
            m.vertices[v * 3 + 0] = cx + STICKER_CORNER * cosf(ang);
            m.vertices[v * 3 + 1] = cy + STICKER_CORNER * sinf(ang);
        }
    }
    for (int t = 0; t < ring; t++) {
        m.indices[t * 3 + 0] = 0;
        m.indices[t * 3 + 1] = (unsigned short)(1 + t);
        m.indices[t * 3 + 2] = (unsigned short)(1 + (t + 1) % ring);
    }
    UploadMesh(&m, false);
    return m;
}

void render_init(void) {
    if (g_lit_ready) return;
    g_shader = LoadShaderFromMemory(LIT_VS, LIT_FS);
    if (!IsShaderValid(g_shader)) {
        TraceLog(LOG_WARNING, "RUBIKS: lit shader unavailable, drawing the cube with flat colors");
        return;
    }
    g_shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(g_shader, "matNormal");
    g_body_mesh = build_body_mesh();
    g_sticker_mesh = build_sticker_mesh();
    g_material = LoadMaterialDefault();
    g_material.shader = g_shader; /* the shader is unloaded once, in render_shutdown */
    g_lit_ready = true;
}

void render_shutdown(void) {
    if (!g_lit_ready) return;
    UnloadMesh(g_body_mesh);
    UnloadMesh(g_sticker_mesh);
    MemFree(g_material.maps);
    UnloadShader(g_shader);
    g_lit_ready = false;
}

static void draw_lit(Mesh mesh, Matrix model, Color col) {
    g_material.maps[MATERIAL_MAP_DIFFUSE].color = col;
    DrawMesh(mesh, g_material, model);
}

/* Local frame of a sticker: columns are the tangents, the outward normal and the center. */
static Matrix sticker_matrix(int i) {
    Vector3 n = g_normal[i];
    Vector3 c = Vector3Add(g_pos[i], Vector3Scale(n, STICKER_OFFSET));
    Vector3 u = fabsf(n.x) > 0.5f ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
    Vector3 v = Vector3CrossProduct(n, u);
    return (Matrix){u.x, v.x, n.x, c.x, u.y, v.y, n.y, c.y, u.z, v.z, n.z, c.z, 0, 0, 0, 1};
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
    float scale = (float)GetRenderWidth() / (float)GetScreenWidth() * a->ui_scale;
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

    int hover = a->edit_mode ? render_pick_sticker(a, view, GetMousePosition()) : -1;
    if (hover >= 0 && hover % 9 != 4) a->cursor = MOUSE_CURSOR_POINTING_HAND;

    /* cubie bodies */
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            for (int z = -1; z <= 1; z++) {
                int p[3] = {x, y, z};
                push_layer_rotation(a, p);
                if (g_lit_ready)
                    draw_lit(g_body_mesh, MatrixTranslate((float)x, (float)y, (float)z), BODY_COLOR);
                else
                    DrawCube((Vector3){(float)x, (float)y, (float)z}, BODY_SIZE, BODY_SIZE, BODY_SIZE,
                             (Color){16, 16, 18, 255});
                rlPopMatrix();
            }

    /* stickers */
    if (!g_lit_ready) rlDisableBackfaceCulling();
    for (int i = 0; i < NUM_FACELETS; i++) {
        push_layer_rotation(a, g_ipos[i]);
        Color col = STICKER_COLORS[a->cube.f[i]];
        if (i == hover && i % 9 != 4) col = ColorBrightness(col, 0.25f);
        if (g_lit_ready)
            draw_lit(g_sticker_mesh, sticker_matrix(i), col);
        else
            draw_sticker(i, col);
        rlPopMatrix();
    }
    if (!g_lit_ready) rlEnableBackfaceCulling();
    if (hover >= 0) draw_sticker_outline(hover, hover % 9 == 4 ? UI_DIM : UI_ACCENT);

    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlScalef(a->ui_scale, a->ui_scale, 1); /* back to the UI's scaled 2D space */
    rlViewport(0, 0, GetRenderWidth(), screen_h);
    rlDisableDepthTest();

    /* face letters next to the visible centers */
    static const char *const LETTERS[NUM_FACES] = {"U", "R", "F", "D", "L", "B"};
    /* each letter sits just outside the cube's outline, on the side its face is turned towards */
    Vector2 c0 = GetWorldToScreenEx((Vector3){0, 0, 0}, cam, (int)view.width, (int)view.height);
    float radius = 0;
    for (int k = 0; k < 8; k++) {
        Vector3 corner = {k & 1 ? 1.5f : -1.5f, k & 2 ? 1.5f : -1.5f, k & 4 ? 1.5f : -1.5f};
        radius = fmaxf(radius, Vector2Distance(c0, GetWorldToScreenEx(corner, cam, (int)view.width, (int)view.height)));
    }
    for (int f = 0; f < NUM_FACES; f++) {
        int i = f * 9 + 4;
        Vector3 to_cam = Vector3Subtract(cam.position, g_pos[i]);
        float facing = Vector3DotProduct(Vector3Normalize(to_cam), g_normal[i]);
        if (facing < 0.15f) continue;
        Vector2 d = Vector2Subtract(GetWorldToScreenEx(g_pos[i], cam, (int)view.width, (int)view.height), c0);
        float len = Vector2Length(d);
        /* a face turned straight at the camera has no side to label from; the net names it instead */
        if (len < radius * 0.15f) continue;
        Vector2 sp = Vector2Add(c0, Vector2Scale(d, (radius + 18) / len));
        Color c = Fade(UI_DIM, fminf(1.0f, (facing - 0.15f) * 3) * fminf(1.0f, (len / radius - 0.15f) * 6));
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
        if (i % 9 == 4) {
            /* the centre sticker names its face, so the net reads without the 3D view */
            char letter[2] = {"URFDLB"[f], 0};
            float size = cell * 0.5f;
            float lum = 0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
            Color ink = lum > 150 ? UI_INK : UI_TEXT;
            float lw = ui_text_width_face(a, TYPE_SEMIBOLD, letter, size);
            ui_text_face(a, TYPE_SEMIBOLD, letter, rc.x + (cell - lw) / 2, rc.y + (cell - size) / 2, size, ink);
        }
    }
    if (a->edit_mode && hover >= 0 && hover % 9 != 4) {
        a->cursor = MOUSE_CURSOR_POINTING_HAND;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->cube.f[hover] = (uint8_t)a->paint_color;
    }
    if (hover_out) *hover_out = hover;
}
