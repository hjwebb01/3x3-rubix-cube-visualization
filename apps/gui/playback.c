#include "playback.h"

#include <string.h>

static void finish_turn(Playback *pb, const PlaybackTurn *t) {
    cube_apply(&pb->cube, t->m);
    pb->playhead += t->sol_dir;
}

static void enqueue(Playback *pb, Move m, bool fast, int sol_dir) {
    if (pb->qtail - pb->qhead >= PLAYBACK_QUEUE_MAX) playback_flush(pb);
    pb->queue[pb->qtail % PLAYBACK_QUEUE_MAX] = (PlaybackTurn){m, fast, sol_dir};
    pb->qtail++;
}

static bool teach_stops_here(const Playback *pb) {
    StepAt at = solution_step_at(&pb->sol, pb->queued_pos);
    return at.k > 1 && pb->sol.steps[at.step].start == pb->queued_pos;
}

/* Called when the queue has run dry while playing: queue the next Solution move, or pause. */
static void auto_play(Playback *pb) {
    if (pb->teach && pb->queued_pos != pb->teach_paused_at && teach_stops_here(pb)) {
        pb->playing = false;
        pb->teach_paused_at = pb->queued_pos;
    } else if (pb->queued_pos < pb->sol.n) {
        Move m = pb->sol.moves[pb->queued_pos++];
        enqueue(pb, m, false, 1);
    } else {
        pb->playing = false;
    }
}

static float turn_seconds_for(const PlaybackTurn *t, float turn_seconds) {
    float secs = t->fast ? PLAYBACK_FAST_SECONDS : turn_seconds;
    if (MOVE_TURNS(t->m) == 2) secs *= 1.5f;
    return secs;
}

void playback_init(Playback *pb) {
    memset(pb, 0, sizeof *pb);
    cube_reset(&pb->cube);
}

PlaybackView playback_view(const Playback *pb) {
    PlaybackView v = {
        .cube = &pb->cube,
        .turning = pb->animating,
        .turn = pb->anim.m,
        .turn_t = pb->animating ? pb->anim_t : 0,
        .playing = pb->playing,
        .idle = !pb->animating && pb->qhead == pb->qtail && !pb->playing,
    };
    if (pb->has_sol) {
        v.sol = &pb->sol;
        v.playhead = pb->playhead;
        v.step = solution_step_at(&pb->sol, pb->playhead);
    } else {
        v.step.step = -1;
    }
    return v;
}

void playback_tick(Playback *pb, float dt, float turn_seconds) {
    for (int guard = 0; guard < 2 * SOL_MAX_MOVES + PLAYBACK_QUEUE_MAX; guard++) {
        if (!pb->animating) {
            if (pb->qhead == pb->qtail && pb->playing && pb->has_sol) auto_play(pb);
            if (pb->qhead == pb->qtail) return;
            pb->anim = pb->queue[pb->qhead++ % PLAYBACK_QUEUE_MAX];
            if (pb->qhead == pb->qtail) pb->qhead = pb->qtail = 0;
            pb->animating = true;
            pb->anim_t = 0;
        }
        float secs = turn_seconds_for(&pb->anim, turn_seconds);
        if (secs > 0) {
            pb->anim_t += dt / secs;
            if (pb->anim_t < 1) return;
        }
        /* finished (instant turns finish within this frame, one after another) */
        finish_turn(pb, &pb->anim);
        pb->animating = false;
        if (secs > 0) return;
    }
}

void playback_turn(Playback *pb, const Move *m, int n, bool fast) {
    playback_drop_solution(pb);
    for (int i = 0; i < n; i++) enqueue(pb, m[i], fast, 0);
}

void playback_set_cube(Playback *pb, const Cube *c) {
    pb->animating = false;
    pb->qhead = pb->qtail = 0;
    pb->has_sol = false;
    pb->playing = false;
    pb->cube = *c;
}

void playback_flush(Playback *pb) {
    if (pb->animating) finish_turn(pb, &pb->anim);
    pb->animating = false;
    for (; pb->qhead < pb->qtail; pb->qhead++) finish_turn(pb, &pb->queue[pb->qhead % PLAYBACK_QUEUE_MAX]);
    pb->qhead = pb->qtail = 0;
}

void playback_drop_solution(Playback *pb) {
    if (!pb->has_sol) return;
    playback_flush(pb);
    pb->has_sol = false;
    pb->playing = false;
}

void playback_load(Playback *pb, const Solution *s) {
    playback_flush(pb);
    pb->has_sol = s->n > 0;
    pb->playing = false;
    if (!pb->has_sol) return;
    pb->sol = *s;
    pb->sol_start = pb->cube;
    pb->queued_pos = 0;
    pb->playhead = 0;
    pb->teach_paused_at = 0;
    pb->playing = !pb->teach;
}

void playback_toggle(Playback *pb) {
    if (!pb->has_sol) return;
    if (pb->playing) {
        pb->playing = false;
        return;
    }
    if (pb->queued_pos == pb->sol.n) playback_seek(pb, 0);
    pb->playing = true;
    pb->teach_paused_at = pb->queued_pos; /* never re-pause where Play was pressed */
}

void playback_step(Playback *pb, int dir) {
    if (!pb->has_sol) return;
    pb->playing = false;
    if (dir > 0 && pb->queued_pos < pb->sol.n) {
        Move m = pb->sol.moves[pb->queued_pos++];
        enqueue(pb, m, false, 1);
    } else if (dir < 0 && pb->queued_pos > 0) {
        Move m = move_inverse(pb->sol.moves[--pb->queued_pos]);
        enqueue(pb, m, false, -1);
    }
}

void playback_seek(Playback *pb, int pos) {
    if (!pb->has_sol) return;
    if (pos < 0) pos = 0;
    if (pos > pb->sol.n) pos = pb->sol.n;
    pb->playing = false;
    pb->animating = false;
    pb->qhead = pb->qtail = 0;
    pb->cube = pb->sol_start;
    cube_apply_seq(&pb->cube, pb->sol.moves, pos);
    pb->queued_pos = pb->playhead = pos;
}

void playback_set_teach(Playback *pb, bool on) { pb->teach = on; }
