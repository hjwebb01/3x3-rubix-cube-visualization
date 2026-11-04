/* Immediate-mode widgets and the panels: setup and method (left), the cube's playback (centre), the solution (right). */
#include <ctype.h>
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
#define UI_BLACK_EDGE ((Color){0, 0, 0, 255})     /* the dark rim around a paint swatch */

/* A run of moves drawn as one segment of the progress bar. */
typedef struct {
    int start, count;
    int step; /* index into Solution.steps, or -1 */
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

/* BeginScissorMode takes window pixels, but the UI is laid out in scaled units */
static void scissor(const App *a, Rectangle r) {
    float k = a->ui_scale;
    BeginScissorMode((int)(r.x * k), (int)(r.y * k), (int)ceilf(r.width * k), (int)ceilf(r.height * k));
}

/* The default font is ASCII only and is narrow without spacing, so it gets a little tracking. */
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

/* Shortens buf with an ellipsis until it fits max_w; "..." when the face lacks U+2026. */
static void fit_text(App *a, TypeFace t, float px, char *buf, size_t size, float max_w) {
    if (ui_text_width_face(a, t, buf, px) <= max_w) return;
    const Font *f = face(a, t);
    const char *ell = f->glyphs[GetGlyphIndex(*f, 0x2026)].value == 0x2026 ? "\xE2\x80\xA6" : "...";
    size_t full = strlen(buf);
    for (size_t keep = full; keep-- > 0;) {
        char trial[256];
        snprintf(trial, sizeof trial, "%.*s%s", (int)keep, buf, ell);
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
            yy += size * 1.45f;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) {
        if (draw) ui_text(a, line, x, yy, size, c);
        yy += size * 1.45f;
    }
    return yy - y;
}

void ui_focus_keys(App *a) {
    a->focus_last = a->focus_n < FOCUS_MAX ? a->focus_n : FOCUS_MAX;
    a->focus_n = 0;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->press_at = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || (IsKeyPressed(KEY_ESCAPE) && !a->input[0])) a->focus = 0; /* rings are for keyboard use */
    if (IsKeyPressed(KEY_TAB) && a->focus_last > 0) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        int f = a->focus;
        for (int i = 0; i < a->focus_last; i++) {
            f += shift ? -1 : 1;
            if (f > a->focus_last) f = 1;
            if (f < 1) f = a->focus_last;
            if (a->focus_ok[f]) break;
        }
        a->focus = f;
    }
}

bool ui_input_focused(const App *a) { return a->focus != 0 && a->focus == a->focus_input; }

/* The left button went down inside r, and the mouse is still inside it. */
static bool pressed_in(const App *a, Rectangle r) {
    return CheckCollisionPointRec(a->press_at, r) && CheckCollisionPointRec(GetMousePosition(), r);
}

/* The left button is down, and went down inside r. */
static bool held(const App *a, Rectangle r) { return IsMouseButtonDown(MOUSE_BUTTON_LEFT) && pressed_in(a, r); }

/* A click on r: pressed and released inside it, so sliding off before letting go cancels. */
bool ui_clicked(const App *a, Rectangle r) { return IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && pressed_in(a, r); }

/* Takes the next Tab stop for the widget at r and rings it while focused.
 * Returns true when it is focused, enabled, and Enter is pressed. */
bool ui_focus_stop(App *a, Rectangle r, float radius, bool enabled) {
    int id = ++a->focus_n;
    if (id <= FOCUS_MAX) a->focus_ok[id] = enabled;
    if (a->focus != id) return false;
    Rectangle ring = {r.x - 3, r.y - 3, r.width + 6, r.height + 6};
    DrawRectangleRoundedLinesEx(ring, corner(ring, radius + 3), 8, 2, UI_TEXT);
    return enabled && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || (IsKeyPressed(KEY_SPACE) && !a->input[0]));
}

bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled) {
    Vector2 m = GetMousePosition();
    bool over = enabled && CheckCollisionPointRec(m, r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    bool down = over && held(a, r);
    Color fill = active ? (down ? ColorBrightness(UI_ACCENT, -0.15f)
                                : over ? ColorBrightness(UI_ACCENT, 0.10f) : UI_ACCENT)
                        : (down ? UI_PRESSED : over ? UI_RAISED_HOVER : UI_RAISED);
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
    bool key = ui_focus_stop(a, r, 6, enabled);
    return key || (enabled && ui_clicked(a, r));
}

void ui_rotations(const Solution *s, const char *sep, char *out, size_t size) {
    int rot = s->n - solution_move_count(s);
    out[0] = 0;
    if (rot > 0) snprintf(out, size, "%s%d %s", sep, rot, rot == 1 ? "rotation" : "rotations");
}

/* A checkbox with its label; returns true when clicked. */
static bool checkbox(App *a, Rectangle r, const char *label, bool on) {
    bool over = CheckCollisionPointRec(GetMousePosition(), r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    Rectangle b = {r.x, r.y + (r.height - 16) / 2, 16, 16};
    bool down = held(a, r);
    box(b, 4, on ? (down ? ColorBrightness(UI_ACCENT, -0.15f) : UI_ACCENT) : (down ? UI_PRESSED : UI_RAISED),
        on ? UI_ACCENT : UI_OUTLINE);
    if (on) {
        DrawLineEx((Vector2){b.x + 4, b.y + 8}, (Vector2){b.x + 7, b.y + 11}, 2, UI_INK);
        DrawLineEx((Vector2){b.x + 7, b.y + 11}, (Vector2){b.x + 12, b.y + 5}, 2, UI_INK);
    }
    ui_text(a, label, r.x + 26, r.y + (r.height - 14) / 2, 14, UI_TEXT);
    bool key = ui_focus_stop(a, r, 6, true);
    return key || ui_clicked(a, r);
}

/* A 13px all-caps caption, tracked out a little so the capitals don't crowd. Returns its width. */
static float caption(App *a, const char *s, float x, float y, Color c) {
    float sp = spacing(&a->font_medium, 13) + 0.8f, k = a->ui_scale;
    DrawTextEx(a->font_medium, s, (Vector2){roundf(x * k) / k, roundf(y * k) / k}, 13, sp, c);
    return MeasureTextEx(a->font_medium, s, 13, sp).x;
}

static void section(App *a, const char *title, float x, float *y) {
    caption(a, title, x, *y, UI_TERTIARY);
    *y += 20;
}

/* A hint for the control under the mouse (why it is disabled, what a term means): shown under it on hover, drawn last so nothing paints over it. */
static struct {
    Rectangle r;
    const char *text;
} hover_tip;

static void tip_at(Rectangle r, const char *text) {
    if (CheckCollisionPointRec(GetMousePosition(), r)) hover_tip.r = r, hover_tip.text = text;
}

static void draw_tip(App *a, Rectangle panel) {
    if (!hover_tip.text) return;
    float w = fminf(panel.width - 16, ui_text_width(a, hover_tip.text, 13) + 20);
    float h = text_wrapped(a, hover_tip.text, 0, 0, w - 20, 13, UI_SECONDARY, false) + 14;
    Rectangle b = {fmaxf(panel.x + 8, fminf(hover_tip.r.x, panel.x + panel.width - 8 - w)),
                   fminf(hover_tip.r.y + hover_tip.r.height + 6, panel.y + panel.height - h - 8), w, h};
    box(b, 6, UI_RAISED, UI_BORDER);
    text_wrapped(a, hover_tip.text, b.x + 10, b.y + 7, w - 20, 13, UI_SECONDARY, true);
    hover_tip.text = NULL;
}

/* One line on what a CFOP step is, for the jargon in its name; NULL where the name says it. */
static const char *step_gloss(const SolveStep *st) {
    switch (st->lesson) {
    case LESSON_INSPECTION: return "Turn the cube so white is on the bottom.";
    case LESSON_CROSS: return "Four white edges on the bottom face.";
    case LESSON_F2L: return "First Two Layers: a corner and its edge, one pair at a time.";
    case LESSON_OLL_SKIP:
    case LESSON_OLL: return "Orient Last Layer: make the whole top face yellow.";
    case LESSON_PLL_SKIP:
    case LESSON_PLL: return "Permute Last Layer: move the top pieces into place.";
    default: return NULL;
    }
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

    /* right side: the Shortcuts button, then the solved badge when it applies */
    float keys_w = ui_text_width_face(a, TYPE_MEDIUM, "Shortcuts", 14) + 24;
    Rectangle keys = {r.x + r.width - PAD - keys_w, r.y + (r.height - 28) / 2, keys_w, 28};
    float right = keys.x - 12;
    PlaybackView v = playback_view(&a->pb);
    if (a->can_undo) {
        Rectangle undo = {right - 64, keys.y, 64, 28};
        if (ui_button(a, undo, "Undo", false, app_can_change_cube(a))) app_undo(a);
        right = undo.x - 12;
    }
    bool solved = cube_is_solved(v.cube) && !v.turning && v.idle && !a->edit_mode;
    if (solved) {
        float bw = ui_text_width_face(a, TYPE_MEDIUM, "Solved", 13) + 24;
        Rectangle badge = {right - bw, r.y + (r.height - 24) / 2, bw, 24};
        box(badge, 12, Fade(UI_DONE, 0.16f), Fade(UI_DONE, 0.6f));
        float tw = ui_text_width_face(a, TYPE_MEDIUM, "Solved", 13);
        ui_text_face(a, TYPE_MEDIUM, "Solved", badge.x + (bw - tw) / 2, badge.y + 5, 13, UI_DONE);
        right = badge.x - 12;
    }
    /* speed: the playback rate, beside the status it changes; - and = step it too */
    float y = r.y + (r.height - 28) / 2, plus_x = right - 28, name_x = plus_x - 6 - 76, minus_x = name_x - 6 - 28;
    if (ui_button(a, (Rectangle){minus_x, y, 28, 28}, "\xE2\x88\x92", false, a->speed > 0)) a->speed--;
    const char *speed = SPEED_NAMES[a->speed];
    float nw = ui_text_width_face(a, TYPE_MEDIUM, speed, 14);
    ui_text_face(a, TYPE_MEDIUM, speed, name_x + (76 - nw) / 2, y + 7, 14, UI_TEXT);
    if (ui_button(a, (Rectangle){plus_x, y, 28, 28}, "+", false, a->speed < NUM_SPEEDS - 1)) a->speed++;
    float slw = ui_text_width_face(a, TYPE_REGULAR, "Speed", 13);
    const float STATUS_MIN = 300; /* the Speed label and Screen only give way before the status clips */
    right = minus_x - 12;
    if (minus_x - 8 - slw - 12 - x >= STATUS_MIN) {
        ui_text_face(a, TYPE_REGULAR, "Speed", minus_x - 8 - slw, y + 7, 13, UI_TERTIARY);
        right = minus_x - 8 - slw - 12;
        float so_w = ui_text_width_face(a, TYPE_MEDIUM, "Screen only", 13) + 24;
        if (right - so_w - 12 - x >= STATUS_MIN) {
            Rectangle so = {right - so_w, y, so_w, 28};
            if (ui_button(a, so, "Screen only", a->screen_only, true)) app_toggle_screen_only(a);
            right = so.x - 12;
        }
    }
    if (ui_button(a, keys, "Shortcuts", false, true)) a->show_help = !a->show_help;

    char buf[256];
    snprintf(buf, sizeof buf, "%s", a->status);
    Color sc = a->status_error ? UI_BAD : UI_SECONDARY;
    /* one line at 14px; two at 13px when it doesn't fit, so long instructions stay readable without a mouse */
    if (ui_text_width(a, buf, 14) <= right - x) {
        ui_text(a, buf, x, r.y + (r.height - 14) / 2, 14, sc);
        return;
    }
    float h2 = text_wrapped(a, buf, x, 0, right - x, 13, sc, false);
    if (h2 <= 2 * 13 * 1.45f + 0.5f) {
        text_wrapped(a, buf, x, r.y + (r.height - h2) / 2 + 2, right - x, 13, sc, true);
        return;
    }
    fit_text(a, TYPE_REGULAR, 14, buf, sizeof buf, right - x);
    ui_text(a, buf, x, r.y + (r.height - 14) / 2, 14, sc);
    /* shortcut: past two lines the full text is hover-only; grow the bar if statuses get that long */
    Rectangle hot = {x, r.y, right - x, r.height};
    if (CheckCollisionPointRec(GetMousePosition(), hot)) {
        float tw = r.x + r.width - PAD - x;
        float th = text_wrapped(a, a->status, 0, 0, tw - 20, 14, sc, false);
        Rectangle tip = {x - 10, r.y + r.height - 2, tw, th + 14};
        box(tip, 6, UI_RAISED, UI_BORDER);
        text_wrapped(a, a->status, tip.x + 10, tip.y + 7, tw - 20, 14, sc, true);
    }
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
    bool down = over && held(a, r);
    Color fill = sel ? Fade(UI_ACCENT, down ? 0.28f : over ? 0.22f : 0.16f)
                     : (down ? UI_PRESSED : over ? UI_RAISED_HOVER : UI_RAISED);
    Color edge = sel ? UI_ACCENT : UI_BORDER;
    if (!enabled && !sel) fill = UI_PANEL, edge = UI_RULE;
    box(r, 6, fill, edge);
    if (sel) DrawRectangleRounded((Rectangle){r.x + 5, r.y + 7, 3, r.height - 14}, 1, 4, UI_ACCENT);
    ui_text_face(a, TYPE_MEDIUM, solver_name(id), r.x + 13, r.y + (r.height - 14) / 2, 14,
                 enabled ? UI_TEXT : UI_DISABLED);
    char est[64];
    method_estimate(solver_description(id), est, sizeof est);
    const Solution *sol = playback_view(&a->pb).sol;
    if (sol && a->sol_method == id) snprintf(est, sizeof est, "%d moves", solution_move_count(sol)); /* the real count once solved */
    float ew = ui_text_width_face(a, TYPE_MONO, est, 13);
    if (ui_text_width_face(a, TYPE_MEDIUM, solver_name(id), 14) + ew + 30 <= r.width) /* grid cells drop it */
        ui_text_face(a, TYPE_MONO, est, r.x + r.width - ew - 10, r.y + (r.height - 13) / 2, 13,
                     sel ? UI_SECONDARY : UI_TERTIARY);
    bool key = ui_focus_stop(a, r, 6, enabled);
    return key || (enabled && ui_clicked(a, r));
}

/* The commands that settle a torn seam: fix, trust MOVES, paint top. Only while Coach is unsynced; the rest of
 * the text after the word is the typed moves. Returns false when the words are not one of them. */
static bool coach_command(App *a, const char *w, const char *rest) {
    if (playback_view(&a->pb).coach.mode != COACH_UNSYNCED) return false;
    if (!strcmp(w, "fix")) {
        app_coach_restore(a);
        a->mismatched_count = 0;
        return true;
    }
    if (!strcmp(w, "paint")) {
        char top[8];
        if (sscanf(rest, " %7s", top) != 1) return false;
        for (char *c = top; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (strcmp(top, "top") != 0) return false;
        app_coach_paint_top(a);
        return true;
    }
    if (!strcmp(w, "trust")) {
        Move mv[256];
        while (isspace((unsigned char)*rest)) rest++;
        int n = *rest ? moves_parse(rest, mv, 256) : a->mismatched_count;
        if (n <= 0) return false;
        app_coach_trust(a, *rest ? mv : a->mismatched_moves, n);
        a->mismatched_count = 0;
        return true;
    }
    return false;
}

/* The word after `show`: "thistle" or "thistlethwaite" asks for Thistlethwaite; anything else is Kociemba. */
static bool thistle_word(const char *rest) {
    char w[16];
    if (sscanf(rest, " %15s", w) != 1) return false;
    for (char *c = w; *c; c++) *c = (char)tolower((unsigned char)*c);
    return !strcmp(w, "thistle") || !strcmp(w, "thistlethwaite");
}

/* Words the prompt takes in place of moves. Returns false when text is not one of them. */
static bool prompt_command(App *a, const char *text) {
    static const struct {
        const char *word;
        int method; /* -1: not a solve */
    } WORDS[] = {{"teach", SOLVER_BEGINNER}, {"faster", SOLVER_CFOP}, {"show", SOLVER_KOCIEMBA},
                 {"solve", -1}, {"scramble", -1}, {"reset", -1}, {"undo", -1}, {"xray", -1}, {"help", -1}};
    char w[16];
    int used = 0;
    if (sscanf(text, " %15s%n", w, &used) != 1) return false;
    for (char *c = w; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (coach_command(a, w, text + used)) return true;
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
        if (strcmp(w, WORDS[i].word)) continue;
        if (WORDS[i].method >= 0) {
            if (app_busy(a)) return true;
            if (playback_view(&a->pb).coach.locked) playback_coach_end(&a->pb);
            a->method = (SolverId)WORDS[i].method;
            if (!strcmp(w, "show") && thistle_word(text + used)) a->method = SOLVER_THISTLETHWAITE;
            bool idle = !app_busy(a);
            app_start_solve(a);
            if (idle && app_busy(a)) a->xray = !strcmp(w, "teach"); /* Coach: teach highlights each Step's pieces */
        } else if (!strcmp(w, "solve")) app_start_solve(a);
        else if (!strcmp(w, "scramble")) app_scramble(a);
        else if (!strcmp(w, "reset")) app_reset(a);
        else if (!strcmp(w, "undo")) app_undo(a);
        else if (!strcmp(w, "xray")) a->xray = !a->xray;
        else a->show_help = !a->show_help;
        return true;
    }
    return false;
}

/* Typed moves while a Step is practised: they are checked against the Step, never applied to the screen. */
static void coach_typed(App *a, const Move *mv, int n) {
    if (playback_view(&a->pb).coach.mode == COACH_UNSYNCED) {
        app_set_status(a, true, "Fix the seam first: fix, trust, or paint top");
        return; /* the input stays for the user to retype after fixing */
    }
    CoachTyped r = playback_coach_typed(&a->pb, mv, n);
    if (r == COACH_TYPED_IGNORED) {
        app_set_status(a, true, "Enter moves after the current animation finishes");
        return;
    }
    a->mismatched_count = r == COACH_TYPED_MISMATCH ? n : 0;
    if (a->mismatched_count) memcpy(a->mismatched_moves, mv, (size_t)n * sizeof *mv);
    a->input[0] = 0;
    a->input_bad = false;
    if (r == COACH_TYPED_MATCH) app_set_status(a, false, "Your moves match: the screen follows your progress");
    else app_set_status(a, false, "Your moves differ from the Step: fix, trust, or paint top");
}

/* The prompt: typed moves or a command word, with its Apply button. Open unless the Solver runs or a Step is
 * playing through; while a Step is practised it takes the typed-move check and the Coach commands. */
static void move_input(App *a, Rectangle r, Rectangle apply_btn) {
    Vector2 m = GetMousePosition();
    bool enabled = !app_busy(a) && !playback_view(&a->pb).coach.running;
    ui_focus_stop(a, r, 6, enabled); /* Enter here submits the moves, below */
    a->focus_input = a->focus_n;
    if (enabled && a->focus_prompt_next) a->focus = a->focus_input;
    a->focus_prompt_next = false;
    /* a click inside takes focus (ui_focus_keys already dropped it for any click) */
    if (enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, r)) a->focus = a->focus_input;
    if (!enabled && ui_input_focused(a)) a->focus = 0;
    bool focused = ui_input_focused(a);
    if (enabled && CheckCollisionPointRec(m, r)) a->cursor = MOUSE_CURSOR_IBEAM;

    if (enabled)
        box(r, 6, (Color){16, 17, 20, 255}, a->input_bad ? UI_BAD : focused ? UI_ACCENT : UI_BORDER);
    else
        box(r, 6, UI_PANEL, UI_RULE);

    bool submit = false;
    if (focused) {
        int ch;
        size_t len = strlen(a->input);
        while ((ch = GetCharPressed()) > 0)
            /* a Space into an empty prompt is the play/pause shortcut instead (handle_keys) */
            if (ch >= 32 && ch < 127 && !(ch == ' ' && len == 0) && len + 1 < sizeof a->input) a->input[len++] = (char)ch, a->input[len] = 0, a->input_bad = false;
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) a->input[--len] = 0, a->input_bad = false;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submit = true;
    }

    /* show the tail of long input */
    const char *shown = a->input;
    while (*shown && ui_text_width_face(a, TYPE_MONO, shown, 15) > r.width - 16) shown++;
    if (!a->input[0] && !focused) {
        char hint[64] = "R U R' U' or teach · faster · show";
        if (a->edit_mode) snprintf(hint, sizeof hint, "Face %c: nine colors from W R G Y O B", "URFDLB"[a->typed_face]);
        ui_text(a, hint, r.x + 10, r.y + (r.height - 14) / 2, 14, enabled ? UI_TERTIARY : UI_DISABLED);
    } else {
        ui_text_face(a, TYPE_MONO, shown, r.x + 10, r.y + (r.height - 15) / 2, 15, enabled ? UI_TEXT : UI_DISABLED);
    }
    if (focused && fmod(GetTime(), 1.0) < 0.55) {
        float cx = r.x + 10 + ui_text_width_face(a, TYPE_MONO, shown, 15) + 1;
        DrawRectangle((int)cx, (int)r.y + 8, 2, (int)r.height - 16, UI_TEXT);
    }

    if (!enabled) {
        const char *why = playback_view(&a->pb).coach.running ? "Wait for the Step to play through." : "Wait for the solver to finish.";
        tip_at(r, why);
        tip_at(apply_btn, why);
    } else if (!a->input[0]) {
        tip_at(apply_btn, "Type some moves first.");
    }
    if (ui_button(a, apply_btn, "Apply", false, enabled && a->input[0])) submit = true;
    if (submit && enabled && a->input[0] && prompt_command(a, a->input)) {
        a->input[0] = 0;
        a->input_bad = false;
    } else if (submit && enabled && a->input[0] && a->edit_mode) {
        /* painting: the prompt takes a face, nine colors in facelet order */
        if (app_type_face(a, a->input)) a->input[0] = 0, a->input_bad = false;
        else a->input_bad = true;
    } else if (submit && enabled && a->input[0]) {
        Move mv[256];
        int n = moves_parse(a->input, mv, 256);
        if (n < 0) {
            app_set_status(a, true, "Unable to read \"%s\"", a->input);
            a->input_bad = true;
        } else if (playback_view(&a->pb).coach.locked) {
            coach_typed(a, mv, n);
        } else if (playback_view(&a->pb).teach_stop) {
            if (!playback_teach_continue(&a->pb, mv, n)) {
                app_set_status(a, true, "Not the next move: type the move to continue");
                a->input_bad = true;
            } else {
                app_set_status(a, false, "Continuing");
                a->input[0] = 0;
                a->input_bad = false;
            }
        } else {
            app_user_moves(a, mv, n, false);
            app_set_status(a, false, "Applied %d %s", n, n == 1 ? "move" : "moves");
            a->input_bad = false;
            a->input[0] = 0;
        }
    }
}

/* Rectangle `len` long along the seam from `s`, `thick` across it from `d`, both measured from r's edge. */
static Rectangle seam_box(Rectangle r, bool across, float s, float len, float d, float thick) {
    return across ? (Rectangle){r.x + s, r.y + d, len, thick} : (Rectangle){r.x + d, r.y + s, thick, len};
}

/* A point `s` along the seam and `d` across it from r's edge. */
static Vector2 seam_pt(Rectangle r, bool across, float s, float d) {
    return across ? (Vector2){r.x + s, r.y + d} : (Vector2){r.x + d, r.y + s};
}

/* The seam between your cube and the screen's: dashed while there is nothing to keep in sync, then a
 * track that fills with the Playhead and has a notch at each Step's start, torn while Coach is unsynced.
 * Stacked, the seam runs across the window: `along` is then x, and `across` is y. */
void ui_seam(App *a, Rectangle r) {
    PlaybackView v = playback_view(&a->pb);
    bool across = r.width > r.height;
    float span = across ? r.width : r.height, t = across ? r.height : r.width; /* along and across the seam */
    /* the seam is the Playhead: drag it to scrub; playback_seek ignores it while a Step is practised */
    bool scrub = v.sol && !v.coach.locked && CheckCollisionPointRec(GetMousePosition(), r);
    if (scrub) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    if (scrub && held(a, r)) {
        Vector2 m = GetMousePosition();
        float f = fminf(fmaxf(((across ? m.x : m.y) - (across ? r.x : r.y)) / span, 0), 1);
        playback_seek(&a->pb, (int)roundf(f * (float)v.sol->n));
    }
    char label[32];
    Color edge;
    DrawRectangleRec(r, UI_BG);
    if (v.coach.mode == COACH_UNSYNCED) {
        /* zigzag: the sync is torn. It runs 4 px past each end of the seam, over the panel edges, so the tear reads */
        for (float s = -4; s < span + 4; s += 16) {
            DrawLineEx(seam_pt(r, across, s, 2), seam_pt(r, across, s + 8, t - 2), 3, UI_DRIFT);
            DrawLineEx(seam_pt(r, across, s + 8, t - 2), seam_pt(r, across, s + 16, 2), 3, UI_DRIFT);
        }
        snprintf(label, sizeof label, "TORN");
        edge = UI_DRIFT;
    } else if (!v.sol && !v.sealed) {
        for (float s = 4; s < span; s += 20)
            DrawRectangleRec(seam_box(r, across, s, 10, t / 2 - 1, 2), UI_BORDER);
        snprintf(label, sizeof label, "OPEN");
        edge = UI_BORDER;
    } else {
        Rectangle track = seam_box(r, across, 0, span, 4, t - 8);
        DrawRectangleRec(track, Fade(UI_ACCENT, 0.25f));
        float frac = !v.sol ? 0 : v.sol->n ? (float)v.playhead / v.sol->n : 1;
        DrawRectangleRec(seam_box(r, across, 0, span * frac, 4, t - 8), UI_ACCENT);
        for (int i = 1; v.sol && i < v.sol->nsteps; i++)
            if (v.sol->steps[i].count && v.sol->n)
                DrawRectangleRec(seam_box(r, across, span * v.sol->steps[i].start / v.sol->n, 2, 4, t - 8), UI_BG);
        snprintf(label, sizeof label, v.playhead == 0 ? "SEALED" : "IN SYNC");
        edge = UI_ACCENT;
    }
    /* the label sits over the seam's top, wider than the strip; stacked, it sits centred on the strip */
    float lw = ui_text_width_face(a, TYPE_MONO, label, 11) + 16;
    Rectangle tag = {r.x + r.width / 2 - lw / 2, across ? r.y + (r.height - 22) / 2 : r.y + 10, lw, 22};
    box(tag, 4, edge, edge);
    ui_text_face(a, TYPE_MONO, label, tag.x + 8, tag.y + 5, 11, UI_INK);
}

/* The prompt bar along the bottom of the window. */
void ui_prompt(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, 1, UI_RULE);
    float x = r.x + PAD, w = r.width - 2 * PAD, h = 36, y = r.y + (r.height - h) / 2;
    const char *bad = a->edit_mode ? "Nine colors, like WWWWWWWWW." : "Use moves like R U' F2, or teach, faster, show.";
    float bad_w = a->input_bad ? ui_text_width(a, bad, 13) + 16 : 0;
    move_input(a, (Rectangle){x, y, w - 80 - bad_w, h}, (Rectangle){x + w - 72, y, 72, h});
    if (a->input_bad) ui_text(a, bad, x + w - 72 - bad_w, y + (h - 13) / 2, 13, UI_BAD);
}

