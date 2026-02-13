# Forward Renderer Implementation Plan

## Goal

Build a real forward raster renderer in `greadbadbeyond` with these properties:

- CPU-driven scene + draw submission.
- World-space mesh rendering (not fullscreen proxy rendering).
- Dynamic rendering with depth from day 1.
- Single-pass opaque forward baseline.
- One imported Kenney 3D asset rendered at world origin as the first scene proof.

## North Star

- Keep the renderer explicit and small.
- Add systems only after measurable need.
- Preserve existing stable frame lifecycle (acquire -> record -> submit -> present).

## First-Principles Constraints

- Forward renderer is the default architecture; no deferred path.
- No temporal accumulation in the core pipeline (`TAA`, temporal denoisers, temporal upscalers).
- Prefer deterministic recompute and stable IDs over hidden mutable state.
- Prefer flat, cache-friendly data (SoA) over pointer-heavy object graphs.
- Add complexity only after profiling proves a bottleneck.
- Keep behavior inspectable: explicit buffers, explicit passes, explicit synchronization.

## Non-Goals (for v1)

- No ray tracing or compute-based lighting.
- No SSAO, SSR, or deferred path.
- No bindless or descriptor indexing.
- No scene graph framework.
- No temporal anti-aliasing.

## Current State Snapshot

- Vulkan lifecycle and swapchain lifecycle are stable.
- Frame overlap and sync objects are stable.
- Dynamic rendering path exists.
- World-space forward mesh path exists (current test content is grid-expanded).
- Legacy compute rendering path is removed from active code.

## Target Architecture (v1)

CPU data:

- `MeshGpu`: vertex/index buffer handles and counts.
- `MaterialGpu`: pipeline/material binding state.
- `ObjectId`, `MeshId`, `MaterialId`: stable integer IDs.
- `ObjectTransformSoA`: flat arrays (`position[]`, `rotation[]`, `scale[]`) indexed by `ObjectId`.
- `ObjectMaterialSoA`: `meshId[]`, `materialId[]` indexed by `ObjectId`.
- `DrawItemSoA`: flat arrays for `pipelineKey[]`, `materialId[]`, `meshId[]`, `firstIndex[]`, `indexCount[]`, `firstInstance[]`, `instanceCount[]`.
- `FrameGlobals`: view, proj, camera position, time.
- Draw list rebuilt each frame from SoA data.

GPU data:

- Per-frame globals UBO (`set = 0`).
- Transform buffer (SSBO) for object/world matrices.
- Light buffer (SSBO or small UBO).

## Pass Plan

Pass 0 (required): Forward opaque

- Transition swapchain image -> `COLOR_ATTACHMENT_OPTIMAL`.
- Transition depth image -> `DEPTH_ATTACHMENT_OPTIMAL`.
- Begin dynamic rendering with color + depth.
- Bind forward pipeline + descriptor set(s).
- Draw opaque list from `DrawItemSoA`.
- Sort by pipeline -> material -> mesh only if profiling shows CPU submission cost is bottleneck.
- End rendering.
- Transition swapchain image -> `PRESENT_SRC_KHR`.

Pass 0.5 (optional, profile-gated): Depth prepass

- Add only when overdraw or fragment cost dominates measured frame time.
- Record depth-only pass for opaque geometry before color pass.
- Keep it toggleable and measurable.

Pass 1 (later): Forward transparent

- Same dynamic rendering infrastructure.
- Back-to-front ordering.
- Blend-enabled pipeline.

## Kenney Asset Bring-Up (first content target)

Source asset for first render target:

- `/Users/idobbins/Downloads/Kenney/3D assets/Prototype Kit/Models/OBJ format/shape-cube.obj`
- `/Users/idobbins/Downloads/Kenney/3D assets/Prototype Kit/Models/OBJ format/shape-cube.mtl`
- `/Users/idobbins/Downloads/Kenney/3D assets/Prototype Kit/Models/OBJ format/Textures/colormap.png`

Repository destination:

- `resources/models/kenney/prototype/shape-cube.obj`
- `resources/models/kenney/prototype/shape-cube.mtl`
- `resources/models/kenney/prototype/Textures/colormap.png`

Import rule:

- Runtime reads only from repository paths, never directly from `Downloads`.

## Phase Plan

### Phase 1: Forward API and State Scaffolding

Changes:

- Add forward-facing API entry points to `src/greadbadbeyond.h`:
- `CreateScene`, `DestroyScene`, `CreateForwardRenderer`, `DestroyForwardRenderer`, `DrawFrameForward`.
- Add forward renderer state fields to Vulkan global bucket in `src/vulkan.cpp`.

Acceptance:

- Project builds.
- App startup/shutdown uses forward API naming and lifecycle.

### Phase 2: Depth Resources (Day 1) + Optional Z Prepass Hook

Changes:

- Add depth image, memory, and view creation/destruction in `src/vulkan.cpp`.
- Pick supported depth format (`D32` fallback chain).
- Recreate depth resources with swapchain recreation.
- Enable depth testing in forward graphics pipeline state.
- Add a compile-time/runtime toggle point for optional depth prepass.

