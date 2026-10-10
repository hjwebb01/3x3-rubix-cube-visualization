/*
 * Kociemba's two-phase algorithm.
 *
 * Phase 1 takes the cube into the subgroup G1 = <U, D, R2, L2, F2, B2>: all
 * corners and edges oriented and the four E-slice edges inside the E slice.
 * Coordinates: twist (3^7 = 2187), flip (2^11 = 2048), slice (C(12,4) = 495).
 *
 * Phase 2 solves the cube using only G1 moves. Coordinates: corner
 * permutation (8! = 40320), U/D edge permutation (8!), E-slice permutation (4!).
 *
 * Both phases are IDA* searches guided by pruning tables (exact distances in
 * a projection of the state space, built with BFS). The search keeps trying
 * longer phase-1 solutions because they often lead to a shorter total.
 *
 * Tables are ~6 MB and are built on first use (well under a second).
 */
#include <string.h>
#include <time.h>

#include "solvers/solvers.h"

#define N_TWIST 2187
#define N_FLIP 2048
#define N_SLICE 495
#define N_PERM8 40320
#define N_SLICE_SORTED 24
#define N_P2_MOVES 10

static const Move P2_MOVES[N_P2_MOVES] = {
    MOVE(MB_U, 1), MOVE(MB_U, 2), MOVE(MB_U, 3), MOVE(MB_D, 1), MOVE(MB_D, 2),
    MOVE(MB_D, 3), MOVE(MB_R, 2), MOVE(MB_L, 2), MOVE(MB_F, 2), MOVE(MB_B, 2),
};

static uint16_t twist_move[N_TWIST][NUM_FACE_MOVES];
static uint16_t flip_move[N_FLIP][NUM_FACE_MOVES];
static uint16_t slice_move[N_SLICE][NUM_FACE_MOVES];
static uint16_t cperm_move[N_PERM8][NUM_FACE_MOVES];
static uint16_t udedge_move[N_PERM8][N_P2_MOVES];
static uint8_t ssort_move[N_SLICE_SORTED][N_P2_MOVES];

static int8_t prune_twist_slice[N_TWIST * N_SLICE];
static int8_t prune_flip_slice[N_FLIP * N_SLICE];
static int8_t prune_cperm_ss[N_PERM8 * N_SLICE_SORTED];
static int8_t prune_udedge_ss[N_PERM8 * N_SLICE_SORTED];

static bool g_ready;
static bool is_p2_move[NUM_FACE_MOVES];

/* ---------------- coordinates ---------------- */

static int binom(int n, int k) {
    if (k < 0 || k > n) return 0;
    int r = 1;
    for (int i = 0; i < k; i++) r = r * (n - i) / (i + 1);
    return r;
}

static int get_twist(const CubieCube *c) {
    int t = 0;
    for (int i = 0; i < 7; i++) t = 3 * t + c->co[i];
    return t;
}

static void set_twist(CubieCube *c, int t) {
    int sum = 0;
    for (int i = 6; i >= 0; i--) {
        c->co[i] = (uint8_t)(t % 3);
        sum += c->co[i];
        t /= 3;
    }
    c->co[7] = (uint8_t)((3 - sum % 3) % 3);
}

static int get_flip(const CubieCube *c) {
    int f = 0;
    for (int i = 0; i < 11; i++) f = 2 * f + c->eo[i];
    return f;
}

static void set_flip(CubieCube *c, int f) {
    int sum = 0;
    for (int i = 10; i >= 0; i--) {
        c->eo[i] = (uint8_t)(f & 1);
        sum += c->eo[i];
        f >>= 1;
    }
    c->eo[11] = (uint8_t)(sum & 1);
}

/* Which 4 of the 12 positions hold E-slice edges (FR, FL, BL, BR), ignoring order. 0 when solved. */
static int get_slice(const CubieCube *c) {
    int a = 0, x = 0;
    for (int j = NUM_EDGES - 1; j >= 0; j--)
        if (c->ep[j] >= FR) a += binom(11 - j, ++x);
    return a;
}

static void set_slice(CubieCube *c, int a) {
    static const uint8_t SLICE[4] = {FR, FL, BL, BR}, OTHER[8] = {UR, UF, UL, UB, DR, DF, DL, DB};
    for (int j = 0; j < NUM_EDGES; j++) c->ep[j] = 255;
    int x = 4;
    for (int j = 0; j < NUM_EDGES; j++) {
        if (x > 0 && a - binom(11 - j, x) >= 0) {
            c->ep[j] = SLICE[4 - x];
            a -= binom(11 - j, x--);
        }
    }
    x = 0;
    for (int j = 0; j < NUM_EDGES; j++)
        if (c->ep[j] == 255) c->ep[j] = OTHER[x++];
}

