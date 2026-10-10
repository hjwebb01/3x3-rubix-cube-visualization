/*
 * Thistlethwaite's algorithm: four phases, each moving the cube into a smaller
 * subgroup using only the moves of the current one.
 *
 *   G0 = <U, D, R, L, F, B>
 *   G1 = <U, D, R, L, F2, B2>      phase 1: orient all edges
 *   G2 = <U, D, R2, L2, F2, B2>    phase 2: orient corners, E-slice edges in E slice
 *   G3 = <U2, D2, R2, L2, F2, B2>  phase 3: edges in their slices, corners in their
 *                                           tetrads (plus tetrad twist), even parity
 *   G4 = { solved }                phase 4: finish with half turns only
 *
 * Each phase is a bidirectional breadth-first search between the current state
 * and the solved state, keyed by an "id" that is equal for two states exactly
 * when they lie in the same coset of the target subgroup. This is the approach
 * popularized by Stefan Pochmann's compact implementation.
 */
#include <stdlib.h>
#include <string.h>

#include "solvers/solvers.h"

typedef struct {
    uint64_t a, b;
} Key;

static bool key_eq(Key x, Key y) { return x.a == y.a && x.b == y.b; }

static Key phase_id(const CubieCube *c, int phase) {
    Key k = {0, 0};
    switch (phase) {
    case 0: /* edge orientation */
        for (int i = 0; i < NUM_EDGES; i++) k.a |= (uint64_t)c->eo[i] << i;
        break;
    case 1: /* corner orientation + which positions hold E-slice edges */
        for (int i = 0; i < NUM_CORNERS; i++) k.a |= (uint64_t)c->co[i] << (2 * i);
        for (int i = 0; i < NUM_EDGES; i++) k.a |= (uint64_t)(c->ep[i] >= FR) << (16 + i);
        break;
    case 2:
        /* slice of the edge in each position: E (2), M (1: UF UB DF DB) or S (0) */
        for (int i = 0; i < NUM_EDGES; i++) k.a |= (uint64_t)(c->ep[i] >= FR ? 2 : (c->ep[i] & 1)) << (2 * i);
        /* corners labelled by diagonal pair (URF/ULB, UFL/UBR, DFR/DBL, DLF/DRB).
         * This captures tetrad membership and the tetrad twist. */
        for (int i = 0; i < NUM_CORNERS; i++) k.a |= (uint64_t)(c->cp[i] & 5) << (24 + 3 * i);
        k.a |= (uint64_t)perm_parity(c->cp, NUM_CORNERS) << 48;
        break;
    default: /* everything (orientations are already solved) */
        for (int i = 0; i < NUM_EDGES; i++) k.a |= (uint64_t)c->ep[i] << (4 * i);
        for (int i = 0; i < NUM_CORNERS; i++) k.b |= (uint64_t)c->cp[i] << (3 * i);
        break;
    }
    return k;
}

static bool move_allowed(int phase, Move m) {
    int face = m / 3, half = MOVE_TURNS(m) == 2;
    switch (phase) {
    case 0: return true;
    case 1: return half || (face != FACE_F && face != FACE_B);
    case 2: return half || face == FACE_U || face == FACE_D;
    default: return half;
    }
}

/* ---------- small open-addressing hash map: Key -> search node ---------- */

typedef struct {
    Key key, parent;
    uint8_t used, dir, move;
} Node;

typedef struct {
    Node *slots;
    size_t cap, count;
} Map;