/* Left panel: set up a cube, then pick a method and solve it. */
void ui_left_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangle((int)(r.x + r.width - 1), 0, 1, (int)r.height, UI_RULE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;
    bool busy = app_busy(a);
    CoachView cv = playback_view(&a->pb).coach;
    bool can_edit = app_can_change_cube(a); /* while a Step is practised the physical cube is the one that changes */
    float half = (w - 8) / 2;

    PlaybackView initial = playback_view(&a->pb);
    bool open = cv.mode == COACH_OFF && !initial.sol && !initial.sealed && !a->edit_mode;
    bool compact = r.height < 500;
    if (!compact) {
        ui_text_face(a, TYPE_SEMIBOLD, "Rubik's Cube Solver", x, y, 20, UI_TEXT);
        y += 34;
    }

    /* while a Step is practised the setup and method controls can't be used, so they make way for the net */
    if (!cv.locked && !(compact && open)) {
        if (!compact) section(a, "SET UP THE CUBE", x, &y);
        if (!compact || cv.mode == COACH_OFF) {
            Rectangle scramble_r = {x, y, half, 34}, reset_r = {x + half + 8, y, half, 34};
            if (!can_edit) tip_at(scramble_r, "Wait for the solver to finish."), tip_at(reset_r, "Wait for the solver to finish.");
            if (ui_button(a, scramble_r, "Scramble", false, can_edit)) app_scramble(a);
            if (ui_button(a, reset_r, "Reset", false, can_edit)) app_reset(a);
            y += compact ? 38 : 42;
            if (!compact) { /* scramble speed; Shift - = step it too */
                if (ui_button(a, (Rectangle){x, y, 28, 28}, "\xE2\x88\x92", false, a->scramble_speed > 0)) a->scramble_speed--;
                char sl[48];
                snprintf(sl, sizeof sl, "Scramble: %s", SPEED_NAMES[a->scramble_speed]);
                float sw = ui_text_width_face(a, TYPE_MEDIUM, sl, 13);
                ui_text_face(a, TYPE_MEDIUM, sl, x + (w - sw) / 2, y + 7, 13, UI_TEXT);
                if (ui_button(a, (Rectangle){x + w - 28, y, 28, 28}, "+", false, a->scramble_speed < NUM_SPEEDS - 1))
                    a->scramble_speed++;
                y += 36;
            }
        }
        if (!compact || (cv.mode == COACH_OFF && !a->edit_mode)) {
            PlaybackView orientation = playback_view(&a->pb);
            for (int row = 0; row < 2; row++) {
                int front = cube_center(orientation.cube, FACE_F);
                int top = cube_center(orientation.cube, FACE_U);
                char label[64];
                snprintf(label, sizeof label, "%s: %s", row ? "Top" : "Front",
                         COLOR_NAMES[row ? top : front]);
                Rectangle or_r = {x + row * (half + 8), y, half, 30};
                bool or_ok = can_edit && orientation.idle && cv.mode == COACH_OFF;
                if (!or_ok)
                    tip_at(or_r, !can_edit              ? "Wait for the solver to finish."
                                       : cv.mode != COACH_OFF ? "Stop coaching to change the orientation."
                                                              : "Wait for the turns to finish.");
                if (ui_button(a, or_r, label, false, or_ok)) {
                    for (int step = 1; step < NUM_COLORS; step++) {
                        int color = ((row ? top : front) + step) % NUM_COLORS;
                        Cube candidate = *orientation.cube;
                        if (cube_orient(&candidate, row ? front : color, row ? color : top)) {
                            app_orient(a, row ? front : color, row ? color : top);
                            break;
                        }
                    }
                }
            }
            y += compact ? 34 : 36;
        }

        if (a->edit_mode) {
            if (ui_button(a, (Rectangle){x, y, half, 32}, "Done painting", false, true)) app_set_edit(a, false);
            if (ui_button(a, (Rectangle){x + half + 8, y, half, 32}, "Check cube", false, true)) app_check_cube(a);
            y += compact ? 36 : 40;
            float sw = (w - 5 * 6) / 6;
            for (int c = 0; c < NUM_COLORS; c++) {
                Rectangle s = {x + c * (sw + 6), y, sw, 26};
                box(s, 6, STICKER_COLORS[c], a->paint_color == c ? UI_TEXT : Fade(UI_BLACK_EDGE, 0.5f));
                if (a->paint_color == c) DrawRectangleRoundedLinesEx(s, corner(s, 6), 8, 2, UI_TEXT);
                if (ui_focus_stop(a, s, 6, true)) a->paint_color = c;
                if (CheckCollisionPointRec(GetMousePosition(), s)) {
                    a->cursor = MOUSE_CURSOR_POINTING_HAND;
                    if (ui_clicked(a, s)) a->paint_color = c;
                }
            }
            y += compact ? 30 : 34;
        } else {
            Rectangle paint_r = {x, y, w, 32};
            if (!can_edit) tip_at(paint_r, "Wait for the solver to finish.");
            if (ui_button(a, paint_r, "Paint stickers", false, can_edit)) app_set_edit(a, true);
            y += compact ? 36 : 42;
        }
    }
    PlaybackView pv = playback_view(&a->pb);
    if (cv.mode == COACH_OFF && !pv.sol && !pv.sealed && !a->edit_mode) {
        /* the open seam: the two ways in. A scramble is handed out as a guided one, for the user's own cube */
        if (ui_button(a, (Rectangle){x, y, w, 32}, "I have a cube", false, !busy)) {
            app_coach_toggle(a);
            app_set_edit(a, true);
            a->focus_prompt_next = true;
        }
        y += 40;
        if (ui_button(a, (Rectangle){x, y, w, 32}, "Give me a scramble", false, can_edit)) {
            app_coach_toggle(a);
            app_scramble(a);
        }
        y += 42;
    } else {
        if (ui_button(a, (Rectangle){x, y, w, 32}, cv.mode == COACH_OFF ? "Coach me on my cube" : "Stop coaching", false,
                      !busy))
            app_coach_toggle(a);
        y += compact ? 34 : 42;
    }

    /* the method block and Solve sit at the bottom; the net fills the gap above them */
    char summary[160];
    method_summary(solver_description(a->method), summary, sizeof summary);
    float desc_h = r.height < 700 ? 0 : text_wrapped(a, summary, x, 0, w, 13, UI_TERTIARY, false);
    float solve_helper_y = r.y + r.height - PAD - 16;
    float solve_h = compact ? 36 : 44, check_h = compact ? 24 : 30;
    float solve_y = solve_helper_y - 8 - solve_h;
    float xray_y = solve_y - (compact ? 8 : 12) - check_h;
    float teach_y = xray_y - check_h;
    float method_pitch = r.height < 700 ? 28 : 36;
    float methods_y = cv.locked ? teach_y - 8 : teach_y - 16 - (20 + SOLVER_COUNT * method_pitch + (desc_h > 0 ? 8 + desc_h : -4));
    int method_cols = 1;
    if (!cv.locked && (compact || methods_y < y)) {
        /* short window: the list goes in a 2-column grid right under the setup block */
        methods_y = compact ? teach_y - 8 - ((SOLVER_COUNT + 1) / 2) * method_pitch : y;
        desc_h = 0;
        method_cols = 2;
    }

    /* the net keeps its 12:9 shape and sits under the setup controls it belongs to */
    float avail = methods_y - 16 - y, caption_h = 24;
    float net_h = fminf(w / 12.0f * 9, avail - caption_h);
    if (net_h > 60 && !a->edit_mode) {
        float ny = y + 8 + fmaxf(0, avail - caption_h - net_h) / 2; /* a width-limited net sits centred in its gap */
        render_net(a, (Rectangle){x, ny, w, net_h});
        const char *hint = a->edit_mode ? "Click a sticker to paint it."
                           : can_edit  ? "View only. Paint stickers to edit."
                                       : "View only.";
        ui_text(a, hint, x, ny + net_h + 8, 13, UI_TERTIARY);
    }

    y = methods_y;
    if (!cv.locked) {
        if (!compact) section(a, "CHOOSE A METHOD", x, &y);
        float cw = (w - (method_cols - 1) * 8) / method_cols;
        for (int i = 0; i < SOLVER_COUNT; i++) {
            float cx = x + (i % method_cols) * (cw + 8), cy = y + (i / method_cols) * method_pitch;
            if (method_row(a, (Rectangle){cx, cy, cw, method_pitch - 4}, (SolverId)i, can_edit)) a->method = (SolverId)i;
        }
        y += (SOLVER_COUNT + method_cols - 1) / method_cols * method_pitch;
        y += 8;
        if (desc_h > 0) text_wrapped(a, summary, x, y, w, 13, UI_TERTIARY, true);
    }

    if (checkbox(a, (Rectangle){x, teach_y, w, check_h}, "Pause to explain each step", a->teach)) {
        a->teach = !a->teach;
        playback_set_teach(&a->pb, a->teach);
    }
    if (checkbox(a, (Rectangle){x, xray_y, w, check_h}, "Highlight each step's pieces", a->xray)) a->xray = !a->xray;
    bool has_sol = playback_view(&a->pb).sol != NULL;
    const char *solve_label = busy ? "Solving\xE2\x80\xA6" : (has_sol ? "Solve again" : "Solve");
    /* once there is a solution, Play is the next action and takes the accent */
    if (ui_button(a, (Rectangle){x, solve_y, w, solve_h}, solve_label, !busy && !has_sol, can_edit)) app_start_solve(a);
    const char *helper;
    if (busy)
        helper = "";
    else if (cv.locked)
        helper = "Stop coaching to solve another cube.";
    else if (cv.mode == COACH_SETUP)
        helper = "Starts practice from this cube.";
    else if (cv.mode == COACH_REPAINT)
        helper = "Checks the painted cube, then starts practice.";
    else
        helper = has_sol ? "Replaces the current solution." : "Solves the cube as it is now.";
    ui_text(a, helper, x, solve_helper_y, 13, UI_TERTIARY);
    draw_tip(a, r);
}

