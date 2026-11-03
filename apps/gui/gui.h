/* Shared state for the raylib front end. */
#ifndef RUBIKS_GUI_H
#define RUBIKS_GUI_H
#include <pthread.h>
#include <stdatomic.h>

#include "raylib.h"

#include "cube/cube.h"
#include "cube/solver.h"
#include "playback.h"

#define NUM_SPEEDS 6
#define FOCUS_MAX 128 /* room for the 48 paintable net stickers */

/* The background solver: IDLE -> RUNNING (app_start_solve) -> DONE (worker) -> IDLE (poll_solver).
 * The request (solve_input, solve_method, solve_coach) is fixed in RUNNING and DONE;
 * solve_output and solve_error are valid only in DONE. */
typedef enum { SOLVE_IDLE, SOLVE_RUNNING, SOLVE_DONE } SolveState;

typedef struct {
    /* the cube on screen, its turn queue, and the Solution being played */
    Playback pb;

    int speed; /* index into SPEED_SECONDS */
    SolverId method;     /* the selected method, used by the next Solve */
    SolverId sol_method; /* the method that produced the current solution */
    bool teach;          /* the Pause to explain checkbox; pushed to pb when it changes */
    bool xray;           /* fade the stickers the current Step doesn't work on */

    /* background solver */
    pthread_t worker;
    _Atomic(SolveState) solving;
    Cube solve_input;
    SolverId solve_method; /* a->method when the solve started; the worker reads only this */
    bool solve_coach;      /* the result starts Coach practice rather than playback */
    Solution solve_output;
    const char *solve_error;

    /* camera */
    float yaw, pitch, dist;
    float yaw_v, pitch_v; /* radians/s the view keeps spinning after a drag */
    bool dragging;

    /* typed moves */
    char input[256];

    /* sticker editing */
    bool edit_mode;
    int paint_color;

    /* coach: the last guided scramble and the cube it makes; read it through app_guided_scramble */
    char coach_scramble[256];
    Cube scramble_cube;

    bool show_help; /* keyboard reference over the 3D view */

    /* Scramble and Reset keep the cube they replace, so a painted cube is one Undo away */
    Cube undo_cube;
    char undo_scramble[256];
    bool can_undo;

    /* keyboard focus: widgets take Tab stops 1..focus_n in draw order each frame */
    int focus;      /* the focused stop, 0 for none; the move input when it equals focus_input */
    int focus_n;    /* stops registered so far this frame */
    int focus_last; /* stops registered last frame */
    int focus_input; /* the move input's stop last frame */
    bool focus_ok[FOCUS_MAX + 1]; /* which stops were enabled last frame; Tab skips the rest */

    /* solution list: scroll position, follow-the-playhead, scrollbar drag, and the current step's move box */
    float sol_scroll;
    bool follow_current;
    int follow_seen; /* Playhead last seen by the step list; a change re-enables follow_current */
    bool thumb_drag;
    float thumb_grab;
    float move_scroll;

    float ui_scale; /* UI pixels per layout unit; the layout is designed at 1360x820 */
    int cursor; /* MouseCursor wanted this frame; widgets raise it from the arrow */
    Vector2 press_at; /* where the left button last went down; a click needs press and release on one widget */
    bool input_bad;   /* the typed moves failed to parse; cleared when the text changes */

    char status[256];
    bool status_error;

    /* IBM Plex, bundled next to the executable; see load_font */
    Font font, font_medium, font_semibold, font_mono;
    bool prime_ok; /* the mono face has U+2032, so moves can be written U′ */
    uint32_t rng;
} App;

/* Which face of the bundled family a label uses. */
typedef enum { TYPE_REGULAR, TYPE_MEDIUM, TYPE_SEMIBOLD, TYPE_MONO } TypeFace;

extern const float SPEED_SECONDS[NUM_SPEEDS];
extern const char *const SPEED_NAMES[NUM_SPEEDS];
extern const Color STICKER_COLORS[NUM_COLORS];

