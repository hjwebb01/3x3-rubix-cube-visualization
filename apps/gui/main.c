/*
 * rubik_gui - 3D cube with animated solutions from every solver.
 *
 *   mouse drag        orbit the camera (right drag works in edit mode too)
 *   wheel             zoom (scrolls the solution list when over it)
 *   U R F D L B       face turns, Shift for prime
 *   M E S, X Y Z      slices and rotations
 *   Space             play / pause the solution
 *   Left / Right      step through the solution, Home / End jump
 *   PgUp / PgDn       previous / next step
 */
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gui.h"
#include "rlgl.h"
#include "cube/cubie.h"

/* logical px: below this width the Seam stacks, so the window may now be narrower than the side-by-side layout */
#define STACK_W 900

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

void app_set_status(App *a, bool error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = error;
}

bool app_busy(const App *a) { return atomic_load(&a->solving) != SOLVE_IDLE; }
bool app_can_change_cube(const App *a) { return !app_busy(a) && !playback_view(&a->pb).coach.locked; }
bool app_can_turn(const App *a) { return app_can_change_cube(a) && !a->edit_mode; }

const char *app_guided_scramble(const App *a) {
    if (!a->coach_scramble[0]) return NULL;
    PlaybackView v = playback_view(&a->pb);
    /* shortcut: while turns animate the screen lags the queue, so trust the scramble until idle */
    return !v.idle || cube_equal(v.cube, &a->scramble_cube) ? a->coach_scramble : NULL;
}

void app_user_moves(App *a, const Move *m, int n, bool fast) {
    if (!app_can_turn(a)) return;
    if (!fast) a->can_undo = false;
    playback_turn(&a->pb, m, n, fast);
}

/* Keeps the cube about to be replaced, for app_undo. */
static void save_undo(App *a) {
    playback_flush(&a->pb);
    a->undo_cube = *playback_view(&a->pb).cube;
    const char *g = app_guided_scramble(a);
    snprintf(a->undo_scramble, sizeof a->undo_scramble, "%s", g ? g : "");
    a->can_undo = true;
}

void app_undo(App *a) {
    if (!a->can_undo || !app_can_change_cube(a)) return;
    playback_set_cube(&a->pb, &a->undo_cube);
    snprintf(a->coach_scramble, sizeof a->coach_scramble, "%s", a->undo_scramble);
    a->scramble_cube = a->undo_cube;
    a->can_undo = false;
    app_set_status(a, false, "Restored the previous cube");
}

void app_orient(App *a, int front, int top) {
    PlaybackView v = playback_view(&a->pb);
    if (!app_can_change_cube(a) || !v.idle || v.coach.mode != COACH_OFF) return;
    Cube c = *v.cube;
    if (!cube_orient(&c, front, top)) return;
    playback_set_cube(&a->pb, &c);
    a->coach_scramble[0] = 0;
    a->can_undo = false;
    a->yaw = 0.62f;
    a->pitch = 0.5f;
    a->yaw_v = a->pitch_v = 0;
    app_set_status(a, false, "Front: %s. Top: %s", COLOR_NAMES[front], COLOR_NAMES[top]);
}

static void reset_in_orientation(App *a, Cube *c) {
    const Cube *current = playback_view(&a->pb).cube;
    int front = cube_center(current, FACE_F), top = cube_center(current, FACE_U);
    cube_reset(c);
    cube_orient(c, front, top);
}

void app_scramble(App *a) {
    if (!app_can_change_cube(a)) return;
    save_undo(a);
    app_set_edit(a, false);
    Cube solved;
    reset_in_orientation(a, &solved);
    playback_set_cube(&a->pb, &solved);
    Move scr[25];
    scramble_generate(scr, 25, &a->rng);
    char buf[256];
    moves_format(scr, 25, buf, sizeof buf);
    app_set_status(a, false, "Scramble: %s", buf);
    snprintf(a->coach_scramble, sizeof a->coach_scramble, "%s", buf);
    a->scramble_cube = solved;
    cube_apply_seq(&a->scramble_cube, scr, 25);
    app_user_moves(a, scr, 25, true);
}

