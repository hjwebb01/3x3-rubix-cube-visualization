#include <string.h>

#include "cube/solver.h"

/* Algs people memorize as units, first match wins, so longer and step-specific entries come first. Written
 * for the front slot; every y-turn of each is tried, so the other slots and sides match too. A lesson
 * limits an entry to steps that teach it: R U R' is "take out corner" in the beginner corners but an F2L
 * trigger in CFOP. The white cross moves have no standard name, so they are described. */
static const struct {
    const char *name, *alg;
    LessonId lesson; /* LESSON_NONE = any step */
} CHUNKS[] = {
    {"Yellow cross alg x3", "F R U R' U' R U R' U' R U R' U' F'"}, /* repeats merge: F' F cancels */
    {"Yellow cross alg x2", "F R U R' U' R U R' U' F'"},
    {"Right edge insert", "U R U' R' U' F' U F"},
    {"Left edge insert", "U' L' U L U F U' F'"},
    {"Corner swap (Niklas)", "U R U' L' U R' U' L"},
    {"Sune", "R U R' U R U2 R'"},
    {"Antisune", "R U2 R' U' R U' R'"},
    {"Yellow cross alg", "F R U R' U' F'"},
    {"Yellow cross alg", "F U R U' R' F'"},
    {"Flipped edge insert", "U' R' F R", LESSON_WHITE_CROSS},
    {"Lift edge out", "R U R'", LESSON_WHITE_CROSS},
    {"Lift edge out", "R' U R", LESSON_WHITE_CROSS},
    {"Sexy move (righty)", "R U R' U'"},
    {"Left sexy move (lefty)", "L' U' L U"},
    {"Sledgehammer", "R' F R F'"},
    {"Hedgeslammer", "F R' F' R"},
    {"Corner twist", "R' D' R D"},
    {"Take out corner", "R U R'", LESSON_WHITE_CORNERS},
    {"Right trigger", "R U R'", LESSON_F2L},
    {"Right trigger", "R U' R'", LESSON_F2L},
    {"Right trigger", "R U2 R'", LESSON_F2L},
    {"Left trigger", "L' U' L", LESSON_F2L},
    {"Left trigger", "L' U L", LESSON_F2L},
    {"Left trigger", "L' U2 L", LESSON_F2L},
};
#define NCHUNKS ((int)(sizeof CHUNKS / sizeof CHUNKS[0]))

static Move seq[NCHUNKS][4][16];
static int seq_n[NCHUNKS];

/* Moves chunk c covers at m (0 if it does not match). A leading U merges with the setup U before it
 * (U2 + U -> U', U' + U -> nothing), so the alg may start with any U turn or none. When two algs run
 * back to back their touching turns merge too (L + L -> L2): merged_head/merged_tail compare that end
 * move by face only, and the merged move then belongs to both. */
static int chunk_match(const Move *m, int avail, int c, bool merged_head, bool merged_tail) {
    int len = seq_n[c], lead = MOVE_BASE(seq[c][0][0]) == MB_U;
    int has_u = lead && !merged_head && MOVE_BASE(m[0]) == MB_U, n = len - lead + has_u;
    if (n > avail) return 0;
    for (int k = 0; k < 4; k++) {
        int j = lead;
        for (; j < len; j++) {
            Move want = seq[c][k][j], got = m[has_u + j - lead];
            bool loose = (j == lead && merged_head) || (j == len - 1 && merged_tail);
            if (loose ? MOVE_BASE(want) != MOVE_BASE(got) : want != got) break;
        }
        if (j == len) return n;
    }
    return 0;
}

int solution_chunks(const Solution *s, int step, Chunk *out, int max) {
    if (step < 0 || step >= s->nsteps || max <= 0) return 0;
    if (s->steps[step].lesson >= LESSON_THISTLETHWAITE_1) return 0;
    static bool ready;
    if (!ready) {
        for (int c = 0; c < NCHUNKS; c++) {
            Move base[16];
            seq_n[c] = moves_parse(CHUNKS[c].alg, base, 16);
            for (int k = 0; k < 4; k++)
                for (int i = 0; i < seq_n[c]; i++) seq[c][k][i] = move_remap_y(base[i], k);
        }
        ready = true;
    }
    const SolveStep *st = &s->steps[step];
    int end = st->start + st->count, found = 0;
    bool head = false; /* the move at i is shared with the chunk before it */
    for (int i = st->start; i < end && found < max;) {
        int c = 0, n = 0;
        for (; c < NCHUNKS && !n; c++)
            if (!CHUNKS[c].lesson || CHUNKS[c].lesson == st->lesson) n = chunk_match(&s->moves[i], end - i, c, head, false);
        if (n) {
            out[found++] = (Chunk){i, n, CHUNKS[c - 1].name};
            i += n, head = false;
            continue;
        }
        /* no exact match: an alg whose last move merged into the next alg's first */
        for (c = 0; c < NCHUNKS && !n; c++) {
            if (CHUNKS[c].lesson && CHUNKS[c].lesson != st->lesson) continue;
            n = chunk_match(&s->moves[i], end - i, c, head, true);
            bool next = false;
            for (int d = 0; n && d < NCHUNKS && !next; d++)
                next = (!CHUNKS[d].lesson || CHUNKS[d].lesson == st->lesson) &&
                       chunk_match(&s->moves[i + n - 1], end - i - n + 1, d, true, false);
            if (!next) n = 0;
        }
        if (n) {
            out[found++] = (Chunk){i, n, CHUNKS[c - 1].name};
            i += n - 1, head = true;
        } else {
            i++, head = false;
        }
    }
    return found;
}
