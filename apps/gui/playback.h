/* Playback: the cube on screen, the queue of turns waiting to animate, and
 * Solution playback with its Playhead and Teach pauses. No raylib: the GUI
 * feeds it frame time and reads a PlaybackView back. See GLOSSARY.md. */
#ifndef RUBIKS_PLAYBACK_H
#define RUBIKS_PLAYBACK_H

#include "cube/cube.h"
#include "cube/solver.h"

#define PLAYBACK_QUEUE_MAX 4096
#define PLAYBACK_FAST_SECONDS 0.06f /* scramble turns, whatever the chosen speed */

typedef struct {
    Move m;
    bool fast;
    int sol_dir; /* +1 Solution move played forward, -1 undone, 0 anything else */
} PlaybackTurn;

/* Coach: practising the human-method Steps of a Solution on a physical cube.
 * The screen cube is the assumed state of the physical cube, the Playhead its
 * progress, and the Step's moves stay hidden unless shown. See GLOSSARY.md. */
typedef enum {
    COACH_OFF,
    COACH_SETUP,    /* match the screen to the physical cube; Solve starts practice */
    COACH_PRACTICE, /* a Step's goal and Lesson; its moves hidden unless shown */
    COACH_UNSYNCED, /* the physical cube may differ from the screen: restore it or paint it */
    COACH_REPAINT,  /* paint the physical cube as it is; Solve checks it and solves afresh */
    COACH_DONE,     /* every Step followed: the screen cube is solved */
} CoachMode;

/* Whether the screen state was validated after painting or inferred from followed moves.
 * In either case, the physical match is assumed. */
typedef enum { COACH_BASIS_ASSUMED, COACH_BASIS_PAINTED } CoachBasis;

/* Where the practised Step stands, meaningful only in PRACTICE and UNSYNCED.
 *   PRACTICE: HIDDEN <-> SHOWN; HIDDEN or SHOWN -> DEMO -> SHOWN; SHOWN -> FOLLOW -> HIDDEN at the next Step (or DONE)
 *   UNSYNCED: ATTEMPTED (tried alone) or MISMATCH (reported); restore -> HIDDEN
 * DEMO and FOLLOW move the Playhead by themselves; nothing else is accepted until they finish. */
typedef enum {
    COACH_STEP_HIDDEN,
    COACH_STEP_SHOWN,
    COACH_STEP_DEMO,
    COACH_STEP_FOLLOW,
    COACH_STEP_ATTEMPTED,
    COACH_STEP_MISMATCH,
} CoachStep;

typedef struct {
    CoachMode mode;
    CoachBasis basis;
    bool attempted; /* UNSYNCED after an independent attempt, not after a mismatch */
    bool locked;    /* PRACTICE or UNSYNCED: turns, transport, Solve, paint and scramble are off */
    bool hidden;    /* the Step's moves are not shown: every move list stays hidden */
    int step;       /* sol->steps index of the Step practised, -1 outside PRACTICE */
    bool running;   /* the Playhead is moving by itself */
} CoachView;

/* Solution playback. EMPTY: no Solution. A Teach pause is PAUSED at a Step's start. */
typedef enum { TRANSPORT_EMPTY, TRANSPORT_PAUSED, TRANSPORT_PLAYING } Transport;

/* Fields are private: use the functions below. Embeddable by value. */
typedef struct {
    Cube cube; /* on screen, not including the turn in progress */
    PlaybackTurn queue[PLAYBACK_QUEUE_MAX];
    int qhead, qtail;
    bool animating;
    PlaybackTurn anim;
    float anim_t;
    Transport transport;
    Solution sol; /* valid unless transport is EMPTY */
    Cube sol_start;
    int queued_pos; /* Solution moves queued or finished */
    int playhead;   /* Solution moves finished on screen */
    bool sealed;    /* set by a scramble (fast turns), cleared by set_cube */
    bool teach;
    int teach_paused_at; /* the position not to Teach-pause at again: the last pause, or where Play was pressed */
    CoachMode coach;
    CoachBasis coach_basis;
    CoachStep coach_sub;
    int coach_step;   /* sol.steps index of the Step practised, -1 outside PRACTICE */
    bool coach_top;   /* REPAINT from paint top: only the U face may be painted */
} Playback;

