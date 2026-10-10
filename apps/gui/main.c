/*
 * rubik_gui - 3D cube with animated solutions from every solver.
 *
 *   mouse drag        orbit the camera (right drag works in edit mode too)
 *   wheel             zoom (scrolls the solution list when over it)
 *   U R F D L B       face turns, Shift for prime
 *   M E S, X Y Z      slices and rotations
 *   Space             play / pause the solution
 *   Left / Right      step through the solution, Home / End jump
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gui.h"
#include "rlgl.h"
#include "cube/cubie.h"

const float SPEED_SECONDS[NUM_SPEEDS] = {0.8f, 0.45f, 0.25f, 0.14f, 0.07f, 0.0f};
const char *const SPEED_NAMES[NUM_SPEEDS] = {"Slow", "Relaxed", "Normal", "Fast", "Very fast", "Instant"};
const Color STICKER_COLORS[NUM_COLORS] = {
    {245, 245, 245, 255}, /* white */
    {200, 30, 40, 255},   /* red */
    {20, 160, 70, 255},   /* green */
    {250, 210, 30, 255},  /* yellow */
    {250, 120, 20, 255},  /* orange */
    {25, 90, 210, 255},   /* blue */
};

#define FAST_SECONDS 0.06f

void app_set_status(App *a, bool error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = error;
}

bool app_busy(const App *a) { return atomic_load(&a->solving) != 0; }

static void enqueue(App *a, Move m, bool fast, int sol_dir) {
    if (a->qtail - a->qhead >= QUEUE_MAX) app_flush(a);
    a->queue[a->qtail % QUEUE_MAX] = (QueuedMove){m, fast, sol_dir};
    a->qtail++;
}

/* A turn finishing on screen: the logical cube and the solution's done count move together. */
static void apply_queued(App *a, QueuedMove q) {
    cube_apply(&a->cube, q.m);
    a->done_pos += q.sol_dir;
}

void app_flush(App *a) {
    if (a->animating) apply_queued(a, a->anim);
    a->animating = false;
    for (; a->qhead < a->qtail; a->qhead++) apply_queued(a, a->queue[a->qhead % QUEUE_MAX]);
    a->qhead = a->qtail = 0;
}

static void drop_solution(App *a) {
    if (!a->has_sol) return;
    app_flush(a);
    a->has_sol = false;
    a->playing = false;
}

void app_user_moves(App *a, const Move *m, int n, bool fast) {
    if (app_busy(a) || a->edit_mode) return;
    drop_solution(a);
    for (int i = 0; i < n; i++) enqueue(a, m[i], fast, 0);
}

void app_scramble(App *a) {
    if (app_busy(a)) return;
    app_set_edit(a, false);
    drop_solution(a);
    app_flush(a);
    cube_reset(&a->cube);
    Move scr[25];
    scramble_generate(scr, 25, &a->rng);
    char buf[256];
    moves_format(scr, 25, buf, sizeof buf);
    app_set_status(a, false, "Scramble: %s", buf);
    app_user_moves(a, scr, 25, true);
}

void app_reset(App *a) {
    if (app_busy(a)) return;
    drop_solution(a);
    app_flush(a);
    cube_reset(&a->cube);
    app_set_status(a, false, "Reset to solved");
}

static void *solve_worker(void *arg) {
    App *a = arg;
    a->solve_error = solve_cube(a->method, &a->solve_input, &a->solve_output);
    atomic_store(&a->solving, 2);
    return NULL;
}

void app_start_solve(App *a) {
    if (app_busy(a)) return;
    drop_solution(a);
    app_flush(a);
    const char *err = cube_validate(&a->cube);
    if (err) {
        app_set_status(a, true, "Cannot solve: %s", err);
        return;
    }
    app_set_edit(a, false);
    a->solve_input = a->cube;
    atomic_store(&a->solving, 1);
    if (pthread_create(&a->worker, NULL, solve_worker, a) != 0) {
        atomic_store(&a->solving, 0);
        app_set_status(a, true, "Could not start the solver thread");
        return;
    }
    app_set_status(a, false, "Solving with %s...", solver_name(a->method));
}

