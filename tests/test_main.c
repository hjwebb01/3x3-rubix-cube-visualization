/* Self-contained test runner: move algebra, conversions, algorithm table
 * coverage, Lessons, Step lookup, Focus, Playback (test_playback.c) and
 * randomized end-to-end solves for every solver. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "check.h"
#include "solvers/solvers.h"

int g_failures, g_checks;

void test_playback(void);

static Cube apply_alg(const char *alg) {
    Cube c;
    Move m[128];
    cube_reset(&c);
    int n = moves_parse(alg, m, 128);
    if (n < 0) {
        printf("bad alg in test: %s\n", alg);
        exit(1);
    }
    cube_apply_seq(&c, m, n);
    return c;
}


static void test_orientation(void) {
    Cube original = apply_alg("R U F2 L D'");
    int valid = 0;
    for (int front = 0; front < NUM_COLORS; front++)
        for (int top = 0; top < NUM_COLORS; top++) {
            Cube c = original;
            if (cube_orient(&c, front, top)) {
                valid++;
                CHECK(cube_center(&c, FACE_F) == front && cube_center(&c, FACE_U) == top,
                      "orientation centers");
                CHECK(cube_validate(&c) == NULL, "orientation preserves legality");
                cube_orient(&c, COLOR_GREEN, COLOR_WHITE);
                CHECK(cube_equal(&c, &original), "orientation preserves arrangement");
            } else {
                CHECK(cube_equal(&c, &original), "invalid orientation leaves cube unchanged");
            }
        }
    CHECK(valid == 24, "all 24 orientations available");
}

static void test_moves(void) {
    printf("moves\n");
    for (int m = 0; m < NUM_MOVES; m++) {
        Cube c;
        cube_reset(&c);
        for (int i = 0; i < 4; i++) cube_apply(&c, MOVE(MOVE_BASE(m), 1));
        CHECK(cube_is_solved(&c), "%s^4 is not identity", move_name(MOVE(MOVE_BASE(m), 1)));
        cube_reset(&c);
        Cube ref = c;
        cube_apply(&c, (Move)m);
        cube_apply(&c, move_inverse((Move)m));
        CHECK(cube_equal(&c, &ref), "%s then inverse is not identity", move_name((Move)m));
    }
    /* known effects: R moves the F face's right column to U */
    Cube c = apply_alg("R");
    CHECK(c.f[FACE_U * 9 + 2] == COLOR_GREEN && c.f[FACE_U * 9 + 8] == COLOR_GREEN, "R does not move F to U");
    c = apply_alg("U");
    CHECK(c.f[FACE_L * 9 + 0] == COLOR_GREEN, "U does not move F to L");
    c = apply_alg("F");
    CHECK(c.f[FACE_R * 9 + 0] == COLOR_WHITE, "F does not move U to R");

    /* slices / wide / rotations are consistent with face turns */
    Cube a = apply_alg("r"), b = apply_alg("R M'");
    CHECK(cube_equal(&a, &b), "r != R M'");
    a = apply_alg("x");
    b = apply_alg("R M' L'");
    CHECK(cube_equal(&a, &b), "x != R M' L'");
    a = apply_alg("y");
    b = apply_alg("U E' D'");
    CHECK(cube_equal(&a, &b), "y != U E' D'");
    a = apply_alg("z");
    b = apply_alg("F S B'");
    CHECK(cube_equal(&a, &b), "z != F S B'");
    a = apply_alg("Rw2 Uw'");
    b = apply_alg("r2 u'");
    CHECK(cube_equal(&a, &b), "Rw notation differs from r notation");

    /* y-remap: performing R "as if R were the front" is B */
    for (int k = 0; k < 4; k++) {
        static const int FRONTS[4] = {FACE_F, FACE_R, FACE_B, FACE_L};
        CHECK(move_remap_y(MOVE(MB_F, 1), k) == MOVE(FRONTS[k], 1), "remap y^%d of F", k);
    }
    for (int m = 0; m < NUM_MOVES; m++) {
        Move seq[3] = {MOVE(MB_Y, 1), (Move)m, MOVE(MB_Y, 3)};
        Cube x, y;
        cube_reset(&x);
        cube_reset(&y);
        cube_apply_seq(&x, seq, 3);
        cube_apply(&y, move_remap_y((Move)m, 1));
        CHECK(cube_equal(&x, &y), "remap of %s", move_name((Move)m));
    }

    /* parse / format round trip */
    Move mv[64], mv2[64];
    int n = moves_parse("R U2 R' F' (r u) M2 x y' z2 Lw' D3", mv, 64);
    CHECK(n == 12, "parse count %d", n);
    char buf[256];
    moves_format(mv, n, buf, sizeof buf);
    CHECK(strcmp(buf, "R U2 R' F' r u M2 x y' z2 l' D'") == 0, "format gave '%s'", buf);
    int n2 = moves_parse(buf, mv2, 64);
    CHECK(n2 == n && memcmp(mv, mv2, (size_t)n) == 0, "round trip");
    CHECK(moves_parse("R Q", mv, 64) < 0, "parse should reject Q");
}

