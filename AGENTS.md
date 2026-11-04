# rubiks
C11 Rubik's cube solver: core library (`src/`), CLI (`apps/cli.c`), raylib GUI (`apps/gui/`), tests (`tests/`).

## Build and verify (run before saying done)
```sh
cmake -B build && cmake --build build -j && ctest --test-dir build
./build/rubik --bench 200        # Kociemba: average ≤18.5 moves, first solution <100 ms
```
GUI changes: before/after shots with `./build/rubik_gui --screenshot PATH W H` or `--faces NAME W H`, at 1360x820 and 960x600.

## Settled decisions (don't re-derive)
- Layout is the Seam: `docs/seam-design.md`. Terms: `GLOSSARY.md`.
- State machines are explicit enums; map in `docs/state-machines.md`. Read it before hunting for implicit state.
- Keep public API without in-repo callers (`moves_invert`, `solver_key`).
- Cleanup audit done (Oct 10, repeated Oct 29–31); don't rerun unless code changed a lot.
- Parked ideas: `docs/future-features.md`. Rendering stays raylib: `docs/raylib-rendering-*.md`.

## Working rules
- Review findings: rank must / should / nit. Fix must + should unless told otherwise.
- Commit when build + tests pass. No tracker. Branch off `main`.
- One design skill per pass; don't stack several.
- Teams: at most 3 workers (implement, adversarial review, judge running bench + ctest).
- After two compactions, write `docs/handoff.md` (what happened, why, what's left) and start a new thread. Read it first if it exists.
