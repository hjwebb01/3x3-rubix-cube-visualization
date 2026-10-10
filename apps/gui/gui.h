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
    int sol_pos;
    bool playing;
    int speed; /* index into SPEED_SECONDS */
    SolverId method;

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

    /* right panel scroll */
    float sol_scroll;
    bool follow_current;

    int cursor; /* MouseCursor wanted this frame; widgets raise it from the arrow */

    char status[256];
    bool status_error;

    Font font;
    uint32_t rng;
} App;

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
void render_cube3d(App *a, Rectangle view);
/* Sticker under the mouse in the 3D view, or -1. */
int render_pick_sticker(App *a, Rectangle view, Vector2 mouse);
void render_net(App *a, Rectangle area, int *hover_out);

/* ui.c */
void ui_text(App *a, const char *s, float x, float y, float size, Color c);
float ui_text_width(App *a, const char *s, float size);
bool ui_button(App *a, Rectangle r, const char *label, bool active, bool enabled);
void ui_left_panel(App *a, Rectangle r);
void ui_right_panel(App *a, Rectangle r);
void ui_top_bar(App *a, Rectangle r);

/* palette */
#define UI_BG ((Color){24, 26, 32, 255})
#define UI_PANEL ((Color){33, 36, 44, 255})
#define UI_PANEL_EDGE ((Color){50, 54, 66, 255})
#define UI_BUTTON ((Color){52, 57, 70, 255})
#define UI_BUTTON_HOVER ((Color){68, 75, 92, 255})
#define UI_ACCENT ((Color){86, 156, 255, 255})
#define UI_TEXT ((Color){225, 228, 235, 255})
#define UI_DIM ((Color){140, 146, 160, 255})
#define UI_GOOD ((Color){110, 200, 120, 255})
#define UI_BAD ((Color){240, 100, 90, 255})

#endif
