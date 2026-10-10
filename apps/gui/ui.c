/* Immediate-mode widgets and the panels: setup and method (left), the cube's playback (centre), the solution (right). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"

#define PAD 16.0f
#define TOKEN_H 26.0f
#define TOKEN_GAP 4.0f
#define TOKEN_ROW (TOKEN_H + 4)  /* token height plus the gap between rows */
#define ROW_H 30.0f              /* a step header row */
#define TOKEN_ROWS_MAX 4         /* the current step shows this many token rows, then scrolls */
#define UI_ROTATION ((Color){190, 150, 255, 255}) /* whole-cube rotations in the move list */
#define UI_BLACK_EDGE ((Color){0, 0, 0, 255})     /* the dark rim around a paint swatch */

/* A run of moves drawn as one segment of the progress bar. */
typedef struct {
    int start, count;
} Segment;

/* ---------- primitives ---------- */

static const Font *face(const App *a, TypeFace t) {
    switch (t) {
    case TYPE_MEDIUM: return &a->font_medium;
    case TYPE_SEMIBOLD: return &a->font_semibold;
    case TYPE_MONO: return &a->font_mono;
    default: return &a->font;
    }
}

/* The default font is ASCII only and is narrow without spacing, so it gets a little tracking. */
/* BeginScissorMode takes window pixels, but the UI is laid out in scaled units */
static void scissor(const App *a, Rectangle r) {
    float k = a->ui_scale;
    BeginScissorMode((int)(r.x * k), (int)(r.y * k), (int)ceilf(r.width * k), (int)ceilf(r.height * k));
}

static float spacing(const Font *f, float size) {
    return f->texture.id == GetFontDefault().texture.id ? size / 10 : 0;
}

#define PRIME "\xE2\x80\xB2"
#define PRIME_PULL 0.18f /* a mono prime sits in its own cell; pull it back against the letter, in ems */

/* Draws (or only measures) s, kerning each prime against the letter before it. Returns the width. */
static float text_run(App *a, TypeFace t, const char *s, float x, float y, float size, Color c, bool draw) {
    const Font *f = face(a, t);
    float sp = spacing(f, size), x0 = x;
    /* snap to whole window pixels, not layout units, so scaled text stays crisp */
    float k = a->ui_scale;
    y = roundf(y * k) / k;
    char seg[256];
    for (;;) {
        const char *p = strstr(s, PRIME);
        size_t len = p ? (size_t)(p - s) : strlen(s);
        if (len >= sizeof seg) len = sizeof seg - 1;
        memcpy(seg, s, len);
        seg[len] = 0;
        if (len) {
            if (draw) DrawTextEx(*f, seg, (Vector2){roundf(x * k) / k, y}, size, sp, c);
            x += MeasureTextEx(*f, seg, size, sp).x + sp;
        }
        if (!p) break;
        if (len) x -= size * PRIME_PULL;
        if (draw) DrawTextEx(*f, PRIME, (Vector2){roundf(x * k) / k, y}, size, sp, c);
        x += MeasureTextEx(*f, PRIME, size, sp).x + sp;
        s = p + strlen(PRIME);
    }
    return x > x0 ? x - x0 - sp : 0;
}

void ui_text_face(App *a, TypeFace t, const char *s, float x, float y, float size, Color c) {
    text_run(a, t, s, x, y, size, c, true);
}

float ui_text_width_face(App *a, TypeFace t, const char *s, float size) {
    return text_run(a, t, s, 0, 0, size, BLANK, false);
}

void ui_text(App *a, const char *s, float x, float y, float size, Color c) {
    ui_text_face(a, TYPE_REGULAR, s, x, y, size, c);
}

float ui_text_width(App *a, const char *s, float size) {
    return ui_text_width_face(a, TYPE_REGULAR, s, size);
}

/* Roundness that gives a corner radius of px on this rectangle. */
static float corner(Rectangle r, float px) { return px * 2 / fminf(r.width, r.height); }

/* A filled box with an optional 1px edge (edge.a == 0 for none). */
static void box(Rectangle r, float px, Color fill, Color edge) {
    DrawRectangleRounded(r, corner(r, px), 8, fill);
    if (edge.a) DrawRectangleRoundedLinesEx(r, corner(r, px), 8, 1, edge);
}

/* Shortens buf with "..." until it fits max_w. */
static void fit_text(App *a, TypeFace t, float px, char *buf, size_t size, float max_w) {
    if (ui_text_width_face(a, t, buf, px) <= max_w) return;
    size_t full = strlen(buf);
    for (size_t keep = full; keep-- > 0;) {
        char trial[256];
        snprintf(trial, sizeof trial, "%.*s...", (int)keep, buf);
        size_t n = strlen(trial);
        if (n < size && ui_text_width_face(a, t, trial, px) <= max_w) {
            memcpy(buf, trial, n + 1);
            return;
        }
    }
    buf[0] = 0;
}

/* Word-wrapped text; returns the height used. With draw false it only measures. */
static float text_wrapped(App *a, const char *s, float x, float y, float width, float size, Color c, bool draw) {
    char line[400], word[128];
    line[0] = 0;
    float yy = y;
    const char *p = s;
    while (*p) {
        int n = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < (int)sizeof word - 1) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        char trial[400];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && ui_text_width(a, trial, size) > width) {
            if (draw) ui_text(a, line, x, yy, size, c);
            yy += size * 1.25f;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) {
        if (draw) ui_text(a, line, x, yy, size, c);
        yy += size * 1.25f;
    }
    return yy - y;
}

bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled) {
    Vector2 m = GetMousePosition();
    bool over = enabled && CheckCollisionPointRec(m, r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    Color fill = active ? UI_ACCENT : (over ? UI_RAISED_HOVER : UI_RAISED);
    Color edge = active ? UI_ACCENT : UI_BORDER;
    Color tc = active ? UI_INK : UI_TEXT;
    if (!enabled) {
        fill = UI_PANEL;
        edge = UI_RULE;
        tc = UI_DISABLED;
    }
    box(r, 6, fill, edge);
    float size = 14;
    float w = ui_text_width_face(a, TYPE_MEDIUM, label, size);
    ui_text_face(a, TYPE_MEDIUM, label, r.x + (r.width - w) / 2, r.y + (r.height - size) / 2, size, tc);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* A checkbox with its label; returns true when clicked. */
static bool checkbox(App *a, Rectangle r, const char *label, bool on) {
    bool over = CheckCollisionPointRec(GetMousePosition(), r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    Rectangle b = {r.x, r.y + (r.height - 16) / 2, 16, 16};
    box(b, 4, on ? UI_ACCENT : UI_RAISED, on ? UI_ACCENT : UI_BORDER);
    if (on) {
        DrawLineEx((Vector2){b.x + 4, b.y + 8}, (Vector2){b.x + 7, b.y + 11}, 2, UI_INK);
        DrawLineEx((Vector2){b.x + 7, b.y + 11}, (Vector2){b.x + 12, b.y + 5}, 2, UI_INK);
    }
    ui_text(a, label, r.x + 26, r.y + (r.height - 14) / 2, 14, UI_TEXT);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void section(App *a, const char *title, float x, float *y) {
    ui_text_face(a, TYPE_MEDIUM, title, x, *y, 13, UI_TERTIARY);
    *y += 20;
}

/* A move as written on screen: U' with the real prime when the font has it. */
static void move_label(const App *a, Move m, char *out, size_t size) {
    const char *name = move_name(m);
    size_t n = 0;
    for (; *name && n + 4 < size; name++) {
        if (*name == '\'' && a->prime_ok)
            n += (size_t)snprintf(out + n, size - n, PRIME);
        else
            out[n++] = *name;
    }
    out[n] = 0;
}

/* What a move does, in words, for the big-move card. */
static void move_meaning(Move m, char *out, size_t size) {
    static const char *const FACE[MB_COUNT] = {
        "Top face", "Right face", "Front face", "Bottom face", "Left face", "Back face",
        "Middle slice", "Equator slice", "Standing slice", "Whole cube (x)", "Whole cube (y)", "Whole cube (z)",
        "Top two layers", "Right two layers", "Front two layers", "Bottom two layers", "Left two layers",
        "Back two layers",
    };
    /* slices and rotations are described by the face they follow */
    static const char *const LIKE[MB_COUNT] = {
        [MB_M] = "L", [MB_E] = "D", [MB_S] = "F", [MB_X] = "R", [MB_Y] = "U", [MB_Z] = "F",
        [MB_UW] = "U", [MB_RW] = "R", [MB_FW] = "F", [MB_DW] = "D", [MB_LW] = "L", [MB_BW] = "B",
    };
    int base = MOVE_BASE(m), turns = MOVE_TURNS(m);
    if (turns == 2) {
        snprintf(out, size, "%s: a half turn.", FACE[base]);
    } else if (LIKE[base]) {
        snprintf(out, size, "%s: a quarter turn %s the direction of %s.", FACE[base],
                 turns == 1 ? "in" : "against", LIKE[base]);
    } else {
        snprintf(out, size, "%s: a quarter turn %s, looking at the face.", FACE[base],
                 turns == 1 ? "clockwise" : "counter-clockwise");
    }
}

/* Number of steps that have moves, and which of them step `cur` is. */
static void step_counts(const Solution *s, int cur, int *k, int *total) {
    *k = 0;
    *total = 0;
    for (int st = 0; st < s->nsteps; st++) {
        if (!s->steps[st].count) continue;
        (*total)++;
        if (st <= cur) *k = *total;
    }
}

/* The step the playhead is in: the last one that has started, or -1 before the first. */
static int playhead_step(const App *a) {
    int cur = -1;
    for (int st = 0; st < a->sol.nsteps; st++)
        if (a->sol.steps[st].count && a->sol_pos >= a->sol.steps[st].start) cur = st;
    return cur;
}

/* ---------- top bar ---------- */

void ui_top_bar(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_BG);
    DrawRectangle((int)r.x, (int)(r.y + r.height - 1), (int)r.width, 1, UI_RULE);
    float x = r.x + PAD;
    if (app_busy(a)) {
        float t = (float)GetTime() * 6;
        Vector2 c = {x + 7, r.y + r.height / 2};
        DrawRing(c, 5, 7, t * 57.3f, t * 57.3f + 270, 24, UI_ACCENT);
        x += 22;
    }

    /* right side: the Keys button, then the solved badge when it applies */
    Rectangle keys = {r.x + r.width - PAD - 64, r.y + (r.height - 28) / 2, 64, 28};
    float right = keys.x - 12;
    bool solved = cube_is_solved(&a->cube) && !a->animating && a->qhead == a->qtail && !a->edit_mode;
    if (solved) {
        float bw = ui_text_width_face(a, TYPE_MEDIUM, "Solved", 13) + 24;
        Rectangle badge = {right - bw, r.y + (r.height - 24) / 2, bw, 24};
        box(badge, 12, Fade(UI_DONE, 0.16f), Fade(UI_DONE, 0.6f));
        float tw = ui_text_width_face(a, TYPE_MEDIUM, "Solved", 13);
        ui_text_face(a, TYPE_MEDIUM, "Solved", badge.x + (bw - tw) / 2, badge.y + 5, 13, UI_DONE);
        right = badge.x - 12;
    }
    if (ui_button(a, keys, "Keys", a->show_help, true)) a->show_help = !a->show_help;

    char buf[256];
    snprintf(buf, sizeof buf, "%s", a->status);
    fit_text(a, TYPE_REGULAR, 14, buf, sizeof buf, right - x);
    ui_text(a, buf, x, r.y + (r.height - 14) / 2, 14, a->status_error ? UI_BAD : UI_SECONDARY);
}

/* ---------- left: setup, method and solve ---------- */

/* "~55-65 moves" from a description ending in "(~55-65 moves)"; empty if there is none */
static void method_estimate(const char *desc, char *out, size_t size) {
    const char *p = strrchr(desc, '(');
    out[0] = 0;
    if (!p) return;
    snprintf(out, size, "%s", p + 1);
    char *close = strchr(out, ')');
    if (close) *close = 0;
}

/* the description without its trailing "(...)" estimate */
static void method_summary(const char *desc, char *out, size_t size) {
    snprintf(out, size, "%s", desc);
    char *p = strrchr(out, '(');
    if (!p) return;
    while (p > out && p[-1] == ' ') p--;
    *p = 0;
}

static bool method_row(App *a, Rectangle r, SolverId id, bool enabled) {
    bool sel = a->method == id;
    bool over = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    Color fill = sel ? Fade(UI_ACCENT, 0.16f) : (over ? UI_RAISED_HOVER : UI_RAISED);
    Color edge = sel ? UI_ACCENT : UI_BORDER;
    if (!enabled) fill = UI_PANEL;
    box(r, 6, fill, enabled ? edge : UI_RULE);
    ui_text_face(a, TYPE_MEDIUM, solver_name(id), r.x + 10, r.y + (r.height - 14) / 2, 14,
                 enabled ? UI_TEXT : UI_DISABLED);
    char est[64];
    method_estimate(solver_description(id), est, sizeof est);
    float ew = ui_text_width_face(a, TYPE_MONO, est, 13);
    ui_text_face(a, TYPE_MONO, est, r.x + r.width - ew - 10, r.y + (r.height - 13) / 2, 13,
                 sel ? UI_SECONDARY : UI_TERTIARY);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* Text box for typed moves, with its Apply button. */
static void move_input(App *a, Rectangle r, Rectangle apply_btn) {
    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->input_focus = CheckCollisionPointRec(m, r);
    if (CheckCollisionPointRec(m, r)) a->cursor = MOUSE_CURSOR_IBEAM;

    bool enabled = !app_busy(a) && !a->edit_mode;
    box(r, 6, (Color){16, 17, 20, 255}, a->input_focus ? UI_ACCENT : UI_BORDER);

    bool submit = false;
    if (a->input_focus) {
        int ch;
        size_t len = strlen(a->input);
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && len + 1 < sizeof a->input) a->input[len++] = (char)ch, a->input[len] = 0;
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) a->input[--len] = 0;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submit = true;
        if (IsKeyPressed(KEY_ESCAPE)) a->input_focus = false;
    }

    /* show the tail of long input */
    const char *shown = a->input;
    while (*shown && ui_text_width_face(a, TYPE_MONO, shown, 15) > r.width - 16) shown++;
    if (!a->input[0] && !a->input_focus)
        ui_text(a, "e.g. R U R' U'", r.x + 10, r.y + (r.height - 14) / 2, 14, UI_TERTIARY);
    else
        ui_text_face(a, TYPE_MONO, shown, r.x + 10, r.y + (r.height - 15) / 2, 15, UI_TEXT);
    if (a->input_focus && fmod(GetTime(), 1.0) < 0.55) {
        float cx = r.x + 10 + ui_text_width_face(a, TYPE_MONO, shown, 15) + 1;
        DrawRectangle((int)cx, (int)r.y + 8, 2, (int)r.height - 16, UI_TEXT);
    }

    if (ui_button(a, apply_btn, "Apply", false, enabled && a->input[0])) submit = true;
    if (submit && enabled && a->input[0]) {
        Move mv[256];
        int n = moves_parse(a->input, mv, 256);
        if (n < 0) {
            app_set_status(a, true, "Could not parse \"%s\"", a->input);
        } else {
            app_user_moves(a, mv, n, false);
            app_set_status(a, false, "Applied %d moves", n);
            a->input[0] = 0;
        }
    }
}

/* Left panel: set up a cube, then pick a method and solve it. */
void ui_left_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangle((int)(r.x + r.width - 1), 0, 1, (int)r.height, UI_RULE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;
    bool busy = app_busy(a);
    float half = (w - 8) / 2;

    ui_text_face(a, TYPE_SEMIBOLD, "Rubik's Cube Solver", x, y, 16, UI_TEXT);
    y += 30;

    section(a, "SET UP THE CUBE", x, &y);
    if (ui_button(a, (Rectangle){x, y, half, 34}, "Scramble", false, !busy)) app_scramble(a);
    if (ui_button(a, (Rectangle){x + half + 8, y, half, 34}, "Reset", false, !busy)) app_reset(a);
    y += 42;
    move_input(a, (Rectangle){x, y, w - 72, 34}, (Rectangle){x + w - 64, y, 64, 34});
    y += 46;

    if (a->edit_mode) {
        if (ui_button(a, (Rectangle){x, y, half, 32}, "Done painting", true, true)) app_set_edit(a, false);
        if (ui_button(a, (Rectangle){x + half + 8, y, half, 32}, "Check cube", false, true)) {
            const char *err = cube_validate(&a->cube);
            if (err)
                app_set_status(a, true, "Invalid: %s", err);
            else
                app_set_status(a, false, "Cube is valid and solvable");
        }
        y += 40;
        float sw = (w - 5 * 6) / 6;
        for (int c = 0; c < NUM_COLORS; c++) {
            Rectangle s = {x + c * (sw + 6), y, sw, 26};
            box(s, 6, STICKER_COLORS[c], a->paint_color == c ? UI_TEXT : Fade(UI_BLACK_EDGE, 0.5f));
            if (a->paint_color == c) DrawRectangleRoundedLinesEx(s, corner(s, 6), 8, 2, UI_TEXT);
            if (CheckCollisionPointRec(GetMousePosition(), s)) {
                a->cursor = MOUSE_CURSOR_POINTING_HAND;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->paint_color = c;
            }
        }
        y += 34;
    } else {
        if (ui_button(a, (Rectangle){x, y, w, 32}, "Paint stickers", false, !busy)) app_set_edit(a, true);
        y += 42;
    }

    /* the method block and Solve sit at the bottom; the net fills the gap above them */
    char summary[160];
    method_summary(solver_description(a->method), summary, sizeof summary);
    float desc_h = text_wrapped(a, summary, x, 0, w, 13, UI_TERTIARY, false);
    float solve_helper_y = r.y + r.height - PAD - 16;
    float solve_y = solve_helper_y - 8 - 44;
    float teach_y = solve_y - 12 - 30;
    float methods_y = teach_y - 16 - (20 + SOLVER_COUNT * 36 + 8 + desc_h);

    /* the net keeps its 12:9 shape, so centre it and its caption together in the free space */
    float avail = methods_y - 16 - y, caption_h = 24;
    float net_h = fminf(w / 12.0f * 9, avail - caption_h);
    if (net_h > 60) {
        float ny = y + (avail - net_h - caption_h) / 2;
        render_net(a, (Rectangle){x, ny, w, net_h}, NULL);
        ui_text(a, a->edit_mode ? "Click a sticker to paint it." : "View only. Paint stickers to edit.", x,
                ny + net_h + 8, 13, UI_TERTIARY);
    }

    y = methods_y;
    section(a, "CHOOSE A METHOD", x, &y);
    for (int i = 0; i < SOLVER_COUNT; i++) {
        if (method_row(a, (Rectangle){x, y, w, 32}, (SolverId)i, !busy)) a->method = (SolverId)i;
        y += 36;
    }
    y += 8;
    text_wrapped(a, summary, x, y, w, 13, UI_TERTIARY, true);

    if (checkbox(a, (Rectangle){x, teach_y, w, 30}, "Pause to explain each step", a->teach)) a->teach = !a->teach;
    const char *solve_label = busy ? "Solving..." : (a->has_sol ? "Solve again" : "Solve");
    if (ui_button(a, (Rectangle){x, solve_y, w, 44}, solve_label, !busy, !busy)) app_start_solve(a);
    const char *helper = busy ? "" : (a->has_sol ? "Replaces the current solution." : "Solves the cube as it is now.");
    ui_text(a, helper, x, solve_helper_y, 13, UI_TERTIARY);
}

/* ---------- centre: the next move and playback ---------- */

/* The big card under the cube: the move that runs next, what it does, and how far through the solution we are. */
void ui_move_display(App *a, Rectangle r) {
    Rectangle card = {r.x + 16, r.y + 6, r.width - 32, r.height - 12};
    box(card, 6, UI_PANEL, UI_RULE);
    const Solution *s = &a->sol;
    int n = a->has_sol ? s->n : 0;
    float x = card.x + PAD, y = card.y + 10;
    float right_w = 150, right_x = card.x + card.width - PAD - right_w;

    char caption[64] = "", label[16] = "-", meaning[200] = "";
    Color glyph_c = UI_TEXT;
    bool turning = a->animating;
    if (turning) {
        snprintf(caption, sizeof caption, "TURNING NOW");
        move_label(a, a->anim.m, label, sizeof label);
        move_meaning(a->anim.m, meaning, sizeof meaning);
    } else if (a->has_sol && a->done_pos < n) {
        snprintf(caption, sizeof caption, "NEXT MOVE, %d OF %d", a->done_pos + 1, n);
        move_label(a, s->moves[a->done_pos], label, sizeof label);
        move_meaning(s->moves[a->done_pos], meaning, sizeof meaning);
    } else if (a->has_sol) {
        snprintf(caption, sizeof caption, "SOLVED");
        snprintf(label, sizeof label, "Done");
        glyph_c = UI_DONE;
        snprintf(meaning, sizeof meaning, "All %d moves played. Step back with Left, or press Home to play them again.", n);
    } else {
        snprintf(caption, sizeof caption, "NO SOLUTION YET");
        snprintf(meaning, sizeof meaning, "Moves appear here after you press Solve.");
    }

    ui_text_face(a, TYPE_MEDIUM, caption, x, y, 13, turning ? UI_ACCENT : UI_TERTIARY);
    float gy = card.y + 22;
    float gw = ui_text_width_face(a, TYPE_MONO, label, 60);
    ui_text_face(a, TYPE_MONO, label, x, gy, 60, glyph_c);
    if (turning) DrawRectangle((int)x, (int)(gy + 62), (int)gw, 3, UI_ACCENT);

    float meaning_x = x + gw + 20;
    float meaning_right = a->has_sol ? right_x - 20 : card.x + card.width - PAD;
    if (meaning[0]) text_wrapped(a, meaning, meaning_x, gy + 4, meaning_right - meaning_x, 14, UI_SECONDARY, true);

    if (a->has_sol) {
        char done[64], prev[16] = "", next[16] = "", nb[64] = "";
        snprintf(done, sizeof done, "%d of %d", a->done_pos, n);
        ui_text_face(a, TYPE_MEDIUM, "MOVES DONE", right_x, y, 13, UI_TERTIARY);
        ui_text_face(a, TYPE_MONO, done, right_x, y + 16, 22, UI_TEXT);
        if (a->done_pos > 0) move_label(a, s->moves[a->done_pos - 1], prev, sizeof prev);
        if (a->done_pos + 1 < n) move_label(a, s->moves[a->done_pos + 1], next, sizeof next);
        /* only name the neighbours that exist */
        if (prev[0] && next[0]) snprintf(nb, sizeof nb, "Prev %s  Next %s", prev, next);
        else if (prev[0]) snprintf(nb, sizeof nb, "Prev %s", prev);
        else if (next[0]) snprintf(nb, sizeof nb, "Next %s", next);
        ui_text_face(a, TYPE_MONO, nb, right_x, y + 50, 13, UI_TERTIARY);
    }
}

/* Playback under the cube: a progress bar split by step, transport buttons and speed. */
void ui_transport(App *a, Rectangle r) {
    Rectangle card = {r.x + 16, r.y + 6, r.width - 32, r.height - 12};
    box(card, 6, UI_PANEL, UI_RULE);
    float x = card.x + PAD, w = card.width - 2 * PAD, y = card.y + 12;
    const Solution *s = &a->sol;
    bool has = a->has_sol && s->n > 0;

    /* first row: where we are, and the move count */
    char count[64];
    if (has)
        snprintf(count, sizeof count, "Move %d of %d", a->done_pos, s->n);
    else
        snprintf(count, sizeof count, "No solution yet");
    float cw = ui_text_width_face(a, TYPE_MONO, count, 13);
    ui_text_face(a, TYPE_MONO, count, x + w - cw, y, 13, UI_TERTIARY);
    if (!has) {
        ui_text(a, "Press Solve to make a solution you can play back.", x, y, 13, UI_TERTIARY);
    } else {
        int cur = playhead_step(a);
        if (cur >= 0) {
            int k, total;
            step_counts(s, cur, &k, &total);
            char label[160];
            snprintf(label, sizeof label, "Step %d of %d: %s", k, total, s->steps[cur].name);
            fit_text(a, TYPE_REGULAR, 13, label, sizeof label, w - cw - 16);
            ui_text(a, label, x, y, 13, UI_SECONDARY);
        }
    }

    /* second row: progress, one segment per step that has moves */
    float by = y + 24;
    if (!has) {
        box((Rectangle){x, by, w, 8}, 4, UI_RAISED, (Color){0});
    } else {
        Segment segs[64];
        int nseg = 0, total = 0;
        for (int st = 0; st < s->nsteps && nseg < 64; st++)
            if (s->steps[st].count) segs[nseg++] = (Segment){s->steps[st].start, s->steps[st].count};
        if (nseg == 0) segs[nseg++] = (Segment){0, s->n};
        for (int i = 0; i < nseg; i++) total += segs[i].count;

        const float gap = 3;
        float avail = w - gap * (float)(nseg - 1);
        Vector2 mouse = GetMousePosition();
        bool over = CheckCollisionPointRec(mouse, (Rectangle){x, by - 6, w, 20});
        if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
        bool scrub = over && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        float sx = x;
        for (int i = 0; i < nseg; i++) {
            float sw = avail * (float)segs[i].count / (float)total;
            float done = fminf(fmaxf((float)(a->done_pos - segs[i].start) / (float)segs[i].count, 0), 1);
            box((Rectangle){sx, by, sw, 8}, 4, UI_RAISED, (Color){0});
            if (done > 0) DrawRectangleRounded((Rectangle){sx, by, sw * done, 8}, 1, 4, UI_ACCENT);
            if (scrub && (mouse.x < sx + sw + gap || i == nseg - 1)) {
                float f = fminf(fmaxf((mouse.x - sx) / sw, 0), 1);
                int pos = segs[i].start + (int)roundf(f * (float)segs[i].count);
                a->playing = false;
                if (pos != a->sol_pos) app_jump(a, pos);
                scrub = false;
            }
            sx += sw + gap;
        }
    }

    /* third row: transport buttons on the left, speed on the right */
    bool tight = w < 420;
    float sb = tight ? 32 : 36, play_w = tight ? 64 : 84, name_w = tight ? 64 : 84, gap = 6;
    float by2 = y + 46;
    const char *labels[4] = {"|<", "<", ">", ">|"};
    float bx = x;
    for (int i = 0; i < 4; i++) {
        if (i == 2) {
            if (ui_button(a, (Rectangle){bx, by2, play_w, 34}, a->playing ? "Pause" : "Play", a->playing, has)) {
                if (!a->playing && a->sol_pos == s->n) app_jump(a, 0);
                a->playing = !a->playing;
            }
            bx += play_w + gap;
        }
        if (ui_button(a, (Rectangle){bx, by2, sb, 34}, labels[i], false, has)) {
            a->playing = false;
            switch (i) {
            case 0: app_jump(a, 0); break;
            case 1: app_step(a, -1); break;
            case 2: app_step(a, 1); break;
            case 3: app_jump(a, s->n); break;
            }
        }
        bx += sb + gap;
    }

    float plus_x = x + w - sb, name_x = plus_x - gap - name_w, minus_x = name_x - gap - sb;
    if (ui_button(a, (Rectangle){minus_x, by2, sb, 34}, "-", false, a->speed > 0)) a->speed--;
    if (ui_button(a, (Rectangle){plus_x, by2, sb, 34}, "+", false, a->speed < NUM_SPEEDS - 1)) a->speed++;
    const char *name = SPEED_NAMES[a->speed];
    float nw = ui_text_width_face(a, TYPE_MEDIUM, name, 14);
    ui_text_face(a, TYPE_MEDIUM, name, name_x + (name_w - nw) / 2, by2 + 10, 14, UI_TEXT);
    float lw = ui_text_width_face(a, TYPE_REGULAR, "Speed", 13);
    if (minus_x - 10 - lw > bx) ui_text_face(a, TYPE_REGULAR, "Speed", minus_x - 10 - lw, by2 + 11, 13, UI_TERTIARY);
}

/* Keyboard reference drawn over the 3D view while toggled on. */
void ui_help_overlay(App *a, Rectangle view) {
    if (!a->show_help) return;
    static const char *const lines[] = {
        "U R F D L B   turn a face (Shift = prime)",
        "M E S   turn a slice",
        "X Y Z   rotate the whole cube",
        "Enter   solve",
        "Space   play / pause",
        "Left / Right   step,  Home / End   jump",
        "Ctrl+S   scramble",
        "Drag   orbit the camera",
    };
    const int n = sizeof lines / sizeof lines[0];
    float w = 0;
    for (int i = 0; i < n; i++) w = fmaxf(w, ui_text_width(a, lines[i], 14));
    Rectangle b = {view.x + 20, view.y + 12, w + 2 * PAD, 44 + n * 22 + 30};
    box(b, 6, UI_PANEL, UI_BORDER);
    float x = b.x + PAD, y = b.y + 12;
    ui_text_face(a, TYPE_SEMIBOLD, "Keyboard", x, y, 16, UI_TEXT);
    y += 28;
    for (int i = 0; i < n; i++, y += 22) ui_text(a, lines[i], x, y, 14, UI_SECONDARY);
    ui_text(a, "Press ? or Esc to close", x, y + 4, 13, UI_TERTIARY);
}

/* ---------- right: the solution, its current step and the step list ---------- */

typedef struct {
    float content_h;           /* height of every step row, before scrolling */
    float cur_top, cur_h;      /* the current step's header top and height, relative to the list top */
    Rectangle inner;           /* the current step's token area, on screen */
    float inner_max;           /* how far that area scrolls; 0 when it all fits */
} StepsLayout;

static Rectangle intersect(Rectangle p, Rectangle q) {
    float left = fmaxf(p.x, q.x), top = fmaxf(p.y, q.y);
    float right = fminf(p.x + p.width, q.x + q.width), bottom = fminf(p.y + p.height, q.y + q.height);
    if (right <= left || bottom <= top) return (Rectangle){0, 0, 0, 0};
    return (Rectangle){left, top, right - left, bottom - top};
}

/* Places a step's move tokens in wrapped rows inside area, shifted up by scroll.
 * Returns the height of all rows; with draw false it only measures. */
static float token_width(App *a, Move m) {
    char name[16];
    move_label(a, m, name, sizeof name);
    return ui_text_width_face(a, TYPE_MONO, name, 14) + 16;
}

static int token_rows(App *a, const Solution *s, const SolveStep *step, float width) {
    float tx = 0;
    int rows = 1;
    for (int k = 0; k < step->count; k++) {
        float tw = token_width(a, s->moves[step->start + k]);
        if (tx > 0 && tx + tw > width) tx = 0, rows++;
        tx += tw + TOKEN_GAP;
    }
    return rows;
}

static float place_tokens(App *a, const Solution *s, const SolveStep *step, Rectangle area, float scroll, bool draw,
                          Vector2 mouse) {
    /* narrow the wrap width as far as the row count allows, so the last row is not a lone token */
    int rows = token_rows(a, s, step, area.width);
    if (rows > 1) {
        float lo = area.width / (float)rows, hi = area.width;
        for (int it = 0; it < 12; it++) {
            float mid = (lo + hi) / 2;
            if (token_rows(a, s, step, mid) > rows) lo = mid; else hi = mid;
        }
        area.width = hi;
    }
    float tx = area.x, row = 0;
    for (int k = 0; k < step->count; k++) {
        int j = step->start + k;
        char name[16];
        move_label(a, s->moves[j], name, sizeof name);
        float tw = token_width(a, s->moves[j]);
        if (tx > area.x && tx + tw > area.x + area.width) tx = area.x, row++;
        if (draw) {
            Rectangle tok = {tx, area.y + row * TOKEN_ROW - scroll, tw, TOKEN_H};
            bool hover = CheckCollisionPointRec(mouse, tok) && CheckCollisionPointRec(mouse, area);
            bool next = j == a->done_pos; /* the move running now, or the next one to run */
            bool played = j < a->done_pos;
            bool rot = move_is_rotation(s->moves[j]);
            Color fill = {0};
            Color tc = played ? UI_TEXT : UI_TERTIARY;
            if (rot && !next) tc = UI_ROTATION;
            if (next) {
                fill = UI_ACCENT;
                tc = UI_INK;
            } else if (hover) {
                fill = UI_RAISED_HOVER;
            } else if (played) {
                fill = UI_RAISED;
            }
            if (fill.a) DrawRectangleRounded(tok, corner(tok, 6), 8, fill);
            ui_text_face(a, TYPE_MONO, name, tok.x + 8, tok.y + (TOKEN_H - 14) / 2, 14, tc);
            if (hover) a->cursor = MOUSE_CURSOR_POINTING_HAND;
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                a->playing = false;
                app_jump(a, j + 1);
            }
        }
        tx += tw + TOKEN_GAP;
    }
    return step->count ? (row + 1) * TOKEN_ROW : 0;
}

/* Lays out or draws every step row. Only the current step opens to its moves; the rest are one line each. */
static void steps_pass(App *a, const Solution *s, int cur, Rectangle list, float x, float w, bool draw,
                       StepsLayout *L) {
    Vector2 mouse = GetMousePosition();
    bool over_list = CheckCollisionPointRec(mouse, list);
    float top = list.y - a->sol_scroll;
    float cy = top;
    L->cur_top = L->cur_h = 0;
    L->inner = (Rectangle){0};
    L->inner_max = 0;
    for (int st = 0; st < s->nsteps; st++) {
        const SolveStep *step = &s->steps[st];
        bool done = a->sol_pos >= step->start + step->count;
        bool current = st == cur;
        Rectangle row = {x, cy, w, ROW_H};
        bool over = over_list && CheckCollisionPointRec(mouse, row);
        if (current) L->cur_top = cy - top;

        if (draw) {
            if (over) {
                DrawRectangleRounded(row, corner(row, 6), 8, UI_RAISED);
                a->cursor = MOUSE_CURSOR_POINTING_HAND;
            }
            Vector2 mark = {x + 8, cy + ROW_H / 2};
            if (done) {
                DrawCircleV(mark, 8, UI_DONE);
                DrawLineEx((Vector2){mark.x - 3.5f, mark.y}, (Vector2){mark.x - 1, mark.y + 2.5f}, 2, UI_INK);
                DrawLineEx((Vector2){mark.x - 1, mark.y + 2.5f}, (Vector2){mark.x + 3.5f, mark.y - 2.5f}, 2, UI_INK);
            } else {
                DrawRing(mark, 5.5f, 7.5f, 0, 360, 24, current ? UI_ACCENT : UI_BORDER);
            }

            char name[96], count[16];
            snprintf(name, sizeof name, "%s", step->name);
            snprintf(count, sizeof count, "%d", step->count);
            float cw = ui_text_width_face(a, TYPE_MONO, count, 13);
            if (over && done) cw = ui_text_width_face(a, TYPE_MEDIUM, "Go to start", 13);
            fit_text(a, TYPE_REGULAR, 14, name, sizeof name, w - 24 - cw - 12);
            Color nc = current ? UI_ACCENT : (done ? UI_TEXT : UI_SECONDARY);
            ui_text_face(a, current ? TYPE_MEDIUM : TYPE_REGULAR, name, x + 24, cy + (ROW_H - 14) / 2, 14, nc);
            if (over && done)
                ui_text_face(a, TYPE_MEDIUM, "Go to start", x + w - cw, cy + (ROW_H - 13) / 2, 13, UI_ACCENT);
            else
                ui_text_face(a, TYPE_MONO, count, x + w - cw, cy + (ROW_H - 13) / 2, 13, UI_TERTIARY);
            if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                a->playing = false;
                app_jump(a, step->start);
            }
        }
        cy += ROW_H;

        if (current) {
            float tw = w - 24 - 10; /* leave room for the scroll thumb */
            Rectangle origin = {x + 24, cy, tw, 0};
            float tok_h = place_tokens(a, s, step, origin, 0, false, mouse);
            float inner_h = fminf(tok_h, TOKEN_ROWS_MAX * TOKEN_ROW);
            L->inner = (Rectangle){x + 24, cy, tw, inner_h};
            L->inner_max = fmaxf(0, tok_h - inner_h);
            if (a->move_scroll > L->inner_max) a->move_scroll = L->inner_max;
            if (a->move_scroll < 0) a->move_scroll = 0;
            if (draw && inner_h > 0) {
                scissor(a, intersect(L->inner, list));
                place_tokens(a, s, step, L->inner, a->move_scroll, true, mouse);
                scissor(a, list);
                if (L->inner_max > 0) {
                    float th = fmaxf(20, inner_h * inner_h / tok_h);
                    float ty = L->inner.y + (inner_h - th) * (a->move_scroll / L->inner_max);
                    DrawRectangleRounded((Rectangle){x + w - 4, ty, 3, th}, 0.5f, 4, UI_BORDER);
                }
            }
            cy += inner_h + 10;
            L->cur_h = cy - top - L->cur_top;
        } else {
            cy += 6;
        }
    }
    L->content_h = cy - top;
}

