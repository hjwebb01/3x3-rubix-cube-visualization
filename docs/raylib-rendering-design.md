# Raylib rendering upgrade

## Summary

Improve the existing raylib cube presentation with modestly beveled dark cubies, rounded sticker corners, and gentle diffuse lighting. The agreed visual direction is a clean physical cube with matte materials throughout and almost no shine, floating against the existing subtle gradient. Keep the current application layout, controls, camera interaction, animation, playback, sticker painting, and solver behavior. This document records the completed design interview; implementation and visual acceptance checks remain to be done. No visual prototype was requested or produced. The supporting API investigation is in [raylib-rendering-research.md](raylib-rendering-research.md); the matte finish chosen here supersedes its proposed satin/specular treatment.

## Terms

- **Cubie:** One small physical piece of the displayed cube, drawn as a dark body with colored stickers. Avoid: “voxel.”
- **Bevel:** A softened edge on a cubie that catches light and changes its silhouette. Avoid: “anti-aliasing,” which smooths rendered pixel edges rather than changing geometry.

## Why

Hunter asked how the current visual rendering works and what alternatives exist, then asked to “look into improving raylib.” The current cube uses sharp bodies and flat sticker colors. The purpose of this upgrade is to give pieces a clearer physical shape and improve the readability of moving layers while continuing with raylib and the existing C application. The interview narrowed the investigation into a focused rendering design with explicit preferences for matte surfaces and a floating presentation.

## Locked decisions

None of the interview decisions passed all three gates for a durable architectural decision: hard to reverse, surprising without context, and a real trade-off. These are reversible visual and scope choices, recorded below. They are still the accepted requirements for this first pass.

## Routine choices

- **Q1 — Clean physical cube (A, recommendation accepted):** Use softened plastic edges and rounded stickers with restrained lighting. The diagram-like alternative would retain more of the current flat appearance; a realistic product render would require richer material and shadow work. The subsequent matte decision determines the finish, so the original mention of highlights does not authorize a glossy treatment.
- **Q2 — Cube presentation only (A, recommendation accepted):** Limit this pass to geometry, lighting, background presentation, and hover appearance. Retain layout and controls. UI polish and broader changes to the learning or playback workflows are outside this pass.
- **Q3 — Matte throughout (B, selected over recommended A):** Emphasize shape and recognizable sticker colors with almost no shine. Do not implement the originally recommended satin sticker finish or the glossy showroom alternative. Start with diffuse lighting and no specular term; any later adjustment must remain visibly matte.
- **Q4 — Floating cube with subtle gradient (A, recommendation accepted):** Retain the current background approach. Add neither a decorative grounding shadow nor a floor with real cast shadows in this pass.

### Implementation defaults

These are routine implementation choices derived from the accepted direction and source investigation, rather than additional interview answers.

1. **Reusable geometry:** Generate and upload one beveled body mesh and one rounded planar sticker mesh after window initialization. Reuse them for all current body and facelet positions. Avoid adding a model importer or runtime model-file dependency for this small scene.
2. **Modest rounding:** Preserve body centers, overall cube proportions, and the visible gaps between pieces. Start with the current body size of 0.96 and sticker half-width of 0.41 as references. Tune the bevel and sticker corner radius together so sticker corners remain over the body's flat face region. Keep stickers slightly above the surface to prevent depth fighting; adjust their offset if the geometry requires it.
3. **Surface normals:** Use flat normals on body face interiors and smoothly varying normals around rounded edges. Give sticker geometry its outward normal. Mesh triangle winding and normal orientation must work on all six faces.
4. **Matte lighting:** Use ambient light, a gentle directional key light, and a weaker fill light. Keep light directions consistent in world space, and transform normals with the actual animated object transform. Tune brightness against the existing 2D net so white/yellow and red/orange remain easy to distinguish. Do not blindly copy the official example's gamma correction or specular formula; use one consistent color treatment and inspect the output.
5. **Animation:** Reuse `move_affects`, `move_geometry`, and the existing easing and logical move commit. Body and sticker geometry must share the same layer rotation. Raylib's mesh transform support permits retaining the existing matrix stack with local object transforms; do not also apply the same layer rotation in an explicit transform.
6. **Picking and hover:** Preserve generous facelet quad hit targets and nearest-hit selection. Rounded visible sticker corners do not need smaller painting targets. Retain center protection and the visible hover cue. Draw hover outlines through an unlit path so lighting does not obscure interaction feedback.
7. **Graphics state and ownership:** Keep lit mesh rendering within the custom 3D viewport. Flush pending batches at state boundaries and restore projection, modelview, viewport, depth, culling, and shader state before labels and controls. Explicitly own shared GPU resources, release each once before window shutdown, and avoid unloading a shared shader through every material. Provide a usable fallback if shader initialization fails.
8. **Scope of code changes:** Most work belongs in `apps/gui/render.c`, with renderer initialization/shutdown hooks in `apps/gui/main.c` and declarations in `apps/gui/gui.h` as needed. Do not change cube or solver behavior to achieve the visual effect.

