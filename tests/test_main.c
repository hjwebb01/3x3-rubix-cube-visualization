/* Self-contained test runner: move algebra, conversions, algorithm table
 * coverage and randomized end-to-end solves for every solver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solvers/solvers.h"

static int g_failures, g_checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

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
    Cube c = apply_alg("R U R' U' R U R' U' R U R' U' R U R' U' R U R' U' R U R' U'");
    CHECK(cube_is_solved(&c), "sexy move x6 is not identity");

    /* known effects: R moves the F face's right column to U */
    c = apply_alg("R");
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

static void test_solvers(int count) {
    for (int id = 0; id < SOLVER_COUNT; id++) {
        printf("solver %s (%d scrambles)\n", solver_name((SolverId)id), count);
        uint32_t rng = 777u + (uint32_t)id;
        long total = 0;
        double ms = 0;
        int worst = 0, failures = 0;
        for (int t = 0; t < count; t++) {
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
            int n = solution_move_count(&s);
            total += n;
            ms += s.ms;
            if (n > worst) worst = n;
        }
        CHECK(failures == 0, "%s failed %d/%d", solver_name((SolverId)id), failures, count);
        if (count > failures)
            printf("  avg %.1f moves, worst %d, avg %.2f ms\n", (double)total / (count - failures), worst,
                   ms / (count - failures));
    }
    /* solved input */
    for (int id = 0; id < SOLVER_COUNT; id++) {
        Cube c;
        cube_reset(&c);
        static Solution s;
        CHECK(solve_cube((SolverId)id, &c, &s) == NULL, "%s on solved cube", solver_name((SolverId)id));
    }
}

int main(int argc, char **argv) {
    int count = argc > 1 ? atoi(argv[1]) : 200;
    cubie_init();
    test_moves();
    test_cubie();
    test_cfop_tables();
    test_lessons();
    test_solvers(count);
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
