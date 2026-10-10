/*
 * CFOP (Fridrich): Cross, F2L, OLL, PLL.
 *
 * Cross: optimal (<= 8 moves) via IDA* over a full pruning table of the four
 *        cross edges (24^4 states).
 * F2L:   for each slot, a breadth-first search over the classic intuitive F2L
 *        triggers (R U R', F' U F, ... and U turns) is run once at start-up from
 *        the solved state. This gives a solution for every one of the 150 pair
 *        cases (corner + edge in the top layer or their slot). Pieces stuck in
 *        other slots are popped out first. The pair with the shortest solution
 *        is solved next.
 * OLL/PLL: 57 + 21 algorithms with pre-AUF (and post-AUF for PLL), recognised by
 *        trying them.
 */
#include <string.h>

#include "solvers/solvers.h"

/* ---------------- Cross ---------------- */

#define CROSS_STATES (24 * 24 * 24 * 24)
static uint8_t edge_trans[24][NUM_FACE_MOVES];
static uint8_t cross_prune[CROSS_STATES];
static Move cross_path[16];

static int cross_index(const int s[4]) { return ((s[0] * 24 + s[1]) * 24 + s[2]) * 24 + s[3]; }

static void cross_init(void) {
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        const CubieCube *M = cubie_move((Move)m);
        for (int i = 0; i < NUM_EDGES; i++)
            for (int o = 0; o < 2; o++)
                edge_trans[M->ep[i] * 2 + o][m] = (uint8_t)(i * 2 + ((o + M->eo[i]) & 1));
    }
    static int queue[CROSS_STATES];
    memset(cross_prune, 0xFF, sizeof cross_prune);
    int start[4] = {DR * 2, DF * 2, DL * 2, DB * 2};
    int head = 0, tail = 0;
    queue[tail++] = cross_index(start);
    cross_prune[queue[0]] = 0;
    while (head < tail) {
        int idx = queue[head++], s[4];
        for (int k = 3, x = idx; k >= 0; k--, x /= 24) s[k] = x % 24;
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            int t[4];
            for (int k = 0; k < 4; k++) t[k] = edge_trans[s[k]][m];
            int ni = cross_index(t);
            if (cross_prune[ni] == 0xFF) {
                cross_prune[ni] = (uint8_t)(cross_prune[idx] + 1);
                queue[tail++] = ni;
            }
        }
    }
}

static int cross_dfs(const int s[4], int depth, int n, int last) {
    int h = cross_prune[cross_index(s)];
    if (h == 0) return n;
    if (h > depth) return -1;
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        int f = m / 3;
        if (last >= 0 && (f == last || f == last - 3)) continue;
        int t[4];
        for (int k = 0; k < 4; k++) t[k] = edge_trans[s[k]][m];
        cross_path[n] = (Move)m;
        int r = cross_dfs(t, depth - 1, n + 1, f);
        if (r >= 0) return r;
    }
    return -1;
}

static const char *cfop_cross(Cube *c, Solution *s) {
    CubieCube cc;
    cubie_from_cube(c, &cc);
    int st[4];
    for (int k = 0; k < 4; k++)
        for (int p = 0; p < NUM_EDGES; p++)
            if (cc.ep[p] == DR + k) st[k] = p * 2 + cc.eo[p];
    for (int d = 0; d <= 10; d++) {
        int n = cross_dfs(st, d, 0, -1);
        if (n >= 0) {
            solution_push_seq(s, c, cross_path, n);
            return NULL;
        }
    }
    return "CFOP: cross search failed";
}

/* ---------------- F2L ---------------- */

#define PAIR_STATES (8 * 3 * 12 * 2)
#define F2L_MACROS 9
#define F2L_MAX_MACROS 16

typedef struct {
    int8_t len; /* -1: not reachable */
    uint8_t macro[F2L_MAX_MACROS];
} PairCase;

static const char *const F2L_MACRO_ALGS[F2L_MACROS] = {
    "U", "U2", "U'", "R U R'", "R U' R'", "R U2 R'", "F' U F", "F' U' F", "F' U2 F",
};
static const int F2L_MACRO_INV[F2L_MACROS] = {2, 1, 0, 4, 3, 5, 7, 6, 8};
static Macro f2l_macros[4][F2L_MACROS];
static PairCase f2l_table[4][PAIR_STATES];

static int pair_key(const CubieCube *cc, int slot) {
    int cp = 0, ep = 0;
    for (int p = 0; p < NUM_CORNERS; p++)
        if (cc->cp[p] == SLOT_DCORNER[slot]) cp = p;
    for (int e = 0; e < NUM_EDGES; e++)
        if (cc->ep[e] == SLOT_EDGE[slot]) ep = e;
    return ((cp * 3 + cc->co[cp]) * 12 + ep) * 2 + cc->eo[ep];
}

