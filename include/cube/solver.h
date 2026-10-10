/*
 * Solver front end. Every solver produces a Solution: a flat move list that is
 * split into named steps ("Cross", "OLL 27 (Sune)", "Phase 2", ...).
 */
#ifndef CUBE_SOLVER_H
#define CUBE_SOLVER_H

#include "cube/cube.h"

#define SOL_MAX_MOVES 1024
#define SOL_MAX_STEPS 32

/* What a step teaches; see lessons.c. Solvers pick one when they begin a step. */
typedef enum {
    LESSON_NONE, /* only the fallback "Solve" step */
    /* beginner */
    LESSON_HOLD_WHITE,
    LESSON_WHITE_CROSS,
    LESSON_WHITE_CORNERS,
    LESSON_MIDDLE_EDGES,
    LESSON_YELLOW_CROSS,
    LESSON_YELLOW_EDGES,
    LESSON_POSITION_YELLOW_CORNERS,
    LESSON_ORIENT_YELLOW_CORNERS,
    /* CFOP */
    LESSON_INSPECTION,
    LESSON_CROSS,
    LESSON_F2L,
    LESSON_OLL_SKIP,
    LESSON_OLL,
    LESSON_PLL_SKIP,
    LESSON_PLL,
    /* computer methods */
    LESSON_THISTLETHWAITE_1,
    LESSON_THISTLETHWAITE_2,
    LESSON_THISTLETHWAITE_3,
    LESSON_THISTLETHWAITE_4,
    LESSON_KOCIEMBA_1,
    LESSON_KOCIEMBA_2,
    LESSON_COUNT
} LessonId;

typedef struct {
    char name[48];
    LessonId lesson;
    const char *note; /* case note (e.g. for OLL 27, Sune), or NULL */
    int start; /* index into Solution.moves */
    int count;
} SolveStep;

typedef struct {
    Move moves[SOL_MAX_MOVES];
    int n;
    SolveStep steps[SOL_MAX_STEPS];
    int nsteps;
    bool overflow;
    double ms; /* wall-clock solve time */
} Solution;

typedef enum {
    SOLVER_BEGINNER,
    SOLVER_CFOP,
    SOLVER_THISTLETHWAITE,
    SOLVER_KOCIEMBA,
    SOLVER_COUNT
} SolverId;

const char *solver_name(SolverId id);        /* "CFOP" */
const char *solver_key(SolverId id);         /* "cfop" */
const char *solver_description(SolverId id); /* one line */
int solver_from_key(const char *key);        /* -1 if unknown */

/* Solves c (which is not modified). Returns NULL on success, else an error
 * message. The result is always verified by replaying it. */
const char *solve_cube(SolverId id, const Cube *c, Solution *out);

void solution_clear(Solution *s);
void solution_begin_step(Solution *s, LessonId lesson, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* Same, with a case note attached to the step. */
void solution_begin_step_note(Solution *s, LessonId lesson, const char *note, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
/* Applies m to c (if c is non-NULL) and appends it, merging with the previous
 * move of the current step when both turn the same layer (R R -> R2, R R' -> ). */
void solution_push(Solution *s, Cube *c, Move m);
void solution_push_seq(Solution *s, Cube *c, const Move *seq, int n);
/* Slice turn metric: rotations are free, everything else counts as 1. */
int solution_move_count(const Solution *s);
int solution_step_move_count(const Solution *s, int step);

/* Teaching notes for a step: the lesson for its kind (NULL for LESSON_NONE)
 * and its case note (usually NULL). */
typedef struct {
    const char *text;
    const char *note;
} StepLesson;
StepLesson step_lesson(const SolveStep *st);

#endif