static void poll_solver(App *a) {
    if (atomic_load(&a->solving) != 2) return;
    pthread_join(a->worker, NULL);
    atomic_store(&a->solving, 0);
    if (a->solve_error) {
        app_set_status(a, true, "%s failed: %s", solver_name(a->method), a->solve_error);
        return;
    }
    a->sol = a->solve_output;
    a->sol_start = a->cube;
    a->sol_pos = 0;
    a->done_pos = 0;
    a->sol_method = a->method;
    a->has_sol = true;
    a->sol_scroll = 0;
    a->follow_current = true;
    if (a->sol.n == 0) {
        app_set_status(a, false, "Already solved");
        a->has_sol = false;
        return;
    }
    a->playing = !a->teach; /* in teach mode, read the first lesson before playing */
    a->teach_paused_at = 0;
    app_set_status(a, false, "%s: %d moves in %.1f ms", solver_name(a->method), a->sol.n, a->sol.ms);
}

void app_step(App *a, int dir) {
    if (!a->has_sol) return;
    if (dir > 0 && a->sol_pos < a->sol.n) enqueue(a, a->sol.moves[a->sol_pos++], false, 1);
    if (dir < 0 && a->sol_pos > 0) enqueue(a, move_inverse(a->sol.moves[--a->sol_pos]), false, -1);
    a->follow_current = true;
}

void app_jump(App *a, int pos) {
    if (!a->has_sol) return;
    if (pos < 0) pos = 0;
    if (pos > a->sol.n) pos = a->sol.n;
    a->animating = false;
    a->qhead = a->qtail = 0;
    a->cube = a->sol_start;
    cube_apply_seq(&a->cube, a->sol.moves, pos);
    a->sol_pos = pos;
    a->done_pos = pos;
    a->follow_current = true;
}

void app_set_edit(App *a, bool on) {
    if (on == a->edit_mode) return;
    if (on) {
        if (app_busy(a)) return;
        drop_solution(a);
        app_flush(a);
        app_set_status(a, false, "Edit mode: pick a color, click stickers (3D or net)");
    } else {
        const char *err = cube_validate(&a->cube);
        if (err)
            app_set_status(a, true, "Warning: %s", err);
        else
            app_set_status(a, false, "Cube is valid");
    }
    a->edit_mode = on;
}

/* ---------- per-frame update ---------- */

static bool at_step_start(const App *a) {
    for (int i = 1; i < a->sol.nsteps; i++)
        if (a->sol.steps[i].count && a->sol.steps[i].start == a->sol_pos) return true;
    return false;
}

static void update_animation(App *a, float dt) {
    for (int guard = 0; guard < 2 * SOL_MAX_MOVES + QUEUE_MAX; guard++) {
        if (!a->animating) {
            if (a->qhead == a->qtail && a->playing && a->has_sol) {
                if (a->teach && a->sol_pos != a->teach_paused_at && at_step_start(a)) {
                    a->playing = false;
                    a->teach_paused_at = a->sol_pos;
                } else if (a->sol_pos < a->sol.n)
                    app_step(a, 1);
                else
                    a->playing = false;
            }
            if (a->qhead == a->qtail) return;
            a->anim = a->queue[a->qhead++ % QUEUE_MAX];
            if (a->qhead == a->qtail) a->qhead = a->qtail = 0;
            a->animating = true;
            a->anim_t = 0;
        }
        float secs = a->anim.fast ? FAST_SECONDS : SPEED_SECONDS[a->speed];
        if (MOVE_TURNS(a->anim.m) == 2) secs *= 1.5f;
        if (secs > 0) {
            a->anim_t += dt / secs;
            if (a->anim_t < 1) return;
        }
        /* finished (instant speed finishes every queued move this frame) */
        apply_queued(a, a->anim);
        a->animating = false;
        if (secs > 0) return;
    }
}

static const struct {
    int key;
    int base;
} MOVE_KEYS[] = {
    {KEY_U, MB_U}, {KEY_R, MB_R}, {KEY_F, MB_F}, {KEY_D, MB_D}, {KEY_L, MB_L}, {KEY_B, MB_B},
    {KEY_M, MB_M}, {KEY_E, MB_E}, {KEY_S, MB_S}, {KEY_X, MB_X}, {KEY_Y, MB_Y}, {KEY_Z, MB_Z},
};

