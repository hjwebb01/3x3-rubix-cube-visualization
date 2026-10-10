# Improving the raylib renderer

Research date: 2026-10-10. Recommendations concern this application's renderer, not changes to raylib itself. Official sources below are pinned to raylib 5.5, the version fetched by CMake when a system installation is unavailable. CMake also accepts an installed raylib >= 5.0; check the actual linked version when validating.

## Current repository findings

`apps/gui/render.c` draws 27 sharp `DrawCube` bodies and 54 flat immediate-mode sticker quads. It uses raylib's default shader, so the existing dark bodies do not have directional lighting or material highlights. Stickers emit no normals. Layer turns use `rlPushMatrix`/`rlRotatef`, sharing the move geometry and smoothstep timing with the solver animation. The view has a custom projection and framebuffer viewport, then restores state for 2D controls. The 2D net already has rounded stickers.

Picking intentionally uses facelet-sized quads (half-width 0.5), larger than the visible stickers (0.41). Edit mode flushes animation before allowing painting. `main.c` already requests 4x MSAA and vsync before window creation; another MSAA flag is not a new improvement. There is no GPU mesh/shader resource lifecycle yet.

Baseline verification launched `./build/rubik_gui --screenshot` and inspected the result: flat black bodies and uniformly colored faces support prioritizing geometry and lighting. `build/compile_commands.json` confirms `GRAPHICS_API_OPENGL_33` and `PLATFORM_DESKTOP`, so the current build already supports GLSL 330. No raylib upgrade is needed for this phase.

## Proposed first phase

Keep raylib and the existing interaction model. Upload one reusable beveled cubie mesh and one reusable rounded planar sticker mesh at initialization. Reuse these across all bodies and facelets. Generate geometry procedurally to avoid new model files and deployment paths. Keep the body footprint, centers, gaps, facelet identities, and paint colors stable.

Start with a modest bevel and a few curved corner segments. Use flat normals on face interiors and smooth normals around the bevel; rounding the silhouette without changing normals leaves much of the visual improvement unrealized. Keep sticker corners inside the body's flat face region. Preserve a small positive separation from the body to prevent depth fighting.

Use a custom vertex/fragment shader with ambient + directional key and weaker fill lighting. Add restrained specular highlights with independently adjustable body/sticker strength and shininess. This is a proposed artistic choice, not an API requirement. Directional lights give depth without introducing attenuation or shadow infrastructure. Keep all six colors recognizable, especially yellow versus white and red versus orange.

The official [5.5 lighting vertex shader](https://github.com/raysan5/raylib/blob/5.5/examples/shaders/resources/shaders/glsl330/lighting.vs) exposes `mvp`, `matModel`, `matNormal`, and normals. Use world-space positions and normals consistently with world-space light directions and camera position. Transform normal directions with `w = 0` and normalize after interpolation. The [fragment shader](https://github.com/raysan5/raylib/blob/5.5/examples/shaders/resources/shaders/glsl330/lighting.fs) demonstrates ambient, directional/point diffuse, camera-dependent specular, and gamma correction. Treat its color equation as an example rather than copying its fixed shininess and color handling unchanged. Choose a consistent color pipeline and inspect the result beside the unlit net.

The [basic lighting example](https://github.com/raysan5/raylib/blob/5.5/examples/shaders/shaders_basic_lighting.c) updates `viewPos` as the camera moves and selects GLSL 330 on desktop versus 100 elsewhere. Embedded shader source via `LoadShaderFromMemory` would avoid runtime asset lookup in this small desktop program. A GLSL 330 implementation is appropriate to the current desktop target; adding web/ES portability would require a corresponding shader variant.

## Integration pitfalls

- **Transforms:** [5.5 `DrawMesh`](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c) combines its explicit transform with `rlGetMatrixTransform()`, then supplies the model and inverse-transpose normal matrices. Retain the existing layer stack and pass local object transforms, or use complete explicit world matrices with an identity stack. Applying the layer rotation in both places rotates twice. Check matrix composition with a known cubie at a quarter turn.
- **GPU attributes:** Give uploaded meshes positions, normals, and matching triangle winding. Supply UVs if sampling the default white texture. A custom shader can omit unused texture/color attributes instead of relying on missing attribute defaults.
- **Mixed drawing:** Flush pending 2D batches before changing viewport/projection. Draw lit meshes in the custom 3D viewport, then finish shader work and restore projection, modelview, viewport, depth, and culling before face labels and controls. Hover outlines need their own clear unlit draw path.
- **Picking:** Preserve the current generous quad hit targets and nearest-hit behavior. Rounded corners need not shrink paint targets. If editing during animation is added later, ray tests and outlines must use the same animated transforms as rendering.
- **Animation:** Keep `move_affects`, `move_geometry`, and the current easing as the source of layer selection and angle. Validate face, slice, wide, and whole-cube turns, including prime and half turns. Body and sticker transforms must stay synchronized through the final state commit.
- **Ownership:** [5.5 `UnloadMaterial`](https://github.com/raysan5/raylib/blob/5.5/src/rmodels.c) unloads a non-default shader. Shared shader references therefore need explicit ownership; do not unload that shader once per material. Create GPU resources after `InitWindow`, release each owned mesh/shader once before `CloseWindow`, and preserve a useful fallback if shader loading fails.

## Later options

Real shadows are a separate phase. The official [5.5 shadow map example](https://github.com/raysan5/raylib/blob/5.5/examples/shaders/shaders_shadowmap.c) renders a depth pass from a light camera and a second camera pass, creates a depth framebuffer, and binds the shadow texture explicitly. Adapting this adds bias/filtering decisions, extra draw work, framebuffer/viewport restoration, and duplicated animated scene transforms. A decorative background shadow or gradient costs less but does not provide cubie self-shadowing.

PBR and environment lighting are possible later but add textures, color-space decisions, and material tuning. Instancing is unnecessary until profiling shows mesh draw overhead matters. None of these first-phase improvements requires upgrading raylib or replacing the GUI framework.

## Validation plan

Build with the project's warning flags and run existing core tests. Launch the actual GUI and inspect solved and scrambled cubes while orbiting and zooming; verify highlights follow the camera and bevel lighting follows animated normals. Inspect intermediate layer turns for detached stickers, incorrect rotation, winding holes, or final-frame jumps. Paint each visible face and center, compare nearest picks at boundaries, and confirm center protection and net colors still work. Resize to the minimum window and inspect viewport edges, labels, and 2D controls; check a scaled display if available. Exercise repeated launch/close and inspect shader/graphics logs. Record baseline and changed frame timing before claiming a performance improvement. Rendered screenshots and interaction checks are necessary; a successful C build alone cannot validate appearance.
