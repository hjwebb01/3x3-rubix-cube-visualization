/* Playback: user turns, the turn queue, Solution playback with its Playhead
 * and Teach pauses. Everything goes through the public interface. */
#include <stdint.h>
#include <stdio.h>

#include "check.h"
#include "playback.h"

static Playback pb; /* ~50 KB, so static */

static bool near(float a, float b) { return a - b < 1e-3f && b - a < 1e-3f; }

static Cube fresh_cube(void) {
    Cube c;
    cube_reset(&c);
    return c;
}

/* The solved cube with moves[0..n) applied. */
static Cube after(const Move *moves, int n) {
    Cube c = fresh_cube();
    cube_apply_seq(&c, moves, n);
    return c;
}

/* Pseudo-random moves, for queues that must match a direct application. */
static void random_moves(Move *m, int n, uint32_t seed) {
    for (int i = 0; i < n; i++) m[i] = (Move)(rng_next(&seed) % NUM_MOVES);
}

/* R U F L D: five moves on distinct layers, so none merge. */
static const Move ABC_MOVES[5] = {MOVE(MB_R, 1), MOVE(MB_U, 1), MOVE(MB_F, 1), MOVE(MB_L, 1), MOVE(MB_D, 1)};

/* Steps A [R U], B [F L], C [D]. */
static void build_abc(Solution *s) {
    solution_clear(s);
    solution_begin_step(s, LESSON_CROSS, "A");
    solution_push(s, NULL, ABC_MOVES[0]);
    solution_push(s, NULL, ABC_MOVES[1]);
    solution_begin_step(s, LESSON_F2L, "B");
    solution_push(s, NULL, ABC_MOVES[2]);
    solution_push(s, NULL, ABC_MOVES[3]);
    solution_begin_step(s, LESSON_PLL, "C");
    solution_push(s, NULL, ABC_MOVES[4]);
}

/* Steps A [R U], "OLL skip" [], B [F L]. */
static void build_skip(Solution *s) {
    solution_clear(s);
    solution_begin_step(s, LESSON_CROSS, "A");
    solution_push(s, NULL, ABC_MOVES[0]);
    solution_push(s, NULL, ABC_MOVES[1]);
    solution_begin_step(s, LESSON_OLL_SKIP, "OLL skip");
    solution_begin_step(s, LESSON_PLL, "B");
    solution_push(s, NULL, ABC_MOVES[2]);
    solution_push(s, NULL, ABC_MOVES[3]);
}

/* One Step holding R U F. */
static void build_flat(Solution *s) {
    solution_clear(s);
    solution_begin_step(s, LESSON_CROSS, "Flat");
    solution_push(s, NULL, ABC_MOVES[0]);
    solution_push(s, NULL, ABC_MOVES[1]);
    solution_push(s, NULL, ABC_MOVES[2]);
}

static void test_user_turn(void) {
    printf("playback: user turn\n");
    playback_init(&pb);
    Move r = MOVE(MB_R, 1);
    playback_turn(&pb, &r, 1, false);
    playback_tick(&pb, 0.1f, 0.4f);
    PlaybackView v = playback_view(&pb);
    CHECK(v.turning && v.turn == r, "R should be turning");
    CHECK(near(v.turn_t, 0.25f), "turn_t %f, want 0.25", v.turn_t);
    CHECK(!v.idle, "idle while turning");
    Cube fresh = fresh_cube();
    CHECK(cube_equal(v.cube, &fresh), "cube changed before the turn landed");
    playback_tick(&pb, 0.4f, 0.4f);
    v = playback_view(&pb);
    Cube want = after(&r, 1);
    CHECK(!v.turning, "R still turning after 0.5s");
    CHECK(cube_equal(v.cube, &want), "R did not land");
    CHECK(v.idle, "not idle after the turn");
}

static void test_half_turn_slower(void) {
    printf("playback: half turn takes 1.5x\n");
    playback_init(&pb);
    Move u2 = MOVE(MB_U, 2);
    playback_turn(&pb, &u2, 1, false);
    playback_tick(&pb, 0.5f, 0.4f);
    CHECK(playback_view(&pb).turning, "U2 landed after 0.5s, want 0.6s");
    playback_tick(&pb, 0.15f, 0.4f);
    PlaybackView v = playback_view(&pb);
    Cube want = after(&u2, 1);
    CHECK(!v.turning && cube_equal(v.cube, &want), "U2 not landed after 0.65s");
}

