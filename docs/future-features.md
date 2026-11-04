# Future features

Feature ideas we've chosen to build later. These are not specs. Each one names the existing pieces it would build on.

## Method race

Solve one scramble with all four methods and play the solutions side by side on four small cubes. They play at the same speed, and a shared timeline marks where each method finishes each Step. Kociemba finishes after about 20 moves while Beginner is still building its cross. It shows why no human uses Kociemba (its middle states look like noise) and why CFOP is fast but can still be followed.

- **Builds on:** Playback has no raylib dependency, so four instances can run side by side. The CLI already solves with every method, and every Solution is split into Steps.
- **Open questions:** whether to use one shared transport or a separate one per cube; whether to line the cubes up by move number or by wall-clock time; and how to fit four cubes into the centre view without making them too small to read.

## OLL/PLL case trainer

Flashcards built from the algorithm tables. To show a case, apply the inverse of its algorithm to a solved cube plus a random U turn, then display the result. The learner either names the case ("Sune") or types the algorithm into the move box, and the app checks whether the cube ends up solved. Cases the learner misses come back more often.

- **Builds on:** `OLL_ALGS` / `PLL_ALGS` in `cfop_algs.c` (78 named cases with Case notes), typed move input, and the Lesson box.
- **Open questions:** whether to drill recognition only, execution, or both; how to store progress between sessions; whether to start from 2-look OLL/PLL subsets; and whether X-ray should fade everything below the last layer while drilling.