/* ---------- coach: the Step being practised, with its moves hidden unless shown ---------- */

/* Moves from..to of s as text, e.g. "R U R' U'". */
static void join_moves(const App *a, const Solution *s, int from, int to, char *out, size_t size) {
    size_t n = 0;
    out[0] = 0;
    for (int k = from; k < to; k++) {
        char lab[16];
        move_label(a, s->moves[k], lab, sizeof lab);
        if (n + strlen(lab) + 2 > size) break;
        n += (size_t)snprintf(out + n, size - n, "%s%s", n ? " " : "", lab);
    }
}

/* 1 for a move word, 2 for one with a prime or a 2 (U', F2), 0 for anything else. */
static int move_word(const char *w, size_t len) {
    while (len && *w == '(') w++, len--;
    while (len && strchr(",.;:)", w[len - 1])) len--;
    char buf[16];
    if (len == 0 || len >= sizeof buf) return 0;
    memcpy(buf, w, len);
    buf[len] = 0;
    Move mv[2];
    if (moves_parse(buf, mv, 2) != 1) return 0;
    return strpbrk(buf, "'i2") ? 2 : 1;
}

static void append_word(char *buf, size_t size, size_t *n, const char *s, size_t len) {
    if (*n && *n + 1 < size) buf[(*n)++] = ' ';
    for (size_t k = 0; k < len && *n + 1 < size; k++) buf[(*n)++] = s[k];
    buf[*n] = 0;
}