static void test_fast_ignores_speed(void) {
    printf("playback: fast turns ignore turn_seconds\n");
    playback_init(&pb);
    Move r = MOVE(MB_R, 1);
    playback_turn(&pb, &r, 1, true);
    playback_tick(&pb, 0.05f, 10.0f);
    CHECK(playback_view(&pb).turning, "fast turn landed after 0.05s");
    playback_tick(&pb, 0.02f, 10.0f);
    PlaybackView v = playback_view(&pb);
    Cube want = after(&r, 1);
    CHECK(!v.turning && cube_equal(v.cube, &want), "fast turn not landed after 0.07s");
}

static void test_instant_drains_queue(void) {
    printf("playback: turn_seconds 0 drains the queue in one tick\n");
    playback_init(&pb);
    Move m[25];
    random_moves(m, 25, 99);
    playback_turn(&pb, m, 25, false);
    playback_tick(&pb, 0.016f, 0.0f);
    PlaybackView v = playback_view(&pb);
    Cube want = after(m, 25);
    CHECK(v.idle, "queue not drained in one tick");
    CHECK(cube_equal(v.cube, &want), "instant queue differs from direct application");
}

static void test_load(void) {
    printf("playback: load\n");
    static Solution s;
    build_flat(&s);

    /* Teach off: plays at once; the start cube is the cube on screen. */
    playback_init(&pb);
    Move user[3] = {ABC_MOVES[0], ABC_MOVES[1], ABC_MOVES[2]};
    playback_turn(&pb, user, 3, false);
    playback_load(&pb, &s);
    PlaybackView v = playback_view(&pb);
    Cube on_screen = after(user, 3);
    CHECK(cube_equal(v.cube, &on_screen), "load did not finish the queued user turns");
    CHECK(v.playing && v.playhead == 0, "teach off: load should play at once");
    CHECK(v.sol != NULL, "Solution not loaded");
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    Cube want = after(user, 3);
    for (int i = 0; i < 3; i++) cube_apply(&want, ABC_MOVES[i]);
    CHECK(cube_equal(v.cube, &want), "playback did not start from the cube on screen");

    /* Empty Solution: no Solution. */
    static Solution empty;
    solution_clear(&empty);
    playback_init(&pb);
    playback_load(&pb, &empty);
    v = playback_view(&pb);
    CHECK(v.sol == NULL && !v.playing, "empty Solution should leave no Solution");
}

