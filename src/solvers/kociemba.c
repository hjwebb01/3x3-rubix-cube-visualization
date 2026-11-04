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
 * Phase 1 is pruned by its exact distance, from a symmetry-reduced table of
 * distances mod 3 (~35 MB): the search knows the parent's exact distance and a
 * child's is one less, the same or one more, which mod 3 tells apart. Building
 * it takes ~2-3 s, so it is saved to ~/.cache/rubiks and mapped on later runs.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

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
static uint16_t cperm_move[N_PERM8][N_P2_MOVES];
static uint16_t cperm_move18[N_PERM8][NUM_FACE_MOVES]; /* corner perm under every move, tracked through phase 1 */
static int8_t prune_cperm[N_PERM8]; /* phase-2 distance of the corners alone */
static uint16_t udedge_move[N_PERM8][N_P2_MOVES];
static uint8_t ssort_move[N_SLICE_SORTED][N_P2_MOVES];

/* Phase 1 is pruned by its exact distance, indexed by (flip, slice) reduced by
 * the 16 symmetries that keep the U/D axis, times twist: 4 bits per entry. */
#define N_FLIPSLICE (N_FLIP * N_SLICE)
#define N_FS_CLASS 64430
#define N_UD_SYM 16
#define N_P1 ((uint32_t)N_FS_CLASS * N_TWIST)
static CubieCube sym_cube[N_UD_SYM];
static int sym_inv[N_UD_SYM];
/* class << 4 | sym, where conjugating by sym turns the flipslice into its class rep */
static uint32_t fs_class_sym[N_FLIPSLICE];
static uint32_t fs_rep[N_FS_CLASS];
static uint16_t fs_stab[N_FS_CLASS]; /* syms that leave the rep unchanged */
static uint16_t twist_conj[N_TWIST][N_UD_SYM];
static uint8_t *prune_p1;     /* 4 bits per entry, only while building */
static const uint8_t *p1_m3; /* 2 bits per entry: distance mod 3 */
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
static int mv_cperm(int c, int i) { return cperm_move[c][i]; }
static int mv_udedge(int c, int i) { return udedge_move[c][i]; }
static int mv_ssort(int c, int i) { return ssort_move[c][i]; }

/* BFS distance table over the pair (c1, c2), indexed c1 * n2 + c2. */
static void build_prune(int8_t *table, int n1, int n2, MoveFn f1, MoveFn f2, int nmoves) {
    int *queue = malloc((size_t)n1 * (size_t)n2 * sizeof *queue);
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
    free(queue);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ---------------- phase 1 symmetry table ---------------- */

/* a * b where corner twists 3..5 mark mirrored cubies (Kociemba's convention) */
static void mul_ext(const CubieCube *a, const CubieCube *b, CubieCube *out) {
    cubie_multiply(a, b, out);
    for (int i = 0; i < NUM_CORNERS; i++) {
        int oa = a->co[b->cp[i]], ob = b->co[i], o;
        if (oa < 3 && ob < 3) o = (oa + ob) % 3;
        else if (oa < 3) o = oa + ob >= 6 ? oa + ob - 3 : oa + ob;
        else if (ob < 3) o = oa - ob < 3 ? oa - ob + 3 : oa - ob;
        else o = oa - ob < 0 ? oa - ob + 3 : oa - ob;
        out->co[i] = (uint8_t)o;
    }
}

/* S c S^-1 */
static void sym_conj(const CubieCube *c, int s, CubieCube *out) {
    CubieCube t;
    mul_ext(&sym_cube[s], c, &t);
    mul_ext(&t, &sym_cube[sym_inv[s]], out);
}

static void build_sym_tables(void) {
    static const CubieCube LR2 = {{UFL, URF, UBR, ULB, DLF, DFR, DRB, DBL}, {3, 3, 3, 3, 3, 3, 3, 3},
                                  {UL, UF, UR, UB, DL, DF, DR, DB, FL, FR, BR, BL}, {0}};
    static const CubieCube U4 = {{UBR, URF, UFL, ULB, DRB, DFR, DLF, DBL}, {0},
                                 {UB, UR, UF, UL, DB, DR, DF, DL, BR, FR, FL, BL}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1}};
    static const CubieCube F2 = {{DLF, DFR, DRB, DBL, UFL, URF, UBR, ULB}, {0},
                                 {DL, DF, DR, DB, UL, UF, UR, UB, FL, FR, BR, BL}, {0}};
    CubieCube c, t;
    cubie_reset(&c);
    for (int f2 = 0, n = 0; f2 < 2; f2++) {
        for (int u4 = 0; u4 < 4; u4++) {
            for (int lr2 = 0; lr2 < 2; lr2++) {
                sym_cube[n++] = c;
                mul_ext(&c, &LR2, &t), c = t;
            }
            mul_ext(&c, &U4, &t), c = t;
        }
        mul_ext(&c, &F2, &t), c = t;
    }
    for (int i = 0; i < N_UD_SYM; i++)
        for (int j = 0; j < N_UD_SYM; j++) {
            mul_ext(&sym_cube[i], &sym_cube[j], &t);
            CubieCube id;
            cubie_reset(&id);
            if (!memcmp(&t, &id, sizeof t)) sym_inv[i] = j;
        }

    for (int tw = 0; tw < N_TWIST; tw++) {
        cubie_reset(&c);
        set_twist(&c, tw);
        for (int s = 0; s < N_UD_SYM; s++) {
            sym_conj(&c, s, &t);
            twist_conj[tw][s] = (uint16_t)get_twist(&t);
        }
    }

    memset(fs_class_sym, 0xFF, sizeof fs_class_sym);
    int n = 0;
    for (int fs = 0; fs < N_FLIPSLICE; fs++) {
        if (fs_class_sym[fs] != UINT32_MAX) continue;
        fs_rep[n] = (uint32_t)fs;
        fs_stab[n] = 0;
        cubie_reset(&c);
        set_slice(&c, fs / N_FLIP);
        set_flip(&c, fs % N_FLIP);
        for (int s = 0; s < N_UD_SYM; s++) {
            sym_conj(&c, s, &t);
            int fs2 = get_slice(&t) * N_FLIP + get_flip(&t);
            if (fs2 == fs) fs_stab[n] |= (uint16_t)(1 << s);
            if (fs_class_sym[fs2] == UINT32_MAX) fs_class_sym[fs2] = (uint32_t)n << 4 | (uint32_t)sym_inv[s];
        }
        n++;
    }
}