/* Text with its algorithms replaced by "[moves hidden]" while hide is set: a run of two or more move
 * words, or any move with a prime or a 2. A lone face letter in prose is kept. Returns text, or buf. */
static const char *shown_text(bool hide, const char *text, char *buf, size_t size) {
    if (!hide || !text) return text;
    enum { WORDS = 512 };
    const char *wp[WORDS];
    size_t wl[WORDS];
    int kind[WORDS], nw = 0;
    const char *p = text;
    while (*p && nw < WORDS) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ') p++;
        wp[nw] = s;
        wl[nw] = (size_t)(p - s);
        kind[nw] = move_word(s, wl[nw]);
        nw++;
    }
    size_t n = 0;
    bool in_run = false;
    buf[0] = 0;
    for (int i = 0; i < nw; i++) {
        bool hit = kind[i] == 2 || (kind[i] == 1 && ((i > 0 && kind[i - 1]) || (i + 1 < nw && kind[i + 1])));
        if (hit && in_run) continue;
        in_run = hit;
        if (hit)
            append_word(buf, size, &n, "[moves hidden]", strlen("[moves hidden]"));
        else
            append_word(buf, size, &n, wp[i], wl[i]);
    }
    if (*p) append_word(buf, size, &n, "...", 3);
    return buf;
}