static void test_play_to_end(void) {
    printf("playback: play to the end\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_tick(&pb, 1.0f, 0.0f);
    PlaybackView v = playback_view(&pb);
    Cube want = after(ABC_MOVES, 5);
    CHECK(cube_equal(v.cube, &want), "end cube wrong");
    CHECK(v.playhead == 5, "playhead %d, want 5", v.playhead);
    CHECK(!v.playing && v.idle, "not paused at the end");
}

static void test_toggle_rewinds(void) {
    printf("playback: toggle at the end rewinds\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_tick(&pb, 1.0f, 0.0f);
    playback_toggle(&pb);
    PlaybackView v = playback_view(&pb);
    Cube fresh = fresh_cube();
    CHECK(v.playing && v.playhead == 0, "toggle at the end should rewind and play");
    CHECK(cube_equal(v.cube, &fresh), "rewind did not restore the start cube");
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    Cube want = after(ABC_MOVES, 5);
    CHECK(v.playhead == 5 && !v.playing && cube_equal(v.cube, &want), "replay did not reach the end");
}

static void test_step(void) {
    printf("playback: step\n");
    static Solution s;
    build_flat(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_step(&pb, 1);
    playback_step(&pb, 1);
    PlaybackView v = playback_view(&pb);
    CHECK(!v.playing, "step should pause");
    CHECK(v.playhead == 0, "playhead moved before the turn finished");
    playback_tick(&pb, 0.2f, 0.4f);
    v = playback_view(&pb);
    CHECK(v.turning && v.playhead == 0, "first step turn should be in progress");
    playback_tick(&pb, 0.3f, 0.4f);
    v = playback_view(&pb);
    CHECK(v.playhead == 1, "playhead %d after first turn, want 1", v.playhead);
    playback_tick(&pb, 0.5f, 0.4f);
    v = playback_view(&pb);
    CHECK(v.playhead == 2, "playhead %d after second turn, want 2", v.playhead);
    Cube two = after(ABC_MOVES, 2);
    CHECK(cube_equal(v.cube, &two), "two steps did not land R U");

    playback_step(&pb, -1);
    playback_tick(&pb, 0.5f, 0.4f);
    v = playback_view(&pb);
    Cube one = after(ABC_MOVES, 1);
    CHECK(v.playhead == 1 && cube_equal(v.cube, &one), "step back did not undo U");
}

static void test_seek(void) {
    printf("playback: seek\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_tick(&pb, 0.1f, 0.4f); /* first move in progress, more queued behind it */
    playback_seek(&pb, 2);
    PlaybackView v = playback_view(&pb);
    Cube two = after(ABC_MOVES, 2);
    CHECK(!v.playing && !v.turning, "seek should pause and discard the turn");
    CHECK(v.playhead == 2 && cube_equal(v.cube, &two), "seek did not land on R U");
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    CHECK(v.idle && cube_equal(v.cube, &two), "seek left queued turns behind");
    playback_seek(&pb, 99);
    v = playback_view(&pb);
    Cube all = after(ABC_MOVES, 5);
    CHECK(v.playhead == 5 && cube_equal(v.cube, &all), "seek past the end was not clamped");
}

static void test_seek_step(void) {
    printf("playback: seek by Step\n");
    static Solution s;
    build_skip(&s); /* A at 0, skipped Step at 2, B at 2 */
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_seek(&pb, 1);
    playback_seek_step(&pb, 1);
    CHECK(playback_view(&pb).playhead == 2, "next Step should land on B's start");
    playback_seek_step(&pb, 1);
    CHECK(playback_view(&pb).playhead == 4, "past the last Step should land on the end");
    playback_seek_step(&pb, -1);
    CHECK(playback_view(&pb).playhead == 2, "previous Step should land on B's start");
    playback_seek_step(&pb, -1);
    CHECK(playback_view(&pb).playhead == 0, "previous from B should land on A");
}

static void test_teach_pauses(void) {
    printf("playback: teach pauses\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_set_teach(&pb, true);
    playback_load(&pb, &s);
    PlaybackView v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 0, "teach load should wait at 0");

    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 2, "want a pause at B, playhead %d", v.playhead);

    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 4, "want a pause at C, playhead %d", v.playhead);

    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    Cube all = after(ABC_MOVES, 5);
    CHECK(!v.playing && v.playhead == 5 && cube_equal(v.cube, &all), "did not play to the end");
}

static void test_teach_continue(void) {
    printf("playback: a Teach pause is flagged, and the next move typed continues it\n");
    static Solution s;
    build_abc(&s); /* the next move at the first pause is F */
    playback_init(&pb);
    playback_set_teach(&pb, true);
    playback_load(&pb, &s);
    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    PlaybackView v = playback_view(&pb);
    CHECK(v.teach_stop && v.playhead == 2 && !v.playing, "no Teach pause flagged at B");
    Move wrong = MOVE(MB_R, 1);
    CHECK(!playback_teach_continue(&pb, &wrong, 1), "a wrong move continued the Teach pause");
    CHECK(!playback_view(&pb).playing, "a wrong move started playing");
    Move rot_f[2] = {MOVE(MB_Y, 1), MOVE(MB_F, 1)};
    CHECK(playback_teach_continue(&pb, rot_f, 2), "the next move did not continue the Teach pause");
    v = playback_view(&pb);
    CHECK(v.playing && !v.teach_stop, "continuing did not resume playing");
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 4 && v.teach_stop, "playing on did not pause again at C");
}

static void test_teach_skipped_step(void) {
    printf("playback: teach with a Skipped Step\n");
    static Solution s;
    build_skip(&s);
    playback_init(&pb);
    playback_set_teach(&pb, true);
    playback_load(&pb, &s);
    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    PlaybackView v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 2, "want one pause at B, playhead %d", v.playhead);

    /* Nothing else stops before the end: one toggle plays to the finish. */
    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    v = playback_view(&pb);
    CHECK(!v.playing && v.playhead == 4, "second pause, or did not finish, playhead %d", v.playhead);
}

