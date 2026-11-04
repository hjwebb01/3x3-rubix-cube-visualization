# The Seam: GUI redesign decisions

The GUI is split in two: the light left half is **your cube, in your hand**, and the dark right half is **the cube on screen**. Between them runs the **seam**, which shows whether the two cubes are in sync. Everything is typed into one prompt at the bottom.

Prototype: the "07 · Seam prototype" board on the design canvas.

## Seam states

| State | Look | Meaning |
|---|---|---|
| Open | dashed grey | the cube hasn't been entered yet |
| Sealed | solid blue | the cube is verified and a route is chosen |
| In sync | blue fill from the top | progress through the Solution; doubles as the Playhead |
| Torn | wider, orange zigzag | a typed move didn't match the expected one |

## Decisions

1. **Two ways in.**
   - "I have a cube": type each face, then Check cube.
   - "Give me a scramble": the app shows a scramble to do on your cube, and the seam starts sealed.

   A screen-only mode hides the seam.
2. **The seam replaces the Playback controls.** Dragging the seam scrubs, and Step notches mark Step boundaries.
3. **Pick a way to solve:**
   - `teach` uses Beginner.
   - `faster` uses CFOP.
   - `show` uses Kociemba, with Thistlethwaite as an alternative.

   Each card shows its method and Move count.
4. **Keys.** Space, the arrow keys and Esc act only while the prompt is empty; everything else is typed into the prompt.
5. **Notation.** `R'`, `Ri` and lowercase are all accepted. Rotations (`x y z`) are ignored when checking sync, the same way Move count leaves them out.
6. **Entering faces.** While painting, the prompt takes nine color letters (`WRGYOB`) for the next face in U R F D L B order, or you click stickers on the net. A centre that differs from the cube's is refused. Check cube runs after the sixth face. Under `paint top` only U is asked for.
7. **Checking sync.** Each typed move must match the expected move exactly. When the seam tears:
   - `fix` undoes your move.
   - `trust` applies your move to the model and solves again from there.
   - `paint top` re-enters the top face only.
8. **Coach becomes the seam.** X-ray is on by default for `teach` and off otherwise. Lessons and Case notes appear on the left half, and the Teach pause becomes "type the move to continue".
9. **Colour.** Real sticker colours appear only on the right half and the net; the left half stays neutral.
10. **Window size.** The halves split 50/50 above a minimum width. Below it, they stack and the seam runs horizontally.

## Open risks

- raylib has no text field. The prompt (cursor, editing, focus) is new code in `apps/gui/ui.c`.
