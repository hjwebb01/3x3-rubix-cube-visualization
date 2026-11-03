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
        pb->transport = TRANSPORT_PAUSED;
        pb->teach_paused_at = pb->queued_pos;
    } else if (pb->queued_pos < pb->sol.n) {
        Move m = pb->sol.moves[pb->queued_pos++];
        enqueue(pb, m, false, 1);
    } else {
        pb->transport = TRANSPORT_PAUSED;
    }
}

static float turn_seconds_for(const PlaybackTurn *t, float turn_seconds) {
    float secs = t->fast ? PLAYBACK_FAST_SECONDS : turn_seconds;
    if (MOVE_TURNS(t->m) == 2) secs *= 1.5f;
    return secs;
}

static bool coach_practising(const Playback *pb) { return pb->coach == COACH_PRACTICE || pb->coach == COACH_UNSYNCED; }

/* The coach is moving the Playhead: a demonstration or a followed Step. */
static bool coach_running(const Playback *pb) {
    return pb->coach == COACH_PRACTICE && (pb->coach_sub == COACH_STEP_DEMO || pb->coach_sub == COACH_STEP_FOLLOW);
}

/* PRACTICE, nothing running: the user can act on the Step. */
static bool coach_free(const Playback *pb) { return pb->coach == COACH_PRACTICE && !coach_running(pb); }

/* Moves the Playhead to pos at once, discarding queued turns. */
static void set_playhead(Playback *pb, int pos) {
    pb->animating = false;
    pb->qhead = pb->qtail = 0;
    pb->cube = pb->sol_start;
    cube_apply_seq(&pb->cube, pb->sol.moves, pos);
    pb->queued_pos = pb->playhead = pos;
}

static int next_step_with_moves(const Playback *pb, int after) {
    for (int i = after + 1; i < pb->sol.nsteps; i++)
        if (pb->sol.steps[i].count > 0) return i;
    return -1;
}

/* Leaves coaching when the Solution it practises goes away. */
static void coach_forget(Playback *pb) {
    if (!coach_practising(pb) && pb->coach != COACH_DONE) return;
    pb->coach = COACH_OFF;
    pb->coach_step = -1;
}

/* Called between turns while coach_running: queues the next move of the Step, or ends the run. */
static void coach_drive(Playback *pb) {
    const SolveStep *st = &pb->sol.steps[pb->coach_step];
    int end = st->start + st->count;
    if (pb->queued_pos < end) {
        enqueue(pb, pb->sol.moves[pb->queued_pos++], false, 1);
        return;
    }
    if (pb->coach_sub == COACH_STEP_DEMO) {
        pb->coach_sub = COACH_STEP_SHOWN;
        set_playhead(pb, st->start); /* the demonstration rewinds: the physical cube is still at the start */
        return;
    }
    /* followed: the screen now shows the Step done, which the user is assumed to match */
    int next = next_step_with_moves(pb, pb->coach_step);
    pb->coach_basis = COACH_BASIS_ASSUMED;
    pb->coach_sub = COACH_STEP_HIDDEN;
    if (next < 0) {
        pb->coach = COACH_DONE;
        pb->coach_step = -1;
    } else {
        pb->coach_step = next;
    }
}

void playback_init(Playback *pb) {
    memset(pb, 0, sizeof *pb);
    cube_reset(&pb->cube);
    pb->coach_step = -1;
}

