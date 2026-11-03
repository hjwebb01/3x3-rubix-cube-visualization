# 3x3 Rubik's Cube Visualization

A 3x3 cube solver written in plain C. It has four solving methods, a command-line tool and a raylib GUI with an animated 3D cube.

| Method | Idea | Typical length | Time |
|---|---|---|---|
| Beginner | Layer by layer: cross, corners, middle layer, yellow cross, then the last layer | ~165 moves | <1 ms |
| CFOP | Optimal cross, F2L pairs, OLL (57 algs) and PLL (21 algs) | ~60 moves | <1 ms |
| Thistlethwaite | Four nested subgroups, using a bidirectional BFS per phase | ~33 moves | ~4 ms |
| Kociemba | Two-phase IDA* with pruning tables | ~20 moves | ~130 ms |

Each solution is split into named steps, such as "F2L 2: red-green", "OLL 37", "PLL Gb" or "Phase 1". Every solution is checked by replaying it before it is returned.

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build        # or ./build/rubik_tests [scrambles-per-solver]
```

If raylib (version 5 or later) is not installed, CMake downloads raylib 5.5 automatically. To build only the CLI and the tests, use `-DRUBIKS_BUILD_GUI=OFF`.

## Command line

```sh
./build/rubik                               # random scramble, every method
./build/rubik -m cfop -s "R U R' U' F2 D"   # one method, given scramble
./build/rubik -f <54 chars URFDLB/WRGYOB>   # solve a facelet string
./build/rubik --bench 100                   # averages for every method
./build/rubik -m beginner -e                # explain each step and its algorithm
```

## GUI

```sh
./build/rubik_gui
```

- **Scramble / Reset**, pick a **method**, then **Solve**. The solver runs on a background thread.
- **Learn:** a lesson box above the solution explains the current step: its goal, what to look for and the algorithm to memorize. Tick **Pause to explain each step** to pause at the start of every step so you can read it, then press Play to watch the moves.
- **Coach:** press **Coach me on my cube**, match the screen to your cube (do the guided scramble or paint it), then press **Solve**. Each Step's moves stay hidden until you show them. Coach works with Beginner and CFOP only.
- **X-ray:** tick **Highlight each step's pieces** (or press `H`) to fade the parts of the cube the current step ignores. The pieces it works on stay in full color, such as the F2L pair or the yellow stickers during OLL. Pieces that were already solved stay dimly colored, so you can watch them move out of place and back. The computer methods have no X-ray.
- **Playback:** play/pause, step, jump to the start or end, and set the speed (from slow to instant). Click any move or step in the solution list to jump there, or drag the progress bar.
- **Keys:** `U R F D L B M E S X Y Z` turn the cube, and `Shift` makes the move prime. `Space` plays or pauses and `H` toggles X-ray. The arrow keys step, `Page Up`/`Page Down` jump to the previous or next step, and `Home`/`End` jump to either end. `Enter` solves, `Ctrl+S` scrambles and `Ctrl+Z` undoes the last Scramble or Reset (an **Undo** button appears too). `Tab` and `Shift+Tab` move between the buttons, and `Enter` or `Space` presses the focused one.
- **Type moves:** you can enter any sequence in standard notation, including wide moves (`Rw`/`r`), slices, rotations and brackets.
- **Paint stickers:** choose a color, then click stickers on the 3D cube or the net to paint them. Without a mouse, `Tab` to a sticker on the net and press `Enter`. **Check cube** tells you whether the state is legal (twisted corners, flipped edges, parity and so on).
- **Camera:** drag to orbit and use the mouse wheel to zoom.

## Layout

```
include/cube/   cube.h (facelet model, moves, notation), cubie.h, solver.h
src/            cube.c, cubie.c (cubie model, validation), solution.c (dispatcher), lessons.c, focus.c
src/solvers/    beginner.c, cfop.c, cfop_algs.c, thistlethwaite.c, kociemba.c, common.c
apps/cli.c      command-line tool
apps/gui/       raylib front end (main.c, render.c, ui.c) and playback.c/.h (raylib-free playback)
tests/          move algebra, conversions, full OLL/PLL/F2L coverage, solver fuzzing, playback
```

The stickers use Kociemba's URFDLB facelet order. All move permutations are generated from 3D geometry, so face turns, slices, wide moves and rotations always agree with each other and with the animation.
