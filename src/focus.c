/*
 * X-ray: which pieces each kind of Step works on, and how much each sticker
 * matters while it plays. The human methods solve white first, on D, so the
 * Focus of a lesson is written in terms of white and yellow pieces.
 */
#include "cube/solver.h"

#include "solvers/solvers.h"

#define W (1u << COLOR_WHITE)
#define Y (1u << COLOR_YELLOW)

/* Pieces with n stickers (2 edges, 3 corners, 0 both) showing every color in
 * `with` and none in `without`. Color sets that are no real piece (white with
 * yellow) can be included: no sticker ever matches them. */
static uint64_t pieces(int n, unsigned with, unsigned without) {
    uint64_t set = 0;
    for (unsigned k = 1; k < 64; k++) {
        int size = __builtin_popcount(k);
        if (size < 2 || (n && size != n) || (k & with) != with || (k & without)) continue;
        set |= 1ull << k;
    }
    return set;
}

Focus lesson_focus(LessonId lesson) {
    switch (lesson) {
    case LESSON_HOLD_WHITE:
    case LESSON_INSPECTION: return (Focus){1ull << W, -1}; /* only the centers */
    case LESSON_WHITE_CROSS:
    case LESSON_CROSS: return (Focus){pieces(2, W, 0), -1};
    case LESSON_WHITE_CORNERS: return (Focus){pieces(3, W, 0), -1};
    case LESSON_MIDDLE_EDGES: return (Focus){pieces(2, 0, W | Y), -1};
    case LESSON_F2L: return (Focus){pieces(3, W, 0) | pieces(2, 0, W | Y), -1}; /* cfop.c narrows it to the pair */
    case LESSON_YELLOW_CROSS: return (Focus){pieces(2, Y, 0), COLOR_YELLOW};
    case LESSON_YELLOW_EDGES: return (Focus){pieces(2, Y, 0), -1};
    case LESSON_POSITION_YELLOW_CORNERS: return (Focus){pieces(3, Y, 0), -1};
    case LESSON_ORIENT_YELLOW_CORNERS: return (Focus){pieces(3, Y, 0), COLOR_YELLOW};
    case LESSON_OLL:
    case LESSON_OLL_SKIP: return (Focus){pieces(0, Y, 0), COLOR_YELLOW};
    case LESSON_PLL:
    case LESSON_PLL_SKIP: return (Focus){pieces(0, Y, 0), -1};
    default: return (Focus){0, -1};
    }
}

Focus focus_slot_pair(int a, int b, int d) {
    unsigned edge = 1u << a | 1u << b;
    return (Focus){1ull << edge | 1ull << (edge | 1u << d), -1};
}

static unsigned colors_of(const Cube *c, const uint8_t *facelets, int n) {
    unsigned k = 0;
    for (int i = 0; i < n; i++) k |= 1u << c->f[facelets[i]];
    return k;
}

/* Grades the n stickers of one piece. */
static void grade(Focus f, uint64_t done, const Cube *now, const uint8_t *facelets, int n, uint8_t *out) {
    unsigned k = colors_of(now, facelets, n);
    for (int i = 0; i < n; i++) {
        int idx = facelets[i];
        if ((f.pieces >> k & 1) && (f.color < 0 || now->f[idx] == f.color))
            out[idx] = XRAY_FOCUS;
        else
            out[idx] = (done >> k & 1) ? XRAY_DONE : XRAY_FADED;
    }
}

void focus_xray(Focus f, const Cube *step_start, const Cube *now, uint8_t out[NUM_FACELETS]) {
    for (int i = 0; i < NUM_FACELETS; i++) out[i] = XRAY_FOCUS;
    if (!f.pieces) return;
    uint64_t done = 0;
    for (int p = 0; p < NUM_CORNERS; p++)
        if (corner_solved_at(step_start, p)) done |= 1ull << colors_of(step_start, CORNER_FACELET[p], 3);
    for (int p = 0; p < NUM_EDGES; p++)
        if (edge_solved_at(step_start, p)) done |= 1ull << colors_of(step_start, EDGE_FACELET[p], 2);
    for (int p = 0; p < NUM_CORNERS; p++) grade(f, done, now, CORNER_FACELET[p], 3, out);
    for (int p = 0; p < NUM_EDGES; p++) grade(f, done, now, EDGE_FACELET[p], 2, out);
}