static void test_cubie(void) {
    printf("cubie model\n");
    /* sticker geometry agrees with the corner/edge facelet tables */
    for (int i = 0; i < NUM_CORNERS; i++) {
        int p0[3], p[3], n[3];
        facelet_geometry(CORNER_FACELET[i][0], p0, n);
        for (int k = 1; k < 3; k++) {
            facelet_geometry(CORNER_FACELET[i][k], p, n);
            CHECK(memcmp(p, p0, sizeof p) == 0, "corner %d facelets disagree", i);
        }
    }
    for (int i = 0; i < NUM_EDGES; i++) {
        int p0[3], p[3], n[3];
        facelet_geometry(EDGE_FACELET[i][0], p0, n);
        facelet_geometry(EDGE_FACELET[i][1], p, n);
        CHECK(memcmp(p, p0, sizeof p) == 0, "edge %d facelets disagree", i);
    }
    uint32_t rng = 12345;
    for (int t = 0; t < 500; t++) {
        Move scr[30];
        scramble_generate(scr, 30, &rng);
        Cube c;
        CubieCube cc, viacubie;
        cube_reset(&c);
        cubie_reset(&cc);
        cube_apply_seq(&c, scr, 30);
        for (int i = 0; i < 30; i++) cubie_apply(&cc, scr[i]);
        CHECK(cubie_from_cube(&c, &viacubie) == NULL, "conversion failed");
        CHECK(memcmp(&cc, &viacubie, sizeof cc) == 0, "cubie and facelet models disagree");
        Cube back;
        cubie_to_cube(&cc, &back);
        CHECK(cube_equal(&back, &c), "cubie -> facelet round trip");
        CHECK(cube_validate(&c) == NULL, "scrambled cube reported invalid");
        char str[NUM_FACELETS + 1];
        Cube parsed;
        cube_to_string(&c, str);
        CHECK(cube_from_string(&parsed, str) == 0 && cube_equal(&parsed, &c), "string round trip");
    }

    Cube c;
    cube_reset(&c);
    uint8_t tmp = c.f[CORNER_FACELET[URF][0]]; /* twist one corner */
    c.f[CORNER_FACELET[URF][0]] = c.f[CORNER_FACELET[URF][1]];
    c.f[CORNER_FACELET[URF][1]] = c.f[CORNER_FACELET[URF][2]];
    c.f[CORNER_FACELET[URF][2]] = tmp;
    CHECK(cube_validate(&c) != NULL, "twisted corner accepted");
    cube_reset(&c);
    tmp = c.f[EDGE_FACELET[UF][0]];
    c.f[EDGE_FACELET[UF][0]] = c.f[EDGE_FACELET[UF][1]];
    c.f[EDGE_FACELET[UF][1]] = tmp;
    CHECK(cube_validate(&c) != NULL, "flipped edge accepted");
    CubieCube cc;
    cubie_reset(&cc);
    cc.ep[UR] = UF;
    cc.ep[UF] = UR;
    cubie_to_cube(&cc, &c);
    CHECK(cube_validate(&c) != NULL, "single edge swap accepted");
    cube_reset(&c);
    c.f[0] = COLOR_RED;
    CHECK(cube_validate(&c) != NULL, "wrong color count accepted");
}