static size_t key_hash(Key k) {
    uint64_t h = k.a * 0x9E3779B97F4A7C15ull ^ (k.b + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return (size_t)h;
}

static Node *map_find(Map *m, Key k) {
    size_t i = key_hash(k) & (m->cap - 1);
    while (m->slots[i].used) {
        if (key_eq(m->slots[i].key, k)) return &m->slots[i];
        i = (i + 1) & (m->cap - 1);
    }
    return NULL;
}

static Node *map_insert(Map *m, Key k);

static void map_grow(Map *m) {
    Map bigger = {calloc(m->cap * 2, sizeof(Node)), m->cap * 2, 0};
    for (size_t i = 0; i < m->cap; i++)
        if (m->slots[i].used) *map_insert(&bigger, m->slots[i].key) = m->slots[i];
    free(m->slots);
    *m = bigger;
}

static Node *map_insert(Map *m, Key k) {
    if ((m->count + 1) * 2 > m->cap) map_grow(m);
    size_t i = key_hash(k) & (m->cap - 1);
    while (m->slots[i].used) i = (i + 1) & (m->cap - 1);
    m->count++;
    m->slots[i].used = 1;
    m->slots[i].key = k;
    return &m->slots[i];
}

/* ---------- one phase ---------- */

/* Returns number of moves written to out, or -1 if the phase failed. */
static int solve_phase(const CubieCube *cur, int phase, Move *out, int max) {
    CubieCube goal;
    cubie_reset(&goal);
    Key start = phase_id(cur, phase), target = phase_id(&goal, phase);
    if (key_eq(start, target)) return 0;

    Map map = {calloc(1 << 12, sizeof(Node)), 1 << 12, 0};
    size_t qcap = 1 << 12, qhead = 0, qtail = 0;
    CubieCube *queue = malloc(qcap * sizeof *queue);
    int result = -1;

    Node *n = map_insert(&map, start);
    n->dir = 1;
    n = map_insert(&map, target);
    n->dir = 2;
    queue[qtail++] = *cur;
    queue[qtail++] = goal;

    while (qhead < qtail && result < 0) {
        CubieCube old = queue[qhead++];
        Key old_id = phase_id(&old, phase);
        int dir = map_find(&map, old_id)->dir;
        for (int mi = 0; mi < NUM_FACE_MOVES && result < 0; mi++) {
            Move m = (Move)mi;
            if (!move_allowed(phase, m)) continue;
            CubieCube next;
            cubie_multiply(&old, cubie_move(m), &next);
            Key new_id = phase_id(&next, phase);
            Node *found = map_find(&map, new_id);
            if (found && found->dir != dir) {
                /* the two searches met: forward path, bridge move, backward path */
                Key fwd = dir == 1 ? old_id : new_id, bwd = dir == 1 ? new_id : old_id;
                Move bridge = dir == 1 ? m : move_inverse(m);
                Move tmp[64];
                int nf = 0, len = 0;
                for (Key k = fwd; !key_eq(k, start);) {
                    Node *e = map_find(&map, k);
                    tmp[nf++] = e->move;
                    k = e->parent;
                }
                if (nf + 1 > max) break;
                for (int i = nf - 1; i >= 0; i--) out[len++] = tmp[i];
                out[len++] = bridge;
                for (Key k = bwd; !key_eq(k, target);) {
                    Node *e = map_find(&map, k);
                    if (len >= max) break;
                    out[len++] = move_inverse(e->move);
                    k = e->parent;
                }
                result = len;
                break;
            }
            if (!found) {
                Node *e = map_insert(&map, new_id);
                e->dir = (uint8_t)dir;
                e->parent = old_id;
                e->move = m;
                if (qtail == qcap) {
                    qcap *= 2;
                    queue = realloc(queue, qcap * sizeof *queue);
                }
                queue[qtail++] = next;
            }
        }
    }
    free(map.slots);
    free(queue);
    return result;
}

const char *solve_thistlethwaite(const Cube *c, Solution *s) {
    static const char *const NAMES[4] = {
        "Phase 1: orient edges (to <U,D,R,L,F2,B2>)",
        "Phase 2: orient corners, E slice (to <U,D,R2,L2,F2,B2>)",
        "Phase 3: slices, tetrads, parity (to half turns)",
        "Phase 4: solve with half turns",
    };
    static const LessonId LESSONS[4] = {
        LESSON_THISTLETHWAITE_1, LESSON_THISTLETHWAITE_2, LESSON_THISTLETHWAITE_3, LESSON_THISTLETHWAITE_4,
    };
    CubieCube cc;
    const char *err = cubie_from_cube(c, &cc);
    if (err) return err;
    for (int phase = 0; phase < 4; phase++) {
        Move moves[64];
        int n = solve_phase(&cc, phase, moves, 64);
        if (n < 0) return "Thistlethwaite: phase search failed";
        solution_begin_step(s, LESSONS[phase], "%s", NAMES[phase]);
        for (int i = 0; i < n; i++) {
            cubie_apply(&cc, moves[i]);
            solution_push(s, NULL, moves[i]);
        }
    }
    return cubie_is_solved(&cc) ? NULL : "Thistlethwaite: cube not solved after phase 4";
}