void app_reset(App *a) {
    if (!app_can_change_cube(a)) return;
    save_undo(a);
    Cube solved;
    reset_in_orientation(a, &solved);
    playback_set_cube(&a->pb, &solved);
    app_set_status(a, false, "Reset to solved. Ctrl+Z to undo");
}

static void *solve_worker(void *arg) {
    App *a = arg;
    a->solve_error = solve_cube(a->solve_method, &a->solve_input, &a->solve_output);
    atomic_store(&a->solving, SOLVE_DONE);
    return NULL;
}

void app_start_solve(App *a) {
    if (!app_can_change_cube(a)) return;
    CoachMode mode = playback_view(&a->pb).coach.mode;
    bool coaching = mode == COACH_SETUP || mode == COACH_REPAINT; /* the Solve starts practice, not playback */
    if (coaching && a->method != SOLVER_BEGINNER && a->method != SOLVER_CFOP) {
        app_set_status(a, true, "Coaching needs the Beginner or CFOP method");
        return;
    }
    playback_drop_solution(&a->pb);
    playback_flush(&a->pb);
    Cube cur = *playback_view(&a->pb).cube;
    const char *err = cube_validate(&cur);
    if (err) {
        app_set_status(a, true, "Cannot solve: %s", err);
        return;
    }
    /* Validation checks the screen state; matching the physical cube is always assumed. */
    if (coaching)
        playback_coach_set_basis(&a->pb, (mode == COACH_REPAINT || !app_guided_scramble(a)) ? COACH_BASIS_PAINTED
                                                                                           : COACH_BASIS_ASSUMED);
    app_set_edit(a, false);
    a->can_undo = false;
    a->solve_input = cur;
    a->solve_method = a->method;
    a->solve_coach = coaching;
    atomic_store(&a->solving, SOLVE_RUNNING);
    if (pthread_create(&a->worker, NULL, solve_worker, a) != 0) {
        atomic_store(&a->solving, SOLVE_IDLE);
        app_set_status(a, true, "Could not start the solver thread");
        return;
    }
    app_set_status(a, false, "Solving with %s\xE2\x80\xA6", solver_name(a->method));
}

static void poll_solver(App *a) {
    if (atomic_load(&a->solving) != SOLVE_DONE) return;
    pthread_join(a->worker, NULL);
    atomic_store(&a->solving, SOLVE_IDLE);
    if (a->solve_error) {
        app_set_status(a, true, "%s failed: %s", solver_name(a->solve_method), a->solve_error);
        return;
    }
    const Solution *sol = &a->solve_output;
    bool coaching = a->solve_coach;
    playback_load(&a->pb, sol);
    bool practising = coaching && playback_coach_start(&a->pb);
    if (!playback_view(&a->pb).sol) {
        app_set_status(a, false, coaching ? "Already solved: nothing to coach" : "Already solved");
        return;
    }
    if (coaching && !practising) {
        app_set_status(a, true, "This solution has no Steps to coach");
        return;
    }
    a->sol_method = a->solve_method;
    a->sol_scroll = 0;
    a->follow_current = true;
    a->follow_seen = 0;
    if (practising) {
        app_set_status(a, false, "Coach practice with %s: read each goal, show the moves only if you need them",
                       solver_name(a->solve_method));
        return;
    }
    char rots[48];
    ui_rotations(sol, " \xC2\xB7 ", rots, sizeof rots);
    app_set_status(a, false, "%s: %d moves%s in %.1f ms", solver_name(a->solve_method), solution_move_count(sol), rots,
                   sol->ms);
}

void app_paint_sticker(App *a, int sticker) {
    if (sticker % 9 == 4) return; /* centers fix the color scheme */
    /* shortcut: the other faces still look paintable (hover, focus) under paint top, upgrade: dim them in render.c */
    if (!playback_coach_may_paint(&a->pb, sticker)) return;
    Cube c = *playback_view(&a->pb).cube;
    c.f[sticker] = (uint8_t)a->paint_color;
    a->can_undo = false;
    playback_set_cube(&a->pb, &c);
}

