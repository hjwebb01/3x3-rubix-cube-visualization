/*
 * Beginner layer-by-layer method, done the way it is usually taught:
 *
 *   1. White cross          5. Yellow edges   (R U R' U R U2 R' U)
 *   2. White corners        6. Position yellow corners (U R U' L' U R' U' L)
 *   3. Middle layer edges   7. Orient yellow corners   (R' D' R D)
 *   4. Yellow cross (F R U R' U' F')
 *
 * The cube is first turned so white is on the bottom. Algorithms are always
 * written for the front face and remapped with y-conjugation, so the output
 * contains no extra cube rotations.
 */
#include "solvers/solvers.h"

static const int SIDES[4] = {FACE_F, FACE_R, FACE_B, FACE_L};
static const int D_EDGE[NUM_FACES] = {[FACE_F] = DF, [FACE_R] = DR, [FACE_B] = DB, [FACE_L] = DL};
static const int U_EDGE[NUM_FACES] = {[FACE_F] = UF, [FACE_R] = UR, [FACE_B] = UB, [FACE_L] = UL};

static const char *const RIGHT_INSERT = "U R U' R' U' F' U F";
static const char *const LEFT_INSERT = "U' L' U L U F U' F'";

static int face_with_center(const Cube *c, int color) {
    for (int f = 0; f < NUM_FACES; f++)
        if (cube_center(c, f) == color) return f;
    return -1;
}

static bool cross_prefix_ok(const Cube *c, int n) {
    for (int j = 0; j < n; j++)
        if (!edge_solved_at(c, D_EDGE[SIDES[j]])) return false;
    return true;
}

/* Number of U turns that brings the given piece (via finder) to `pos`. */
static int auf_edge_to(const Cube *c, int a, int b, int pos) {
    Cube t = *c;
    for (int k = 0; k < 4; k++) {
        if (find_edge(&t, a, b).pos == pos) return k;
        cube_apply(&t, MOVE(MB_U, 1));
    }
    return -1;
}

static int auf_corner_to(const Cube *c, int a, int b, int d, int pos) {
    Cube t = *c;
    for (int k = 0; k < 4; k++) {
        if (find_corner(&t, a, b, d).pos == pos) return k;
        cube_apply(&t, MOVE(MB_U, 1));
    }
    return -1;
}

static void push_u(Solution *s, Cube *c, int k) {
    if (k > 0) solution_push(s, c, MOVE(MB_U, k));
}

static const char *white_cross(Cube *c, Solution *s) {
    int white = cube_center(c, FACE_D);
    for (int i = 0; i < 4; i++) {
        int side = SIDES[i], col = cube_center(c, side), target = D_EDGE[side];
        for (int iter = 0;; iter++) {
            if (iter > 8) return "beginner cross: edge did not converge";
            PieceLoc e = find_edge(c, white, col);
            if (e.pos == target && e.ori == 0) break;

            if (e.pos >= DR && e.pos <= DB) {
                /* in the bottom layer but wrong: flip it up to the top */
                solution_push(s, c, MOVE(EDGE_FACELET[e.pos][1] / 9, 2));
            } else if (e.pos >= FR) {
                /* middle layer: X U X' lifts it without disturbing the cross */
                bool lifted = false;
                for (int a = 0; a < 2 && !lifted; a++) {
                    int face = EDGE_FACELET[e.pos][a] / 9;
                    for (int t = 1; t <= 3 && !lifted; t += 2) {
                        Move seq[3] = {MOVE(face, t), MOVE(MB_U, 1), MOVE(face, 4 - t)};
                        Cube tmp = *c;
                        cube_apply_seq(&tmp, seq, 3);
                        if (find_edge(&tmp, white, col).pos <= UB && cross_prefix_ok(&tmp, i)) {
                            solution_push_seq(s, c, seq, 3);
                            lifted = true;
                        }
                    }
                }
                if (!lifted) return "beginner cross: could not lift a middle edge";
            } else {
                /* top layer: turn U until it is above its center, then insert */
                push_u(s, c, auf_edge_to(c, white, col, U_EDGE[side]));
                e = find_edge(c, white, col);
                if (e.ori == 0)
                    solution_push(s, c, MOVE(side, 2)); /* white faces up */
                else
                    push_alg(s, c, "U' R' F R", slot_of_face(side)); /* white faces the side */
            }
        }
    }
    return NULL;
}

static int slot_of_dcorner(int pos) {
    for (int k = 0; k < 4; k++)
        if (SLOT_DCORNER[k] == pos) return k;
    return 0;
}

static int slot_of_edge(int pos) {
    for (int k = 0; k < 4; k++)
        if (SLOT_EDGE[k] == pos) return k;
    return 0;
}

static const char *white_corners(Cube *c, Solution *s) {
    int white = cube_center(c, FACE_D);
    for (int slot = 0; slot < 4; slot++) {
        int a = cube_center(c, SLOT_FRONT[slot]), b = cube_center(c, SLOT_RIGHT[slot]);
        for (int iter = 0;; iter++) {
            if (iter > 6) return "beginner corners: did not converge";
            PieceLoc p = find_corner(c, white, a, b);
            if (p.pos == SLOT_DCORNER[slot] && p.ori == 0) break;
            if (p.pos >= DFR) {
                /* stuck in the bottom layer: pop it out to the top */
                push_alg(s, c, "R U R'", slot_of_dcorner(p.pos));
                continue;
            }
            push_u(s, c, auf_corner_to(c, white, a, b, SLOT_UCORNER[slot]));
            for (int n = 0; n < 6 && !corner_solved_at(c, SLOT_DCORNER[slot]); n++)
                push_alg(s, c, "R U R' U'", slot);
        }
    }
    return NULL;
}