static void handle_keys(App *a) {
    if (a->input_focus) return;
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!ctrl) {
        for (size_t i = 0; i < sizeof MOVE_KEYS / sizeof MOVE_KEYS[0]; i++)
            if (IsKeyPressed(MOVE_KEYS[i].key)) {
                Move m = MOVE(MOVE_KEYS[i].base, shift ? 3 : 1);
                app_user_moves(a, &m, 1, false);
            }
    }
    if (IsKeyPressed(KEY_SPACE) && a->has_sol) {
        if (!a->playing && a->sol_pos == a->sol.n) app_jump(a, 0);
        a->playing = !a->playing;
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) a->playing = false, app_step(a, 1);
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) a->playing = false, app_step(a, -1);
    if (IsKeyPressed(KEY_HOME)) a->playing = false, app_jump(a, 0);
    if (IsKeyPressed(KEY_END)) a->playing = false, app_jump(a, a->sol.n);
    if (IsKeyPressed(KEY_ENTER) && !app_busy(a)) app_start_solve(a);
    if (ctrl && IsKeyPressed(KEY_S)) app_scramble(a);
    if (shift && IsKeyPressed(KEY_SLASH)) a->show_help = !a->show_help;
    if (IsKeyPressed(KEY_ESCAPE)) a->show_help = false;
}

static void handle_view_mouse(App *a, Rectangle view) {
    Vector2 mouse = GetMousePosition();
    bool inside = CheckCollisionPointRec(mouse, view);
    if (inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        if (a->edit_mode && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int s = render_pick_sticker(a, view, mouse);
            if (s >= 0) {
                if (s % 9 != 4) a->cube.f[s] = (uint8_t)a->paint_color;
                return;
            }
        }
        a->dragging = true;
        a->drag_start = mouse;
        a->input_focus = false;
    }
    if (a->dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 d = GetMouseDelta();
            a->yaw -= d.x * 0.008f;
            a->pitch += d.y * 0.008f;
            if (a->pitch > 1.45f) a->pitch = 1.45f;
            if (a->pitch < -1.45f) a->pitch = -1.45f;
        } else {
            a->dragging = false;
        }
    }
    if (inside) {
        float w = GetMouseWheelMove();
        if (w != 0) {
            a->dist *= powf(0.9f, w);
            if (a->dist < 5) a->dist = 5;
            if (a->dist > 25) a->dist = 25;
        }
    }
}

/* Returns a zero Font if the file is missing or unreadable. */
static Font load_font_file(const char *path, int size, int *cps, int ncps) {
    if (!FileExists(path)) return (Font){0};
    Font f = LoadFontEx(path, size, cps, ncps);
    if (f.texture.id == 0) return (Font){0};
    /* glyphs are rasterised large and drawn at 12-22 px; without mipmaps that minification aliases */
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

/* IBM Plex is copied next to the executable by CMake (apps/gui/fonts). */
static Font load_face(const char *dir, const char *file, int size, int *cps, int ncps) {
    char path[512];
    snprintf(path, sizeof path, "%sfonts/%s", dir, file);
    return load_font_file(path, size, cps, ncps);
}

static void load_fonts(App *a) {
    /* printable ASCII plus the marks the UI uses: middle dot, dashes, ellipsis, prime, minus */
    static int cps[128];
    int n = 0;
    for (int c = 32; c < 127; c++) cps[n++] = c;
    const int extra[] = {0xB7, 0x2013, 0x2014, 0x2026, 0x2032, 0x2212};
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) cps[n++] = extra[i];

    const char *dir = GetApplicationDirectory();
    a->font = load_face(dir, "IBMPlexSans-Regular.ttf", 48, cps, n);
    a->font_medium = load_face(dir, "IBMPlexSans-Medium.ttf", 48, cps, n);
    a->font_semibold = load_face(dir, "IBMPlexSans-SemiBold.ttf", 48, cps, n);
    a->font_mono = load_face(dir, "IBMPlexMono-Medium.ttf", 96, cps, n);

    /* fall back to a system font, then to raylib's default, so the UI still runs without the bundle */
    static const char *const CANDIDATES[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    for (size_t i = 0; i < sizeof CANDIDATES / sizeof CANDIDATES[0] && !a->font.texture.id; i++)
        a->font = load_font_file(CANDIDATES[i], 48, cps, n);
    if (!a->font.texture.id) a->font = GetFontDefault();
    if (!a->font_medium.texture.id) a->font_medium = a->font;
    if (!a->font_semibold.texture.id) a->font_semibold = a->font;
    if (!a->font_mono.texture.id) a->font_mono = a->font;

    /* the mono face may lack U+2032; the UI then writes moves with a plain apostrophe */
    int idx = GetGlyphIndex(a->font_mono, 0x2032);
    a->prime_ok = a->font_mono.glyphs[idx].value == 0x2032;
}

static void unload_fonts(App *a) {
    Font *fonts[4] = {&a->font, &a->font_medium, &a->font_semibold, &a->font_mono};
    for (int i = 0; i < 4; i++) {
        if (fonts[i]->texture.id == GetFontDefault().texture.id) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) dup |= fonts[j]->texture.id == fonts[i]->texture.id;
        if (!dup) UnloadFont(*fonts[i]);
    }
}