void app_check_cube(App *a) {
    const char *err = cube_validate(playback_view(&a->pb).cube);
    if (err)
        app_set_status(a, true, "Invalid cube: %s", err);
    else
        app_set_status(a, false, "Cube is valid and solvable");
}

bool app_type_face(App *a, const char *letters) {
    static const char FACES[] = "URFDLB";
    int face = a->typed_face;
    while (face < NUM_FACES && !playback_coach_may_paint(&a->pb, face * 9)) face++; /* paint top: only U */
    if (face == NUM_FACES) face = FACE_U;
    Cube c = *playback_view(&a->pb).cube;
    int err = cube_set_face(&c, face, letters);
    if (err) {
        if (err == -1) app_set_status(a, true, "Face %c needs nine colors from W R G Y O B", FACES[face]);
        else app_set_status(a, true, "The center of face %c is %s: type it there", FACES[face], COLOR_NAMES[c.f[face * 9 + 4]]);
        return false;
    }
    a->can_undo = false;
    playback_set_cube(&a->pb, &c);
    do face++; while (face < NUM_FACES && !playback_coach_may_paint(&a->pb, face * 9));
    a->typed_face = face % NUM_FACES;
    if (face < NUM_FACES) app_set_status(a, false, "Face %c typed: now face %c", FACES[face - 1], FACES[face]);
    else app_check_cube(a);
    return true;
}

void app_toggle_screen_only(App *a) {
    a->screen_only = !a->screen_only;
    a->focus = 0; /* the widget that had focus may be hidden now */
}

void app_set_edit(App *a, bool on) {
    if (on == a->edit_mode) return;
    if (on) {
        if (!app_can_change_cube(a)) return;
        playback_drop_solution(&a->pb);
        playback_flush(&a->pb);
        a->typed_face = FACE_U;
        app_set_status(a, false, "Edit mode: click stickers (3D or net), or type face U as nine colors");
    } else {
        app_check_cube(a);
    }
    a->edit_mode = on;
}

/* ---------- coach ---------- */

void app_coach_toggle(App *a) {
    if (app_busy(a)) return;
    app_set_edit(a, false);
    if (playback_view(&a->pb).coach.mode == COACH_OFF) {
        playback_flush(&a->pb);
        Cube c = *playback_view(&a->pb).cube;
        cube_orient(&c, COLOR_GREEN, COLOR_WHITE);
        playback_set_cube(&a->pb, &c);
        a->yaw = 0.62f;
        a->pitch = 0.5f;
        a->yaw_v = a->pitch_v = 0;
        a->coach_scramble[0] = 0;
        playback_coach_begin(&a->pb);
        app_set_status(a, false, "Coach uses white top, green front. Match your cube, then press Solve");
    } else {
        playback_coach_end(&a->pb);
        app_set_status(a, false, "Coaching stopped");
    }
}

void app_coach_demo(App *a) {
    playback_coach_demo(&a->pb);
    app_set_status(a, false, "Demonstration: the moves are shown. Follow them on your cube, then press Mark as followed");
}

void app_coach_followed(App *a) {
    playback_coach_followed(&a->pb);
    app_set_status(a, false, "The screen follows your moves: the cube is assumed to match yours");
}

void app_coach_unsync(App *a, bool attempted) {
    playback_coach_unsync(&a->pb, attempted);
    app_set_status(a, false, attempted ? "Try the step on your cube, then paint it or restore the step start"
                                       : "Your cube may differ from the screen: restore the step start or paint it");
}

void app_coach_restore(App *a) {
    playback_coach_restore(&a->pb);
    app_set_status(a, false, "You confirmed your physical cube is back at the step start");
}

void app_coach_repaint(App *a) {
    if (app_busy(a)) return;
    playback_coach_repaint(&a->pb);
    app_set_edit(a, true);
    app_set_status(a, false, "Paint your physical cube as it is, then press Solve");
}

/* trust: the typed moves are the truth, so the solver plans again from the cube they leave. */
void app_coach_trust(App *a, const Move *m, int n) {
    if (app_busy(a)) return;
    playback_coach_trust(&a->pb, m, n);
    app_start_solve(a);
}

