#include <stdio.h>
#include <stdlib.h>

#include "solvers/solvers.h"

/* slot k: the slot that sits front-right after remapping by y^k */
const int SLOT_FRONT[4] = {FACE_F, FACE_R, FACE_B, FACE_L};
const int SLOT_RIGHT[4] = {FACE_R, FACE_B, FACE_L, FACE_F};
const int SLOT_UCORNER[4] = {URF, UBR, ULB, UFL};
const int SLOT_DCORNER[4] = {DFR, DRB, DBL, DLF};
const int SLOT_EDGE[4] = {FR, BR, BL, FL};

PieceLoc find_corner(const Cube *c, int a, int b, int d) {
    for (int p = 0; p < NUM_CORNERS; p++) {
        int s[3];
        for (int k = 0; k < 3; k++) s[k] = c->f[CORNER_FACELET[p][k]];
        for (int k = 0; k < 3; k++) {
            if (s[k] != a) continue;
            int x = s[(k + 1) % 3], y = s[(k + 2) % 3];
            if ((x == b && y == d) || (x == d && y == b)) return (PieceLoc){p, k};
        }
    }
    return (PieceLoc){-1, 0};
}

PieceLoc find_edge(const Cube *c, int a, int b) {
    for (int p = 0; p < NUM_EDGES; p++) {
        int x = c->f[EDGE_FACELET[p][0]], y = c->f[EDGE_FACELET[p][1]];
        if (x == a && y == b) return (PieceLoc){p, 0};
        if (x == b && y == a) return (PieceLoc){p, 1};
    }
    return (PieceLoc){-1, 0};
}

static bool sticker_ok(const Cube *c, int idx) { return c->f[idx] == c->f[(idx / 9) * 9 + 4]; }

bool corner_solved_at(const Cube *c, int pos) {
    for (int k = 0; k < 3; k++)
        if (!sticker_ok(c, CORNER_FACELET[pos][k])) return false;
    return true;
}

bool edge_solved_at(const Cube *c, int pos) {
    return sticker_ok(c, EDGE_FACELET[pos][0]) && sticker_ok(c, EDGE_FACELET[pos][1]);
}

bool cross_solved(const Cube *c) {
    for (int e = DR; e <= DB; e++)
        if (!edge_solved_at(c, e)) return false;
    return true;
}

bool slot_solved(const Cube *c, int slot) {
    return corner_solved_at(c, SLOT_DCORNER[slot]) && edge_solved_at(c, SLOT_EDGE[slot]);
}

bool f2l_solved(const Cube *c) {
    if (!cross_solved(c)) return false;
    for (int s = 0; s < 4; s++)
        if (!slot_solved(c, s)) return false;
    return true;
}

bool ll_oriented(const Cube *c) {
    for (int i = 0; i < 9; i++)
        if (c->f[FACE_U * 9 + i] != c->f[FACE_U * 9 + 4]) return false;
    return true;
}

int slot_of_face(int front_face) {
    for (int k = 0; k < 4; k++)
        if (SLOT_FRONT[k] == front_face) return k;
    return 0;
}

int alg_moves(const char *alg, int k, Move *out, int max) {
    int n = moves_parse(alg, out, max);
    if (n < 0) {
        fprintf(stderr, "bad algorithm in solver table: %s\n", alg);
        abort();
    }
    for (int i = 0; i < n; i++) out[i] = move_remap_y(out[i], k);
    return n;
}

void push_alg(Solution *s, Cube *c, const char *alg, int k) {
    Move buf[64];
    int n = alg_moves(alg, k, buf, 64);
    solution_push_seq(s, c, buf, n);
}

void orient_white_down(Cube *c, Solution *s) {
    int face = 0;
    for (int f = 0; f < NUM_FACES; f++)
        if (cube_center(c, f) == COLOR_WHITE) face = f;
    static const char *const ROT[NUM_FACES] = {
        [FACE_U] = "x2", [FACE_R] = "z", [FACE_F] = "x'", [FACE_D] = "", [FACE_L] = "z'", [FACE_B] = "x",
    };
    push_alg(s, c, ROT[face], 0);
}

void macro_make(Macro *mc, const char *alg, int k, bool auf) {
    mc->n = alg_moves(alg, k, mc->m, 32);
    mc->auf = auf;
}

static bool macro_dfs(const Cube *c, const Macro *macros, int nm, GoalFn goal, int depth, int *path, int n, int last) {
    if (depth == 0) return goal(c);
    for (int i = 0; i < nm; i++) {
        if (last >= 0 && macros[last].auf && macros[i].auf) continue;
        Cube t = *c;
        cube_apply_seq(&t, macros[i].m, macros[i].n);
        path[n] = i;
        if (macro_dfs(&t, macros, nm, goal, depth - 1, path, n + 1, i)) return true;
    }
    return false;
}

bool macro_search(Cube *c, Solution *s, const Macro *macros, int nmacros, GoalFn goal, int max_depth) {
    int path[16];
    for (int d = 0; d <= max_depth && d <= 16; d++) {
        if (macro_dfs(c, macros, nmacros, goal, d, path, 0, -1)) {
            for (int i = 0; i < d; i++) solution_push_seq(s, c, macros[path[i]].m, macros[path[i]].n);
            return true;
        }
    }
    return false;
}