typedef struct {
    const Cube *cube;    /* on screen, not including the turn in progress */
    bool turning;        /* a turn is animating */
    Move turn;           /* the turn animating, valid when turning */
    float turn_t;        /* its progress 0..1, linear (the renderer eases it) */
    const Solution *sol; /* NULL when there is no Solution */
    const Cube *sol_start; /* the cube sol starts from; NULL without a Solution */
    int playhead;        /* 0..sol->n; 0 without a Solution */
    bool sealed;         /* a scramble made the cube and nothing has replaced it since: the seam is sealed without a Solution */
    bool playing;
    StepAt step;         /* solution_step_at(sol, playhead); step == -1 without a Solution */
    bool idle;           /* nothing turning, nothing queued, not playing, coach not moving the Playhead */
    CoachView coach;
} PlaybackView;

void playback_init(Playback *pb); /* solved cube, no Solution, paused, teach off */
PlaybackView playback_view(const Playback *pb);

/* Advances animation by dt seconds. turn_seconds is the chosen duration of a
 * quarter turn; <= 0 means instant. */
void playback_tick(Playback *pb, float dt, float turn_seconds);

/* User turns (or a scramble when fast): drops any Solution, then queues the turns. A scramble seals the seam. */
void playback_turn(Playback *pb, const Move *m, int n, bool fast);
/* Replaces the cube (reset, sticker painting): drops any Solution and discards queued turns. */
void playback_set_cube(Playback *pb, const Cube *c);
/* Finishes the turn in progress and every queued turn instantly. */
void playback_flush(Playback *pb);
/* If a Solution is loaded: finishes queued turns, drops it and pauses. */
void playback_drop_solution(Playback *pb);

/* Starts playing s from the cube on screen (after finishing queued turns).
 * Plays at once unless teach is on. A Solution with no moves leaves no Solution. */
void playback_load(Playback *pb, const Solution *s);
/* Play/pause. Playing from the end rewinds to the start first. */
void playback_toggle(Playback *pb);
/* One Solution move forward (dir > 0) or back (dir < 0). Pauses. */
void playback_step(Playback *pb, int dir);
/* Jumps straight to Playhead pos (clamped), discarding queued turns. Pauses. */
void playback_seek(Playback *pb, int pos);
/* Jumps to the start of the previous (dir < 0) or next Step with moves, or to either end. Pauses. */
void playback_seek_step(Playback *pb, int dir);
/* Teach pauses: stop at the start of each Step so its Lesson can be read. */
void playback_set_teach(Playback *pb, bool on);

/* Coach. The Playhead stays at the start of the practised Step while the Step is
 * being tried, so the screen cube is the physical cube's assumed state. Turns,
 * toggle, step and seek do nothing in PRACTICE and UNSYNCED. Turning the physical
 * cube is the user's job: these calls only record what they report. */
/* OFF -> SETUP. Drops any Solution. */
void playback_coach_begin(Playback *pb);
/* Any state -> OFF. Keeps the cube and any Solution as they are. */
void playback_coach_end(Playback *pb);
void playback_coach_set_basis(Playback *pb, CoachBasis basis);
/* SETUP or REPAINT, once a Solution has loaded: PRACTICE at its first Step. Returns
 * false, leaving the state alone, when there is no Step with moves to coach. */
bool playback_coach_start(Playback *pb);
/* PRACTICE: reveal or hide the Step's moves. */
void playback_coach_show(Playback *pb, bool on);
/* PRACTICE: reveal the moves and play them on screen, then return to the Step's start. */
void playback_coach_demo(Playback *pb);
/* PRACTICE with moves revealed: the user reports following them on the physical cube.
 * The screen plays the Step through and moves to the next one, assumed matching. */
void playback_coach_followed(Playback *pb);
/* PRACTICE -> UNSYNCED: the physical cube may differ from the screen. attempted: the
 * user tried the Step alone rather than following moves. */
void playback_coach_unsync(Playback *pb, bool attempted);
/* UNSYNCED -> PRACTICE at the Step's start: the physical cube was restored to it. */
void playback_coach_restore(Playback *pb);
/* PRACTICE or UNSYNCED -> REPAINT: drops the Solution, so the user paints the physical
 * cube and then Solve checks it and solves afresh. */
void playback_coach_repaint(Playback *pb);
/* UNSYNCED -> REPAINT: the typed moves are the truth, applied to the screen cube. The
 * Solution is dropped, so Solve re-plans from there. Does nothing outside UNSYNCED. */
void playback_coach_trust(Playback *pb, const Move *m, int n);
/* UNSYNCED -> REPAINT for the U face only: the other faces stay as the screen shows them. */
void playback_coach_paint_top(Playback *pb);
/* False only for a sticker off the U face while paint top is in force. */
bool playback_coach_may_paint(const Playback *pb, int sticker);

#endif