static void test_cfop_tables(void) {
    printf("CFOP tables\n");
    for (int slot = 0; slot < 4; slot++)
        CHECK(cfop_f2l_coverage(slot) == 150, "F2L slot %d covers %d/150 cases", slot, cfop_f2l_coverage(slot));

    /* every last-layer orientation case (216, including solved) */
    int oll_missing = 0;
    for (int co = 0; co < 81; co++) {
        int t[4] = {co % 3, co / 3 % 3, co / 9 % 3, co / 27};
        if ((t[0] + t[1] + t[2] + t[3]) % 3) continue;
        for (int eo = 0; eo < 16; eo++) {
            if (__builtin_popcount((unsigned)eo) % 2) continue;
            CubieCube cc;
            cubie_reset(&cc);
            for (int i = 0; i < 4; i++) cc.co[i] = (uint8_t)t[i], cc.eo[i] = (uint8_t)((eo >> i) & 1);
            Cube c;
            cubie_to_cube(&cc, &c);
            static Solution s;
            solution_clear(&s);
            if (!cfop_oll(&c, &s) || !ll_oriented(&c) || !f2l_solved(&c)) {
                oll_missing++;
                if (oll_missing <= 10) printf("  OLL case not covered: co=%d%d%d%d eo=%x\n", t[0], t[1], t[2], t[3], eo);
            }
        }
    }
    CHECK(oll_missing == 0, "%d OLL cases not covered", oll_missing);

    /* every last-layer permutation case with even parity (288, including solved) */
    int pll_missing = 0;
    uint8_t perms[24][4];
    int np = 0;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int c2 = 0; c2 < 4; c2++)
                for (int d = 0; d < 4; d++)
                    if (a != b && a != c2 && a != d && b != c2 && b != d && c2 != d) {
                        perms[np][0] = (uint8_t)a, perms[np][1] = (uint8_t)b;
                        perms[np][2] = (uint8_t)c2, perms[np][3] = (uint8_t)d;
                        np++;
                    }
    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 24; j++) {
            if (perm_parity(perms[i], 4) != perm_parity(perms[j], 4)) continue;
            CubieCube cc;
            cubie_reset(&cc);
            memcpy(cc.cp, perms[i], 4);
            memcpy(cc.ep, perms[j], 4);
            Cube c;
            cubie_to_cube(&cc, &c);
            static Solution s;
            solution_clear(&s);
            if (!cfop_pll(&c, &s) || !cube_is_solved(&c)) {
                pll_missing++;
                if (pll_missing <= 10) printf("  PLL case not covered: cp=%d%d%d%d ep=%d%d%d%d\n", perms[i][0],
                                              perms[i][1], perms[i][2], perms[i][3], perms[j][0], perms[j][1],
                                              perms[j][2], perms[j][3]);
            }
        }
    }
    CHECK(pll_missing == 0, "%d PLL cases not covered", pll_missing);

    /* each table algorithm must preserve F2L */
    for (int a = 0; a < NUM_OLL_ALGS; a++) {
        Cube c = apply_alg(OLL_ALGS[a].alg);
        CHECK(f2l_solved(&c), "%s breaks F2L", OLL_ALGS[a].name);
    }
    for (int a = 0; a < NUM_PLL_ALGS; a++) {
        Cube c = apply_alg(PLL_ALGS[a].alg);
        CHECK(f2l_solved(&c) && ll_oriented(&c), "%s breaks F2L or orientation", PLL_ALGS[a].name);
    }
}

static void test_lessons(void) {
    for (int id = LESSON_NONE + 1; id < LESSON_COUNT; id++) {
        SolveStep st = {.lesson = (LessonId)id};
        CHECK(step_lesson(&st).text, "lesson %d has no text", id);
    }
    SolveStep none = {.lesson = LESSON_NONE};
    CHECK(!step_lesson(&none).text, "LESSON_NONE has text");
}

static int count_tier(const uint8_t *tier, XrayTier want) {
    int n = 0;
    for (int i = 0; i < NUM_FACELETS; i++) n += i % 9 != 4 && tier[i] == want;
    return n;
}

/* X-ray: a Step's Focus names the pieces it works on, so when a human-method
 * Step ends its Focus stickers match their centers. */