static inline int p1_get(uint32_t i) {
    return __atomic_load_n(&prune_p1[i >> 1], __ATOMIC_RELAXED) >> (i & 1) * 4 & 0xF;
}

/* Entries start at 0xF (unset) and are set once, so an atomic AND writes the
 * nibble. Returns whether this call was the one that set it. */
static inline bool p1_set(uint32_t i, int d) {
    int shift = (i & 1) * 4;
    uint8_t old = __atomic_fetch_and(&prune_p1[i >> 1], (uint8_t)~((0xF ^ d) << shift), __ATOMIC_RELAXED);
    return (old >> shift & 0xF) == 0xF;
}

static inline uint32_t p1_index(int tw, int fl, int sl) {
    uint32_t cs = fs_class_sym[sl * N_FLIP + fl];
    return (cs >> 4) * N_TWIST + twist_conj[tw][cs & 15];
}

static inline int p1_m3_get(uint32_t i) { return p1_m3[i >> 2] >> (i & 3) * 2 & 3; }

/* P1_STEP[h % 3][child's entry]: how much farther a child is than its parent at distance h. */
static const int8_t P1_STEP[3][3] = {{0, 1, -1}, {-1, 0, 1}, {1, -1, 0}};

static int g1_slice; /* slice coordinate of G1 */

/* Exact distance, found by stepping down to G1: some move always leads one closer. */
static int p1_dist(int tw, int fl, int sl) {
    int d = 0;
    while (tw || fl || sl != g1_slice) {
        int e = p1_m3_get(p1_index(tw, fl, sl)), m = 0;
        if (d >= 12 || e == 3) return -1;
        int r = (e + 2) % 3;
        while (m < NUM_FACE_MOVES && p1_m3_get(p1_index(twist_move[tw][m], flip_move[fl][m], slice_move[sl][m])) != r) m++;
        if (m == NUM_FACE_MOVES) return -1;
        tw = twist_move[tw][m], fl = flip_move[fl][m], sl = slice_move[sl][m];
        d++;
    }
    return d;
}

typedef struct {
    uint32_t lo, hi;
    int depth;
    bool backward;
    long filled;
} P1Layer;

/* One BFS layer over [lo, hi): forward expands entries at `depth`, backward
 * fills unset entries that have a neighbor at `depth`. */