static void apply_macro_cubie(CubieCube *cc, const Macro *mc) {
    for (int i = 0; i < mc->n; i++) cubie_apply(cc, mc->m[i]);
}

static void f2l_init(void) {
    for (int slot = 0; slot < 4; slot++) {
        for (int i = 0; i < F2L_MACROS; i++) macro_make(&f2l_macros[slot][i], F2L_MACRO_ALGS[i], slot, i < 3);

        static CubieCube states[PAIR_STATES];
        int dist[PAIR_STATES], parent[PAIR_STATES], pmacro[PAIR_STATES], queue[PAIR_STATES];
        for (int k = 0; k < PAIR_STATES; k++) dist[k] = -1;
        CubieCube solved;
        cubie_reset(&solved);
        int k0 = pair_key(&solved, slot), head = 0, tail = 0;
        dist[k0] = 0;
        states[k0] = solved;
        queue[tail++] = k0;
        while (head < tail) {
            int k = queue[head++];
            for (int i = 0; i < F2L_MACROS; i++) {
                CubieCube next = states[k];
                apply_macro_cubie(&next, &f2l_macros[slot][i]);
                int nk = pair_key(&next, slot);
                if (dist[nk] >= 0) continue;
                dist[nk] = dist[k] + 1;
                parent[nk] = k;
                pmacro[nk] = i;
                states[nk] = next;
                queue[tail++] = nk;
            }
        }
        /* The BFS found how to reach each case from solved; solving it is the
         * reverse, with every trigger inverted. */
        for (int k = 0; k < PAIR_STATES; k++) {
            PairCase *pc = &f2l_table[slot][k];
            pc->len = (int8_t)(dist[k] <= F2L_MAX_MACROS ? dist[k] : -1);
            if (pc->len < 0) continue;
            int n = 0;
            for (int x = k; x != k0; x = parent[x]) pc->macro[n++] = (uint8_t)F2L_MACRO_INV[pmacro[x]];
        }
    }
}

static void cfop_init(void);
int cfop_f2l_coverage(int slot) {
    cfop_init();
    int n = 0;
    for (int k = 0; k < PAIR_STATES; k++) n += f2l_table[slot][k].len >= 0;
    return n;
}

static int slot_index(const int *table, int pos) {
    for (int k = 0; k < 4; k++)
        if (table[k] == pos) return k;
    return -1;
}

static bool f2l_slot(Cube *c, Solution *s, int slot) {
    for (int iter = 0; iter < 4; iter++) {
        CubieCube cc;
        cubie_from_cube(c, &cc);
        int cp = 0, ep = 0;
        for (int p = 0; p < NUM_CORNERS; p++)
            if (cc.cp[p] == SLOT_DCORNER[slot]) cp = p;
        for (int e = 0; e < NUM_EDGES; e++)
            if (cc.ep[e] == SLOT_EDGE[slot]) ep = e;

        /* a piece stuck in another slot: pop it out to the top layer first */
        int other = -1;
        if (cp >= DFR && cp != SLOT_DCORNER[slot])
            other = slot_index(SLOT_DCORNER, cp);
        else if (ep >= FR && ep != SLOT_EDGE[slot])
            other = slot_index(SLOT_EDGE, ep);
        if (other >= 0) {
            const Macro *mc = &f2l_macros[other][3];
            solution_push_seq(s, c, mc->m, mc->n);
            continue;
        }

        const PairCase *pc = &f2l_table[slot][pair_key(&cc, slot)];
        if (pc->len < 0) return false;
        for (int i = 0; i < pc->len; i++) {
            const Macro *mc = &f2l_macros[slot][pc->macro[i]];
            solution_push_seq(s, c, mc->m, mc->n);
        }
        return slot_solved(c, slot);
    }
    return false;
}

static const char *cfop_f2l(Cube *c, Solution *s) {
    bool done[4] = {false};
    for (int n = 0; n < 4; n++) {
        int best = -1, best_len = 1 << 30;
        static Solution tmp;
        for (int slot = 0; slot < 4; slot++) {
            if (done[slot]) continue;
            Cube t = *c;
            solution_clear(&tmp);
            if (!f2l_slot(&t, &tmp, slot) || !cross_solved(&t)) continue;
            bool kept = true;
            for (int o = 0; o < 4; o++)
                if (done[o] && !slot_solved(&t, o)) kept = false;
            if (kept && tmp.n < best_len) {
                best = slot;
                best_len = tmp.n;
            }
        }
        if (best < 0) return "CFOP: F2L pair could not be solved";
        solution_begin_step(s, LESSON_F2L, "F2L %d: %s-%s", n + 1, COLOR_NAMES[cube_center(c, SLOT_FRONT[best])],
                            COLOR_NAMES[cube_center(c, SLOT_RIGHT[best])]);
        f2l_slot(c, s, best);
        done[best] = true;
    }
    return NULL;
}