static void test_focus(void) {
    Cube solved;
    cube_reset(&solved);
    uint8_t tier[NUM_FACELETS];
    focus_xray(lesson_focus(LESSON_CROSS), &solved, &solved, tier);
    CHECK(count_tier(tier, XRAY_FOCUS) == 8 && count_tier(tier, XRAY_DONE) == 40, "cross on a solved cube");
    focus_xray(lesson_focus(LESSON_KOCIEMBA_1), &solved, &solved, tier);
    CHECK(count_tier(tier, XRAY_FOCUS) == 48, "computer methods have no Focus");

    static const SolverId HUMAN[] = {SOLVER_BEGINNER, SOLVER_CFOP};
    for (int h = 0; h < 2; h++) {
        uint32_t rng = 99u + (uint32_t)h;
        for (int t = 0; t < 40; t++) {
            Cube c;
            Move scr[25];
            cube_reset(&c);
            scramble_generate(scr, 25, &rng);
            cube_apply_seq(&c, scr, 25);
            if (t % 3 == 1) cube_apply(&c, MOVE(MB_X, 1));
            static Solution s;
            if (solve_cube(HUMAN[h], &c, &s)) continue; /* test_solvers reports failures */
            Cube start = c;
            for (int k = 0; k < s.nsteps; k++) {
                const SolveStep *st = &s.steps[k];
                Cube end = start;
                cube_apply_seq(&end, s.moves + st->start, st->count);
                focus_xray(st->focus, &start, &start, tier);
                if (st->lesson == LESSON_F2L)
                    CHECK(count_tier(tier, XRAY_FOCUS) == 5, "'%s' focuses %d stickers, want its pair's 5", st->name,
                          count_tier(tier, XRAY_FOCUS));
                if (st->lesson == LESSON_OLL)
                    CHECK(count_tier(tier, XRAY_FOCUS) == 8, "'%s' focuses %d stickers, want 8 yellow", st->name,
                          count_tier(tier, XRAY_FOCUS));
                if (st->lesson != LESSON_POSITION_YELLOW_CORNERS) {
                    focus_xray(st->focus, &start, &end, tier);
                    for (int i = 0; i < NUM_FACELETS; i++)
                        CHECK(tier[i] != XRAY_FOCUS || end.f[i] == cube_center(&end, i / 9),
                              "%s '%s': Focus sticker %d not solved when the Step ends", solver_name(HUMAN[h]),
                              st->name, i);
                }
                start = end;
            }
        }
    }
}

static void check_at(const Solution *s, int pos, int step, int k, int total, int skipped, int nskipped) {
    StepAt a = solution_step_at(s, pos);
    CHECK(a.step == step && a.k == k && a.total == total && a.skipped == skipped && a.nskipped == nskipped,
          "pos %d: got step %d k %d/%d skipped %d+%d, want step %d k %d/%d skipped %d+%d", pos, a.step, a.k, a.total,
          a.skipped, a.nskipped, step, k, total, skipped, nskipped);
}