void app_coach_paint_top(App *a) {
    if (app_busy(a)) return;
    playback_coach_paint_top(&a->pb);
    app_set_edit(a, true);
    app_set_status(a, false, "Paint the top face, then press Solve");
}

/* ---------- per-frame update ---------- */

static const struct {
    int key;
    int base;
} MOVE_KEYS[] = {
    {KEY_U, MB_U}, {KEY_R, MB_R}, {KEY_F, MB_F}, {KEY_D, MB_D}, {KEY_L, MB_L}, {KEY_B, MB_B},
    {KEY_M, MB_M}, {KEY_E, MB_E}, {KEY_S, MB_S}, {KEY_X, MB_X}, {KEY_Y, MB_Y}, {KEY_Z, MB_Z},
};

static void handle_keys(App *a) {
    bool prompt = ui_input_focused(a);
    if (prompt && a->input[0]) return; /* an empty prompt still takes Space, the arrows and Esc */
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!ctrl && !prompt) {
        for (size_t i = 0; i < sizeof MOVE_KEYS / sizeof MOVE_KEYS[0]; i++)
            if (IsKeyPressed(MOVE_KEYS[i].key)) {
                Move m = MOVE(MOVE_KEYS[i].base, shift ? 3 : 1);
                app_user_moves(a, &m, 1, false);
            }
    }
    if (IsKeyPressed(KEY_SPACE) && (!a->focus || prompt)) playback_toggle(&a->pb); /* else Space presses the focused widget */
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) playback_step(&a->pb, 1);
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) playback_step(&a->pb, -1);
    if (IsKeyPressed(KEY_ESCAPE)) a->show_help = false;
    if (prompt) return;
    if (IsKeyPressed(KEY_V) && !a->input[0] && !ctrl) app_toggle_screen_only(a);
    if (IsKeyPressed(KEY_MINUS) && a->speed > 0) a->speed--;
    if (IsKeyPressed(KEY_EQUAL) && a->speed < NUM_SPEEDS - 1) a->speed++;
    if (IsKeyPressed(KEY_HOME)) playback_seek(&a->pb, 0);
    if (IsKeyPressed(KEY_END)) playback_seek(&a->pb, INT_MAX);
    if (IsKeyPressed(KEY_PAGE_UP)) playback_seek_step(&a->pb, -1);
    if (IsKeyPressed(KEY_PAGE_DOWN)) playback_seek_step(&a->pb, 1);
    /* with a widget focused, Enter presses that widget instead */
    if (IsKeyPressed(KEY_ENTER) && !a->focus) app_start_solve(a);
    if (ctrl && IsKeyPressed(KEY_S)) app_scramble(a);
    if (ctrl && IsKeyPressed(KEY_Z)) app_undo(a);
    if (IsKeyPressed(KEY_H)) a->xray = !a->xray;
    if (shift && IsKeyPressed(KEY_SLASH)) a->show_help = !a->show_help;
}

static void handle_view_mouse(App *a, Rectangle view) {
    Vector2 mouse = GetMousePosition();
    bool inside = CheckCollisionPointRec(mouse, view);
    if (inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        if (a->edit_mode && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int s = render_pick_sticker(a, view, mouse);
            if (s >= 0) {
                app_paint_sticker(a, s);
                return;
            }
        }
        a->dragging = true;
        a->yaw_v = a->pitch_v = 0;
        a->focus = 0;
    }
    if (a->dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 d = GetMouseDelta();
            float dt = fmaxf(GetFrameTime(), 1e-3f);
            /* smoothed so the release speed is the drag's recent speed, not one jittery frame */
            a->yaw_v += (-d.x * 0.008f / dt - a->yaw_v) * 0.5f;
            a->pitch_v += (d.y * 0.008f / dt - a->pitch_v) * 0.5f;
            a->yaw -= d.x * 0.008f;
            a->pitch += d.y * 0.008f;
        } else {
            a->dragging = false;
        }
    } else if (a->yaw_v != 0 || a->pitch_v != 0) {
        /* inertia: coast after release and slow down over ~0.3 s */
        float dt = fminf(GetFrameTime(), 0.05f), decay = expf(-dt * 7);
        a->yaw += a->yaw_v * dt;
        a->pitch += a->pitch_v * dt;
        a->yaw_v *= decay;
        a->pitch_v *= decay;
        if (fabsf(a->yaw_v) + fabsf(a->pitch_v) < 0.02f) a->yaw_v = a->pitch_v = 0;
    }
    if (a->pitch > 1.45f) a->pitch = 1.45f, a->pitch_v = 0;
    if (a->pitch < -1.45f) a->pitch = -1.45f, a->pitch_v = 0;
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
        playback_flush(&a->pb);
        app_start_solve(a);
    }
    static bool jumped;
    PlaybackView v = playback_view(&a->pb);
    if (v.sol && !jumped) {
        jumped = true;
        playback_seek(&a->pb, v.sol->n / 2);
        playback_step(&a->pb, 1);
        a->speed = 0;
        v = playback_view(&a->pb);
    }
    if (v.turning && v.turn_t > 0.35f) {
        TakeScreenshot(path);
        exit(0);
    }
}