/* --screenshot PATH [W H]: scramble, solve, stop mid-turn halfway through the
 * solution, save a screenshot and exit (used to check rendering headlessly). */
static void screenshot_driver(App *a, const char *path, int frame) {
    if (frame == 2) {
        app_scramble(a);
        app_flush(a);
        app_start_solve(a);
    }
    static bool jumped;
    if (a->has_sol && !jumped) {
        jumped = true;
        app_jump(a, a->sol.n / 2);
        a->playing = false;
        app_step(a, 1);
        a->speed = 0;
    }
    if (a->animating && a->anim_t > 0.35f) {
        a->anim_t = 0.5f;
        TakeScreenshot(path);
        exit(0);
    }
}

int main(int argc, char **argv) {
    const char *shot = argc > 2 && strcmp(argv[1], "--screenshot") == 0 ? argv[2] : NULL;
    cubie_init();
    static App app;
    App *a = &app;
    cube_reset(&a->cube);
    a->yaw = 0.62f;
    a->pitch = 0.5f;
    a->dist = 10.5f;
    a->speed = 2;
    a->method = SOLVER_CFOP;
    a->rng = (uint32_t)time(NULL) | 1u;
    app_set_status(a, false, "Scramble, turn with U R F D L B (Shift = prime), then Solve");

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    /* --screenshot PATH [W H] also sets the window size, to check the small layout */
    int w = shot && argc >= 5 ? atoi(argv[3]) : 1360, h = shot && argc >= 5 ? atoi(argv[4]) : 820;
    InitWindow(w, h, "Rubik's Cube Solver");
    SetWindowMinSize(960, 600);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    load_fonts(a);
    render_init();

    int cursor_shown = -1;
    for (int frame = 0; !WindowShouldClose(); frame++) {
        if (shot) screenshot_driver(a, shot, frame);
        /* scale the whole UI with the window, so a large window gets larger type instead of empty space */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        a->ui_scale = fminf(fmaxf(fminf(sw / 1360, sh / 820), 1), 2);
        SetMouseScale(1 / a->ui_scale, 1 / a->ui_scale);
        float W = sw / a->ui_scale, H = sh / a->ui_scale;
        /* three columns: setup and method on the left, the cube and playback in the middle, the solution on the right */
        float lw = fminf(fmaxf(W * 0.21f, 236), 288), rw = fminf(fmaxf(W * 0.26f, 260), 372);
        float top_h = 44;
        float move_h = fminf(fmaxf(H * 0.16f, 104), 136), trans_h = fminf(fmaxf(H * 0.17f, 112), 128);
        Rectangle left = {0, 0, lw, H};
        Rectangle right = {W - rw, 0, rw, H};
        Rectangle top = {lw, 0, W - lw - rw, top_h};
        Rectangle transport = {lw, H - trans_h, top.width, trans_h};
        Rectangle move = {lw, transport.y - move_h, top.width, move_h};
        Rectangle view = {lw, top_h, top.width, move.y - top_h};

        poll_solver(a);
        handle_keys(a);
        handle_view_mouse(a, view);
        /* clamp: after an idle wait the frame time spans the whole pause */
        update_animation(a, fminf(GetFrameTime(), 0.05f));

        a->cursor = MOUSE_CURSOR_ARROW;
        BeginDrawing();
        ClearBackground(UI_BG);
        rlScalef(a->ui_scale, a->ui_scale, 1);
        render_cube3d(a, view);
        ui_top_bar(a, top);
        ui_move_display(a, move);
        ui_transport(a, transport);
        ui_help_overlay(a, view);
        ui_left_panel(a, left);
        ui_right_panel(a, right);
        /* sleep until input arrives unless something on screen is changing by itself */
        if (a->animating || a->qhead != a->qtail || a->playing || a->input_focus || app_busy(a) || shot)
            DisableEventWaiting();
        else
            EnableEventWaiting();
        EndDrawing();
        /* always set an explicit cursor: the default (none) shows nothing under XWayland on Hyprland */
        if (a->cursor != cursor_shown) SetMouseCursor(cursor_shown = a->cursor);
    }
    if (app_busy(a)) pthread_join(a->worker, NULL);
    unload_fonts(a);
    render_shutdown();
    CloseWindow();
    return 0;
}
