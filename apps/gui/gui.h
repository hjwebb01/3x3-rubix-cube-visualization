/* Shared state for the raylib front end. */
#ifndef RUBIKS_GUI_H
#define RUBIKS_GUI_H
#include <pthread.h>
#include <stdatomic.h>

#include "raylib.h"

#include "cube/cube.h"
#include "cube/solver.h"

#define QUEUE_MAX 4096
#define NUM_SPEEDS 6

typedef struct {
    Move m;
    bool fast; /* scramble moves animate quickly regardless of the speed setting */
    int sol_dir; /* +1 solution move played forward, -1 undone, 0 anything else */
} QueuedMove;

typedef struct {
    /* cube shown on screen, not including the move currently animating */
    Cube cube;

    /* animation queue */
    QueuedMove queue[QUEUE_MAX];
    int qhead, qtail;
    bool animating;
    QueuedMove anim;
    float anim_t; /* 0..1 */

    /* solution playback: the logical state is sol_start + sol.moves[0..sol_pos) */
    Solution sol;
    bool has_sol;
    Cube sol_start;
    int sol_pos;  /* moves queued or applied: where the playhead is */
    int done_pos; /* moves finished on screen: the next move to run is sol.moves[done_pos] */
    bool playing;
    int speed; /* index into SPEED_SECONDS */
    SolverId method;     /* the selected method, used by the next Solve */
    SolverId sol_method; /* the method that produced the current solution */
    bool teach;         /* pause playback at the start of each step */
    int teach_paused_at; /* sol_pos of the last teach pause, so Play continues past it */

    /* background solver */
    pthread_t worker;
    atomic_int solving; /* 1 while the worker runs, 2 when it has a result */
    Cube solve_input;
    Solution solve_output;
    const char *solve_error;

    /* camera */
    float yaw, pitch, dist;
    bool dragging;
    Vector2 drag_start;
    bool drag_moved;

    /* typed moves */
    char input[256];
    bool input_focus;

    /* sticker editing */
    bool edit_mode;
    int paint_color;

    bool show_help; /* keyboard reference over the 3D view */

    /* solution list: scroll position, follow-the-playhead, scrollbar drag, and the current step's move box */
    float sol_scroll;
    bool follow_current;
    bool thumb_drag;
    float thumb_grab;
    float move_scroll;

    float ui_scale; /* UI pixels per layout unit; the layout is designed at 1360x820 */
    int cursor; /* MouseCursor wanted this frame; widgets raise it from the arrow */

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
void app_start_solve(App *a);
void app_flush(App *a); /* finish all queued animation instantly */
void app_step(App *a, int dir);
void app_jump(App *a, int pos);
void app_set_edit(App *a, bool on);
bool app_busy(const App *a);

/* render.c */
void render_init(void); /* after InitWindow */
void render_shutdown(void); /* before CloseWindow */
void render_cube3d(App *a, Rectangle view);
/* Sticker under the mouse in the 3D view, or -1. */
int render_pick_sticker(App *a, Rectangle view, Vector2 mouse);
void render_net(App *a, Rectangle area, int *hover_out);

/* ui.c */
void ui_text(App *a, const char *s, float x, float y, float size, Color c);
float ui_text_width(App *a, const char *s, float size);
void ui_text_face(App *a, TypeFace t, const char *s, float x, float y, float size, Color c);
float ui_text_width_face(App *a, TypeFace t, const char *s, float size);
bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled);
void ui_left_panel(App *a, Rectangle r);
void ui_right_panel(App *a, Rectangle r);
void ui_top_bar(App *a, Rectangle r);
void ui_move_display(App *a, Rectangle r); /* the big next-move card under the cube */
void ui_transport(App *a, Rectangle r);
void ui_help_overlay(App *a, Rectangle view);

/* palette: values from the Workbench design tokens */
#define UI_BG ((Color){20, 21, 24, 255})            /* #141518 window */
#define UI_PANEL ((Color){27, 28, 32, 255})         /* #1b1c20 side panels */
#define UI_RAISED ((Color){37, 39, 45, 255})        /* #25272d buttons */
#define UI_RAISED_HOVER ((Color){48, 51, 59, 255})
#define UI_BORDER ((Color){67, 70, 79, 255})        /* #43464f */
#define UI_RULE ((Color){43, 45, 51, 255})          /* #2b2d33 dividers */
#define UI_TEXT ((Color){236, 235, 230, 255})       /* #ecebe6 */
#define UI_SECONDARY ((Color){180, 178, 171, 255})  /* #b4b2ab */
#define UI_TERTIARY ((Color){143, 141, 135, 255})   /* #8f8d87 */
#define UI_DISABLED ((Color){102, 101, 95, 255})    /* #66655f inactive controls only */
#define UI_ACCENT ((Color){169, 148, 255, 255})     /* #a994ff selection, current move */
#define UI_DONE ((Color){109, 192, 139, 255})       /* #6dc08b completed steps */
#define UI_INK ((Color){21, 22, 26, 255})           /* text on light fills */
#define UI_BAD ((Color){240, 100, 90, 255})

/* aliases used by the drawing code */
#define UI_DIM UI_TERTIARY
#define UI_PANEL_EDGE UI_RULE
#define UI_BUTTON UI_RAISED
#define UI_BUTTON_HOVER UI_RAISED_HOVER
#define UI_GOOD UI_DONE

#endif