static void *p1_layer(void *arg) {
    P1Layer *L = arg;
    for (uint32_t i = L->lo; i < L->hi; i++) {
        int v = p1_get(i);
        if (v != (L->backward ? 0xF : L->depth)) continue;
        uint32_t rep = fs_rep[i / N_TWIST];
        int tw = (int)(i % N_TWIST), fl = (int)(rep % N_FLIP), sl = (int)(rep / N_FLIP);
        for (int m = 0; m < NUM_FACE_MOVES; m++) {
            int tw2 = twist_move[tw][m], fl2 = flip_move[fl][m], sl2 = slice_move[sl][m];
            uint32_t j = p1_index(tw2, fl2, sl2);
            if (L->backward) {
                if (p1_get(j) != L->depth) continue;
                L->filled += p1_set(i, L->depth + 1);
                break;
            }
            if (p1_get(j) != 0xF) continue;
            /* symmetric twins of the target are the same position */
            uint32_t c = j / N_TWIST;
            L->filled += p1_set(j, L->depth + 1);
            for (int s = 1; fs_stab[c] >> s; s++)
                if (fs_stab[c] >> s & 1) L->filled += p1_set(c * N_TWIST + twist_conj[j % N_TWIST][s], L->depth + 1);
        }
    }
    return NULL;
}

static void build_prune_p1(void) {
    enum { T = 32 };
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int nt = cpus < 1 ? 1 : cpus > T ? T : (int)cpus;
    prune_p1 = malloc(N_P1 / 2);
    if (!prune_p1) abort();
    memset(prune_p1, 0xFF, N_P1 / 2);
    p1_set(0, 0);
    long filled = 1, at_depth = 1;
    for (int d = 0; at_depth; d++) {
        bool backward = (long)N_P1 - filled < 2 * at_depth;
        P1Layer L[T];
        pthread_t th[T];
        bool started[T];
        for (int t = 0; t < nt; t++) {
            L[t] = (P1Layer){(uint32_t)((uint64_t)N_P1 * t / nt), (uint32_t)((uint64_t)N_P1 * (t + 1) / nt), d, backward, 0};
            started[t] = pthread_create(&th[t], NULL, p1_layer, &L[t]) == 0;
            if (!started[t]) p1_layer(&L[t]);
        }
        at_depth = 0;
        for (int t = 0; t < nt; t++) {
            if (started[t]) pthread_join(th[t], NULL);
            at_depth += L[t].filled;
        }
        filled += at_depth;
    }
    uint8_t *m3 = calloc((N_P1 + 3) / 4, 1);
    if (!m3) abort();
    for (uint32_t i = 0; i < N_P1; i++) m3[i >> 2] |= (uint8_t)(p1_get(i) % 3 << (i & 3) * 2);
    free(prune_p1);
    prune_p1 = NULL;
    p1_m3 = m3;
}

/* The phase-1 table on disk: a header, then the 2-bit entries. Bump the version
 * whenever the table's layout or coordinates change. */
static const char P1_MAGIC[16] = "rubiks-p1-v1";
#define P1_BYTES ((N_P1 + 3) / 4)

static bool p1_cache_path(char *out, size_t n) {
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    int k = xdg && *xdg ? snprintf(out, n, "%s/rubiks", xdg) : home ? snprintf(out, n, "%s/.cache/rubiks", home) : -1;
    if (k < 0 || (size_t)k >= n) return false;
    return snprintf(out + k, n - (size_t)k, "/kociemba-p1.bin") < (int)(n - (size_t)k);
}

static bool p1_cache_load(void) {
    char path[512];
    if (!p1_cache_path(path, sizeof path)) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st;
    const uint8_t *map = MAP_FAILED;
    if (fstat(fd, &st) == 0 && st.st_size == (off_t)(sizeof P1_MAGIC + P1_BYTES))
        map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return false;
    if (memcmp(map, P1_MAGIC, sizeof P1_MAGIC) != 0) {
        munmap((void *)map, (size_t)st.st_size);
        return false;
    }
    p1_m3 = map + sizeof P1_MAGIC;
    uint32_t x = 12345;
    for (int i = 0; i < 64; i++) {
        x = x * 1664525u + 1013904223u;
        if (p1_dist((int)(x >> 8) % N_TWIST, (int)(x >> 4) % N_FLIP, (int)(x >> 12) % N_SLICE) < 0) {
            p1_m3 = NULL;
            munmap((void *)map, (size_t)st.st_size);
            return false;
        }
    }
    return true;
}