Acceptance:

- Build succeeds.
- Resize/recreate path remains stable.
- Validation output has no depth-layout misuse.
- Prepass toggle path compiles and can be enabled without changing architecture.

### Phase 3: Mesh Pipeline (replace fullscreen draw path)

Changes:

- Add `Vertex` layout definition in `src/greadbadbeyond.h`.
- Update shader pair to world-space forward mesh shaders in `resources/shaders/`.
- Configure vertex input bindings/attributes in `src/vulkan.cpp`.
- Replace `vkCmdDraw(3,...)` with indexed draw path.

Acceptance:

- Indexed draw executes.
- Depth-tested geometry visible.

### Phase 4: OBJ Loader + Kenney Cube in Center

Changes:

- Add minimal OBJ parser module (`src/asset_obj.cpp`, declarations in `src/greadbadbeyond.h`).
- Support: `v`, `vt`, `vn`, `f` (triangles + quads triangulated).
- Resolve `mtllib` and `map_Kd` paths relative to model directory.
- Upload parsed mesh to GPU buffers.
- Create one render object at world origin.

Acceptance:

- `shape-cube.obj` renders centered in scene.
- Texture resolves from `resources/models/kenney/prototype/Textures/colormap.png`.

### Phase 5: Frame Globals UBO

Changes:

- Add per-frame UBO buffers and descriptor sets.
- Write camera matrices + camera position + time each frame.
- Bind frame globals set in draw path.

Acceptance:

- Camera movement affects rendered mesh correctly.
- No per-frame descriptor allocation churn.

### Phase 6: Scene List and DrawItem Batching

Changes:

- Add SoA scene containers with stable IDs for objects/materials/meshes.
- Build draw-item arrays from SoA each frame.
- Add profile-gated stable sort by pipeline/material/mesh.
- Loop draw list in `DrawFrameForward`.

Acceptance:

- One or more objects render via draw-list loop.
- Draw call order is deterministic frame-to-frame.
- Data traversal is index-based and avoids pointer chasing in hot paths.

### Phase 7: Lighting v0

Changes:

- Add one directional light + bounded point-light array.
- Add minimal forward shading (Lambert + simple spec) in fragment shader.
- Add light buffer updates on CPU path.

Acceptance:

- Lighting responds to light direction/position changes.
- Shader remains compact and understandable.

### Phase 8: Hardening and Diagnostics

Changes:

- Improve logging for asset load failures and Vulkan setup failures.
- Add runtime assertions for invalid mesh/material references.
- Document render path and asset expectations in `PERF.md`.
- Add on-screen debug visualization toggles for timings and scene/light counts.
- Add shader hot-reload path for rapid iteration (debug builds).

Acceptance:

- Clean debug build with warnings treated as regressions.
- Basic smoke test checklist passes.
- Debug overlays can be toggled at runtime without destabilizing frame loop.

### Phase 9: Forward+ / Clustered Lights (Optional, Profile-Gated)

Changes:

- Add tiled/clustered light culling only if measured light scaling requires it.
- Keep baseline forward path as fallback.
- Compare CPU-driven baseline vs clustered path on identical scenes.

Acceptance:

- Demonstrated performance win at target light counts.
- Added complexity is justified by measured data and can be disabled.

## File-Level Worklist

- `src/greadbadbeyond.h`: API surface, shared structs, asset loader declarations.
- `src/vulkan.cpp`: forward pipeline, depth resources, descriptor setup, draw loop.
- `src/platform.cpp`: call forward draw function in main loop.
- `src/camera.cpp`: continue feeding camera state; no major architecture changes.
- `resources/shaders/*.vert|*.frag`: forward mesh shaders.
- `src/asset_obj.cpp`: minimal OBJ parse/load path.
- `src/debug_*.cpp` or equivalent: runtime visualization and instrumentation hooks.
- `CMakeLists.txt`: add new source files and shader inputs.
- `resources/models/kenney/prototype/*`: first in-repo model assets.

## Validation Checklist Per Phase

- Configure/build:
- `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build/debug`
- Smoke run:
- `./build/debug/greadbadbeyond`
- Resize window and confirm no crash.
- Confirm one centered model render before adding additional features.
- Profiling gates before each new system:
- CPU: record submission time and draw call count.
- GPU: record pass timings with timestamp queries where supported.
- Overdraw: estimate fragment pressure before enabling prepass.
- Feature additions must include before/after numbers in notes.

## Risks and Mitigations

- OBJ format edge cases: keep loader intentionally narrow for v1 and fail loudly.
- Swapchain + depth recreation bugs: keep depth lifecycle strictly paired with swapchain lifecycle.
- Over-scoping: postpone transparent path and clustered lights until base opaque path is stable and measured.
- Complexity creep: reject changes that cannot show measurable benefit.

## Guardrails

- No hidden systems.
- No extra passes without profiling evidence.
- Keep create/destroy order explicit and symmetric.
- Keep runtime data flow explainable from structs + draw loop + shaders.
- No temporal AA in core renderer.
- Prefer SoA + stable IDs for hot-path scene traversal.
