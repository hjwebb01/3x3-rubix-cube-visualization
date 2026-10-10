/* Internal header shared by the solvers (and the tests). */
#ifndef SOLVERS_SOLVERS_H
#define SOLVERS_SOLVERS_H

#include "cube/cube.h"
#include "cube/cubie.h"
#include "cube/solver.h"

const char *solve_beginner(const Cube *c, Solution *out);
const char *solve_cfop(const Cube *c, Solution *out);
const char *solve_thistlethwaite(const Cube *c, Solution *out);
const char *solve_kociemba(const Cube *c, Solution *out);

/* Two-phase search on a cubie cube. Stops at the first solution of at most
 * `target` moves, or after `timeout_ms` with the best found so far. Returns the
 * length, or -1 if nothing was found. */
int kociemba_search(const CubieCube *cc, int target, double timeout_ms, Move *out);

/* ---- helpers shared by the human-style methods (common.c) ---- */

/* F2L slots, indexed so that move_remap_y(alg, slot) performs an algorithm
 * written for the front-right slot in that slot. */
extern const int SLOT_FRONT[4], SLOT_RIGHT[4];
extern const int SLOT_UCORNER[4], SLOT_DCORNER[4], SLOT_EDGE[4];

typedef struct {
    int pos; /* position index (corner 0..7 / edge 0..11), -1 if not found */
    int ori; /* which sticker of the position (0..2 / 0..1) holds the first color */
} PieceLoc;

PieceLoc find_corner(const Cube *c, int a, int b, int d);
PieceLoc find_edge(const Cube *c, int a, int b);
bool corner_solved_at(const Cube *c, int pos); /* stickers match their centers */
bool edge_solved_at(const Cube *c, int pos);
bool cross_solved(const Cube *c);  /* D-layer edges */
bool slot_solved(const Cube *c, int slot);
bool f2l_solved(const Cube *c);    /* first two layers (D face + bottom two rows) */
bool ll_oriented(const Cube *c);   /* U face single colored */

int slot_of_face(int front_face); /* y-remap amount that makes front_face the front */
/* Parses alg (aborts on a typo; algs are compile-time constants), remaps it by
 * y^k and pushes it. */
void push_alg(Solution *s, Cube *c, const char *alg, int k);
int alg_moves(const char *alg, int k, Move *out, int max);
/* Puts the white center on D with a whole-cube rotation. */
void orient_white_down(Cube *c, Solution *s);

/* Iterative-deepening search over whole algorithms ("macros"). Finds the
 * shortest macro sequence (by macro count) reaching goal, applies it and pushes
 * it. Consecutive AUF macros are skipped. */
typedef struct {
    Move m[32];
    int n;
    bool auf;
} Macro;
typedef bool (*GoalFn)(const Cube *c);
void macro_make(Macro *mc, const char *alg, int k, bool auf);
bool macro_search(Cube *c, Solution *s, const Macro *macros, int nmacros, GoalFn goal, int max_depth);

/* ---- CFOP pieces exposed for tests ---- */
typedef struct {
    const char *name;
    const char *alg;
} NamedAlg;
extern const NamedAlg OLL_ALGS[];
extern const int NUM_OLL_ALGS;
extern const NamedAlg PLL_ALGS[];
extern const int NUM_PLL_ALGS;

/* Each returns false if the case is not recognized. */
bool cfop_oll(Cube *c, Solution *s);
bool cfop_pll(Cube *c, Solution *s);
/* Number of F2L pair states (out of 150 per slot) the F2L tables can solve. */
int cfop_f2l_coverage(int slot);

#endif
