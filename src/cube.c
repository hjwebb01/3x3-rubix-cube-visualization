#include "cube/cube.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *const COLOR_NAMES[NUM_COLORS] = {"white", "red", "green", "yellow", "orange", "blue"};

typedef struct {
    int8_t axis; /* 0 = x, 1 = y, 2 = z */
    int8_t mask; /* affected layers along the axis: bit0 = -1, bit1 = 0, bit2 = +1 */
    int8_t dir;  /* quarter turns about +axis for one clockwise turn */
} BaseSpec;

/* Clockwise when looking at a face from outside is a negative rotation about
 * its outward normal. */
static const BaseSpec BASE_SPEC[MB_COUNT] = {
    [MB_U] = {1, 4, -1},  [MB_R] = {0, 4, -1},  [MB_F] = {2, 4, -1},
    [MB_D] = {1, 1, 1},   [MB_L] = {0, 1, 1},   [MB_B] = {2, 1, 1},
    [MB_M] = {0, 2, 1},   [MB_E] = {1, 2, 1},   [MB_S] = {2, 2, -1},
    [MB_X] = {0, 7, -1},  [MB_Y] = {1, 7, -1},  [MB_Z] = {2, 7, -1},
    [MB_UW] = {1, 6, -1}, [MB_RW] = {0, 6, -1}, [MB_FW] = {2, 6, -1},
    [MB_DW] = {1, 3, 1},  [MB_LW] = {0, 3, 1},  [MB_BW] = {2, 3, 1},
};

static const char *const BASE_CHARS = "URFDLBMESxyzurfdlb";

static uint8_t g_perm[NUM_MOVES][NUM_FACELETS]; /* new[j] = old[g_perm[m][j]] */
static Move g_remap_y[4][NUM_MOVES];
static char g_names[NUM_MOVES][4];
static bool g_ready;

void facelet_geometry(int idx, int pos[3], int n[3]) {
    int face = idx / 9, r = (idx % 9) / 3, c = idx % 3;
    n[0] = n[1] = n[2] = 0;
    switch (face) {
    case FACE_U: pos[0] = c - 1; pos[1] = 1;     pos[2] = r - 1; n[1] = 1;  break;
    case FACE_R: pos[0] = 1;     pos[1] = 1 - r; pos[2] = 1 - c; n[0] = 1;  break;
    case FACE_F: pos[0] = c - 1; pos[1] = 1 - r; pos[2] = 1;     n[2] = 1;  break;
    case FACE_D: pos[0] = c - 1; pos[1] = -1;    pos[2] = 1 - r; n[1] = -1; break;
    case FACE_L: pos[0] = -1;    pos[1] = 1 - r; pos[2] = c - 1; n[0] = -1; break;
    default:     pos[0] = 1 - c; pos[1] = 1 - r; pos[2] = -1;    n[2] = -1; break;
    }
}

/* +90 degree (counter-clockwise, right-handed) rotation about an axis. */
static void rot90(int v[3], int axis) {
    int x = v[0], y = v[1], z = v[2];
    switch (axis) {
    case 0: v[1] = -z; v[2] = y; break;
    case 1: v[0] = z;  v[2] = -x; break;
    default: v[0] = -y; v[1] = x; break;
    }
}

static int facelet_at(const int pos[3], const int n[3]) {
    for (int i = 0; i < NUM_FACELETS; i++) {
        int p[3], m[3];
        facelet_geometry(i, p, m);
        if (memcmp(p, pos, sizeof p) == 0 && memcmp(m, n, sizeof m) == 0) return i;
    }
    return -1;
}

void move_geometry(Move m, int *axis, int *layer_mask, int *quarter_turns) {
    const BaseSpec *s = &BASE_SPEC[MOVE_BASE(m)];
    int t = MOVE_TURNS(m);
    *axis = s->axis;
    *layer_mask = s->mask;
    *quarter_turns = t == 2 ? 2 * s->dir : (t == 1 ? s->dir : -s->dir);
}

bool move_affects(Move m, const int pos[3]) {
    const BaseSpec *s = &BASE_SPEC[MOVE_BASE(m)];
    return (s->mask >> (pos[s->axis] + 1)) & 1;
}

void cube_init(void) {
    if (g_ready) return;
    for (int m = 0; m < NUM_MOVES; m++) {
        const BaseSpec *s = &BASE_SPEC[MOVE_BASE(m)];
        int q = ((s->dir * MOVE_TURNS(m)) % 4 + 4) % 4;
        for (int i = 0; i < NUM_FACELETS; i++) {
            int pos[3], n[3];
            facelet_geometry(i, pos, n);
            if (move_affects((Move)m, pos)) {
                for (int k = 0; k < q; k++) {
                    rot90(pos, s->axis);
                    rot90(n, s->axis);
                }
            }
            g_perm[m][facelet_at(pos, n)] = (uint8_t)i;
        }
        static const char *const SUFFIX[3] = {"", "2", "'"};
        snprintf(g_names[m], sizeof g_names[m], "%c%s", BASE_CHARS[MOVE_BASE(m)], SUFFIX[m % 3]);
    }

    /* y-conjugation table: match the permutation of y^k m y^-k against every move. */
    Cube ident, cand[NUM_MOVES];
    for (int i = 0; i < NUM_FACELETS; i++) ident.f[i] = (uint8_t)i;
    for (int m = 0; m < NUM_MOVES; m++) {
        cand[m] = ident;
        cube_apply(&cand[m], (Move)m);
    }
    for (int k = 0; k < 4; k++) {
        for (int m = 0; m < NUM_MOVES; m++) {
            Cube t = ident;
            for (int i = 0; i < k; i++) cube_apply(&t, MOVE(MB_Y, 1));
            cube_apply(&t, (Move)m);
            for (int i = 0; i < k; i++) cube_apply(&t, MOVE(MB_Y, 3));
            g_remap_y[k][m] = (Move)m;
            for (int j = 0; j < NUM_MOVES; j++) {
                if (cube_equal(&t, &cand[j])) {
                    g_remap_y[k][m] = (Move)j;
                    break;
                }
            }
        }
    }
    g_ready = true;
}