/* Best effort: a failure just means the next run builds the table again. */
static void p1_cache_save(void) {
    char path[512], tmp[520];
    if (!p1_cache_path(path, sizeof path)) return;
    char *slash = strrchr(path, '/');
    *slash = 0;
    char *parent = strrchr(path, '/');
    *parent = 0;
    mkdir(path, 0755);
    *parent = '/';
    mkdir(path, 0755);
    *slash = '/';
    snprintf(tmp, sizeof tmp, "%s.%d", path, (int)getpid());
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    bool ok = fwrite(P1_MAGIC, sizeof P1_MAGIC, 1, f) == 1 && fwrite(p1_m3, P1_BYTES, 1, f) == 1;
    if (fclose(f) != 0 || !ok || rename(tmp, path) != 0) remove(tmp);
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
            cperm_move18[p][m] = (uint16_t)get_cperm(&d);
        }
        for (int i = 0; i < N_P2_MOVES; i++) cperm_move[p][i] = cperm_move18[p][P2_MOVES[i]];
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

    build_sym_tables();
    CubieCube g1;
    cubie_reset(&g1);
    g1_slice = get_slice(&g1);
    if (!p1_cache_load()) {
        build_prune_p1();
        p1_cache_save();
    }
    build_prune(prune_cperm_ss, N_PERM8, N_SLICE_SORTED, mv_cperm, mv_ssort, N_P2_MOVES);
    build_prune(prune_udedge_ss, N_PERM8, N_SLICE_SORTED, mv_udedge, mv_ssort, N_P2_MOVES);
    for (int p = 0; p < N_PERM8; p++) {
        prune_cperm[p] = 127;
        for (int ss = 0; ss < N_SLICE_SORTED; ss++)
            if (prune_cperm_ss[p * N_SLICE_SORTED + ss] < prune_cperm[p]) prune_cperm[p] = prune_cperm_ss[p * N_SLICE_SORTED + ss];
    }
    g_ready = true;
}

/* ---------------- search ---------------- */

#define N_PARTS 3

typedef struct {
    CubieCube start;
    Move path[40];
    Move best[40];
    int best_len;
    int best_split; /* phase-1 length of best */
    int target;
    double deadline;
    long nodes;
    atomic_int *shared_best; /* best length over all parallel searches */
    int part;                /* this search only tries first moves m with m % N_PARTS == part */
    bool stop;
} Search;


static inline int max2(int a, int b) { return a > b ? a : b; }

static inline int bound(const Search *S) {
    int b = atomic_load_explicit(S->shared_best, memory_order_relaxed);
    return b < S->best_len ? b : S->best_len;
}

static bool phase2_dfs(Search *S, int cp, int ue, int ss, int depth, int n, int last) {
    int h = max2(prune_cperm_ss[cp * N_SLICE_SORTED + ss], prune_udedge_ss[ue * N_SLICE_SORTED + ss]);
    if (h == 0) return depth == 0;
    if (h > depth) return false;
    for (int i = 0; i < N_P2_MOVES; i++) {
        Move m = P2_MOVES[i];
        int f = m / 3;
        if (last >= 0 && (f == last || f == last - 3)) continue;
        S->path[n] = m;
        if (phase2_dfs(S, cperm_move[cp][i], udedge_move[ue][i], ssort_move[ss][i], depth - 1, n + 1, f)) return true;
    }
    return false;
}

static void phase2_start(Search *S, int n, int last, int cp) {
    int maxd = bound(S) - 1 - n;
    if (maxd > 11) maxd = 11;
    /* most phase-1 leaves fail on the corners alone; skip them before the costly replay */
    if (prune_cperm[cp] > maxd) return;
    CubieCube c = S->start;
    for (int i = 0; i < n; i++) cubie_apply(&c, S->path[i]);
    int ue = get_udedge(&c), ss = get_ssort(&c);
    int h = max2(prune_cperm_ss[cp * N_SLICE_SORTED + ss], prune_udedge_ss[ue * N_SLICE_SORTED + ss]);
    for (int d = h; d <= maxd; d++) {
        if (phase2_dfs(S, cp, ue, ss, d, n, last)) {
            S->best_len = n + d;
            S->best_split = n;
            int b = atomic_load(S->shared_best);
            while (S->best_len < b && !atomic_compare_exchange_weak(S->shared_best, &b, S->best_len)) {}
            memcpy(S->best, S->path, (size_t)S->best_len * sizeof(Move));
            if (S->best_len <= S->target) S->stop = true;
            break;
        }
    }
    if (bound(S) < 99 && now_ms() > S->deadline) S->stop = true;
}

