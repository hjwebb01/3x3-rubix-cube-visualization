/*
 * Teaching notes for each kind of solve step: what the step achieves, what to
 * look for, and which algorithm to memorize. Indexed by LessonId.
 */
#include "cube/solver.h"

static const char *const LESSONS[LESSON_COUNT] = {
    /* beginner */
    [LESSON_HOLD_WHITE] =
        "Rotate the whole cube so the white center faces down. The centers never move relative to each other, so "
        "they tell you which color each face must become.",
    [LESSON_WHITE_CROSS] =
        "Goal: four white edges on the bottom, each one's side color matching its center. Find a white edge, turn "
        "U until its side color sits above the matching center, then turn that face twice (F2) to drop it in. If "
        "white points sideways instead of up, use U' R' F R. Practice doing this one without algorithms: it is "
        "mostly intuition.",
    [LESSON_WHITE_CORNERS] =
        "Goal: finish the white layer. Find a corner with white on top layer, turn U until it sits above the slot "
        "where its colors belong (front-right, bottom), then repeat R U R' U' until it drops in correctly. It takes "
        "1, 3 or 5 repeats. A corner stuck in the wrong bottom slot is popped out with one R U R'. The solution does each alg from whichever side the slot is on, without "
        "rotating the cube: F U F' U' is R U R' U' done on the front-left slot.",
    [LESSON_MIDDLE_EDGES] =
        "Goal: fill the four middle edges. Find a top edge with no yellow. Turn U so its front color matches the "
        "front center. If its top color matches the right center: U R U' R' U' F' U F. If it matches the left: "
        "U' L' U L U F U' F'. A wrong edge already in the middle is kicked out by inserting any top edge over it.",
    [LESSON_YELLOW_CROSS] =
        "Goal: a yellow cross on top (ignore the corners). You will see a dot, an L or a line. Hold the L in the "
        "back-left, or the line horizontally, then do F R U R' U' F'. A dot needs the alg three times, an L two and "
        "a line one.",
    [LESSON_YELLOW_EDGES] =
        "Goal: the side colors of the cross edges match their centers. Turn U until two edges match. If they are "
        "on adjacent sides, hold them back and right; do R U R' U R U2 R' U. Opposite pairs need it twice.",
    [LESSON_POSITION_YELLOW_CORNERS] =
        "Goal: every top corner in its right spot (twisting is fine for now). Find a corner already in place, hold "
        "it front-right and do U R U' L' U R' U' L until the other three cycle home. If none are in place, do it "
        "once from anywhere first.",
    [LESSON_ORIENT_YELLOW_CORNERS] =
        "Goal: twist the last corners. Hold the cube so an unsolved corner is front-right on top, and repeat "
        "R' D' R D until its yellow faces up. Then turn only U to bring the next unsolved corner there and repeat. "
        "The cube looks scrambled midway: that's normal, don't rotate the cube. Finish with a U turn.",

    /* CFOP */
    [LESSON_INSPECTION] =
        "CFOP solvers start with white on the bottom, so the cross is built where you can't see it and the rest "
        "of the solve happens on top.",
    [LESSON_CROSS] =
        "Same goal as the beginner cross, but planned in inspection and done on the bottom in 8 moves or fewer. "
        "Plan where each edge goes before turning, and solve edges relative to each other, not to the centers.",
    [LESSON_F2L] =
        "First Two Layers: pair a white corner with its matching middle edge in the top layer, then insert the "
        "pair together into its slot. The basic idea: split the pieces apart, join them with R U R' style moves, "
        "then insert. Learn it intuitively first; watch how each pair is built here.",
    [LESSON_OLL_SKIP] = "The top is already all yellow after F2L, so there is no OLL to do. Lucky!",
    [LESSON_OLL] =
        "Orient Last Layer: make the whole top yellow in one algorithm. There are 57 cases. Start with '2-look "
        "OLL': first make the yellow cross with F R U R' U' F', then use one of 7 corner algs (Sune, Antisune, H, "
        "Pi, Headlights, Chameleon, Bowtie). Watch the name on this step and look it up to learn the full alg.",
    [LESSON_PLL_SKIP] = "The last layer is already permuted after OLL; at most a U turn is needed.",
    [LESSON_PLL] =
        "Permute Last Layer: move the yellow pieces to their spots in one algorithm. There are 21 cases. Start "
        "with '2-look PLL': first fix the corners (headlights on one side means A-perm, none means Y/E), then the "
        "edges with the U perms (Ua/Ub), H or Z. Look for 'headlights' (two matching corner stickers on one side).",

    /* computer methods */
    [LESSON_THISTLETHWAITE_1] =
        "Thistlethwaite is a computer method, not one a person can follow. Each phase restricts which moves are "
        "allowed. Phase 1 fixes edge orientation so F and B quarter turns are never needed again.",
    [LESSON_THISTLETHWAITE_2] =
        "Corners get oriented and the E-slice edges are placed in the E slice, so R and L quarter turns are no "
        "longer needed.",
    [LESSON_THISTLETHWAITE_3] =
        "Pieces are moved into their orbits so that only half turns are needed to finish. This also fixes parity.",
    [LESSON_THISTLETHWAITE_4] = "Finish using only half turns (U2, R2, F2...).",
    [LESSON_KOCIEMBA_1] =
        "Kociemba's two-phase algorithm is a computer method used to find near-optimal solutions (about 20 moves). "
        "Phase 1 orients all pieces and puts the middle-slice edges in the middle slice.",
    [LESSON_KOCIEMBA_2] =
        "Phase 2 solves the rest using only U, D and half turns of the sides. A computer searches millions of "
        "positions to pick these moves; don't try to learn them. Use the Beginner method to learn.",
};

StepLesson step_lesson(const SolveStep *st) {
    return (StepLesson){st->lesson < LESSON_COUNT ? LESSONS[st->lesson] : NULL, st->note};
}