PlaybackView playback_view(const Playback *pb) {
    PlaybackView v = {
        .cube = &pb->cube,
        .turning = pb->animating,
        .turn = pb->anim.m,
        .turn_t = pb->animating ? pb->anim_t : 0,
        .playing = pb->transport == TRANSPORT_PLAYING,
        .idle = !pb->animating && pb->qhead == pb->qtail && pb->transport != TRANSPORT_PLAYING && !coach_running(pb),
    };
    if (pb->transport != TRANSPORT_EMPTY) {
        v.sol = &pb->sol;
        v.sol_start = &pb->sol_start;
        v.playhead = pb->playhead;
        v.step = solution_step_at(&pb->sol, pb->playhead);
    } else {
        v.step.step = -1;
    }
    v.sealed = pb->sealed;
    v.coach.mode = pb->coach;
    v.coach.basis = pb->coach_basis;
    v.coach.locked = coach_practising(pb);
    v.coach.attempted = pb->coach == COACH_UNSYNCED && pb->coach_sub == COACH_STEP_ATTEMPTED;
    v.coach.hidden = pb->coach == COACH_UNSYNCED || (pb->coach == COACH_PRACTICE && pb->coach_sub == COACH_STEP_HIDDEN);
    v.coach.step = -1;
    v.coach.running = coach_running(pb);
    if (v.coach.locked || pb->coach == COACH_DONE) {
        v.coach.step = pb->coach_step;
    }
    return v;
}

