#include "cube/solver.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cube/cubie.h"
#include "solvers/solvers.h"

static const struct {
    const char *name, *key, *desc;
} SOLVER_INFO[SOLVER_COUNT] = {
    [SOLVER_BEGINNER] = {"Beginner", "beginner", "Layer by layer, the way most people learn (~100-150 moves)"},
    [SOLVER_CFOP] = {"CFOP", "cfop", "Cross, F2L, OLL (57 algs), PLL (21 algs) (~55-65 moves)"},
    [SOLVER_THISTLETHWAITE] = {"Thistlethwaite", "thistlethwaite", "4-phase subgroup descent, bidirectional BFS (~30-45 moves)"},
    [SOLVER_KOCIEMBA] = {"Kociemba", "kociemba", "Two-phase IDA* with pruning tables (~19-22 moves)"},
};

const char *solver_name(SolverId id) { return id < SOLVER_COUNT ? SOLVER_INFO[id].name : "?"; }
const char *solver_key(SolverId id) { return id < SOLVER_COUNT ? SOLVER_INFO[id].key : "?"; }
const char *solver_description(SolverId id) { return id < SOLVER_COUNT ? SOLVER_INFO[id].desc : "?"; }

int solver_from_key(const char *key) {
    for (int i = 0; i < SOLVER_COUNT; i++)
        if (strcmp(key, SOLVER_INFO[i].key) == 0) return i;
    return -1;
}

void solution_clear(Solution *s) {
    s->n = 0;
    s->nsteps = 0;
    s->overflow = false;
    s->ms = 0;
}

static void begin_step(Solution *s, LessonId lesson, const char *note, const char *fmt, va_list ap) {
    if (s->nsteps >= SOL_MAX_STEPS) {
        s->overflow = true;
        return;
    }
    SolveStep *st = &s->steps[s->nsteps++];
    vsnprintf(st->name, sizeof st->name, fmt, ap);
    st->lesson = lesson;
    st->note = note;
    st->start = s->n;
    st->count = 0;
}

void solution_begin_step(Solution *s, LessonId lesson, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    begin_step(s, lesson, NULL, fmt, ap);
    va_end(ap);
}

void solution_begin_step_note(Solution *s, LessonId lesson, const char *note, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    begin_step(s, lesson, note, fmt, ap);
    va_end(ap);
}

void solution_push(Solution *s, Cube *c, Move m) {
    if (c) cube_apply(c, m);
    if (s->nsteps == 0) solution_begin_step(s, LESSON_NONE, "Solve");
    SolveStep *st = &s->steps[s->nsteps - 1];
    if (st->count > 0) {
        Move last = s->moves[s->n - 1];
        if (MOVE_BASE(last) == MOVE_BASE(m)) {
            int t = (MOVE_TURNS(last) + MOVE_TURNS(m)) % 4;
            if (t == 0) {
                s->n--;
                st->count--;
            } else {
                s->moves[s->n - 1] = MOVE(MOVE_BASE(m), t);
            }
            return;
        }
    }
    if (s->n >= SOL_MAX_MOVES) {
        s->overflow = true;
        return;
    }
    s->moves[s->n++] = m;
    st->count++;
}

void solution_push_seq(Solution *s, Cube *c, const Move *seq, int n) {
    for (int i = 0; i < n; i++) solution_push(s, c, seq[i]);
}

int solution_step_move_count(const Solution *s, int step) {
    const SolveStep *st = &s->steps[step];
    int n = 0;
    for (int i = st->start; i < st->start + st->count; i++)
        if (!move_is_rotation(s->moves[i])) n++;
    return n;
}

int solution_move_count(const Solution *s) {
    int n = 0;
    for (int i = 0; i < s->n; i++)
        if (!move_is_rotation(s->moves[i])) n++;
    return n;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

const char *solve_cube(SolverId id, const Cube *c, Solution *out) {
    cubie_init();
    solution_clear(out);
    const char *err = cube_validate(c);
    if (err) return err;

    double t0 = now_ms();
    switch (id) {
    case SOLVER_BEGINNER: err = solve_beginner(c, out); break;
    case SOLVER_CFOP: err = solve_cfop(c, out); break;
    case SOLVER_THISTLETHWAITE: err = solve_thistlethwaite(c, out); break;
    case SOLVER_KOCIEMBA: err = solve_kociemba(c, out); break;
    default: err = "unknown solver"; break;
    }
    out->ms = now_ms() - t0;
    if (err) return err;
    if (out->overflow) return "solution too long (internal limit)";

    Cube check = *c;
    cube_apply_seq(&check, out->moves, out->n);
    if (!cube_is_solved(&check)) return "internal error: solution did not solve the cube";
    return NULL;
}