static void test_seek_then_play_in_teach(void) {
    printf("playback: seek to a Step start, then play in teach mode\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_set_teach(&pb, true);
    playback_load(&pb, &s);
    playback_seek(&pb, 2);
    playback_toggle(&pb);
    playback_tick(&pb, 1.0f, 0.0f);
    PlaybackView v = playback_view(&pb);
    CHECK(v.playhead == 4 && !v.playing, "play from B's start should reach the pause at C, playhead %d", v.playhead);
}

static void test_turn_and_set_cube_drop_solution(void) {
    printf("playback: user turn and set_cube drop the Solution\n");
    static Solution s;
    build_abc(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    Move f = MOVE(MB_F, 1);
    playback_turn(&pb, &f, 1, false);
    CHECK(playback_view(&pb).sol == NULL, "user turn kept the Solution");

    playback_load(&pb, &s);
    Cube c = after(&f, 1);
    playback_set_cube(&pb, &c);
    PlaybackView v = playback_view(&pb);
    CHECK(v.sol == NULL && !v.playing, "set_cube kept the Solution or kept playing");
    CHECK(cube_equal(v.cube, &c), "set_cube did not replace the cube");
}

static void test_flush(void) {
    printf("playback: flush\n");
    static Solution s;
    build_flat(&s);
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_step(&pb, 1);
    playback_step(&pb, 1);
    playback_step(&pb, 1);
    playback_tick(&pb, 0.1f, 0.4f);
    playback_flush(&pb);
    PlaybackView v = playback_view(&pb);
    Cube three = after(ABC_MOVES, 3);
    CHECK(v.playhead == 3 && cube_equal(v.cube, &three), "flush did not finish the queue");
    CHECK(v.idle, "flush left something turning");
}

static void test_overflow(void) {
    printf("playback: queue overflow\n");
    static Move m[PLAYBACK_QUEUE_MAX + 10];
    random_moves(m, PLAYBACK_QUEUE_MAX + 10, 4242);
    playback_init(&pb);
    for (int i = 0; i < PLAYBACK_QUEUE_MAX + 10; i++) playback_turn(&pb, &m[i], 1, false);
    playback_flush(&pb);
    PlaybackView v = playback_view(&pb);
    Cube want = after(m, PLAYBACK_QUEUE_MAX + 10);
    CHECK(v.idle, "flush left turns queued");
    CHECK(cube_equal(v.cube, &want), "overflowing queue lost or reordered turns");
}

static void test_coach_transitions(void) {
    printf("playback: coach demonstration, recovery and completion\n");
    static Solution s;
    build_skip(&s);
    playback_init(&pb);
    Cube start = fresh_cube();
    for (int i = s.n - 1; i >= 0; i--) cube_apply(&start, move_inverse(s.moves[i]));
    playback_set_cube(&pb, &start);
    playback_coach_begin(&pb);
    CHECK(!playback_coach_start(&pb), "coach started without a solution");
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    PlaybackView v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_PRACTICE && v.coach.hidden && v.coach.step == 0, "practice must start hidden");
    playback_coach_followed(&pb);
    CHECK(!playback_view(&pb).coach.running, "followed hidden moves");
    playback_coach_demo(&pb);
    CHECK(playback_view(&pb).coach.running && !playback_view(&pb).coach.hidden, "demo did not reveal and run");
    playback_tick(&pb, 1, 0);
    v = playback_view(&pb);
    CHECK(!v.coach.running && v.playhead == 0 && cube_equal(v.cube, &start), "demo did not rewind");
    playback_coach_followed(&pb);
    playback_tick(&pb, 1, 0);
    v = playback_view(&pb);
    CHECK(v.coach.step == 2 && v.playhead == 2 && v.coach.hidden, "did not skip empty step");
    playback_coach_unsync(&pb, true);
    v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_UNSYNCED && v.coach.attempted, "attempt not recorded");
    playback_coach_restore(&pb);
    v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_PRACTICE && v.playhead == 2 && v.coach.hidden, "restore did not return to step start");
    playback_coach_show(&pb, true);
    playback_coach_followed(&pb);
    playback_tick(&pb, 1, 0);
    v = playback_view(&pb);
    Cube solved = fresh_cube();
    CHECK(v.coach.mode == COACH_DONE && v.playhead == s.n && cube_equal(v.cube, &solved), "coach did not finish solved");
}