/* ---------------- OLL / PLL ---------------- */

#define MAX_LL_ALGS 64
static Macro oll_macros[MAX_LL_ALGS], pll_macros[MAX_LL_ALGS];

static void push_u(Solution *s, Cube *c, int k) {
    if (k > 0) solution_push(s, c, MOVE(MB_U, k));
}

bool cfop_oll(Cube *c, Solution *s) {
    cfop_init();
    if (ll_oriented(c)) {
        solution_begin_step(s, LESSON_OLL_SKIP, "OLL skip");
        return true;
    }
    int best = -1, best_k = 0, best_len = 1 << 30;
    for (int k = 0; k < 4; k++) {
        for (int a = 0; a < NUM_OLL_ALGS; a++) {
            Cube t = *c;
            if (k) cube_apply(&t, MOVE(MB_U, k));
            cube_apply_seq(&t, oll_macros[a].m, oll_macros[a].n);
            int len = oll_macros[a].n + (k > 0);
            if (ll_oriented(&t) && f2l_solved(&t) && len < best_len) {
                best = a;
                best_k = k;
                best_len = len;
            }
        }
    }
    if (best < 0) return false;
    solution_begin_step_note(s, LESSON_OLL, OLL_ALGS[best].note, "%s", OLL_ALGS[best].name);
    push_u(s, c, best_k);
    solution_push_seq(s, c, oll_macros[best].m, oll_macros[best].n);
    return true;
}

bool cfop_pll(Cube *c, Solution *s) {
    cfop_init();
    if (cube_is_solved(c)) {
        solution_begin_step(s, LESSON_PLL_SKIP, "PLL skip");
        return true;
    }
    for (int k = 1; k < 4; k++) {
        Cube t = *c;
        cube_apply(&t, MOVE(MB_U, k));
        if (cube_is_solved(&t)) {
            solution_begin_step(s, LESSON_PLL_SKIP, "PLL skip (AUF)");
            push_u(s, c, k);
            return true;
        }
    }
    int best = -1, best_pre = 0, best_post = 0, best_len = 1 << 30;
    for (int pre = 0; pre < 4; pre++) {
        for (int a = 0; a < NUM_PLL_ALGS; a++) {
            Cube t = *c;
            if (pre) cube_apply(&t, MOVE(MB_U, pre));
            cube_apply_seq(&t, pll_macros[a].m, pll_macros[a].n);
            for (int post = 0; post < 4; post++) {
                int len = pll_macros[a].n + (pre > 0) + (post > 0);
                if (cube_is_solved(&t) && len < best_len) {
                    best = a;
                    best_pre = pre;
                    best_post = post;
                    best_len = len;
                }
                cube_apply(&t, MOVE(MB_U, 1));
            }
        }
    }
    if (best < 0) return false;
    solution_begin_step_note(s, LESSON_PLL, PLL_ALGS[best].note, "%s", PLL_ALGS[best].name);
    push_u(s, c, best_pre);
    solution_push_seq(s, c, pll_macros[best].m, pll_macros[best].n);
    push_u(s, c, best_post);
    return true;
}

/* ---------------- driver ---------------- */

static void cfop_init(void) {
    static bool ready;
    if (ready) return;
    cross_init();
    f2l_init();
    for (int a = 0; a < NUM_OLL_ALGS; a++) macro_make(&oll_macros[a], OLL_ALGS[a].alg, 0, false);
    for (int a = 0; a < NUM_PLL_ALGS; a++) macro_make(&pll_macros[a], PLL_ALGS[a].alg, 0, false);
    ready = true;
}

const char *solve_cfop(const Cube *in, Solution *s) {
    cfop_init();
    Cube c = *in;
    const char *err;
    if (cube_center(&c, FACE_D) != COLOR_WHITE) {
        solution_begin_step(s, LESSON_INSPECTION, "Inspection: white to bottom");
        orient_white_down(&c, s);
    }
    solution_begin_step(s, LESSON_CROSS, "Cross");
    if ((err = cfop_cross(&c, s))) return err;
    if ((err = cfop_f2l(&c, s))) return err;
    if (!cfop_oll(&c, s)) return "CFOP: OLL case not recognized";
    if (!cfop_pll(&c, s)) return "CFOP: PLL case not recognized";
    return NULL;
}
