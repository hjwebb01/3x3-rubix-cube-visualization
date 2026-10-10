/*
 * Cubie-level model: the permutation and orientation of the 8 corners and 12
 * edges, following Kociemba's conventions. cp[i] is the corner that sits in
 * position i, co[i] its twist (0..2); likewise ep/eo for edges (flip 0..1).
 *
 * Edge orientation is relative to F/B quarter turns; corner orientation is
 * measured by where the U/D sticker points.
 */
#ifndef CUBE_CUBIE_H
#define CUBE_CUBIE_H

#include "cube/cube.h"

enum { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB, NUM_CORNERS };
enum { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR, NUM_EDGES };

typedef struct {
    uint8_t cp[NUM_CORNERS], co[NUM_CORNERS];
    uint8_t ep[NUM_EDGES], eo[NUM_EDGES];
} CubieCube;

/* Facelet indices of each corner/edge position. Sticker 0 is the U/D sticker
 * for corners and U/D edges, and the F/B sticker for middle-layer edges. */
extern const uint8_t CORNER_FACELET[NUM_CORNERS][3];
extern const uint8_t EDGE_FACELET[NUM_EDGES][2];

void cubie_init(void);
void cubie_reset(CubieCube *c);
bool cubie_is_solved(const CubieCube *c);

/* Colors are mapped to faces by the center stickers, so a cube held in any
 * orientation converts relative to its current centers. Returns NULL on
 * success, else an error message. */
const char *cubie_from_cube(const Cube *in, CubieCube *out);
void cubie_to_cube(const CubieCube *in, Cube *out); /* standard colors and centers */

/* out = a followed by b. out must not alias a or b. */
void cubie_multiply(const CubieCube *a, const CubieCube *b, CubieCube *out);
const CubieCube *cubie_move(Move m); /* face moves only (m < NUM_FACE_MOVES) */
void cubie_apply(CubieCube *c, Move m);

/* NULL if the state is reachable from solved, else what is wrong. */
const char *cubie_verify(const CubieCube *c);
int perm_parity(const uint8_t *p, int n);

#endif
