# Glossary

- **Solution**: the flat move list a solver produces, split into Steps.
- **Step**: a named slice of a Solution ("White cross", "OLL 27 (Sune)", "Phase 2"). Each Step carries a Lesson.
- **Skipped Step**: a Step with no moves, because the cube already satisfied it ("OLL skip", a Thistlethwaite phase already reached). It is shown with the next Step that has moves (or the last one, at the end) rather than as a stop of its own, and is not counted in "Step k of N".
- **Lesson**: teaching notes for a kind of Step: what it achieves, what to look for, which algorithm to memorize. Identified by a `LessonId`, not by the Step's display name.
- **Focus**: the pieces a Step works on (for F2L, the corner and edge of the pair), optionally narrowed to one color's stickers (the yellow stickers in OLL). Pieces are named by their colors, so a Focus follows them as they move. Each Lesson has a default Focus; the computer methods have none.
- **X-ray**: showing a Step's Focus on the cube: Focus stickers in full color, pieces solved when the Step began dimmed, everything else grey. Centers always keep their color.
- **Case note**: extra guidance for one specific OLL/PLL case (e.g. Sune), stored with that case's algorithm and shown after the Step's Lesson.
- **Move count**: the slice turn metric: every turn counts 1 and whole-cube rotations count 0. Rotations are reported separately. _Avoid_: using a Solution's raw move list length as its length.

## Playback

- **Playback**: watching a cube turn on screen: the user's own turns, a scramble, or a Solution being played forward and back.
- **Playhead**: how far into the Solution the cube *on screen* is: the number of Solution moves that have finished turning. Moves waiting to animate are not counted. _Avoid_: cursor, position (ambiguous).
- **Teach pause**: Playback stopping by itself when the Playhead reaches the start of a new Step, so the learner can read its Lesson before the moves play.

## Coach

- **Coach**: practising the Steps of a human-method Solution (Beginner or CFOP) on a physical cube. The screen cube is the assumed state of the physical cube, and the Playhead its progress.
- **Setup**: matching the screen to the physical cube, by a guided scramble or by painting, before Solve starts practice.
- **Practice**: one Step at a time: its goal and Lesson are shown and its moves stay hidden until the user shows them or asks for a demonstration. Following the moves advances to the next Step.
- **Unsynced**: the physical cube may no longer match the screen, after an independent attempt or a reported mismatch. The user restores it to the Step's start or repaints it and solves afresh.
- **Basis**: what the screen state rests on: *assumed* from followed moves or a guided scramble, or *painted* and validated. Either way the physical match is assumed, never checked.