/* h is the exact phase-1 distance of (tw, fl, sl), at most depth. */
static void phase1_dfs(Search *S, int tw, int fl, int sl, int cp, int h, int depth, int n, int last) {
    if (S->stop || bound(S) <= S->target) return; /* another search already hit the target */
    if ((++S->nodes & 0xFFFF) == 0 && bound(S) < 99 && now_ms() > S->deadline) {
        S->stop = true;
        return;
    }
    if (depth == 0) {
        if (tw || fl || sl != g1_slice) return;
        if (n == 0 ? S->part == 0 : !is_p2_move[S->path[n - 1]]) phase2_start(S, n, last, cp);
        return;
    }
    /* leaving G1 and coming back in under 5 moves never beats letting phase 2 do it */
    if (h == 0 && depth < 5) return;
    /* no point continuing if even an instant phase 2 can't beat the best */
    if (n + depth >= bound(S)) return;
    /* the tables are too big for cache, so fetch all children's entries before descending: first the
     * flipslice classes the indexes need, then the pruning entries */
    uint32_t idx[NUM_FACE_MOVES];
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        int f = m / 3;
        idx[m] = UINT32_MAX;
        if (last >= 0 && (f == last || f == last - 3)) continue;
        if (n == 0 && m % N_PARTS != S->part) continue;
        idx[m] = (uint32_t)slice_move[sl][m] * N_FLIP + flip_move[fl][m];
        __builtin_prefetch(&fs_class_sym[idx[m]]);
    }
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        if (idx[m] == UINT32_MAX) continue;
        uint32_t cs = fs_class_sym[idx[m]];
        idx[m] = (cs >> 4) * N_TWIST + twist_conj[twist_move[tw][m]][cs & 15];
        __builtin_prefetch(&p1_m3[idx[m] >> 2]);
    }
    const int8_t *step = P1_STEP[h % 3];
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        if (idx[m] == UINT32_MAX) continue;
        int entry = p1_m3_get(idx[m]);
        if (entry == 3) continue;
        int h2 = h + step[entry];
        if (h2 < 0 || h2 >= depth) continue;
        S->path[n] = (Move)m;
        phase1_dfs(S, twist_move[tw][m], flip_move[fl][m], slice_move[sl][m], cperm_move18[cp][m], h2, depth - 1, n + 1, m / 3);
        if (S->stop) return;
    }
}

/* Two-phase search on a cubie cube. Stops at the first solution of at most
 * `target` moves, or after `timeout_ms` with the best found so far. Returns the
 * length (and the phase-1 length in *split), or -1 if nothing was found. */
static int kociemba_search(const CubieCube *cc, int target, double timeout_ms, atomic_int *shared_best, int part,
                           Move *out, int *split) {
    Search S;
    memset(&S, 0, sizeof S);
    S.shared_best = shared_best;
    S.part = part;
    S.start = *cc;
    S.best_len = 99;
    S.target = target;
    S.deadline = now_ms() + timeout_ms;
    int tw = get_twist(cc), fl = get_flip(cc), sl = get_slice(cc);
    int h = p1_dist(tw, fl, sl), cp = get_cperm(cc);
    if (h < 0) return -1;
    for (int d1 = h; d1 <= 20 && !S.stop; d1++) phase1_dfs(&S, tw, fl, sl, cp, h, d1, 0, -1);
    if (S.best_len >= 99) return -1;
    memcpy(out, S.best, (size_t)S.best_len * sizeof(Move));
    *split = S.best_split;
    return S.best_len;
}

/* The search runs on 3 axes (cube held as is, after x, after z), each on the
 * cube and on its inverse, each split N_PARTS ways by first move, in parallel: different axes find different short
 * solutions, which is how Kociemba's own solver gets ~18 moves fast. */
#define N_JOBS (6 * N_PARTS)
static const Move JOB_ROT[3] = {NUM_MOVES, MOVE(MB_X, 1), MOVE(MB_Z, 1)}; /* NUM_MOVES: no rotation */
static Move face_map[3][MB_COUNT]; /* face of a move made after JOB_ROT -> the same turn before it */

typedef struct {
    Cube cube;
    int job;
    Move moves[40];
    int len, split;
    atomic_int *shared_best;
} Job;

