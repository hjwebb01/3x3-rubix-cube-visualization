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

static void enqueue(App *a, Move m, bool fast) {
    if (a->qtail - a->qhead >= QUEUE_MAX) app_flush(a);
    a->queue[a->qtail % QUEUE_MAX] = (QueuedMove){m, fast};
    a->qtail++;
}

void app_flush(App *a) {
    if (a->animating) cube_apply(&a->cube, a->anim.m);
    a->animating = false;
    for (; a->qhead < a->qtail; a->qhead++) cube_apply(&a->cube, a->queue[a->qhead % QUEUE_MAX].m);
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
    for (int i = 0; i < n; i++) enqueue(a, m[i], fast);
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
    a->has_sol = true;
    a->sol_scroll = 0;
    a->follow_current = true;
    int n = solution_move_count(&a->sol);
    if (a->sol.n == 0) {
        app_set_status(a, false, "Already solved");
        a->has_sol = false;
        return;
    }
    a->playing = !a->teach; /* in teach mode, read the first lesson before playing */
    a->teach_paused_at = 0;
    app_set_status(a, false, "%s: %d moves in %.1f ms", solver_name(a->method), n, a->sol.ms);
}

void app_step(App *a, int dir) {
    if (!a->has_sol) return;
    if (dir > 0 && a->sol_pos < a->sol.n) enqueue(a, a->sol.moves[a->sol_pos++], false);
    if (dir < 0 && a->sol_pos > 0) enqueue(a, move_inverse(a->sol.moves[--a->sol_pos]), false);
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
        cube_apply(&a->cube, a->anim.m);
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

static void load_font(App *a) {
    static const char *const CANDIDATES[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/Inter-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    a->font = GetFontDefault();
    for (size_t i = 0; i < sizeof CANDIDATES / sizeof CANDIDATES[0]; i++) {
        if (!FileExists(CANDIDATES[i])) continue;
        Font f = LoadFontEx(CANDIDATES[i], 40, NULL, 0);
        if (f.texture.id == 0) continue;
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        a->font = f;
        return;
    }
}

/* --screenshot PATH: scramble, solve, stop mid-turn halfway through the
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
    InitWindow(1360, 820, "Rubik's Cube Solver");
    SetWindowMinSize(960, 600);
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);
    load_font(a);

    int cursor_shown = -1;
    for (int frame = 0; !WindowShouldClose(); frame++) {
        if (shot) screenshot_driver(a, shot, frame);
        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        float lw = fminf(fmaxf(W * 0.21f, 236), 290), rw = fminf(fmaxf(W * 0.24f, 240), 350);
        Rectangle left = {0, 0, lw, H};
        Rectangle right = {W - rw, 0, rw, H};
        Rectangle top = {left.width, 0, W - left.width - right.width, 52};
        Rectangle view = {left.width, top.height, top.width, H - top.height};

        poll_solver(a);
        handle_keys(a);
        handle_view_mouse(a, view);
        update_animation(a, GetFrameTime());

        a->cursor = MOUSE_CURSOR_ARROW;
        BeginDrawing();
        ClearBackground(UI_BG);
        render_cube3d(a, view);
        ui_top_bar(a, top);
        ui_left_panel(a, left);
        ui_right_panel(a, right);
        EndDrawing();
        /* always set an explicit cursor: the default (none) shows nothing under XWayland on Hyprland */
        if (a->cursor != cursor_shown) SetMouseCursor(cursor_shown = a->cursor);
    }
    if (app_busy(a)) pthread_join(a->worker, NULL);
    if (a->font.texture.id != GetFontDefault().texture.id) UnloadFont(a->font);
    CloseWindow();
    return 0;
}