/* main.c */
void app_set_status(App *a, bool error, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void app_user_moves(App *a, const Move *m, int n, bool fast); /* clears any solution */
void app_scramble(App *a);
void app_reset(App *a);
void app_orient(App *a, int front, int top);
void app_undo(App *a); /* restores the cube the last Scramble or Reset replaced */
void app_start_solve(App *a);
void app_set_edit(App *a, bool on);
void app_paint_sticker(App *a, int sticker); /* paint_color onto one sticker; centers are left alone */
bool app_busy(const App *a);
bool app_can_change_cube(const App *a); /* Scramble, Reset, Undo, paint, Solve */
bool app_can_turn(const App *a);        /* typed and keyed turns: also off while painting */
/* The guided scramble while the screen cube is still what it made, else NULL. */
const char *app_guided_scramble(const App *a);
/* coach: on/off, then the actions the Coach card offers (see playback.h) */
void app_coach_toggle(App *a);
void app_coach_demo(App *a);
void app_coach_followed(App *a);
void app_coach_unsync(App *a, bool attempted);
void app_coach_restore(App *a);
void app_coach_repaint(App *a);
void app_coach_trust(App *a, const Move *m, int n); /* the typed moves become the truth; Solve again from there */
void app_coach_paint_top(App *a);                  /* paint the U face only, the rest stays locked */

/* render.c */
void render_init(void); /* after InitWindow */
void render_shutdown(void); /* before CloseWindow */
void render_cube3d(App *a, Rectangle view);
/* Sticker under the mouse in the 3D view, or -1. */
int render_pick_sticker(App *a, Rectangle view, Vector2 mouse);
void render_net(App *a, Rectangle area);

/* ui.c */
void ui_text(App *a, const char *s, float x, float y, float size, Color c);
float ui_text_width(App *a, const char *s, float size);
void ui_text_face(App *a, TypeFace t, const char *s, float x, float y, float size, Color c);
float ui_text_width_face(App *a, TypeFace t, const char *s, float size);
void ui_focus_keys(App *a); /* Tab, Shift+Tab and Esc; call once per frame before the widgets */
bool ui_input_focused(const App *a); /* typing goes to the move input */
bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled);
/* The next Tab stop for the widget at r, ringed while focused; true when focused, enabled and Enter is pressed. */
bool ui_focus_stop(App *a, Rectangle r, float radius, bool enabled);
/* Pressed and released inside r. */
bool ui_clicked(const App *a, Rectangle r);
/* "<sep>3 rotations" to follow a Move count, or "" when s has no rotations. */
void ui_rotations(const Solution *s, const char *sep, char *out, size_t size);
void ui_left_panel(App *a, Rectangle r);
void ui_seam(App *a, Rectangle r);   /* sync between your cube and the screen's, between the halves */
void ui_prompt(App *a, Rectangle r); /* moves and command words, along the bottom */
void ui_right_panel(App *a, Rectangle r);
void ui_top_bar(App *a, Rectangle r);
void ui_move_display(App *a, Rectangle r); /* the big next-move card under the cube */
void ui_help_overlay(App *a, Rectangle view);

/* palette: values from the Workbench design tokens */
#define UI_BG ((Color){20, 21, 24, 255})            /* #141518 window */
#define UI_PANEL ((Color){27, 28, 32, 255})         /* #1b1c20 side panels */
#define UI_RAISED ((Color){37, 39, 45, 255})        /* #25272d buttons */
#define UI_RAISED_HOVER ((Color){48, 51, 59, 255})
#define UI_BORDER ((Color){67, 70, 79, 255})        /* #43464f */
#define UI_OUTLINE ((Color){120, 124, 135, 255})    /* #787c87: 3:1 or more on panel, raised and hover, for unchecked controls */
#define UI_PRESSED ((Color){30, 32, 37, 255})       /* button fill while held */
#define UI_RULE ((Color){43, 45, 51, 255})          /* #2b2d33 dividers */
#define UI_TEXT ((Color){236, 235, 230, 255})       /* #ecebe6 */
#define UI_SECONDARY ((Color){180, 178, 171, 255})  /* #b4b2ab */
#define UI_TERTIARY ((Color){165, 163, 157, 255})   /* #a5a39d: 4.5:1 or more on raised, hover and tinted fills */
#define UI_DISABLED ((Color){138, 137, 131, 255})   /* #8a8983 inactive controls: dimmed but readable (~5:1 on panel) */
#define UI_ACCENT ((Color){169, 148, 255, 255})     /* #a994ff selection, current move */
#define UI_DONE ((Color){109, 192, 139, 255})       /* #6dc08b completed steps */
#define UI_INK ((Color){21, 22, 26, 255})           /* text on light fills */
#define UI_BAD ((Color){240, 100, 90, 255})
#define UI_DRIFT ((Color){240, 163, 94, 255})     /* #f0a35e the torn seam */

#endif