/* Lehmer-code rank of a permutation (only the relative order of values matters). */
static int perm_rank(const uint8_t *p, int n) {
    int r = 0;
    for (int i = 0; i < n; i++) {
        int smaller = 0;
        for (int j = i + 1; j < n; j++)
            if (p[j] < p[i]) smaller++;
        r = r * (n - i) + smaller;
    }
    return r;
}

static void perm_unrank(int r, uint8_t *p, int n, int offset) {
    int digits[12];
    for (int i = n - 1; i >= 0; i--) {
        digits[i] = r % (n - i);
        r /= (n - i);
    }
    bool used[12] = {false};
    for (int i = 0; i < n; i++) {
        int d = digits[i], v = 0;
        for (;; v++) {
            if (used[v]) continue;
            if (d-- == 0) break;
        }
        used[v] = true;
        p[i] = (uint8_t)(v + offset);
    }
}

static int get_cperm(const CubieCube *c) { return perm_rank(c->cp, 8); }
static int get_udedge(const CubieCube *c) { return perm_rank(c->ep, 8); }
static int get_ssort(const CubieCube *c) { return perm_rank(c->ep + 8, 4); }

/* ---------------- tables ---------------- */

typedef int (*MoveFn)(int coord, int i);
static int mv_twist(int c, int i) { return twist_move[c][i]; }
static int mv_flip(int c, int i) { return flip_move[c][i]; }
static int mv_slice(int c, int i) { return slice_move[c][i]; }
static int mv_cperm(int c, int i) { return cperm_move[c][P2_MOVES[i]]; }
static int mv_udedge(int c, int i) { return udedge_move[c][i]; }
static int mv_ssort(int c, int i) { return ssort_move[c][i]; }

/* BFS distance table over the pair (c1, c2), indexed c1 * n2 + c2. */
static void build_prune(int8_t *table, int n1, int n2, MoveFn f1, MoveFn f2, int nmoves) {
    static int queue[N_PERM8 * N_SLICE_SORTED > N_TWIST * N_SLICE ? N_PERM8 * N_SLICE_SORTED : N_TWIST * N_SLICE];
    memset(table, -1, (size_t)n1 * (size_t)n2);
    int head = 0, tail = 0;
    table[0] = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int idx = queue[head++], c1 = idx / n2, c2 = idx % n2;
        for (int i = 0; i < nmoves; i++) {
            int ni = f1(c1, i) * n2 + f2(c2, i);
            if (table[ni] < 0) {
                table[ni] = (int8_t)(table[idx] + 1);
                queue[tail++] = ni;
            }
        }
    }
}

static void kociemba_init(void) {
    if (g_ready) return;
    cubie_init();
    CubieCube c, d;

    for (int i = 0; i < N_P2_MOVES; i++) is_p2_move[P2_MOVES[i]] = true;

    for (int t = 0; t < N_TWIST; t++) {
        cubie_reset(&c);
        set_twist(&c, t);
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            cubie_multiply(&c, cubie_move((Move)m), &d);
            twist_move[t][m] = (uint16_t)get_twist(&d);
        }
    }
    for (int f = 0; f < N_FLIP; f++) {
        cubie_reset(&c);
        set_flip(&c, f);
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            cubie_multiply(&c, cubie_move((Move)m), &d);
            flip_move[f][m] = (uint16_t)get_flip(&d);
        }
    }
    for (int s = 0; s < N_SLICE; s++) {
        cubie_reset(&c);
        set_slice(&c, s);
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            cubie_multiply(&c, cubie_move((Move)m), &d);
            slice_move[s][m] = (uint16_t)get_slice(&d);
        }
    }
    for (int p = 0; p < N_PERM8; p++) {
        cubie_reset(&c);
        perm_unrank(p, c.cp, 8, 0);
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            cubie_multiply(&c, cubie_move((Move)m), &d);
            cperm_move[p][m] = (uint16_t)get_cperm(&d);
        }
        cubie_reset(&c);
        perm_unrank(p, c.ep, 8, 0);
        for (int i = 0; i < N_P2_MOVES; i++) {
            cubie_multiply(&c, cubie_move(P2_MOVES[i]), &d);
            udedge_move[p][i] = (uint16_t)get_udedge(&d);
        }
    }
    for (int p = 0; p < N_SLICE_SORTED; p++) {
        cubie_reset(&c);
        perm_unrank(p, c.ep + 8, 4, 8);
        for (int i = 0; i < N_P2_MOVES; i++) {
            cubie_multiply(&c, cubie_move(P2_MOVES[i]), &d);
            ssort_move[p][i] = (uint8_t)get_ssort(&d);
        }
    }

    build_prune(prune_twist_slice, N_TWIST, N_SLICE, mv_twist, mv_slice, NUM_FACE_MOVES);
    build_prune(prune_flip_slice, N_FLIP, N_SLICE, mv_flip, mv_slice, NUM_FACE_MOVES);
    build_prune(prune_cperm_ss, N_PERM8, N_SLICE_SORTED, mv_cperm, mv_ssort, N_P2_MOVES);
    build_prune(prune_udedge_ss, N_PERM8, N_SLICE_SORTED, mv_udedge, mv_ssort, N_P2_MOVES);
    g_ready = true;
}