/* One button of the Coach row, sized to its label. Returns true when clicked. */
static bool coach_btn(App *a, float *bx, float y, float h, const char *label, bool primary, bool enabled) {
    float bw = ui_text_width_face(a, TYPE_MEDIUM, label, 14) + 24;
    bool clicked = ui_button(a, (Rectangle){*bx, y, bw, h}, label, primary, enabled);
    *bx += bw + 6;
    return clicked;
}

/* The coach's buttons, in the row where the progress bar is while a Step is practised. */
static void coach_buttons(App *a, float x, float y, const CoachView *c) {
    float bx = x, h = 30;
    if (c->mode == COACH_PRACTICE) {
        bool free_ = !c->running;
        /* the step's next action leads; the escape hatches sit on the row below */
        if (coach_btn(a, &bx, y, h, "Mark as followed", !c->hidden, free_ && !c->hidden)) app_coach_followed(a);
        if (coach_btn(a, &bx, y, h, c->hidden ? "Show moves" : "Hide moves", c->hidden, free_))
            playback_coach_show(&a->pb, c->hidden);
        if (coach_btn(a, &bx, y, h, "Demonstrate", false, free_)) app_coach_demo(a);
        bx = x;
        y += 36;
        if (coach_btn(a, &bx, y, h, "Try it yourself", false, free_)) app_coach_unsync(a, true);
        if (coach_btn(a, &bx, y, h, "Report a mismatch", false, free_)) app_coach_unsync(a, false);
    } else {
        if (coach_btn(a, &bx, y, h, "Mark as restored", true, true)) app_coach_restore(a);
        if (coach_btn(a, &bx, y, h, "Paint the cube", false, true)) app_coach_repaint(a);
    }
}

/* The Coach card under the cube, in place of the next move: what to do now, and what the screen rests on. */
static void coach_card(App *a, Rectangle card, const PlaybackView *v) {
    const CoachView *c = &v->coach;
    const Solution *s = v->sol;
    float x = card.x + PAD, w = card.width - 2 * PAD, y = card.y + 10;
    char cap[64] = "", headline[160] = "", body[512] = "", moves[512] = "", status[96] = "";
    switch (c->mode) {
    case COACH_SETUP:
        snprintf(cap, sizeof cap, "COACH: MATCH YOUR CUBE");
        const char *guided = app_guided_scramble(a);
        if (guided) {
            snprintf(headline, sizeof headline, "Start solved: white on top, green in front.");
            snprintf(body, sizeof body, "Keep that orientation and apply: %s  Then press Solve.", guided);
        } else {
            snprintf(headline, sizeof headline, "Paint the screen to match your cube");
            snprintf(body, sizeof body, "Then press Solve. Or press Scramble for a guided scramble to do on your cube.");
        }
        break;
    case COACH_PRACTICE:
        if (!s || c->step < 0) break;
        snprintf(cap, sizeof cap, "STEP %d OF %d", v->step.k, v->step.total);
        snprintf(headline, sizeof headline, "%s", s->steps[c->step].name);
        if (c->hidden) {
            snprintf(body, sizeof body, "Work out this step on your cube. Show moves to see any required cube rotations.");
        } else {
            const SolveStep *st = &s->steps[c->step];
            join_moves(a, s, st->start, st->start + st->count, moves, sizeof moves);
            snprintf(body, sizeof body, "Follow the moves, including cube rotations, then press Mark as followed.");
        }
        if (c->basis == COACH_BASIS_PAINTED) snprintf(status, sizeof status, "This cube passed the check.");
        break;
    case COACH_UNSYNCED:
        snprintf(cap, sizeof cap, "COACH: CHECK YOUR CUBE");
        snprintf(headline, sizeof headline, "%s", c->attempted ? "Your attempt may have changed your cube"
                                                               : "Your cube may not match the screen");
        snprintf(body, sizeof body, "Physically restore your cube to match the screen, then press Mark as restored. Or press Paint the cube to paint it as it is now.");
        break;
    case COACH_REPAINT:
        snprintf(cap, sizeof cap, "COACH: PAINT YOUR CUBE");
        snprintf(headline, sizeof headline, "Paint your cube as it is now");
        snprintf(body, sizeof body, "Match each sticker on the screen to your cube, then press Solve. Solve checks "
                                    "the paint and starts practice from it.");
        break;
    case COACH_DONE:
        snprintf(cap, sizeof cap, "COACH: FINISHED");
        snprintf(headline, sizeof headline, "Every step is followed");
        snprintf(body, sizeof body, "The screen cube is solved, and your cube is assumed to match it. Stop coaching "
                                    "to leave.");
        break;
    default:
        return;
    }

    float cap_w = caption(a, cap, x, y, UI_ACCENT);
    if (status[0]) {
        fit_text(a, TYPE_REGULAR, 13, status, sizeof status, w - cap_w - 16);
        ui_text(a, status, x + w - ui_text_width(a, status, 13), y, 13, UI_TERTIARY);
    }
    fit_text(a, TYPE_SEMIBOLD, 18, headline, sizeof headline, w);
    ui_text_face(a, TYPE_SEMIBOLD, headline, x, y + 20, 18, UI_TEXT);
    float by = y + 46;
    if (moves[0]) {
        fit_text(a, TYPE_MONO, 15, moves, sizeof moves, w);
        ui_text_face(a, TYPE_MONO, moves, x, by, 15, UI_TEXT);
        by += 20;
    }
    if (body[0]) text_wrapped(a, body, x, by, w, 14, UI_SECONDARY, true);
    if (c->locked) coach_buttons(a, x, card.y + card.height - 80, c);
}

/* ---------- centre: the next move and playback ---------- */

/* The big card under the cube: the move that runs next, what it does, and how far through the solution we are. */
void ui_move_display(App *a, Rectangle r) {
    Rectangle card = {r.x + 16, r.y + 6, r.width - 32, r.height - 12};
    box(card, 6, UI_PANEL, UI_RULE);
    PlaybackView v = playback_view(&a->pb);
    if (v.coach.mode != COACH_OFF) {
        coach_card(a, card, &v);
        return;
    }
    const Solution *s = v.sol;
    int n = s ? s->n : 0;
    float x = card.x + PAD, y = card.y + 10;
    float right_w = 150, right_x = card.x + card.width - PAD - right_w;

    char cap[64] = "", label[16] = "-", meaning[200] = "";
    Color glyph_c = UI_TEXT;
    bool turning = v.turning;
    if (turning) {
        snprintf(cap, sizeof cap, "TURNING NOW");
        move_label(a, v.turn, label, sizeof label);
        move_meaning(v.turn, meaning, sizeof meaning);
    } else if (s && v.playhead < n) {
        snprintf(cap, sizeof cap, "NEXT MOVE");
        move_label(a, s->moves[v.playhead], label, sizeof label);
        move_meaning(s->moves[v.playhead], meaning, sizeof meaning);
    } else if (s) {
        snprintf(cap, sizeof cap, "SOLVED");
        snprintf(label, sizeof label, "Done");
        glyph_c = UI_DONE;
        snprintf(meaning, sizeof meaning, "All %d moves played. Step back with Left, or press Home to play them again.",
                 solution_move_count(s));
    } else {
        snprintf(cap, sizeof cap, "NO SOLUTION YET");
        snprintf(meaning, sizeof meaning, "Moves appear here after you press Solve.");
    }
    if (v.teach_stop) snprintf(meaning, sizeof meaning, "Type the move to continue.");

    caption(a, cap, x, y, (turning || (s && v.playhead < n)) ? UI_ACCENT : UI_TERTIARY);
    float gy = card.y + 22;
    float gw = ui_text_width_face(a, TYPE_MONO, label, 60);
    ui_text_face(a, TYPE_MONO, label, x, gy, 60, glyph_c);
    if (turning || (s && v.playhead < n))
        DrawRectangle((int)x, (int)(gy + 62), (int)gw, turning ? 3 : 2, turning ? UI_ACCENT : Fade(UI_ACCENT, 0.55f));

    float meaning_x = x + gw + 20;
    float meaning_right = s ? right_x - 20 : card.x + card.width - PAD;
    if (meaning[0]) text_wrapped(a, meaning, meaning_x, gy + 4, meaning_right - meaning_x, 14, UI_SECONDARY, true);

    if (s) {
        char done[64], prev[16] = "", next[16] = "", nb[64] = "";
        snprintf(done, sizeof done, "%d of %d", solution_move_count_to(s, v.playhead), solution_move_count(s));
        caption(a, "MOVES DONE", right_x, y, UI_TERTIARY);
        ui_text_face(a, TYPE_MONO, done, right_x, y + 16, 22, UI_TEXT);
        if (v.playhead > 0) move_label(a, s->moves[v.playhead - 1], prev, sizeof prev);
        if (v.playhead + 1 < n) move_label(a, s->moves[v.playhead + 1], next, sizeof next);
        /* only name the neighbours that exist */
        if (prev[0] && next[0]) snprintf(nb, sizeof nb, "Prev %s  Next %s", prev, next);
        else if (prev[0]) snprintf(nb, sizeof nb, "Prev %s", prev);
        else if (next[0]) snprintf(nb, sizeof nb, "Next %s", next);
        ui_text_face(a, TYPE_MONO, nb, right_x, y + 50, 13, UI_TERTIARY);
    }
}

