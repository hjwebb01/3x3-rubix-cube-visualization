/* Immediate-mode widgets and the side panels. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"

#define PAD 16.0f

/* ---------- primitives ---------- */

static float spacing(const App *a, float size) {
    return a->font.texture.id == GetFontDefault().texture.id ? size / 10 : 0;
}

void ui_text(App *a, const char *s, float x, float y, float size, Color c) {
    DrawTextEx(a->font, s, (Vector2){roundf(x), roundf(y)}, size, spacing(a, size), c);
}

float ui_text_width(App *a, const char *s, float size) {
    return MeasureTextEx(a->font, s, size, spacing(a, size)).x;
}

/* Word-wrapped text; returns the height used. */
static float ui_text_wrapped(App *a, const char *s, float x, float y, float width, float size, Color c) {
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
            ui_text(a, line, x, yy, size, c);
            yy += size * 1.25f;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
    }
    if (line[0]) {
        ui_text(a, line, x, yy, size, c);
        yy += size * 1.25f;
    }
    return yy - y;
}

bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled) {
    Vector2 m = GetMousePosition();
    bool over = enabled && CheckCollisionPointRec(m, r);
    if (over) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    Color bg = active ? UI_ACCENT : (over ? UI_BUTTON_HOVER : UI_BUTTON);
    if (!enabled) bg = Fade(bg, 0.45f);
    DrawRectangleRounded(r, 0.3f, 6, bg);
    float size = 18;
    float w = ui_text_width(a, label, size);
    Color tc = active ? (Color){12, 18, 30, 255} : UI_TEXT;
    ui_text(a, label, r.x + (r.width - w) / 2, r.y + (r.height - size) / 2, size, enabled ? tc : Fade(tc, 0.5f));
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void section(App *a, const char *title, float x, float *y) {
    ui_text(a, title, x, *y, 15, UI_DIM);
    *y += 22;
}

/* ---------- text box for typed moves ---------- */

static void move_input(App *a, Rectangle box, Rectangle apply_btn) {
    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->input_focus = CheckCollisionPointRec(m, box);
    if (CheckCollisionPointRec(m, box)) a->cursor = MOUSE_CURSOR_IBEAM;

    bool enabled = !app_busy(a) && !a->edit_mode;
    DrawRectangleRounded(box, 0.25f, 6, (Color){20, 22, 28, 255});
    DrawRectangleRoundedLinesEx(box, 0.25f, 6, 1.5f, a->input_focus ? UI_ACCENT : UI_PANEL_EDGE);

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
    while (*shown && ui_text_width(a, shown, 18) > box.width - 16) shown++;
    if (!a->input[0] && !a->input_focus)
        ui_text(a, "e.g. R U R' U'", box.x + 8, box.y + 8, 16, Fade(UI_DIM, 0.8f));
    else
        ui_text(a, shown, box.x + 8, box.y + 7, 18, UI_TEXT);
    if (a->input_focus && fmod(GetTime(), 1.0) < 0.55) {
        float cx = box.x + 8 + ui_text_width(a, shown, 18) + 1;
        DrawRectangle((int)cx, (int)box.y + 7, 2, 20, UI_TEXT);
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

/* ---------- panels ---------- */

void ui_top_bar(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_BG);
    float x = r.x + PAD;
    if (app_busy(a)) {
        /* spinner */
        float t = (float)GetTime() * 6;
        Vector2 c = {x + 9, r.y + r.height / 2};
        DrawRing(c, 6, 9, t * 57.3f, t * 57.3f + 270, 24, UI_ACCENT);
        x += 28;
    }
    Color c = a->status_error ? UI_BAD : UI_TEXT;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", a->status);
    /* truncate to fit */
    size_t len = strlen(buf);
    while (len > 3 && ui_text_width(a, buf, 18) > r.width - (x - r.x) - PAD) {
        buf[--len] = 0;
        memcpy(buf + len - 3, "...", 3);
    }
    ui_text(a, buf, x, r.y + (r.height - 18) / 2, 18, c);
    if (cube_is_solved(&a->cube) && !a->animating && a->qhead == a->qtail && !a->edit_mode) {
        const char *s = "SOLVED";
        float w = ui_text_width(a, s, 16);
        Rectangle badge = {r.x + r.width - w - 24 - PAD, r.y + 14, w + 24, 24};
        if (badge.x > x + ui_text_width(a, buf, 18) + 8) {
            DrawRectangleRounded(badge, 0.5f, 6, Fade(UI_GOOD, 0.2f));
            ui_text(a, s, badge.x + 12, badge.y + 4, 16, UI_GOOD);
        }
    }
}

void ui_left_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawLine((int)(r.x + r.width), 0, (int)(r.x + r.width), (int)r.height, UI_PANEL_EDGE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;
    bool busy = app_busy(a);

    ui_text(a, "Rubik's Cube Solver", x, y, 24, UI_TEXT);
    y += 40;

    float half = (w - 8) / 2;
    if (ui_button(a, (Rectangle){x, y, half, 34}, "Scramble", false, !busy)) app_scramble(a);
    if (ui_button(a, (Rectangle){x + half + 8, y, half, 34}, "Reset", false, !busy)) app_reset(a);
    y += 48;

    section(a, "METHOD", x, &y);
    for (int i = 0; i < SOLVER_COUNT; i++) {
        Rectangle b = {x + (i % 2) * (half + 8), y + (i / 2) * 40, half, 34};
        if (ui_button(a, b, solver_name((SolverId)i), a->method == (SolverId)i, !busy)) a->method = (SolverId)i;
    }
    y += 80;
    if (ui_button(a, (Rectangle){x, y, w, 30}, a->teach ? "Teach mode: on" : "Teach mode: off", a->teach, true))
        a->teach = !a->teach;
    y += 38;
    y += ui_text_wrapped(a, solver_description(a->method), x, y, w, 15, UI_DIM) + 8;

    if (ui_button(a, (Rectangle){x, y, w, 40}, busy ? "Solving..." : "Solve", !busy, !busy)) app_start_solve(a);
    y += 54;

    section(a, "PLAYBACK", x, &y);
    {
        bool has = a->has_sol;
        float bw = (w - 3 * 6) / 4;
        const char *labels[4] = {"|<", "<", ">", ">|"};
        for (int i = 0; i < 4; i++) {
            Rectangle b = {x + i * (bw + 6), y, bw, 32};
            if (!ui_button(a, b, labels[i], false, has)) continue;
            a->playing = false;
            switch (i) {
            case 0: app_jump(a, 0); break;
            case 1: app_step(a, -1); break;
            case 2: app_step(a, 1); break;
            case 3: app_jump(a, a->sol.n); break;
            }
        }
        y += 40;
        if (ui_button(a, (Rectangle){x, y, w, 32}, a->playing ? "Pause" : "Play", a->playing, has)) {
            if (!a->playing && a->sol_pos == a->sol.n) app_jump(a, 0);
            a->playing = !a->playing;
        }
        y += 40;
        if (ui_button(a, (Rectangle){x, y, 32, 28}, "-", false, a->speed > 0)) a->speed--;
        if (ui_button(a, (Rectangle){x + w - 32, y, 32, 28}, "+", false, a->speed < NUM_SPEEDS - 1)) a->speed++;
        char buf[64];
        snprintf(buf, sizeof buf, "Speed: %s", SPEED_NAMES[a->speed]);
        float tw = ui_text_width(a, buf, 16);
        ui_text(a, buf, x + (w - tw) / 2, y + 6, 16, UI_TEXT);
        y += 42;
    }

    section(a, "TYPE MOVES", x, &y);
    move_input(a, (Rectangle){x, y, w - 72, 34}, (Rectangle){x + w - 64, y, 64, 34});
    y += 48;

    section(a, "EDIT STICKERS", x, &y);
    if (ui_button(a, (Rectangle){x, y, half, 30}, a->edit_mode ? "Done" : "Edit", a->edit_mode, !busy))
        app_set_edit(a, !a->edit_mode);
    if (a->edit_mode) {
        if (ui_button(a, (Rectangle){x + half + 8, y, half, 30}, "Check", false, true)) {
            const char *err = cube_validate(&a->cube);
            if (err)
                app_set_status(a, true, "Invalid: %s", err);
            else
                app_set_status(a, false, "Cube is valid and solvable");
        }
        y += 38;
        float sw = (w - 5 * 6) / 6;
        for (int c = 0; c < NUM_COLORS; c++) {
            Rectangle s = {x + c * (sw + 6), y, sw, 26};
            DrawRectangleRounded(s, 0.3f, 6, STICKER_COLORS[c]);
            if (a->paint_color == c) DrawRectangleRoundedLinesEx(s, 0.3f, 6, 3, UI_ACCENT);
            if (CheckCollisionPointRec(GetMousePosition(), s)) {
                a->cursor = MOUSE_CURSOR_POINTING_HAND;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) a->paint_color = c;
            }
        }
        y += 34;
    } else {
        y += 38;
    }

    /* net fills what is left, above the key help */
    float help_h = 58;
    Rectangle net = {x, y, w, r.height - y - help_h - PAD};
    if (net.height > 60) render_net(a, net, NULL);
    float hy = r.height - help_h - 4;
    ui_text(a, "Keys: U R F D L B M E S X Y Z", x, hy, 14, UI_DIM);
    ui_text(a, "Shift = prime   Enter = solve", x, hy + 18, 14, UI_DIM);
    ui_text(a, "Space play   arrows step   Ctrl+S scramble", x, hy + 36, 14, UI_DIM);
}

void ui_right_panel(App *a, Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawLine((int)r.x, 0, (int)r.x, (int)r.height, UI_PANEL_EDGE);
    float x = r.x + PAD, w = r.width - 2 * PAD, y = r.y + PAD;

    ui_text(a, "Solution", x, y, 24, UI_TEXT);
    y += 36;
    if (!a->has_sol) {
        y += ui_text_wrapped(a, "Scramble the cube (or paint a state in edit mode), choose a method and press Solve.",
                             x, y, w, 16, UI_DIM) + 18;
        for (int i = 0; i < SOLVER_COUNT; i++) {
            ui_text(a, solver_name((SolverId)i), x, y, 17, a->method == (SolverId)i ? UI_ACCENT : UI_TEXT);
            y += 22;
            y += ui_text_wrapped(a, solver_description((SolverId)i), x, y, w, 14, UI_DIM) + 10;
        }
        return;
    }

    const Solution *s = &a->sol;
    char buf[160];
    snprintf(buf, sizeof buf, "%s  -  %d moves  -  %.1f ms", solver_name(a->method), solution_move_count(s), s->ms);
    ui_text(a, buf, x, y, 16, UI_DIM);
    y += 24;
    /* progress bar */
    Rectangle bar = {x, y, w, 6};
    DrawRectangleRounded(bar, 1, 4, UI_BUTTON);
    if (s->n > 0) DrawRectangleRounded((Rectangle){x, y, w * (float)a->sol_pos / (float)s->n, 6}, 1, 4, UI_ACCENT);
    bool over_bar = CheckCollisionPointRec(GetMousePosition(), (Rectangle){x, y - 6, w, 18});
    if (over_bar) a->cursor = MOUSE_CURSOR_POINTING_HAND;
    if (over_bar && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        a->playing = false;
        int pos = (int)roundf((GetMousePosition().x - x) / w * (float)s->n);
        if (pos != a->sol_pos) app_jump(a, pos);
    }
    y += 14;
    snprintf(buf, sizeof buf, "Move %d / %d", a->sol_pos, s->n);
    ui_text(a, buf, x, y, 15, UI_DIM);
    y += 26;

    /* lesson for the step about to be played (or just finished, at the end) */
    int cur_step = -1;
    for (int st = 0; st < s->nsteps; st++)
        if (s->steps[st].count && a->sol_pos >= s->steps[st].start) cur_step = st;
    StepLesson lesson = cur_step >= 0 ? step_lesson(&s->steps[cur_step]) : (StepLesson){0};
    if (lesson.text || lesson.note) {
        float lh = 0, nh = 0;
        BeginScissorMode(0, 0, 0, 0); /* measure only */
        if (lesson.text) lh = ui_text_wrapped(a, lesson.text, x + 10, y + 32, w - 20, 15, UI_TEXT);
        if (lesson.note) nh = ui_text_wrapped(a, lesson.note, x + 10, y + 32, w - 20, 15, UI_DIM) + 8;
        EndScissorMode();
        Rectangle box = {x, y, w, lh + nh + 44};
        DrawRectangleRounded(box, 0.08f, 6, Fade(UI_ACCENT, 0.12f));
        ui_text(a, s->steps[cur_step].name, x + 10, y + 8, 17, UI_ACCENT);
        if (lesson.text) ui_text_wrapped(a, lesson.text, x + 10, y + 32, w - 20, 15, UI_TEXT);
        if (lesson.note) ui_text_wrapped(a, lesson.note, x + 10, y + 32 + lh + 8, w - 20, 15, UI_DIM);
        y += box.height + 12;
    }

    Rectangle list = {r.x, y, r.width, r.height - y - 8};
    Vector2 mouse = GetMousePosition();
    bool over_list = CheckCollisionPointRec(mouse, list);
    if (over_list) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) a->sol_scroll -= wheel * 50, a->follow_current = false;
    }

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    float cy = list.y - a->sol_scroll;
    float cur_top = -1, cur_bottom = -1;
    int current = a->sol_pos - 1;
    const float tok_size = 18, tok_h = 26;
    for (int st = 0; st < s->nsteps; st++) {
        const SolveStep *step = &s->steps[st];
        bool in_step = current >= step->start && current < step->start + step->count;
        bool done = a->sol_pos >= step->start + step->count;
        /* step header */
        char count[32];
        snprintf(count, sizeof count, "%d", solution_step_move_count(s, st));
        float cw = ui_text_width(a, count, 15);
        Color hc = in_step ? UI_ACCENT : (done ? UI_TEXT : UI_DIM);
        float hh = ui_text_wrapped(a, step->name, x, cy, w - cw - 12, 16, hc);
        ui_text(a, count, x + w - cw, cy, 15, UI_DIM);
        Rectangle header = {x, cy, w, hh};
        bool over_header = over_list && CheckCollisionPointRec(mouse, header);
        if (over_header) a->cursor = MOUSE_CURSOR_POINTING_HAND;
        if (over_header && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            a->playing = false;
            app_jump(a, step->start);
        }
        cy += hh + 4;
        /* move tokens */
        float tx = x;
        if (step->count == 0) {
            ui_text(a, "(already done)", x, cy, 14, UI_DIM);
            cy += 22;
        }
        for (int k = 0; k < step->count; k++) {
            int j = step->start + k;
            const char *name = move_name(s->moves[j]);
            float tw = ui_text_width(a, name, tok_size) + 12;
            if (tx + tw > x + w) tx = x, cy += tok_h + 4;
            Rectangle tok = {tx, cy, tw, tok_h};
            bool hover = over_list && CheckCollisionPointRec(mouse, tok);
            if (hover) a->cursor = MOUSE_CURSOR_POINTING_HAND;
            Color tc = j < a->sol_pos ? UI_TEXT : UI_DIM;
            if (move_is_rotation(s->moves[j])) tc = (Color){190, 150, 255, 255};
            if (j == current) {
                DrawRectangleRounded(tok, 0.3f, 6, UI_ACCENT);
                tc = (Color){12, 18, 30, 255};
                cur_top = cy, cur_bottom = cy + tok_h;
            } else if (hover) {
                DrawRectangleRounded(tok, 0.3f, 6, UI_BUTTON_HOVER);
            }
            ui_text(a, name, tx + 6, cy + (tok_h - tok_size) / 2, tok_size, tc);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                a->playing = false;
                app_jump(a, j + 1);
            }
            tx += tw + 4;
        }
        if (step->count) cy += tok_h;
        cy += 16;
    }
    EndScissorMode();

    /* clamp scroll and keep the current move visible while playing */
    float content = cy + a->sol_scroll - list.y;
    float max_scroll = fmaxf(0, content - list.height);
    if (a->follow_current && cur_top >= 0) {
        if (cur_top < list.y + 10) a->sol_scroll -= (list.y + 10 - cur_top);
        if (cur_bottom > list.y + list.height - 10) a->sol_scroll += cur_bottom - (list.y + list.height - 10);
    }
    if (a->follow_current && a->sol_pos == 0) a->sol_scroll = 0;
    if (a->sol_scroll > max_scroll) a->sol_scroll = max_scroll;
    if (a->sol_scroll < 0) a->sol_scroll = 0;
}