/* --faces NAME [W H]: paint mode, a face with the wrong center, then a scrambled cube typed face by face. Saves
 * NAME-0.png to NAME-7.png (next to the executable, where raylib puts screenshots) and exits, printing the status line after each step. Types through
 * app_type_face, so the prompt's own key handling is not exercised. */
static void faces_driver(App *a, const char *name, int frame) {
    enum { EVERY = 12, LAST = 7 };
    int step = frame / EVERY;
    if (frame % EVERY == 2) {
        if (step == 0) app_set_edit(a, true);
        else if (step == 1) app_type_face(a, "WWWWRWWWW");
        else if (step <= LAST) {
            Cube t;
            Move mv[16];
            cube_reset(&t);
            int n = moves_parse("R U F' D2 L B' R2 U'", mv, 16);
            for (int i = 0; i < n; i++) cube_apply(&t, mv[i]);
            char letters[10];
            for (int i = 0; i < 9; i++) letters[i] = "WRGYOB"[t.f[(step - 2) * 9 + i]];
            letters[9] = 0;
            app_type_face(a, letters);
        }
    }
    if (frame % EVERY == 8) {
        char file[128];
        snprintf(file, sizeof file, "%s-%d.png", name, step);
        printf("%d: %s%s\n", step, a->status_error ? "error: " : "", a->status); /* the top bar clips it in a narrow window */
        TakeScreenshot(file);
        if (step == LAST) exit(0);
    }
}

static void *warm_kociemba(void *arg) {
    (void)arg;
    kociemba_warm();
    return NULL;
}

