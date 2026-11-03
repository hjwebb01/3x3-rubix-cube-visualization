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

The window is split by the **seam**. The left half is your cube, in your hand: its setup, its Solution and the Lesson for the current Step. The right half is the cube on screen, with its move list. The seam between them shows whether the two cubes are in sync, and everything is typed into one prompt along the bottom. Side by side the halves split the width evenly from 1008 px wide; narrower, the panels keep their minimum widths and the screen cube gets less than half. Below 900 px wide the halves stack: the left half becomes a band across the top, taking most of the height (less while coaching), the seam runs across the window and the screen cube sits below.

- **Two ways in:** **I have a cube** matches the screen to your cube, by painting it or by a guided scramble, then coaches you through the Steps (Beginner and CFOP only). **Give me a scramble** shows a scramble to do on your cube, and the seam starts sealed.
- **Prompt:** type moves in standard notation, including wide moves (`Rw`/`r`), slices, rotations, brackets, and `R'` or `Ri`. Or use a command:
  - `teach` solves with Beginner and turns X-ray on, `faster` with CFOP, and `show` with Kociemba (`show thistle` for Thistlethwaite). Each card shows its Move count.
  - `solve`, `scramble`, `reset`, `undo`, `xray` and `help` do what their buttons and shortcuts do. `help` shows the keyboard list.
  - While a Step is practised, typed moves are checked against it: the Step's moves (rotations skipped) play the Step through on screen, and anything else tears the seam. While a torn seam is waiting, `fix` returns to the Step's start, `trust` followed by moves makes them the truth and solves again, and `paint top` repaints only the top face.
  - At a Teach pause the prompt asks for the next move: typing it resumes playing.
- **Keys:** `U R F D L B M E S X Y Z` turn the cube, and `Shift` makes the move prime. `Space` plays or pauses the Solution, and `Left`/`Right` step through it. `Home`/`End` jump to either end, `Page Up`/`Page Down` to the previous or next Step, and `-`/`=` slow down or speed up. `H` toggles X-ray and `V` shows the screen only, which hides the seam and both halves. `Enter` solves, `Ctrl+S` scrambles and `Ctrl+Z` undoes the last Scramble or Reset. `Space`, the arrows and `Esc` act only while the prompt is empty. `Tab` and `Shift+Tab` move between the controls, and `Enter` or `Space` presses the focused one.
- **Seam:** drag it to scrub through the Solution. Each Step has a notch at its start. Click any move or Step in the list to jump there.
- **Learn:** tick **Pause to explain each step** to pause at the start of every Step so you can read its lesson, then press `Space` to watch the moves. Tick **Highlight each step's pieces** (or press `H`) to fade the parts of the cube the current step ignores. The computer methods have no X-ray.
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