/* ---------------- search ---------------- */

typedef struct {
    CubieCube start;
    Move path[40];
    Move best[40];
    int best_len;
    int target;
    double deadline;
    long nodes;
    bool stop;
} Search;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static inline int max2(int a, int b) { return a > b ? a : b; }

static bool phase2_dfs(Search *S, int cp, int ue, int ss, int depth, int n, int last) {
    int h = max2(prune_cperm_ss[cp * N_SLICE_SORTED + ss], prune_udedge_ss[ue * N_SLICE_SORTED + ss]);
    if (h == 0) return depth == 0;
    if (h > depth) return false;
    for (int i = 0; i < N_P2_MOVES; i++) {
        Move m = P2_MOVES[i];
        int f = m / 3;
        if (last >= 0 && (f == last || f == last - 3)) continue;
        S->path[n] = m;
        if (phase2_dfs(S, cperm_move[cp][m], udedge_move[ue][i], ssort_move[ss][i], depth - 1, n + 1, f)) return true;
    }
    return false;
}

static void phase2_start(Search *S, int n, int last) {
    CubieCube c = S->start;
    for (int i = 0; i < n; i++) cubie_apply(&c, S->path[i]);
    int cp = get_cperm(&c), ue = get_udedge(&c), ss = get_ssort(&c);
    int maxd = S->best_len - 1 - n;
    if (maxd > 18) maxd = 18;
    int h = max2(prune_cperm_ss[cp * N_SLICE_SORTED + ss], prune_udedge_ss[ue * N_SLICE_SORTED + ss]);
    for (int d = h; d <= maxd; d++) {
        if (phase2_dfs(S, cp, ue, ss, d, n, last)) {
            S->best_len = n + d;
            memcpy(S->best, S->path, (size_t)S->best_len * sizeof(Move));
            if (S->best_len <= S->target) S->stop = true;
            break;
        }
    }
    if (S->best_len < 99 && now_ms() > S->deadline) S->stop = true;
}

static void phase1_dfs(Search *S, int tw, int fl, int sl, int depth, int n, int last) {
    if (S->stop) return;
    if ((++S->nodes & 0xFFFF) == 0 && S->best_len < 99 && now_ms() > S->deadline) {
        S->stop = true;
        return;
    }
    if (depth == 0) {
        if (tw == 0 && fl == 0 && sl == 0 && (n == 0 || !is_p2_move[S->path[n - 1]])) phase2_start(S, n, last);
        return;
    }
    int h = max2(prune_twist_slice[tw * N_SLICE + sl], prune_flip_slice[fl * N_SLICE + sl]);
    if (h > depth) return;
    /* no point continuing if even an instant phase 2 can't beat the best */
    if (n + depth >= S->best_len) return;
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        int f = m / 3;
        if (last >= 0 && (f == last || f == last - 3)) continue;
        S->path[n] = (Move)m;
        phase1_dfs(S, twist_move[tw][m], flip_move[fl][m], slice_move[sl][m], depth - 1, n + 1, f);
        if (S->stop) return;
    }
}

int kociemba_search(const CubieCube *cc, int target, double timeout_ms, Move *out) {
    kociemba_init();
    static Search S;
    memset(&S, 0, sizeof S);
    S.start = *cc;
    S.best_len = 99;
    S.target = target;
    S.deadline = now_ms() + timeout_ms;
    int tw = get_twist(cc), fl = get_flip(cc), sl = get_slice(cc);
    for (int d1 = 0; d1 <= 20 && !S.stop; d1++) phase1_dfs(&S, tw, fl, sl, d1, 0, -1);
    if (S.best_len >= 99) return -1;
    memcpy(out, S.best, (size_t)S.best_len * sizeof(Move));
    return S.best_len;
}

const char *solve_kociemba(const Cube *c, Solution *s) {
    CubieCube cc;
    const char *err = cubie_from_cube(c, &cc);
    if (err) return err;
    Move moves[40];
    int n = kociemba_search(&cc, 20, 1000.0, moves);
    if (n < 0) return "Kociemba: no solution found";

    /* split the result into its two phases for display */
    int split = 0;
    CubieCube t = cc;
    for (int i = 0; i <= n; i++) {
        if (get_twist(&t) == 0 && get_flip(&t) == 0 && get_slice(&t) == 0) {
            split = i;
            break;
        }
        if (i < n) cubie_apply(&t, moves[i]);
    }
    solution_begin_step(s, "Phase 1: orient, slice edges (to G1)");
    solution_push_seq(s, NULL, moves, split);
    solution_begin_step(s, "Phase 2: solve in <U,D,R2,L2,F2,B2>");
    solution_push_seq(s, NULL, moves + split, n - split);
    return NULL;
}