int main(int argc, char **argv) {
    bool faces = argc > 2 && strcmp(argv[1], "--faces") == 0;
    const char *shot = argc > 2 && (faces || strcmp(argv[1], "--screenshot") == 0) ? argv[2] : NULL;
    cubie_init();
    /* build Kociemba's tables now so the first Kociemba solve doesn't wait seconds for them */
    pthread_t warm;
    if (pthread_create(&warm, NULL, warm_kociemba, NULL) == 0) pthread_detach(warm);
    static App app;
    App *a = &app;
    playback_init(&a->pb);
    playback_set_teach(&a->pb, a->teach);
    a->yaw = 0.62f;
    a->pitch = 0.5f;
    a->yaw_v = a->pitch_v = 0;
    a->dist = 10.5f;
    a->speed = 2;
    a->method = SOLVER_CFOP;
    a->rng = (uint32_t)time(NULL) | 1u;
    app_set_status(a, false, "Scramble, turn with U R F D L B (Shift = prime), then Solve");

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    /* --screenshot PATH [W H] and --faces NAME [W H] also set the window size, to check the small layout */
    int w = shot && argc >= 5 ? atoi(argv[3]) : 1360, h = shot && argc >= 5 ? atoi(argv[4]) : 820;
    InitWindow(w, h, "Rubik's Cube Solver");
    SetWindowMinSize(600, 640); /* the stacked layout needs its band height; side-by-side is wider than STACK_W */
    if (shot) SetWindowMinSize(w, h), SetWindowMaxSize(w, h); /* a fixed size makes tiling WMs float it at W x H */
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    load_fonts(a);
    render_init();

    int cursor_shown = -1;
    for (int frame = 0; !WindowShouldClose(); frame++) {
        if (shot) (faces ? faces_driver : screenshot_driver)(a, shot, frame);
        /* scale the whole UI with the window, so a large window gets larger type instead of empty space */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        a->ui_scale = fminf(fmaxf(fminf(sw / 1360, sh / 820), 1), 2);
        SetMouseScale(1 / a->ui_scale, 1 / a->ui_scale);
        float W = sw / a->ui_scale, H = sh / a->ui_scale;
        /* the Seam: your cube's setup and Solution on the left half, the screen's cube on the right, the prompt below */
        bool screen_only = a->screen_only;
        /* narrower than STACK_W: the left half is a top band, the seam runs across, the screen half sits below */
        bool stacked = !screen_only && W < STACK_W;
        float seam_w = screen_only ? 0 : stacked ? 24 : 16; /* stacked: thick enough for the seam's label */
        /* side by side the seam sits at the middle: the cube in hand and the screen cube take half the window each.
         * The panels' minimum widths win below about 1008 px, so the screen cube gets less than half there */
        float halves = (W - seam_w) / 2;
        float lw = screen_only ? 0 : stacked ? W / 2 : fmaxf(236, halves * 0.47f);
        float rw = screen_only ? 0 : stacked ? W - lw : fmaxf(260, halves - lw);
        float top_h = 44, prompt_h = 64, body_h = H - prompt_h;
        bool coaching = playback_view(&a->pb).coach.mode != COACH_OFF;
        float move_h = fminf(fmaxf(body_h * 0.16f, 104), 136);
        if (coaching) move_h = stacked ? fminf(270, body_h * 0.4f) : 270; /* the Coach card and its buttons */
        /* height of the left half. Stacked, it takes most of the height so the method grid clears the checkbox;
         * while coaching it holds only the net and the Stop button, so it takes less */
        float lh = stacked ? body_h * (coaching ? 0.4f : 0.62f) : body_h;
        float cx = stacked ? 0 : lw + rw + seam_w, cy = stacked ? lh + seam_w : 0; /* where the screen half starts */
        float tx = stacked ? 0 : 32;                                              /* top bar clears the seam's label */
        Rectangle left = {0, 0, lw, lh};
        Rectangle right = {lw, 0, rw, lh};
        Rectangle top = {cx + tx, cy, W - cx - tx, top_h};
        Rectangle move = {cx, body_h - move_h, W - cx, move_h};
        Rectangle view = {cx, cy + top_h, W - cx, move.y - cy - top_h};
        Rectangle seam = stacked ? (Rectangle){0, lh, W, seam_w} : (Rectangle){lw + rw, 0, seam_w, body_h};
        Rectangle prompt = {0, body_h, W, prompt_h};

        poll_solver(a);
        ui_focus_keys(a);
        handle_keys(a);
        handle_view_mouse(a, view);
        /* clamp: after an idle wait the frame time spans the whole pause */
        playback_tick(&a->pb, fminf(GetFrameTime(), 0.05f), SPEED_SECONDS[a->speed]);

        a->cursor = MOUSE_CURSOR_ARROW;
        BeginDrawing();
        ClearBackground(UI_BG);
        rlScalef(a->ui_scale, a->ui_scale, 1);
        render_cube3d(a, view);
        /* drawn in Tab order: widgets take their Tab stops as they draw */
        if (!screen_only) ui_left_panel(a, left);
        ui_top_bar(a, top);
        ui_move_display(a, move);
        if (!screen_only) {
            ui_right_panel(a, right);
            ui_seam(a, seam); /* after the panels and top bar: its label overlaps them */
        }
        ui_prompt(a, prompt);
        ui_help_overlay(a, view);
        /* sleep until input arrives unless something on screen is changing by itself */
        if (!playback_view(&a->pb).idle || ui_input_focused(a) || app_busy(a) || shot || a->yaw_v != 0 || a->pitch_v != 0)
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