static void invert_cubie(const CubieCube *a, CubieCube *out) {
    for (int i = 0; i < NUM_CORNERS; i++) {
        out->cp[a->cp[i]] = (uint8_t)i;
        out->co[a->cp[i]] = (uint8_t)((3 - a->co[i]) % 3);
    }
    for (int i = 0; i < NUM_EDGES; i++) {
        out->ep[a->ep[i]] = (uint8_t)i;
        out->eo[a->ep[i]] = a->eo[i];
    }
}

static void *job_run(void *arg) {
    Job *j = arg;
    int axis = j->job / (2 * N_PARTS);
    bool inverse = j->job / N_PARTS & 1;
    Cube c = j->cube;
    if (axis) cube_apply(&c, JOB_ROT[axis]);
    CubieCube cc, inv;
    cubie_from_cube(&c, &cc);
    if (inverse) {
        invert_cubie(&cc, &inv);
        cc = inv;
    }
    Move found[40];
    j->len = kociemba_search(&cc, 17, 92.0, j->shared_best, j->job % N_PARTS, found, &j->split);
    if (j->len < 0) return NULL;
    if (inverse) {
        moves_invert(found, j->len, j->moves);
        j->split = j->len - j->split;
    } else {
        memcpy(j->moves, found, (size_t)j->len * sizeof(Move));
    }
    for (int i = 0; i < j->len; i++) j->moves[i] = MOVE(face_map[axis][j->moves[i] / 3], j->moves[i] % 3 + 1);
    return NULL;
}

static void build_face_map(void) {
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 6; b++)
            for (int b0 = 0; b0 < 6; b0++) {
                Cube x, y;
                cube_reset(&x);
                cube_reset(&y);
                if (a) cube_apply(&x, JOB_ROT[a]);
                cube_apply(&x, MOVE(b, 1));
                cube_apply(&y, MOVE(b0, 1));
                if (a) cube_apply(&y, JOB_ROT[a]);
                if (cube_equal(&x, &y)) face_map[a][b] = (Move)b0;
            }
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void init_once(void) {
    kociemba_init();
    build_face_map();
}

void kociemba_warm(void) { pthread_once(&g_once, init_once); }

const char *solve_kociemba(const Cube *c, Solution *s) {
    CubieCube cc;
    const char *err = cubie_from_cube(c, &cc);
    if (err) return err;
    kociemba_warm();

    Job jobs[N_JOBS];
    atomic_int shared_best = 99;
    pthread_t th[N_JOBS];
    bool started[N_JOBS];
    for (int i = 0; i < N_JOBS; i++) {
        jobs[i] = (Job){.cube = *c, .job = i, .len = -1, .shared_best = &shared_best};
        started[i] = pthread_create(&th[i], NULL, job_run, &jobs[i]) == 0;
        if (!started[i]) job_run(&jobs[i]);
    }
    const Job *best = NULL;
    for (int i = 0; i < N_JOBS; i++) {
        if (started[i]) pthread_join(th[i], NULL);
        if (jobs[i].len >= 0 && (!best || jobs[i].len < best->len)) best = &jobs[i];
    }
    if (!best) return "Kociemba: no solution found";

    /* phases belong to the axis and direction that found the solution */
    int axis = best->job / (2 * N_PARTS), split = best->split, n = best->len;
    bool inverse = best->job / N_PARTS & 1;
    int ud = face_map[axis][MB_U] % 3;
    char g1[32];
    int k = snprintf(g1, sizeof g1, "<%c,%c", "URFDLB"[ud], "URFDLB"[ud + 3]);
    for (int f = 0; f < 6; f++)
        if (f % 3 != ud) k += snprintf(g1 + k, sizeof g1 - (size_t)k, ",%c2", "URFDLB"[f]);
    snprintf(g1 + k, sizeof g1 - (size_t)k, ">");
    if (inverse) {
        solution_begin_step(s, LESSON_KOCIEMBA_2, "Undo phase 2 of the inverse: %s", g1);
        solution_push_seq(s, NULL, best->moves, split);
        solution_begin_step(s, LESSON_KOCIEMBA_1, "Undo phase 1 of the inverse");
    } else {
        solution_begin_step(s, LESSON_KOCIEMBA_1, "Phase 1: orient, slice edges (to G1, %c/%c axis)", "URFDLB"[ud],
                            "URFDLB"[ud + 3]);
        solution_push_seq(s, NULL, best->moves, split);
        solution_begin_step(s, LESSON_KOCIEMBA_2, "Phase 2: solve in %s", g1);
    }
    solution_push_seq(s, NULL, best->moves + split, n - split);
    return NULL;
}