static void test_step_at(void) {
    static Solution s;

    /* Plain: Cross [R U], F2L [F], PLL [L]. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_CROSS, "Cross");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_F2L, "F2L");
    solution_push(&s, NULL, MOVE(MB_F, 1));
    solution_begin_step(&s, LESSON_PLL, "PLL");
    solution_push(&s, NULL, MOVE(MB_L, 1));
    check_at(&s, 0, 0, 1, 3, 0, 0);
    check_at(&s, 1, 0, 1, 3, 0, 0);
    check_at(&s, 2, 1, 2, 3, 0, 0); /* boundary: the Step about to play */
    check_at(&s, 3, 2, 3, 3, 0, 0);
    check_at(&s, 4, 2, 3, 3, 0, 0); /* pos == n: last real Step */

    /* CFOP shape: F2L [R U], OLL skip (empty), PLL [F L]. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_F2L, "F2L");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_OLL_SKIP, "OLL skip");
    solution_begin_step(&s, LESSON_PLL, "PLL");
    solution_push(&s, NULL, MOVE(MB_F, 1));
    solution_push(&s, NULL, MOVE(MB_L, 1));
    check_at(&s, 0, 0, 1, 2, 0, 0);
    check_at(&s, 1, 0, 1, 2, 0, 0);
    check_at(&s, 2, 2, 2, 2, 1, 1); /* the skipped Step folds into PLL */
    check_at(&s, 3, 2, 2, 2, 1, 1);
    check_at(&s, 4, 2, 2, 2, 0, 0); /* pos == n: nothing trailing */

    /* A U closing F2L and a U opening PLL merge into one U2 inside PLL; U then U' cancels outright. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_F2L, "F2L");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_PLL, "PLL");
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_push(&s, NULL, MOVE(MB_F, 1));
    CHECK(s.n == 3 && s.moves[1] == MOVE(MB_U, 2) && s.steps[0].count == 1 && s.steps[1].start == 1 && s.steps[1].count == 2,
          "U + U merges into U2 within the later Step");
    solution_clear(&s);
    solution_begin_step(&s, LESSON_F2L, "F2L");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_PLL, "PLL");
    solution_push(&s, NULL, MOVE(MB_U, 3));
    solution_push(&s, NULL, MOVE(MB_F, 1));
    CHECK(s.n == 2 && s.moves[1] == MOVE(MB_F, 1) && s.steps[0].count == 1 && s.steps[1].count == 1, "U + U' cancels");

    /* Named chunks: Sune from the front slot, then the same alg turned by y. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_OLL, "OLL");
    Move seq[16];
    int ns = moves_parse("U2 R U R' U R U2 R' L' U' L U", seq, 16);
    solution_push_seq(&s, NULL, seq, ns);
    Chunk ch[8];
    int nc = solution_chunks(&s, 0, ch, 8);
    CHECK(nc == 2 && ch[0].start == 1 && ch[0].count == 7 && !strcmp(ch[0].name, "Sune") && ch[1].start == 8 &&
              !strcmp(ch[1].name, "Left sexy move (lefty)"),
          "solution_chunks finds Sune and the left sexy move");

    /* An insert whose leading U merged into U' or cancelled still counts; R U R' only names a corner step. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_MIDDLE_EDGES, "Middle");
    ns = moves_parse("U' R U' R' U' F' U F R U' R' U' F' U F", seq, 16);
    solution_push_seq(&s, NULL, seq, ns);
    solution_begin_step(&s, LESSON_WHITE_CORNERS, "Corners");
    ns = moves_parse("R U R'", seq, 16);
    solution_push_seq(&s, NULL, seq, ns);
    nc = solution_chunks(&s, 0, ch, 8);
    CHECK(nc == 2 && ch[0].count == 8 && ch[1].start == 8 && ch[1].count == 7 &&
              !strcmp(ch[1].name, "Right edge insert"),
          "solution_chunks matches an insert whose U merged or cancelled");
    nc = solution_chunks(&s, 1, ch, 8);
    CHECK(nc == 1 && !strcmp(ch[0].name, "Take out corner"), "R U R' is take out corner in the corner step");

    /* Two inserts back to back: the second's U cancels and its first turn merges (L + L -> L2), shared. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_MIDDLE_EDGES, "Middle");
    ns = moves_parse("U F U' F' U' L' U L2 U' L' U' B' U B", seq, 16);
    solution_push_seq(&s, NULL, seq, ns);
    nc = solution_chunks(&s, 0, ch, 8);
    CHECK(nc == 2 && ch[0].start == 0 && ch[0].count == 8 && ch[1].start == 7 && ch[1].count == 7,
          "solution_chunks splits two inserts that share a merged move");

    /* Generic patterns must not leak into computer-solver phases. */
    for (int lesson = LESSON_THISTLETHWAITE_1; lesson <= LESSON_KOCIEMBA_2; lesson++) {
        solution_clear(&s);
        solution_begin_step(&s, (LessonId)lesson, "Computer phase");
        ns = moves_parse("R U R' U'", seq, 16);
        solution_push_seq(&s, NULL, seq, ns);
        CHECK(solution_chunks(&s, 0, ch, 8) == 0, "computer phases never receive algorithm labels");
    }

    /* A long step must not silently lose labels after the old GUI limit of 32. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_WHITE_CORNERS, "Corners");
    ns = moves_parse("R U R' U'", seq, 16);
    for (int i = 0; i < 40; i++) solution_push_seq(&s, NULL, seq, ns);
    Chunk many[SOL_MAX_MOVES];
    CHECK(solution_chunks(&s, 0, many, SOL_MAX_MOVES) == 40 && many[39].start == 156,
          "solution_chunks returns every chunk in a long step");

    /* Thistlethwaite-like: two empty phases, then two real phases. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_THISTLETHWAITE_1, "Phase 1");
    solution_begin_step(&s, LESSON_THISTLETHWAITE_2, "Phase 2");
    solution_begin_step(&s, LESSON_THISTLETHWAITE_3, "Phase 3");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_THISTLETHWAITE_4, "Phase 4");
    solution_push(&s, NULL, MOVE(MB_F, 1));
    check_at(&s, 0, 2, 1, 2, 0, 2); /* Phase 3 absorbs both empty phases */
    check_at(&s, 1, 2, 1, 2, 0, 2);
    check_at(&s, 2, 3, 2, 2, 0, 0);
    check_at(&s, 3, 3, 2, 2, 0, 0);

    /* Middle run of two empty phases between real ones. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_THISTLETHWAITE_1, "Phase 1");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_begin_step(&s, LESSON_THISTLETHWAITE_2, "Phase 2");
    solution_begin_step(&s, LESSON_THISTLETHWAITE_3, "Phase 3");
    solution_begin_step(&s, LESSON_THISTLETHWAITE_4, "Phase 4");
    solution_push(&s, NULL, MOVE(MB_U, 1));
    check_at(&s, 0, 0, 1, 2, 0, 0);
    check_at(&s, 1, 3, 2, 2, 1, 2);
    check_at(&s, 2, 3, 2, 2, 0, 0); /* pos == n */

    /* Trailing Skipped Step ("PLL skip") appears only at pos == n. */
    solution_clear(&s);
    solution_begin_step(&s, LESSON_CROSS, "Cross");
    solution_push(&s, NULL, MOVE(MB_R, 1));
    solution_push(&s, NULL, MOVE(MB_U, 1));
    solution_begin_step(&s, LESSON_F2L, "F2L");
    solution_push(&s, NULL, MOVE(MB_F, 1));
    solution_begin_step(&s, LESSON_PLL_SKIP, "PLL skip");
    check_at(&s, 0, 0, 1, 2, 0, 0);
    check_at(&s, 2, 1, 2, 2, 0, 0); /* n - 1 */
    check_at(&s, 3, 1, 2, 2, 2, 1); /* pos == n */
    /* Clamping: out-of-range positions behave like 0 and n. */
    check_at(&s, -5, 0, 1, 2, 0, 0);
    check_at(&s, 10, 1, 2, 2, 2, 1);

    /* No moves at all: no current Step, even with Skipped Steps present. */
    solution_clear(&s);
    check_at(&s, 0, -1, 0, 0, 0, 0);
    solution_begin_step(&s, LESSON_PLL_SKIP, "Solved");
    check_at(&s, 0, -1, 0, 0, 0, 0);
    check_at(&s, 5, -1, 0, 0, 0, 0);
}