## Verified facts

- `apps/gui/render.c` currently draws 27 dark `DrawCube` bodies and 54 flat sticker quads. The sticker drawing function does not emit normals. There are no custom lighting shaders or shadow passes in this renderer.
- The renderer builds a perspective camera and custom viewport/projection, then restores state for 2D labels and controls. Camera orbit and zoom are handled in `apps/gui/main.c`.
- Layer animation applies the current move to the logical cube when the visual turn completes. Entering sticker edit mode flushes queued animation first.
- The current picking quads have half-width 0.5, larger than the visible stickers' half-width 0.41. Center stickers are protected from painting. The 2D net already has rounded sticker rectangles.
- The application requests 4x MSAA, VSync, and a 120 FPS target. These are requested settings, not measured performance guarantees.
- CMake fetches raylib 5.5 if it does not find an installed raylib of at least 5.0. The inspected build uses the fetched raylib source; `build/compile_commands.json` confirms desktop OpenGL 3.3.
- A baseline GUI screenshot was captured and visually inspected during the research. It showed the current flat colors and sharp geometry mid-turn. This verifies the baseline launch and appearance, not the proposed rendering changes.
- The official [raylib 5.5 lighting example](https://github.com/raysan5/raylib/blob/5.5/examples/shaders/shaders_basic_lighting.c) demonstrates desktop GLSL 330 shader setup. [Raylib's mesh source](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c) supplies object and normal matrices and combines explicit mesh transforms with the current transform stack. The proposed first pass does not require a raylib upgrade.

## Risks

- Incorrect normals or transform composition can make lighting detach from rotating pieces, or rotate geometry twice.
- Oversized bevels or sticker corners can leave unsupported sticker areas, change gaps, or introduce depth fighting.
- Diffuse shading can muddy the palette or make faces too dark even without shine. Tune for readability from the full camera orbit.
- A custom shader or mesh draw can leak graphics state into labels and controls. Shared material/shader cleanup can also release resources more than once if ownership is unclear.
- More geometry and individual mesh draws may change frame time. No performance improvement has been measured or promised.

### Required verification before implementation is complete

- Build with the existing warning flags and run the existing core tests.
- Inspect rendered solved, scrambled, and intermediate-turn states. Verify bevel silhouette, rounded sticker corners, matte appearance, recognizable colors, and no detached stickers or final-frame jumps.
- Exercise face, prime, half, slice, wide, and whole-cube turns. Check backward stepping, playback speed changes, and jumps using the existing controls.
- Orbit and zoom through the supported range. Resize to the minimum window size and check viewport boundaries, face letters, and surrounding controls. Check a scaled display where available and report if that check cannot run.
- Paint visible stickers on different faces and through the 2D net. Verify nearest-hit behavior at boundaries, hover appearance, and center protection.
- Repeatedly launch and close the GUI and inspect graphics/shader logs. Verify resource cleanup and the shader-failure fallback.
- Compare baseline and changed frame timing on the same machine and state before making performance claims. A successful build alone does not validate appearance or interaction.

## Deferred

No interview question was formally deferred. The following work is outside the accepted first-pass scope:

- UI layout, typography, button polish, and changes to learning/playback workflows; reopen through a separate UI scope decision.
- Satin or glossy materials, environment reflections, and PBR; reopen if Hunter requests a different finish.
- Decorative shadows, a floor, and real shadow mapping; reopen if the floating presentation proves insufficient or grounding is explicitly requested.
- A renderer/framework migration, raylib upgrade, instancing optimization, or new web/mobile target; reopen only for a concrete requirement or profiling evidence.

## Open threads

None. All four questions were answered, there were no unresolved discussion messages, and Hunter finished the interview. The next stage is implementation of this design followed by the verification above.