static const char *middle_layer(Cube *c, Solution *s) {
    for (int slot = 0; slot < 4; slot++) {
        int a = cube_center(c, SLOT_FRONT[slot]), b = cube_center(c, SLOT_RIGHT[slot]);
        for (int iter = 0;; iter++) {
            if (iter > 6) return "beginner middle layer: did not converge";
            PieceLoc e = find_edge(c, a, b);
            if (e.pos == SLOT_EDGE[slot] && edge_solved_at(c, e.pos)) break;
            if (e.pos >= FR) {
                /* in the wrong slot (or flipped): replace it with a top edge */
                push_alg(s, c, RIGHT_INSERT, slot_of_edge(e.pos));
                continue;
            }
            int side_col = c->f[EDGE_FACELET[e.pos][1]];
            int top_col = c->f[EDGE_FACELET[e.pos][0]];
            int front = face_with_center(c, side_col);
            push_u(s, c, auf_edge_to(c, a, b, U_EDGE[front]));
            int k = slot_of_face(front);
            if (top_col == cube_center(c, SLOT_RIGHT[k]))
                push_alg(s, c, RIGHT_INSERT, k);
            else
                push_alg(s, c, LEFT_INSERT, k);
        }
    }
    return NULL;
}

static bool edge_up(const Cube *c, int e) { return c->f[EDGE_FACELET[e][0]] == cube_center(c, FACE_U); }

static const char *yellow_cross(Cube *c, Solution *s) {
    static const char *const ALG = "F R U R' U' F'";
    for (int iter = 0;; iter++) {
        if (iter > 6) return "beginner yellow cross: did not converge";
        int n = edge_up(c, UR) + edge_up(c, UF) + edge_up(c, UL) + edge_up(c, UB);
        if (n == 4) return NULL;
        if (n == 2 && edge_up(c, UF) && edge_up(c, UB)) {
            push_u(s, c, 1); /* vertical line -> horizontal */
        } else if (n == 2 && !(edge_up(c, UL) && edge_up(c, UR))) {
            /* "L" shape: hold it in the back-left */
            Cube t = *c;
            int k;
            for (k = 0; k < 4 && !(edge_up(&t, UB) && edge_up(&t, UL)); k++) cube_apply(&t, MOVE(MB_U, 1));
            push_u(s, c, k);
        }
        push_alg(s, c, ALG, 0);
    }
}

static bool goal_ll_edges(const Cube *c) {
    for (int e = UR; e <= UB; e++)
        if (!edge_solved_at(c, e)) return false;
    return true;
}

static bool corner_placed(const Cube *c, int pos) {
    int have = 0, want = 0;
    for (int k = 0; k < 3; k++) {
        have |= 1 << c->f[CORNER_FACELET[pos][k]];
        want |= 1 << cube_center(c, CORNER_FACELET[pos][k] / 9);
    }
    return have == want;
}

static bool goal_ll_corners_placed(const Cube *c) {
    for (int p = URF; p <= UBR; p++)
        if (!corner_placed(c, p)) return false;
    return goal_ll_edges(c);
}

static const char *yellow_edges(Cube *c, Solution *s) {
    Macro m[7];
    macro_make(&m[0], "U", 0, true);
    macro_make(&m[1], "U2", 0, true);
    macro_make(&m[2], "U'", 0, true);
    for (int k = 0; k < 4; k++) macro_make(&m[3 + k], "R U R' U R U2 R' U", k, false);
    return macro_search(c, s, m, 7, goal_ll_edges, 5) ? NULL : "beginner yellow edges: no solution";
}

static const char *place_yellow_corners(Cube *c, Solution *s) {
    Macro m[4];
    for (int k = 0; k < 4; k++) macro_make(&m[k], "U R U' L' U R' U' L", k, false);
    return macro_search(c, s, m, 4, goal_ll_corners_placed, 3) ? NULL : "beginner corner placement: no solution";
}

static const char *orient_yellow_corners(Cube *c, Solution *s) {
    int yellow = cube_center(c, FACE_U);
    for (int i = 0; i < 4; i++) {
        for (int n = 0; c->f[CORNER_FACELET[URF][0]] != yellow; n++) {
            if (n >= 3) return "beginner corner orientation: did not converge";
            push_alg(s, c, "R' D' R D R' D' R D", 0);
        }
        solution_push(s, c, MOVE(MB_U, 1));
    }
    Macro m[3];
    macro_make(&m[0], "U", 0, true);
    macro_make(&m[1], "U2", 0, true);
    macro_make(&m[2], "U'", 0, true);
    return macro_search(c, s, m, 3, cube_is_solved, 1) ? NULL : "beginner: last layer not solved";
}

const char *solve_beginner(const Cube *in, Solution *s) {
    Cube c = *in;
    const char *err;
    if (cube_center(&c, FACE_D) != COLOR_WHITE) {
        solution_begin_step(s, "Hold white on the bottom");
        orient_white_down(&c, s);
    }
    solution_begin_step(s, "White cross");
    if ((err = white_cross(&c, s))) return err;
    solution_begin_step(s, "White corners");
    if ((err = white_corners(&c, s))) return err;
    solution_begin_step(s, "Middle layer edges");
    if ((err = middle_layer(&c, s))) return err;
    solution_begin_step(s, "Yellow cross");
    if ((err = yellow_cross(&c, s))) return err;
    solution_begin_step(s, "Yellow edges");
    if ((err = yellow_edges(&c, s))) return err;
    solution_begin_step(s, "Position yellow corners");
    if ((err = place_yellow_corners(&c, s))) return err;
    solution_begin_step(s, "Orient yellow corners");
    return orient_yellow_corners(&c, s);
}
