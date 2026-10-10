#include "cube/cubie.h"

#include <string.h>

const uint8_t CORNER_FACELET[NUM_CORNERS][3] = {
    {8, 9, 20},   /* URF: U9 R1 F3 */
    {6, 18, 38},  /* UFL: U7 F1 L3 */
    {0, 36, 47},  /* ULB: U1 L1 B3 */
    {2, 45, 11},  /* UBR: U3 B1 R3 */
    {29, 26, 15}, /* DFR: D3 F9 R7 */
    {27, 44, 24}, /* DLF: D1 L9 F7 */
    {33, 53, 42}, /* DBL: D7 B9 L7 */
    {35, 17, 51}, /* DRB: D9 R9 B7 */
};

const uint8_t EDGE_FACELET[NUM_EDGES][2] = {
    {5, 10},  /* UR */ {7, 19},  /* UF */ {3, 37},  /* UL */ {1, 46},  /* UB */
    {32, 16}, /* DR */ {28, 25}, /* DF */ {30, 43}, /* DL */ {34, 52}, /* DB */
    {23, 12}, /* FR */ {21, 41}, /* FL */ {50, 39}, /* BL */ {48, 14}, /* BR */
};

static CubieCube g_moves[NUM_FACE_MOVES];
static bool g_ready;

void cubie_reset(CubieCube *c) {
    for (int i = 0; i < NUM_CORNERS; i++) c->cp[i] = (uint8_t)i, c->co[i] = 0;
    for (int i = 0; i < NUM_EDGES; i++) c->ep[i] = (uint8_t)i, c->eo[i] = 0;
}

bool cubie_is_solved(const CubieCube *c) {
    CubieCube s;
    cubie_reset(&s);
    return memcmp(c, &s, sizeof s) == 0;
}

void cubie_init(void) {
    if (g_ready) return;
    cube_init();
    for (int m = 0; m < NUM_FACE_MOVES; m++) {
        Cube c;
        cube_reset(&c);
        cube_apply(&c, (Move)m);
        cubie_from_cube(&c, &g_moves[m]);
    }
    g_ready = true;
}

const char *cubie_from_cube(const Cube *in, CubieCube *out) {
    int face_of[256];
    for (int i = 0; i < 256; i++) face_of[i] = -1;
    for (int f = 0; f < NUM_FACES; f++) {
        int col = cube_center(in, f);
        if (face_of[col] >= 0) return "two centers have the same color";
        face_of[col] = f;
    }
    int fc[NUM_FACELETS];
    for (int i = 0; i < NUM_FACELETS; i++) {
        fc[i] = face_of[in->f[i]];
        if (fc[i] < 0) return "a sticker has a color that matches no center";
    }

    for (int i = 0; i < NUM_CORNERS; i++) {
        int ori;
        for (ori = 0; ori < 3; ori++) {
            int f = fc[CORNER_FACELET[i][ori]];
            if (f == FACE_U || f == FACE_D) break;
        }
        if (ori == 3) return "a corner has no top/bottom colored sticker";
        int c1 = fc[CORNER_FACELET[i][(ori + 1) % 3]];
        int c2 = fc[CORNER_FACELET[i][(ori + 2) % 3]];
        int j;
        for (j = 0; j < NUM_CORNERS; j++)
            if (c1 == CORNER_FACELET[j][1] / 9 && c2 == CORNER_FACELET[j][2] / 9) break;
        if (j == NUM_CORNERS) return "a corner has an impossible color combination";
        out->cp[i] = (uint8_t)j;
        out->co[i] = (uint8_t)ori;
    }
    for (int i = 0; i < NUM_EDGES; i++) {
        int a = fc[EDGE_FACELET[i][0]], b = fc[EDGE_FACELET[i][1]];
        int j;
        for (j = 0; j < NUM_EDGES; j++) {
            int ja = EDGE_FACELET[j][0] / 9, jb = EDGE_FACELET[j][1] / 9;
            if (a == ja && b == jb) {
                out->eo[i] = 0;
                break;
            }
            if (a == jb && b == ja) {
                out->eo[i] = 1;
                break;
            }
        }
        if (j == NUM_EDGES) return "an edge has an impossible color combination";
        out->ep[i] = (uint8_t)j;
    }
    return NULL;
}

void cubie_to_cube(const CubieCube *in, Cube *out) {
    for (int f = 0; f < NUM_FACES; f++) out->f[f * 9 + 4] = (uint8_t)f;
    for (int i = 0; i < NUM_CORNERS; i++) {
        int j = in->cp[i], ori = in->co[i];
        for (int n = 0; n < 3; n++)
            out->f[CORNER_FACELET[i][(n + ori) % 3]] = CORNER_FACELET[j][n] / 9;
    }
    for (int i = 0; i < NUM_EDGES; i++) {
        int j = in->ep[i], ori = in->eo[i];
        for (int n = 0; n < 2; n++)
            out->f[EDGE_FACELET[i][(n + ori) % 2]] = EDGE_FACELET[j][n] / 9;
    }
}

void cubie_multiply(const CubieCube *a, const CubieCube *b, CubieCube *out) {
    for (int i = 0; i < NUM_CORNERS; i++) {
        out->cp[i] = a->cp[b->cp[i]];
        out->co[i] = (uint8_t)((a->co[b->cp[i]] + b->co[i]) % 3);
    }
    for (int i = 0; i < NUM_EDGES; i++) {
        out->ep[i] = a->ep[b->ep[i]];
        out->eo[i] = (uint8_t)((a->eo[b->ep[i]] + b->eo[i]) & 1);
    }
}

const CubieCube *cubie_move(Move m) { return &g_moves[m]; }

void cubie_apply(CubieCube *c, Move m) {
    CubieCube t;
    cubie_multiply(c, &g_moves[m], &t);
    *c = t;
}

int perm_parity(const uint8_t *p, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (p[i] > p[j]) s ^= 1;
    return s;
}

const char *cubie_verify(const CubieCube *c) {
    int seen_e[NUM_EDGES] = {0}, seen_c[NUM_CORNERS] = {0}, sum = 0;
    for (int i = 0; i < NUM_EDGES; i++) seen_e[c->ep[i]]++, sum += c->eo[i];
    for (int i = 0; i < NUM_EDGES; i++)
        if (seen_e[i] != 1) return "an edge appears more than once";
    if (sum % 2) return "one edge is flipped (edge orientation parity)";
    sum = 0;
    for (int i = 0; i < NUM_CORNERS; i++) seen_c[c->cp[i]]++, sum += c->co[i];
    for (int i = 0; i < NUM_CORNERS; i++)
        if (seen_c[i] != 1) return "a corner appears more than once";
    if (sum % 3) return "a corner is twisted (corner orientation parity)";
    if (perm_parity(c->cp, NUM_CORNERS) != perm_parity(c->ep, NUM_EDGES))
        return "two pieces are swapped (permutation parity)";
    return NULL;
}

const char *cube_validate(const Cube *c) {
    int count[256] = {0};
    for (int i = 0; i < NUM_FACELETS; i++) count[c->f[i]]++;
    for (int col = 0; col < NUM_COLORS; col++)
        if (count[col] != 9) return "each color must appear exactly 9 times";
    CubieCube cc;
    const char *err = cubie_from_cube(c, &cc);
    return err ? err : cubie_verify(&cc);
}