static void test_solvers(int count) {
    for (int id = 0; id < SOLVER_COUNT; id++) {
        /* Kociemba takes ~100 ms a solve; `rubik --bench` is the soak test */
        int runs = id == SOLVER_KOCIEMBA ? (count + 9) / 10 : count;
        printf("solver %s (%d scrambles)\n", solver_name((SolverId)id), runs);
        uint32_t rng = 777u + (uint32_t)id;
        int failures = 0;
        for (int t = 0; t < runs; t++) {
            Cube c;
            Move scr[25];
            cube_reset(&c);
            scramble_generate(scr, 25, &rng);
            cube_apply_seq(&c, scr, 25);
            /* also exercise odd holding orientations */
            if (t % 3 == 1) cube_apply(&c, MOVE(MB_X, 1));
            if (t % 5 == 2) cube_apply(&c, MOVE(MB_Z, 3));
            static Solution s;
            const char *err = solve_cube((SolverId)id, &c, &s);
            if (err) {
                if (failures++ < 5) {
                    char buf[256];
                    moves_format(scr, 25, buf, sizeof buf);
                    printf("  FAIL: %s\n    scramble: %s\n", err, buf);
                }
                continue;
            }
            for (int st = 0; st < s.nsteps; st++)
                CHECK(step_lesson(&s.steps[st]).text, "%s step '%s' has no lesson", solver_name((SolverId)id),
                      s.steps[st].name);
            /* a Kociemba step named with its group "<A,B,C2,...>" only turns A and B by quarters */
            for (int st = 0; st < s.nsteps && id == SOLVER_KOCIEMBA; st++) {
                const char *g = strchr(s.steps[st].name, '<');
                if (!g) continue;
                for (int i = s.steps[st].start; i < s.steps[st].start + s.steps[st].count; i++) {
                    char face = "URFDLBMESxyz"[s.moves[i] / 3 % 12];
                    bool quarter = s.moves[i] % 3 != 1;
                    CHECK(!quarter || face == g[1] || face == g[3], "%s step '%s' turns %c by a quarter",
                          solver_name((SolverId)id), s.steps[st].name, face);
                }
            }
        }
        CHECK(failures == 0, "%s failed %d/%d", solver_name((SolverId)id), failures, runs);
    }
    /* solved input */
    for (int id = 0; id < SOLVER_COUNT; id++) {
        Cube c;
        cube_reset(&c);
        static Solution s;
        CHECK(solve_cube((SolverId)id, &c, &s) == NULL, "%s on solved cube", solver_name((SolverId)id));
    }
}