void cube_reset(Cube *c) {
    for (int i = 0; i < NUM_FACELETS; i++) c->f[i] = (uint8_t)(i / 9);
}

void cube_apply(Cube *c, Move m) {
    Cube t;
    const uint8_t *p = g_perm[m];
    for (int j = 0; j < NUM_FACELETS; j++) t.f[j] = c->f[p[j]];
    *c = t;
}

void cube_apply_seq(Cube *c, const Move *seq, int n) {
    for (int i = 0; i < n; i++) cube_apply(c, seq[i]);
}

bool cube_is_solved(const Cube *c) {
    for (int f = 0; f < NUM_FACES; f++)
        for (int i = 0; i < 9; i++)
            if (c->f[f * 9 + i] != c->f[f * 9 + 4]) return false;
    return true;
}

bool cube_equal(const Cube *a, const Cube *b) { return memcmp(a->f, b->f, NUM_FACELETS) == 0; }

Move move_inverse(Move m) { return MOVE(MOVE_BASE(m), 4 - MOVE_TURNS(m)); }

bool move_is_rotation(Move m) {
    int b = MOVE_BASE(m);
    return b == MB_X || b == MB_Y || b == MB_Z;
}

Move move_remap_y(Move m, int k) { return g_remap_y[((k % 4) + 4) % 4][m]; }

const char *move_name(Move m) { return m < NUM_MOVES ? g_names[m] : "?"; }

int moves_parse(const char *s, Move *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char ch = *p;
        if (isspace(ch) || strchr("()[],.", ch)) {
            p++;
            continue;
        }
        const char *hit = strchr(BASE_CHARS, ch);
        int base;
        if (ch == 'X' || ch == 'Y' || ch == 'Z')
            base = MB_X + (ch - 'X');
        else if (hit)
            base = (int)(hit - BASE_CHARS);
        else
            return -1;
        p++;
        if (base <= MB_B && *p == 'w') {
            base += MB_UW;
            p++;
        }
        int turns = 1;
        if (*p == '2') {
            turns = 2;
            p++;
        } else if (*p == '3') {
            turns = 3;
            p++;
        }
        if (*p == '\'') {
            turns = 4 - turns;
            p++;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0x99) { /* typographic ’ */
            turns = 4 - turns;
            p += 3;
        }
        if (n >= max) return -1;
        out[n++] = MOVE(base, turns);
    }
    return n;
}

int moves_format(const Move *seq, int n, char *buf, size_t size) {
    size_t len = 0;
    if (size) buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        int w = snprintf(buf + len, size > len ? size - len : 0, "%s%s", i ? " " : "", move_name(seq[i]));
        if (w < 0 || (size_t)w >= size - len) break;
        len += (size_t)w;
    }
    return (int)len;
}

void moves_invert(const Move *seq, int n, Move *out) {
    for (int i = 0; i < n; i++) out[i] = move_inverse(seq[n - 1 - i]);
}

uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

void scramble_generate(Move *out, int n, uint32_t *rng) {
    int last = -1, last2 = -1;
    for (int i = 0; i < n; i++) {
        int f;
        for (;;) {
            f = (int)(rng_next(rng) % 6);
            if (f == last) continue;
            /* avoid things like U D U: three turns on the same axis */
            if (last >= 0 && last2 >= 0 && f % 3 == last % 3 && last2 % 3 == f % 3) continue;
            break;
        }
        out[i] = MOVE(f, (int)(rng_next(rng) % 3) + 1);
        last2 = last;
        last = f;
    }
}

int cube_from_string(Cube *c, const char *s) {
    if (strlen(s) != NUM_FACELETS) return -1;
    for (int i = 0; i < NUM_FACELETS; i++) {
        switch (toupper((unsigned char)s[i])) {
        case 'U': case 'W': c->f[i] = COLOR_WHITE; break;
        case 'R': c->f[i] = COLOR_RED; break;
        case 'F': case 'G': c->f[i] = COLOR_GREEN; break;
        case 'D': case 'Y': c->f[i] = COLOR_YELLOW; break;
        case 'L': case 'O': c->f[i] = COLOR_ORANGE; break;
        case 'B': c->f[i] = COLOR_BLUE; break;
        default: return -1;
        }
    }
    return 0;
}

void cube_to_string(const Cube *c, char out[NUM_FACELETS + 1]) {
    static const char FACE_CHARS[] = "URFDLB";
    char by_color[256];
    memset(by_color, '?', sizeof by_color);
    for (int f = 0; f < NUM_FACES; f++) by_color[cube_center(c, f)] = FACE_CHARS[f];
    for (int i = 0; i < NUM_FACELETS; i++) out[i] = by_color[c->f[i]];
    out[NUM_FACELETS] = '\0';
}