void playback_tick(Playback *pb, float dt, float turn_seconds) {
    for (int guard = 0; guard < 2 * SOL_MAX_MOVES + PLAYBACK_QUEUE_MAX; guard++) {
        if (!pb->animating) {
            if (pb->qhead == pb->qtail && coach_running(pb)) coach_drive(pb);
            if (pb->qhead == pb->qtail && pb->transport == TRANSPORT_PLAYING) auto_play(pb);
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
    if (coach_practising(pb)) return; /* the physical cube is not turned on screen while coaching */
    playback_drop_solution(pb);
    for (int i = 0; i < n; i++) enqueue(pb, m[i], fast, 0);
    if (fast) pb->sealed = true;
}

void playback_set_cube(Playback *pb, const Cube *c) {
    coach_forget(pb);
    pb->animating = false;
    pb->qhead = pb->qtail = 0;
    pb->transport = TRANSPORT_EMPTY;
    pb->sealed = false;
    pb->cube = *c;
}

void playback_flush(Playback *pb) {
    if (pb->animating) finish_turn(pb, &pb->anim);
    pb->animating = false;
    for (; pb->qhead < pb->qtail; pb->qhead++) finish_turn(pb, &pb->queue[pb->qhead % PLAYBACK_QUEUE_MAX]);
    pb->qhead = pb->qtail = 0;
}

void playback_drop_solution(Playback *pb) {
    coach_forget(pb);
    if (pb->transport == TRANSPORT_EMPTY) return;
    playback_flush(pb);
    pb->transport = TRANSPORT_EMPTY;
}

void playback_load(Playback *pb, const Solution *s) {
    playback_flush(pb);
    pb->transport = TRANSPORT_EMPTY;
    if (s->n == 0) return;
    pb->sol = *s;
    pb->sol_start = pb->cube;
    pb->queued_pos = 0;
    pb->playhead = 0;
    pb->teach_paused_at = 0;
    pb->transport = pb->teach ? TRANSPORT_PAUSED : TRANSPORT_PLAYING;
}

void playback_toggle(Playback *pb) {
    if (pb->transport == TRANSPORT_EMPTY || coach_practising(pb)) return;
    if (pb->transport == TRANSPORT_PLAYING) {
        pb->transport = TRANSPORT_PAUSED;
        return;
    }
    if (pb->queued_pos == pb->sol.n) playback_seek(pb, 0);
    pb->transport = TRANSPORT_PLAYING;
    pb->teach_paused_at = pb->queued_pos; /* never re-pause where Play was pressed */
}

void playback_step(Playback *pb, int dir) {
    if (pb->transport == TRANSPORT_EMPTY || coach_practising(pb)) return;
    pb->transport = TRANSPORT_PAUSED;
    if (dir > 0 && pb->queued_pos < pb->sol.n) {
        Move m = pb->sol.moves[pb->queued_pos++];
        enqueue(pb, m, false, 1);
    } else if (dir < 0 && pb->queued_pos > 0) {
        Move m = move_inverse(pb->sol.moves[--pb->queued_pos]);
        enqueue(pb, m, false, -1);
    }
}

void playback_seek(Playback *pb, int pos) {
    if (pb->transport == TRANSPORT_EMPTY || coach_practising(pb)) return;
    if (pos < 0) pos = 0;
    if (pos > pb->sol.n) pos = pb->sol.n;
    pb->transport = TRANSPORT_PAUSED;
    set_playhead(pb, pos);
}

void playback_seek_step(Playback *pb, int dir) {
    int target = dir < 0 ? 0 : pb->sol.n;
    for (int i = 0; i < pb->sol.nsteps; i++) {
        int st = pb->sol.steps[i].start;
        if (!pb->sol.steps[i].count) continue;
        if (dir < 0 && st < pb->playhead && st > target) target = st;
        if (dir > 0 && st > pb->playhead && st < target) target = st;
    }
    playback_seek(pb, target);
}

void playback_set_teach(Playback *pb, bool on) { pb->teach = on; }

void playback_coach_begin(Playback *pb) {
    if (pb->coach != COACH_OFF) return;
    playback_drop_solution(pb);
    pb->coach = COACH_SETUP;
    pb->coach_basis = COACH_BASIS_ASSUMED;
}

void playback_coach_end(Playback *pb) {
    if (coach_running(pb)) set_playhead(pb, pb->playhead);
    pb->coach = COACH_OFF;
    pb->coach_step = -1;
}

void playback_coach_set_basis(Playback *pb, CoachBasis basis) { pb->coach_basis = basis; }

bool playback_coach_start(Playback *pb) {
    if (pb->coach != COACH_SETUP && pb->coach != COACH_REPAINT) return false;
    int st = pb->transport != TRANSPORT_EMPTY ? next_step_with_moves(pb, -1) : -1;
    if (st < 0) return false;
    pb->transport = TRANSPORT_PAUSED;
    pb->coach = COACH_PRACTICE;
    pb->coach_step = st;
    pb->coach_sub = COACH_STEP_HIDDEN;
    set_playhead(pb, pb->sol.steps[st].start);
    return true;
}

void playback_coach_show(Playback *pb, bool on) {
    if (coach_free(pb)) pb->coach_sub = on ? COACH_STEP_SHOWN : COACH_STEP_HIDDEN;
}

void playback_coach_demo(Playback *pb) {
    if (!coach_free(pb)) return;
    set_playhead(pb, pb->sol.steps[pb->coach_step].start);
    pb->coach_sub = COACH_STEP_DEMO;
}

void playback_coach_followed(Playback *pb) {
    if (pb->coach != COACH_PRACTICE || pb->coach_sub != COACH_STEP_SHOWN) return;
    pb->coach_sub = COACH_STEP_FOLLOW;
}

void playback_coach_unsync(Playback *pb, bool attempted) {
    if (!coach_free(pb)) return;
    pb->coach = COACH_UNSYNCED;
    pb->coach_sub = attempted ? COACH_STEP_ATTEMPTED : COACH_STEP_MISMATCH;
}

void playback_coach_restore(Playback *pb) {
    if (pb->coach != COACH_UNSYNCED) return;
    pb->coach = COACH_PRACTICE;
    pb->coach_sub = COACH_STEP_HIDDEN;
    set_playhead(pb, pb->sol.steps[pb->coach_step].start);
}

void playback_coach_repaint(Playback *pb) {
    if (!coach_practising(pb) || coach_running(pb)) return;
    playback_drop_solution(pb); /* forgets the practice */
    pb->coach = COACH_REPAINT;
    pb->coach_top = false;
}

void playback_coach_trust(Playback *pb, const Move *m, int n) {
    if (pb->coach != COACH_UNSYNCED) return;
    playback_coach_repaint(pb); /* the typed moves change the cube, so the Solution is re-planned */
    for (int i = 0; i < n; i++)
        if (!move_is_rotation(m[i])) cube_apply(&pb->cube, m[i]); /* rotations are view turns: not sync */
}

void playback_coach_paint_top(Playback *pb) {
    if (pb->coach != COACH_UNSYNCED) return;
    playback_coach_repaint(pb);
    pb->coach_top = true;
}

bool playback_coach_may_paint(const Playback *pb, int sticker) {
    return !(pb->coach == COACH_REPAINT && pb->coach_top) || sticker / 9 == FACE_U;
}