#define CONC_THREADS 4
#define CONC_SCRAMBLE 25

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t open;
    bool go;
} StartGate;

typedef struct {
    int index;
    StartGate *gate;
    Move scramble[CONC_SCRAMBLE];
    Cube cube;
    Solution *sol;
    const char *err;
    bool replay_ok;
} ConcurrentJob;

static void *concurrent_worker(void *arg) {
    ConcurrentJob *j = arg;
    pthread_mutex_lock(&j->gate->lock);
    while (!j->gate->go) pthread_cond_wait(&j->gate->open, &j->gate->lock);
    pthread_mutex_unlock(&j->gate->lock);
    if (j->index % 2) {
        cube_init();
        cubie_init();
    } else {
        cubie_init();
        cube_init();
    }
    cube_reset(&j->cube);
    cube_apply_seq(&j->cube, j->scramble, CONC_SCRAMBLE);
    j->err = solve_cube(SOLVER_KOCIEMBA, &j->cube, j->sol);
    if (!j->err) {
        Cube c = j->cube;
        cube_apply_seq(&c, j->sol->moves, j->sol->n);
        j->replay_ok = cube_is_solved(&c);
    }
    return NULL;
}

static void test_concurrent_kociemba(void) {
    printf("concurrent kociemba (%d threads)\n", CONC_THREADS);
    StartGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false};
    pthread_t th[CONC_THREADS];
    ConcurrentJob jobs[CONC_THREADS];
    bool started[CONC_THREADS] = {false};
    int ready = 0;
    for (int i = 0; i < CONC_THREADS; i++) {
        uint32_t rng = 9001u + 7919u * (uint32_t)i;
        jobs[i] = (ConcurrentJob){.index = i, .gate = &gate, .replay_ok = false};
        scramble_generate(jobs[i].scramble, CONC_SCRAMBLE, &rng);
        jobs[i].sol = calloc(1, sizeof *jobs[i].sol);
        if (jobs[i].sol) ready++;
    }
    int created = 0;
    if (ready == CONC_THREADS) {
        for (int i = 0; i < CONC_THREADS; i++) {
            if (pthread_create(&th[i], NULL, concurrent_worker, &jobs[i]) != 0) break;
            started[i] = true;
            created++;
        }
    }
    pthread_mutex_lock(&gate.lock);
    gate.go = true;
    pthread_cond_broadcast(&gate.open);
    pthread_mutex_unlock(&gate.lock);
    for (int i = 0; i < CONC_THREADS; i++)
        if (started[i]) pthread_join(th[i], NULL);
    CHECK(created == CONC_THREADS, "started %d/%d threads", created, CONC_THREADS);
    for (int i = 0; i < CONC_THREADS && created == CONC_THREADS; i++) {
        CHECK(jobs[i].err == NULL, "thread %d solve failed: %s", i, jobs[i].err ? jobs[i].err : "");
        CHECK(jobs[i].replay_ok, "thread %d moves do not solve its own cube", i);
        for (int k = 0; k < i; k++)
            CHECK(jobs[i].sol->n != jobs[k].sol->n || memcmp(jobs[i].sol->moves, jobs[k].sol->moves,
                                                            (size_t)jobs[i].sol->n * sizeof(Move)) != 0,
                  "threads %d and %d returned identical solutions", i, k);
    }
    for (int i = 0; i < CONC_THREADS; i++) free(jobs[i].sol);
}

int main(int argc, char **argv) {
    int count = argc > 1 ? atoi(argv[1]) : 200;
    test_concurrent_kociemba();
    cubie_init();
    test_moves();
    test_orientation();
    test_cubie();
    test_cfop_tables();
    test_lessons();
    test_step_at();
    test_focus();
    test_playback();
    test_solvers(count);
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
