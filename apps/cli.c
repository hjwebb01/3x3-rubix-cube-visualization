/*
 * rubik - command line front end.
 *
 *   rubik                                 random scramble, solve with every method
 *   rubik -m cfop -s "R U R' U' F2 D"     solve a given scramble with one method
 *   rubik -f UUUUUUUUURRR...              solve a facelet string (URFDLB order)
 *   rubik --bench 200                     average length/time over random scrambles
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cube/cube.h"
#include "cube/cubie.h"
#include "cube/solver.h"

static const char *const ANSI_BG[NUM_COLORS] = {
    "\x1b[48;5;255m", "\x1b[48;5;160m", "\x1b[48;5;34m", "\x1b[48;5;226m", "\x1b[48;5;208m", "\x1b[48;5;27m",
};

static void print_sticker(int color, bool use_color) {
    if (use_color)
        printf("%s  \x1b[0m", ANSI_BG[color]);
    else
        printf("%c ", "WRGYOB"[color]);
}

static void print_net(const Cube *c, bool use_color) {
    /* rows of faces: U on top, L F R B in the middle, D at the bottom */
    for (int r = 0; r < 3; r++) {
        printf("        ");
        for (int k = 0; k < 3; k++) print_sticker(c->f[FACE_U * 9 + r * 3 + k], use_color);
        printf("\n");
    }
    static const int MID[4] = {FACE_L, FACE_F, FACE_R, FACE_B};
    for (int r = 0; r < 3; r++) {
        printf("  ");
        for (int f = 0; f < 4; f++)
            for (int k = 0; k < 3; k++) print_sticker(c->f[MID[f] * 9 + r * 3 + k], use_color);
        printf("\n");
    }
    for (int r = 0; r < 3; r++) {
        printf("        ");
        for (int k = 0; k < 3; k++) print_sticker(c->f[FACE_D * 9 + r * 3 + k], use_color);
        printf("\n");
    }
}

static bool explain;

static void print_solution(SolverId id, const Solution *s) {
    printf("\n== %s: %d moves, %.1f ms ==\n", solver_name(id), solution_move_count(s), s->ms);
    for (int i = 0; i < s->nsteps; i++) {
        const SolveStep *st = &s->steps[i];
        char buf[2048];
        moves_format(s->moves + st->start, st->count, buf, sizeof buf);
        printf("  %-46s (%2d) %s\n", st->name, solution_step_move_count(s, i), st->count ? buf : "-");
        if (!explain) continue;
        StepLesson l = step_lesson(st);
        if (l.text) printf("      %s\n", l.text);
        if (l.note) printf("      %s\n", l.note);
        if (l.text || l.note) printf("\n");
    }
}

static void usage(const char *argv0) {
    printf("usage: %s [options]\n"
           "  -m, --method NAME     beginner | cfop | thistlethwaite | kociemba | all (default: all)\n"
           "  -s, --scramble ALG    scramble to apply to a solved cube (default: random)\n"
           "  -f, --facelets STR    54-char cube state in URFDLB order (face or color letters)\n"
           "  -n, --length N        random scramble length (default 25)\n"
           "      --seed N          random seed\n"
           "      --bench N         solve N random scrambles per method and print statistics\n"
           "  -e, --explain         explain each step and the algorithms it uses\n"
           "      --no-color        plain text cube net\n",
           argv0);
}

static int bench(int count, int method, uint32_t seed) {
    printf("%-15s %8s %8s %8s %10s\n", "method", "avg", "min", "max", "avg ms");
    for (int id = 0; id < SOLVER_COUNT; id++) {
        if (method >= 0 && id != method) continue;
        uint32_t rng = seed;
        double total_ms = 0;
        long total = 0;
        int lo = 1 << 30, hi = 0;
        for (int i = 0; i < count; i++) {
            Cube c;
            Move scr[25];
            cube_reset(&c);
            scramble_generate(scr, 25, &rng);
            cube_apply_seq(&c, scr, 25);
            static Solution s;
            const char *err = solve_cube((SolverId)id, &c, &s);
            if (err) {
                printf("%s failed: %s\n", solver_name((SolverId)id), err);
                return 1;
            }
            int n = solution_move_count(&s);
            total += n;
            total_ms += s.ms;
            if (n < lo) lo = n;
            if (n > hi) hi = n;
        }
        printf("%-15s %8.1f %8d %8d %10.2f\n", solver_name((SolverId)id), (double)total / count, lo, hi,
               total_ms / count);
    }
    return 0;
}

int main(int argc, char **argv) {
    cubie_init();
    int method = -1, length = 25, bench_n = 0;
    const char *scramble = NULL, *facelets = NULL;
    uint32_t seed = (uint32_t)time(NULL);
    bool color = true;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if ((!strcmp(a, "-m") || !strcmp(a, "--method")) && v) {
            i++;
            if (strcmp(v, "all") == 0) continue;
            method = solver_from_key(v);
            if (method < 0) {
                fprintf(stderr, "unknown method '%s'\n", v);
                return 2;
            }
        } else if ((!strcmp(a, "-s") || !strcmp(a, "--scramble")) && v) {
            scramble = v, i++;
        } else if ((!strcmp(a, "-f") || !strcmp(a, "--facelets")) && v) {
            facelets = v, i++;
        } else if ((!strcmp(a, "-n") || !strcmp(a, "--length")) && v) {
            length = atoi(v), i++;
        } else if (!strcmp(a, "--seed") && v) {
            seed = (uint32_t)strtoul(v, NULL, 10), i++;
        } else if (!strcmp(a, "--bench") && v) {
            bench_n = atoi(v), i++;
        } else if (!strcmp(a, "-e") || !strcmp(a, "--explain")) {
            explain = true;
        } else if (!strcmp(a, "--no-color")) {
            color = false;
        } else {
            usage(argv[0]);
            return strcmp(a, "-h") && strcmp(a, "--help") ? 2 : 0;
        }
    }

    if (bench_n > 0) return bench(bench_n, method, seed);

    Cube c;
    cube_reset(&c);
    if (facelets) {
        if (cube_from_string(&c, facelets) != 0) {
            fprintf(stderr, "facelet string must be 54 characters of URFDLB or WRGYOB\n");
            return 2;
        }
    } else {
        Move moves[256];
        int n;
        if (scramble) {
            n = moves_parse(scramble, moves, 256);
            if (n < 0) {
                fprintf(stderr, "could not parse scramble '%s'\n", scramble);
                return 2;
            }
        } else {
            if (length < 0 || length > 256) length = 25;
            scramble_generate(moves, length, &seed);
            n = length;
        }
        char buf[2048];
        moves_format(moves, n, buf, sizeof buf);
        printf("Scramble: %s\n", buf);
        cube_apply_seq(&c, moves, n);
    }
    print_net(&c, color);

    const char *err = cube_validate(&c);
    if (err) {
        fprintf(stderr, "invalid cube: %s\n", err);
        return 1;
    }
    int rc = 0;
    for (int id = 0; id < SOLVER_COUNT; id++) {
        if (method >= 0 && id != method) continue;
        static Solution s;
        err = solve_cube((SolverId)id, &c, &s);
        if (err) {
            printf("\n== %s failed: %s ==\n", solver_name((SolverId)id), err);
            rc = 1;
            continue;
        }
        print_solution((SolverId)id, &s);
    }
    return rc;
}