/* Right panel: the solution summary, the current step's lesson, and the step list. */
void ui_right_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangle((int)r.x, 0, 1, (int)r.height, UI_RULE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;

    ui_text_face(a, TYPE_SEMIBOLD, "Solution", x, y, 22, UI_TEXT);
    y += 34;

    if (!a->has_sol) {
        y += text_wrapped(a, "Scramble the cube, or paint a state, choose a method and press Solve.", x, y, w, 14,
                          UI_SECONDARY, true) + 16;
        section(a, "METHODS", x, &y);
        for (int i = 0; i < SOLVER_COUNT; i++) {
            char summ[160], est[64];
            method_summary(solver_description((SolverId)i), summ, sizeof summ);
            method_estimate(solver_description((SolverId)i), est, sizeof est);
            ui_text_face(a, TYPE_MEDIUM, solver_name((SolverId)i), x, y, 14,
                         a->method == (SolverId)i ? UI_ACCENT : UI_TEXT);
            float ew = ui_text_width_face(a, TYPE_MONO, est, 13);
            ui_text_face(a, TYPE_MONO, est, x + w - ew, y + 2, 13, UI_TERTIARY);
            y += 20;
            y += text_wrapped(a, summ, x, y, w, 13, UI_TERTIARY, true) + 12;
        }
        return;
    }

    const Solution *s = &a->sol;
    char head[160];
    snprintf(head, sizeof head, "%s  \xC2\xB7  %d moves  \xC2\xB7  %.1f ms", solver_name(a->sol_method), s->n, s->ms);
    fit_text(a, TYPE_REGULAR, 13, head, sizeof head, w);
    ui_text(a, head, x, y, 13, UI_TERTIARY);
    y += 26;

    /* the lesson for the step the playhead is in */
    int cur = playhead_step(a);
    if (cur >= 0) {
        const SolveStep *st = &s->steps[cur];
        StepLesson lesson = step_lesson(st);
        float body_w = w - 24, body_h = 0;
        if (lesson.text) body_h += text_wrapped(a, lesson.text, x + 12, 0, body_w, 14, UI_TEXT, false) + 6;
        if (lesson.note) body_h += text_wrapped(a, lesson.note, x + 12, 0, body_w, 13, UI_SECONDARY, false);
        Rectangle card = {x, y, w, 40 + body_h};
        box(card, 6, Fade(UI_ACCENT, 0.12f), Fade(UI_ACCENT, 0.45f));

        int k, total;
        step_counts(s, cur, &k, &total);
        char pos[32], title[96];
        snprintf(pos, sizeof pos, "%d of %d", k, total);
        float pw = ui_text_width_face(a, TYPE_MONO, pos, 13);
        snprintf(title, sizeof title, "%s", st->name);
        fit_text(a, TYPE_MEDIUM, 15, title, sizeof title, w - 24 - pw - 10);
        ui_text_face(a, TYPE_MEDIUM, title, x + 12, y + 10, 15, UI_ACCENT);
        ui_text_face(a, TYPE_MONO, pos, x + w - 12 - pw, y + 12, 13, UI_TERTIARY);

        float by = y + 34;
        if (lesson.text) by += text_wrapped(a, lesson.text, x + 12, by, body_w, 14, UI_TEXT, true) + 6;
        if (lesson.note) text_wrapped(a, lesson.note, x + 12, by, body_w, 13, UI_SECONDARY, true);
        y += card.height + 14;
    }

    section(a, "STEPS", x, &y);
    Rectangle list = {r.x, y, r.width, r.y + r.height - y - 8};
    if (list.height <= 0) return;
    Vector2 mouse = GetMousePosition();
    float list_x = x, list_w = w - 10;

    /* measure first: the wheel and the current-step follow need the layout */
    StepsLayout L;
    steps_pass(a, s, cur, list, list_x, list_w, false, &L);
    float max_scroll = fmaxf(0, L.content_h - list.height);

    float wheel = CheckCollisionPointRec(mouse, list) ? GetMouseWheelMove() : 0;
    if (wheel != 0) {
        if (L.inner_max > 0 && CheckCollisionPointRec(mouse, L.inner))
            a->move_scroll -= wheel * 30;
        else
            a->sol_scroll -= wheel * 40, a->follow_current = false;
    }

    /* scroll thumb: drag it, or click the track to jump */
    Rectangle track = {r.x + r.width - 8, list.y, 6, list.height};
    float thumb_h = fmaxf(24, list.height * list.height / fmaxf(L.content_h, 1));
    float thumb_y = list.y + (list.height - thumb_h) * (max_scroll > 0 ? a->sol_scroll / max_scroll : 0);
    if (max_scroll > 0) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, (Rectangle){track.x - 6, list.y, 18, list.height})) {
            a->thumb_drag = true;
            a->thumb_grab = CheckCollisionPointRec(mouse, (Rectangle){track.x - 4, thumb_y, 14, thumb_h})
                                ? mouse.y - thumb_y
                                : thumb_h / 2;
        }
        if (a->thumb_drag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float f = (mouse.y - a->thumb_grab - list.y) / (list.height - thumb_h);
            a->sol_scroll = fminf(fmaxf(f, 0), 1) * max_scroll;
            a->follow_current = false;
        } else {
            a->thumb_drag = false;
        }
    }

    /* keep the current step on screen while it plays */
    if (a->follow_current && cur >= 0) {
        if (L.cur_top - a->sol_scroll < 0) a->sol_scroll = L.cur_top;
        if (L.cur_top + L.cur_h - a->sol_scroll > list.height) a->sol_scroll = L.cur_top + L.cur_h - list.height;
    }
    if (a->sol_scroll > max_scroll) a->sol_scroll = max_scroll;
    if (a->sol_scroll < 0) a->sol_scroll = 0;

    scissor(a, list);
    steps_pass(a, s, cur, list, list_x, list_w, true, &L);
    EndScissorMode();

    if (max_scroll > 0) {
        float ty = list.y + (list.height - thumb_h) * (a->sol_scroll / max_scroll);
        bool hot = CheckCollisionPointRec(mouse, track) || a->thumb_drag;
        DrawRectangleRounded((Rectangle){track.x + 1, ty, 4, thumb_h}, 0.5f, 4, hot ? UI_TERTIARY : UI_BORDER);
    }
}