static void test_coach_repaint(void) {
    printf("playback: coach repaint and re-solve\n");
    static Solution s, replacement;
    build_flat(&s);
    playback_init(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    playback_coach_unsync(&pb, true);
    playback_coach_repaint(&pb);
    CHECK(playback_view(&pb).coach.mode == COACH_REPAINT && !playback_view(&pb).sol, "repaint retained old solution");
    Cube painted = after(ABC_MOVES, 1);
    playback_set_cube(&pb, &painted);
    solution_clear(&replacement);
    solution_begin_step(&replacement, LESSON_CROSS, "Re-solved");
    solution_push(&replacement, NULL, move_inverse(ABC_MOVES[0]));
    playback_coach_set_basis(&pb, COACH_BASIS_PAINTED);
    playback_load(&pb, &replacement);
    CHECK(playback_coach_start(&pb), "repainted solution did not start");
    PlaybackView v = playback_view(&pb);
    CHECK(v.coach.basis == COACH_BASIS_PAINTED && cube_equal(v.cube, &painted), "painted basis lost");
    playback_coach_show(&pb, true);
    playback_coach_followed(&pb);
    playback_tick(&pb, 1, 0);
    Cube solved = fresh_cube();
    v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_DONE && cube_equal(v.cube, &solved), "repainted cube did not finish");
}

static void test_coach_trust(void) {
    printf("playback: coach trust applies the typed move and re-solves\n");
    static Solution s, replacement;
    build_flat(&s);
    playback_init(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    Move typed = ABC_MOVES[1];
    playback_coach_trust(&pb, &typed, 1);
    CHECK(playback_view(&pb).coach.mode == COACH_PRACTICE, "trust outside UNSYNCED changed the state");
    playback_coach_unsync(&pb, false);
    playback_coach_trust(&pb, &typed, 1);
    PlaybackView v = playback_view(&pb);
    Cube typed_cube = after(&typed, 1);
    CHECK(v.coach.mode == COACH_REPAINT && !v.sol, "trust did not drop the solution to re-solve");
    CHECK(cube_equal(v.cube, &typed_cube), "trust did not apply the typed move to the model");
    solution_clear(&replacement);
    solution_begin_step(&replacement, LESSON_CROSS, "Re-solved");
    solution_push(&replacement, NULL, move_inverse(typed));
    playback_coach_set_basis(&pb, COACH_BASIS_PAINTED);
    playback_load(&pb, &replacement);
    CHECK(playback_coach_start(&pb), "re-solved practice did not start");
    v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_PRACTICE && v.coach.step == 0 && v.playhead == 0, "practice did not continue from the re-solve");
}

