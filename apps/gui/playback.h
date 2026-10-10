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

/* Fields are private: use the functions below. Embeddable by value. */
typedef struct {
    Cube cube; /* on screen, not including the turn in progress */
    PlaybackTurn queue[PLAYBACK_QUEUE_MAX];
    int qhead, qtail;
    bool animating;
    PlaybackTurn anim;
    float anim_t;
    Solution sol;
    bool has_sol;
    Cube sol_start;
    int queued_pos; /* Solution moves queued or finished */
    int playhead;   /* Solution moves finished on screen */
    bool playing, teach;
    int teach_paused_at;
} Playback;

typedef struct {
    const Cube *cube;    /* on screen, not including the turn in progress */
    bool turning;        /* a turn is animating */
    Move turn;           /* the turn animating, valid when turning */
    float turn_t;        /* its progress 0..1, linear (the renderer eases it) */
    const Solution *sol; /* NULL when there is no Solution */
    int playhead;        /* 0..sol->n; 0 without a Solution */
    bool playing;
    StepAt step;         /* solution_step_at(sol, playhead); step == -1 without a Solution */
    bool idle;           /* nothing turning, nothing queued, not playing */
} PlaybackView;

void playback_init(Playback *pb); /* solved cube, no Solution, paused, teach off */
PlaybackView playback_view(const Playback *pb);

/* Advances animation by dt seconds. turn_seconds is the chosen duration of a
 * quarter turn; <= 0 means instant. */
void playback_tick(Playback *pb, float dt, float turn_seconds);

/* User turns (or a scramble when fast): drops any Solution, then queues the turns. */
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
/* Teach pauses: stop at the start of each Step so its Lesson can be read. */
void playback_set_teach(Playback *pb, bool on);

#endif
