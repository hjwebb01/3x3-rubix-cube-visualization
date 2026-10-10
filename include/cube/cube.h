/*
 * Facelet-level cube model.
 *
 * The cube is stored as 54 stickers in the standard URFDLB facelet order
 * (the same layout Kociemba's solver uses):
 *
 *              |U1 U2 U3|
 *              |U4 U5 U6|
 *              |U7 U8 U9|
 *     |L1 L2 L3|F1 F2 F3|R1 R2 R3|B1 B2 B3|
 *     |L4 L5 L6|F4 F5 F6|R4 R5 R6|B4 B5 B6|
 *     |L7 L8 L9|F7 F8 F9|R7 R8 R9|B7 B8 B9|
 *              |D1 D2 D3|
 *              |D4 D5 D6|
 *              |D7 D8 D9|
 *
 * Index = face * 9 + row * 3 + col, faces ordered U R F D L B.
 * Each sticker holds a color (0..5). Every move (face turns, slices, wide
 * moves and whole-cube rotations) is a precomputed sticker permutation that is
 * derived from 3D geometry, so all of them are guaranteed consistent.
 */
#ifndef CUBE_CUBE_H
#define CUBE_CUBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FACE_U, FACE_R, FACE_F, FACE_D, FACE_L, FACE_B, NUM_FACES };

/* Standard WCA color scheme: white top, green front. */
enum { COLOR_WHITE, COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_ORANGE, COLOR_BLUE, NUM_COLORS };

#define NUM_FACELETS 54

typedef struct {
    uint8_t f[NUM_FACELETS];
} Cube;

typedef enum {
    MB_U, MB_R, MB_F, MB_D, MB_L, MB_B, /* face turns (same order as faces) */
    MB_M, MB_E, MB_S,                   /* slices: M follows L, E follows D, S follows F */
    MB_X, MB_Y, MB_Z,                   /* rotations: x follows R, y follows U, z follows F */
    MB_UW, MB_RW, MB_FW, MB_DW, MB_LW, MB_BW, /* wide turns: u r f d l b */
    MB_COUNT
} MoveBase;

/* A move is base * 3 + (quarter turns - 1): R = 1 turn, R2 = 2, R' = 3. */
typedef uint8_t Move;
#define MOVE(base, turns) ((Move)((base) * 3 + (turns) - 1))
#define MOVE_BASE(m) ((m) / 3)
#define MOVE_TURNS(m) ((m) % 3 + 1)
#define NUM_MOVES (MB_COUNT * 3)
#define NUM_FACE_MOVES 18 /* moves 0..17 are the outer face turns */

/* Builds the move tables. Safe to call more than once; call before anything else. */
void cube_init(void);

void cube_reset(Cube *c);
void cube_apply(Cube *c, Move m);
void cube_apply_seq(Cube *c, const Move *seq, int n);
bool cube_is_solved(const Cube *c); /* every face a single color (any orientation) */
bool cube_equal(const Cube *a, const Cube *b);
static inline int cube_center(const Cube *c, int face) { return c->f[face * 9 + 4]; }

Move move_inverse(Move m);
bool move_is_rotation(Move m);
/* The move that does the same thing as m, performed with the cube held after y^k
 * (k = 1 means "treat the R face as the front"). */
Move move_remap_y(Move m, int k);
const char *move_name(Move m);

/* Parses standard notation ("R U R' U'", "Rw2", "M'", "x y2", "(R U)"). Returns
 * the number of moves, or -1 on a syntax error / overflow. */
int moves_parse(const char *s, Move *out, int max);
/* Writes space-separated notation. Returns the number of chars written. */
int moves_format(const Move *seq, int n, char *buf, size_t size);
/* Writes the inverse sequence into out (out must not alias seq). */
void moves_invert(const Move *seq, int n, Move *out);

/* Geometry: cubie position (each coordinate -1..1, x=R, y=U, z=F) and outward
 * normal of a sticker. */
void facelet_geometry(int idx, int pos[3], int normal[3]);
/* Rotation of a move: axis (0=x,1=y,2=z), mask of affected layers along that axis
 * (bit 0 = -1, bit 1 = 0, bit 2 = +1) and signed quarter turns about +axis. */
void move_geometry(Move m, int *axis, int *layer_mask, int *quarter_turns);
bool move_affects(Move m, const int pos[3]);

uint32_t rng_next(uint32_t *state);
/* Random-move scramble that never turns the same face twice in a row. */
void scramble_generate(Move *out, int n, uint32_t *rng);

/* 54-char facelet string. Accepts face letters (URFDLB) or color letters (WRGYOB). */
int cube_from_string(Cube *c, const char *s);
void cube_to_string(const Cube *c, char out[NUM_FACELETS + 1]); /* face letters */

/* NULL if the cube is a legal, solvable state; otherwise a description of the
 * problem. Implemented in cubie.c. */
const char *cube_validate(const Cube *c);

extern const char *const COLOR_NAMES[NUM_COLORS];

#endif