static void test_coach_trust_rotations(void) {
    printf("playback: coach trust ignores rotations\n");
    static Solution s;
    build_flat(&s);
    playback_init(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    playback_coach_unsync(&pb, false);
    Move typed[2] = {ABC_MOVES[1], MOVE(MB_X, 1)};
    playback_coach_trust(&pb, typed, 2);
    Move face = ABC_MOVES[1];
    Cube typed_cube = after(&face, 1);
    CHECK(cube_equal(playback_view(&pb).cube, &typed_cube), "trust applied a rotation to the model");
}

static void test_coach_paint_top(void) {
    printf("playback: coach paint top reopens only the top face\n");
    static Solution s;
    build_flat(&s);
    playback_init(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    playback_coach_paint_top(&pb);
    CHECK(playback_view(&pb).coach.mode == COACH_PRACTICE, "paint top outside UNSYNCED changed the state");
    playback_coach_unsync(&pb, false);
    playback_coach_paint_top(&pb);
    CHECK(playback_view(&pb).coach.mode == COACH_REPAINT && !playback_view(&pb).sol, "paint top did not reopen for painting");
    CHECK(playback_coach_may_paint(&pb, FACE_U * 9 + 1), "top face locked");
    CHECK(!playback_coach_may_paint(&pb, FACE_R * 9 + 1), "other face unlocked by paint top");
    CHECK(!playback_coach_may_paint(&pb, FACE_L * 9 + 7), "other face unlocked by paint top");
    playback_coach_unsync(&pb, false); /* REPAINT is not PRACTICE: this must not leave it */
    CHECK(playback_view(&pb).coach.mode == COACH_REPAINT, "unsync left repaint");
    playback_coach_end(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not restart");
    playback_coach_unsync(&pb, false);
    playback_coach_repaint(&pb);
    CHECK(playback_coach_may_paint(&pb, FACE_R * 9 + 1), "full repaint kept the top-only lock");
}

static void test_coach_typed(void) {
    printf("playback: typed moves check the Step: a match plays it through, a mismatch tears the seam, rotations are ignored\n");
    static Solution s;
    build_abc(&s); /* Step A is R U */
    playback_init(&pb);
    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");

    Move wrong = MOVE(MB_F, 1);
    CHECK(playback_coach_typed(&pb, &wrong, 1) == COACH_TYPED_MISMATCH, "a wrong move was not a mismatch");
    PlaybackView v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_UNSYNCED && v.coach.attempted, "a mismatch did not tear the seam as an attempt");
    playback_coach_restore(&pb);

    Move rot_ru[3] = {MOVE(MB_X, 1), MOVE(MB_R, 1), MOVE(MB_U, 1)};
    CHECK(playback_coach_typed(&pb, rot_ru, 3) == COACH_TYPED_MATCH, "rotations were not ignored in the check");
    CHECK(playback_view(&pb).coach.running, "a match did not start playing the Step through");
    playback_tick(&pb, 0.0f, 0.0f); /* instant turns: the Step plays through in one call */
    v = playback_view(&pb);
    CHECK(!v.coach.running && v.coach.step == 1, "the matched Step did not advance to Step B");
    Cube two = after(ABC_MOVES, 2);
    CHECK(cube_equal(v.cube, &two), "the screen did not play the matched Step through");
    CHECK(v.coach.basis == COACH_BASIS_ASSUMED, "a matched Step did not assume the physical match");

    Move partial = MOVE(MB_F, 1);
    CHECK(playback_coach_typed(&pb, &partial, 1) == COACH_TYPED_MISMATCH, "a Step B mismatch was not reported");
    playback_coach_restore(&pb);
    CHECK(playback_coach_typed(&pb, rot_ru, 1) == COACH_TYPED_IGNORED, "a rotation alone was checked as a move");
    CHECK(playback_view(&pb).coach.step == 1 && playback_view(&pb).coach.mode == COACH_PRACTICE,
          "an ignored input changed the state");
}

static void test_scramble_seals(void) {
    printf("playback: a scramble seals the seam; a user turn does not; replacing the cube unseals it\n");
    playback_init(&pb);
    Move f = MOVE(MB_F, 1);
    playback_turn(&pb, &f, 1, false);
    CHECK(!playback_view(&pb).sealed, "a user turn sealed the seam");
    Move scr[2] = {MOVE(MB_R, 1), MOVE(MB_U, 1)};
    playback_turn(&pb, scr, 2, true);
    PlaybackView v = playback_view(&pb);
    CHECK(v.sealed && v.sol == NULL && v.playhead == 0, "a scramble did not seal the seam at playhead 0");
    Cube solved = fresh_cube();
    playback_set_cube(&pb, &solved);
    CHECK(!playback_view(&pb).sealed, "set_cube kept the seal");
}

static void test_playhead_seam(void) {
    printf("playback: the Playhead the seam drives: seek, clamping, and no seeking while practising\n");
    static Solution s;
    build_abc(&s); /* 5 moves: A [0..2), B [2..4), C [4..5) */
    playback_init(&pb);
    playback_load(&pb, &s);
    playback_seek(&pb, 3);
    Cube three = after(ABC_MOVES, 3);
    PlaybackView v = playback_view(&pb);
    CHECK(v.playhead == 3 && cube_equal(v.cube, &three), "seek to the middle did not land on the Playhead");
    CHECK(v.step.step == 1, "the middle of the Solution should be in Step B");
    playback_seek(&pb, -4);
    CHECK(playback_view(&pb).playhead == 0, "seek below 0 was not clamped");
    playback_seek(&pb, 42);
    CHECK(playback_view(&pb).playhead == 5, "seek above n was not clamped");

    playback_coach_begin(&pb);
    playback_load(&pb, &s);
    CHECK(playback_coach_start(&pb), "coach did not start");
    int before = playback_view(&pb).playhead;
    playback_seek(&pb, 4);
    v = playback_view(&pb);
    CHECK(v.coach.mode == COACH_PRACTICE && v.playhead == before, "seek moved the Playhead while a Step is practised");
}

void test_playback(void) {
    test_user_turn();
    test_half_turn_slower();
    test_fast_ignores_speed();
    test_instant_drains_queue();
    test_load();
    test_play_to_end();
    test_toggle_rewinds();
    test_step();
    test_seek();
    test_seek_step();
    test_teach_pauses();
    test_teach_skipped_step();
    test_seek_then_play_in_teach();
    test_turn_and_set_cube_drop_solution();
    test_flush();
    test_overflow();
    test_coach_transitions();
    test_coach_repaint();
    test_coach_trust();
    test_coach_trust_rotations();
    test_coach_paint_top();
    test_coach_typed();
    test_teach_continue();
    test_scramble_seals();
    test_playhead_seam();
}
