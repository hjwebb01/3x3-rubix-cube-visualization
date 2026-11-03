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

/* The pieces a Step works on, for X-ray. A piece is named by its colors as a
 * bit set (1 << color for each sticker), so a Focus follows its pieces
 * wherever they move and whichever way the cube is held. */
typedef struct {
    uint64_t pieces; /* bit (1 << colors) per piece; 0 = no Focus (computer methods) */
    int color;       /* only that color's stickers of those pieces, or -1 for whole pieces */
} Focus;

typedef struct {
    char name[48];
    LessonId lesson;
    const char *note; /* case note (e.g. for OLL 27, Sune), or NULL */
    Focus focus;      /* lesson_focus(lesson) unless the solver narrowed it */
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

/* Builds Kociemba's tables (~2-3 s, ~75 MB) if not built yet; thread-safe.
 * The first Kociemba solve does this itself, so call it early to hide it. */
void kociemba_warm(void);

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
int solution_move_count_to(const Solution *s, int pos); /* moves[0..pos) */
int solution_step_move_count(const Solution *s, int step);

/* Where position pos (0..n, clamped) falls among the Steps. Skipped Steps (no
 * moves) are never the current Step; they are folded into a neighbour, see
 * GLOSSARY.md. */
typedef struct {
    int step;          /* index into steps[] of the Step with moves that pos is in; -1 if the Solution has no moves */
    int k, total;      /* "Step k of total", counting only Steps with moves; 0, 0 when step == -1 */
    int skipped;       /* Skipped Steps folded into this one: steps[skipped .. skipped + nskipped) */
    int nskipped;
} StepAt;
StepAt solution_step_at(const Solution *s, int pos);

/* Teaching notes for a step: the lesson for its kind (NULL for LESSON_NONE)
 * and its case note (usually NULL). */
typedef struct {
    const char *text;
    const char *note;
} StepLesson;
StepLesson step_lesson(const SolveStep *st);

/* What every Step of a kind works on, assuming white is solved first (see
 * focus.c). A solver can narrow it with solution_set_focus. */
Focus lesson_focus(LessonId lesson);
/* The Focus of an F2L pair: the edge of colors a and b and the corner of a, b and d. */
Focus focus_slot_pair(int a, int b, int d);
/* Replaces the Focus of the Step being built. */
void solution_set_focus(Solution *s, Focus f);

/* X-ray: how much each sticker of `now` matters to a Step with Focus f that
 * began at step_start. Centers are always XRAY_FOCUS, as the reference colors;
 * so is everything when f has no pieces. */
typedef enum {
    XRAY_FADED, /* not part of the Step and not solved when it began */
    XRAY_DONE,  /* solved when the Step began: the Step must put it back if it moves it */
    XRAY_FOCUS, /* what the Step works on */
} XrayTier;
void focus_xray(Focus f, const Cube *step_start, const Cube *now, uint8_t out[NUM_FACELETS]);

/* A run of moves in one Step that people memorize as a unit (Sune, the sexy move, ...). start is an index
 * into Solution.moves. */
typedef struct {
    int start, count;
    const char *name;
} Chunk;
/* Fills out in order and returns how many (at most max). Adjacent chunks may share a merged move. */
int solution_chunks(const Solution *s, int step, Chunk *out, int max);

#endif