/* Keyboard reference drawn over the 3D view while toggled on. */
void ui_help_overlay(App *a, Rectangle view) {
    if (!a->show_help) return;
    static const char *const lines[] = {
        "U R F D L B   turn a face (Shift = prime)",
        "M E S   turn a slice",
        "V   screen only (hides the seam and panels)",
        "X Y Z   rotate the whole cube",
        "Enter   solve",
        "Space   play / pause",
        "H   highlight the current step's pieces",
        "Left / Right   step,  Home / End   jump",
        "-  =   slower / faster,  Shift   scramble speed",
        "PgUp / PgDn   previous / next step",
        "Ctrl+S   scramble,  Ctrl+Z   undo Scramble or Reset",
        "Tab   move between controls,  Enter   press",
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

/* A merged move can belong to two algorithms. */
static int chunk_label(const Chunk *chunks, int nch, int move, char *label, size_t size) {
    int matches = 0;
    label[0] = 0;
    for (int c = 0; c < nch; c++) {
        if (move < chunks[c].start || move >= chunks[c].start + chunks[c].count) continue;
        size_t len = strlen(label);
        snprintf(label + len, size - len, "%s%s", matches ? " + " : "", chunks[c].name);
        matches++;
    }
    if (matches > 1) {
        size_t len = strlen(label);
        snprintf(label + len, size - len, " (shared move)");
    }
    return matches;
}

/* Places a step's move tokens in wrapped rows inside area, shifted up by scroll.
 * Returns the height of all rows; with draw false it only measures. */
static float place_tokens(App *a, const PlaybackView *v, const SolveStep *step, const Chunk *chunks, int nch,
                          Rectangle area, float scroll, bool draw, Vector2 mouse) {
    const Solution *s = v->sol;
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
        const Chunk *first = NULL, *last = NULL;
        for (int c = 0; c < nch; c++)
            if (j >= chunks[c].start && j < chunks[c].start + chunks[c].count) {
                if (!first) first = &chunks[c];
                last = &chunks[c];
            }
        char name[16];
        move_label(a, s->moves[j], name, sizeof name);
        float tw = token_width(a, s->moves[j]);
        if (tx > area.x && tx + tw > area.x + area.width) tx = area.x, row++;
        if (draw) {
            Rectangle tok = {tx, area.y + row * TOKEN_ROW - scroll, tw, TOKEN_H};
            bool hover = CheckCollisionPointRec(mouse, tok) && CheckCollisionPointRec(mouse, area);
            bool next = j == v->playhead; /* the move running now, or the next one to run */
            bool played = j < v->playhead;
            bool rot = move_is_rotation(s->moves[j]);
            Color fill = {0};
            Color tc = played ? UI_TEXT : UI_TERTIARY;
            if (next) {
                fill = UI_ACCENT;
                tc = UI_INK;
            } else if (hover) {
                fill = UI_RAISED_HOVER;
            } else if (played) {
                fill = UI_RAISED;
            }
            if (fill.a) DrawRectangleRounded(tok, corner(tok, 6), 8, fill);
            if (rot && !next) DrawRectangleRoundedLinesEx(tok, corner(tok, 6), 8, 1, UI_BORDER); /* whole-cube rotation */
            ui_text_face(a, TYPE_MONO, name, tok.x + 8, tok.y + (TOKEN_H - 14) / 2, 14, tc);
            if (first) {
                bool more = j + 1 < last->start + last->count;
                float len = tw + (more && tx + tw + TOKEN_GAP + 1 < area.x + area.width ? TOKEN_GAP : 0);
                float line_y = tok.y + TOKEN_H + 1;
                if (first != last) {
                    DrawRectangle((int)tok.x, (int)line_y, (int)(tw / 2 - 1), 2, UI_ACCENT);
                    DrawRectangle((int)(tok.x + tw / 2 + 1), (int)line_y, (int)(len - tw / 2 - 1), 2, UI_ACCENT);
                } else {
                    DrawRectangle((int)tok.x, (int)line_y, (int)len, 2, UI_ACCENT);
                }
                if (hover) {
                    static char label[256];
                    chunk_label(chunks, nch, j, label, sizeof label);
                    tip_at(tok, label);
                }
            }
            if (hover) a->cursor = MOUSE_CURSOR_POINTING_HAND;
            if (hover && ui_clicked(a, tok)) playback_seek(&a->pb, j + 1);
        }
        tx += tw + TOKEN_GAP;
    }
    return step->count ? (row + 1) * TOKEN_ROW : 0;
}

/* Lays out or draws every step row. Only the current step opens to its moves; the rest are one line each. */
static void steps_pass(App *a, const PlaybackView *v, Rectangle list, float x, float w, bool draw, StepsLayout *L) {
    const Solution *s = v->sol;
    Vector2 mouse = GetMousePosition();
    bool over_list = CheckCollisionPointRec(mouse, list);
    float top = list.y - a->sol_scroll;
    float cy = top;
    L->cur_top = L->cur_h = 0;
    L->inner = (Rectangle){0};
    L->inner_max = 0;
    for (int st = 0; st < s->nsteps; st++) {
        const SolveStep *step = &s->steps[st];
        bool done = v->playhead >= step->start + step->count;
        bool current = st == v->step.step;
        Rectangle row = {x, cy, w, ROW_H};
        bool over = over_list && CheckCollisionPointRec(mouse, row);
        if (current) L->cur_top = cy - top;

        if (draw) {
            if (over) {
                DrawRectangleRounded(row, corner(row, 6), 8, held(a, row) ? UI_PRESSED : UI_RAISED);
                a->cursor = MOUSE_CURSOR_POINTING_HAND;
            }
            Vector2 mark = {x + 8, cy + ROW_H / 2};
            if (done) {
                DrawCircleV(mark, 8, UI_DONE);
                DrawLineEx((Vector2){mark.x - 3.5f, mark.y}, (Vector2){mark.x - 1, mark.y + 2.5f}, 2, UI_INK);
                DrawLineEx((Vector2){mark.x - 1, mark.y + 2.5f}, (Vector2){mark.x + 3.5f, mark.y - 2.5f}, 2, UI_INK);
            } else {
                DrawRing(mark, 5.5f, 7.5f, 0, 360, 24, current ? UI_ACCENT : UI_OUTLINE);
                if (current) DrawCircleV(mark, 3.5f, UI_ACCENT);
            }

            char name[96], count[16];
            snprintf(name, sizeof name, "%s", step->name);
            count[0] = 0; /* no move counts while coaching: they would tell the length of the Steps not yet done */
            if (!v->coach.locked) {
                int n = solution_step_move_count(s, st);
                snprintf(count, sizeof count, "%d %s", n, n == 1 ? "move" : "moves");
            }
            float cw = ui_text_width_face(a, TYPE_MONO, count, 13);
            if (over && done) cw = ui_text_width_face(a, TYPE_MEDIUM, "Go to start", 13);
            fit_text(a, TYPE_REGULAR, 14, name, sizeof name, w - 24 - cw - 12);
            Color nc = current ? UI_ACCENT : (done ? UI_TEXT : UI_SECONDARY);
            ui_text_face(a, current ? TYPE_MEDIUM : TYPE_REGULAR, name, x + 24, cy + (ROW_H - 14) / 2, 14, nc);
            if (over && done)
                ui_text_face(a, TYPE_MEDIUM, "Go to start", x + w - cw, cy + (ROW_H - 13) / 2, 13, UI_ACCENT);
            else
                ui_text_face(a, TYPE_MONO, count, x + w - cw, cy + (ROW_H - 13) / 2, 13, UI_TERTIARY);
            const char *gloss = step_gloss(step);
            if (over && gloss) tip_at(row, gloss);
            if (over && ui_clicked(a, row)) playback_seek(&a->pb, step->start);
        }
        cy += ROW_H;

        bool expanded = current && !v->coach.hidden; /* a Step being practised with its moves hidden opens to nothing */
        if (expanded) {
            float tw = w - 24 - 10; /* leave room for the scroll thumb */
            Chunk chunks[SOL_MAX_MOVES];
            int nch = solution_chunks(s, st, chunks, SOL_MAX_MOVES);
            char names[256], label[280];
            if (v->playhead < step->start + step->count &&
                chunk_label(chunks, nch, v->playhead, names, sizeof names)) {
                snprintf(label, sizeof label, "Next: %s", names);
                cy += text_wrapped(a, label, x + 24, cy, tw, 13, UI_SECONDARY, draw) + 5;
            }
            Rectangle origin = {x + 24, cy, tw, 0};
            float tok_h = place_tokens(a, v, step, chunks, nch, origin, 0, false, mouse);
            float inner_h = fminf(tok_h, TOKEN_ROWS_MAX * TOKEN_ROW);
            L->inner = (Rectangle){x + 24, cy, tw, inner_h};
            L->inner_max = fmaxf(0, tok_h - inner_h);
            if (a->move_scroll > L->inner_max) a->move_scroll = L->inner_max;
            if (a->move_scroll < 0) a->move_scroll = 0;
            if (draw && inner_h > 0) {
                scissor(a, intersect(L->inner, list));
                Vector2 token_mouse = over_list ? mouse : (Vector2){-1, -1};
                place_tokens(a, v, step, chunks, nch, L->inner, a->move_scroll, true, token_mouse);
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
            if (current) L->cur_h = cy - top - L->cur_top;
        }
    }
    L->content_h = cy - top;
}

/* The Skipped Steps folded into the current Step, each with its name and Lesson. Returns the height used;
 * with draw false it only measures. */
static float skipped_block(App *a, const Solution *s, const StepAt *at, float x, float y, float w, bool hide,
                           bool draw) {
    float h = 0;
    for (int i = at->skipped; i < at->skipped + at->nskipped; i++) {
        char line[96];
        snprintf(line, sizeof line, "Skipped: %s", s->steps[i].name);
        fit_text(a, TYPE_MEDIUM, 13, line, sizeof line, w);
        if (draw) ui_text_face(a, TYPE_MEDIUM, line, x, y + h, 13, UI_SECONDARY);
        h += 18;
        StepLesson lesson = step_lesson(&s->steps[i]);
        char lbuf[1024];
        const char *text = shown_text(hide, lesson.text, lbuf, sizeof lbuf);
        if (text) h += text_wrapped(a, text, x, y + h, w, 13, UI_SECONDARY, draw) + 6;
    }
    return h;
}

/* Right panel: the solution summary, the current step's lesson, and the step list. */
void ui_right_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangle((int)r.x, 0, 1, (int)r.height, UI_RULE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;

    ui_text_face(a, TYPE_SEMIBOLD, a->edit_mode ? "Paint the cube" : "Solution", x, y, 20, UI_TEXT);
    y += 34;

    if (a->edit_mode) {
        y += text_wrapped(a, "Choose a color, then paint each sticker, or type each face into the prompt as nine colors (U R F D L B). Centers stay fixed.", x, y, w, 14,
                          UI_SECONDARY, true) + 16;
        float net_h = w * 9 / 12;
        render_net(a, (Rectangle){x, y, w, net_h});
        y += net_h + 12;
        text_wrapped(a, "Use Tab to select a sticker and Space to paint it.", x, y, w, 13, UI_SECONDARY, true);
        return;
    }

    PlaybackView v = playback_view(&a->pb);
    if (r.height < 500 && v.coach.mode != COACH_OFF && !v.coach.locked) {
        coach_card(a, (Rectangle){r.x, y, r.width, r.height - (y - r.y)}, &v);
        return;
    }
    if (!v.sol) {
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

    const Solution *s = v.sol;
    char head[160];
    if (v.coach.locked) {
        snprintf(head, sizeof head, "%s  \xC2\xB7  coaching", solver_name(a->sol_method));
    } else {
        char rots[48];
        ui_rotations(s, "  \xC2\xB7  ", rots, sizeof rots);
        snprintf(head, sizeof head, "%s  \xC2\xB7  %d moves%s  \xC2\xB7  %.1f ms", solver_name(a->sol_method),
                 solution_move_count(s), rots, s->ms);
    }
    fit_text(a, TYPE_REGULAR, 13, head, sizeof head, w);
    ui_text(a, head, x, y, 13, UI_TERTIARY);
    y += 26;

    /* the lesson for the step the playhead is in, after the Skipped Steps folded into it */
    int cur = v.step.step;
    if (cur >= 0) {
        const SolveStep *st = &s->steps[cur];
        StepLesson lesson = step_lesson(st);
        char tbuf[1024], nbuf[1024];
        const char *text = shown_text(v.coach.hidden, lesson.text, tbuf, sizeof tbuf);
        const char *note = shown_text(v.coach.hidden, lesson.note, nbuf, sizeof nbuf);
        float body_w = w - 24, body_h = 0;
        float skip_h = skipped_block(a, s, &v.step, x + 12, 0, body_w, v.coach.hidden, false);
        if (text) body_h += text_wrapped(a, text, x + 12, 0, body_w, 14, UI_TEXT, false) + 6;
        if (note) body_h += text_wrapped(a, note, x + 12, 0, body_w, 13, UI_SECONDARY, false);
        Rectangle card = {x, y, w, 40 + skip_h + body_h};
        box(card, 6, Fade(UI_ACCENT, 0.12f), Fade(UI_ACCENT, 0.45f));

        char title[96];
        snprintf(title, sizeof title, "%s", st->name);
        fit_text(a, TYPE_MEDIUM, 15, title, sizeof title, w - 24);
        ui_text_face(a, TYPE_MEDIUM, title, x + 12, y + 10, 15, UI_ACCENT);

        float by = y + 34;
        skipped_block(a, s, &v.step, x + 12, by, body_w, v.coach.hidden, true);
        by += skip_h;
        if (text) by += text_wrapped(a, text, x + 12, by, body_w, 14, UI_TEXT, true) + 6;
        if (note) text_wrapped(a, note, x + 12, by, body_w, 13, UI_SECONDARY, true);
        y += card.height + 14;
    }

    section(a, "STEPS", x, &y);
    Rectangle list = {r.x, y, r.width, r.y + r.height - y - 8};
    if (list.height <= 0) return;
    Vector2 mouse = GetMousePosition();
    float list_x = x, list_w = w - 10;

    /* measure first: the wheel and the current-step follow need the layout */
    StepsLayout L;
    steps_pass(a, &v, list, list_x, list_w, false, &L);
    float max_scroll = fmaxf(0, L.content_h - list.height);

    /* a move finishing on screen brings the current step back into view */
    if (v.playhead != a->follow_seen) {
        a->follow_seen = v.playhead;
        a->follow_current = true;
    }

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
    steps_pass(a, &v, list, list_x, list_w, true, &L);
    EndScissorMode();
    draw_tip(a, r);

    if (max_scroll > 0) {
        float ty = list.y + (list.height - thumb_h) * (a->sol_scroll / max_scroll);
        bool hot = CheckCollisionPointRec(mouse, track) || a->thumb_drag;
        DrawRectangleRounded((Rectangle){track.x + 1, ty, 4, thumb_h}, 0.5f, 4, hot ? UI_TERTIARY : UI_BORDER);
    }
}
