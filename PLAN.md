# Compute Voxel Plan

## Objective
Fuse Jebrim-style deterministic execution with Seb Aaltonen-style compact voxel bricks, while preserving fixed layout and minimal runtime variability.

## Current Status
- M1 complete: camera-driven ray direction debug path exists.
- M2 complete: single 4x4x4 test brick hit rendering exists.

## Next Execution Order

### 1) Arena Header Normalization (start here)
- Lock a fixed header schema with `constexpr` offsets.
- Camera fields:
- `camPosX`, `camPosY`, `camPosZ`
- `yaw`, `pitch`
- `moveSpeed`
- `mouseSensitivity`
- `frameIndex`
- Grid/brick fields:
- `gridMinX`, `gridMinY`, `gridMinZ`
- `gridDimX`, `gridDimY`, `gridDimZ`
- `brickCount`
- `brickTableOffsetWords`
- `brickPoolOffsetWords`
- Keep one packed arena buffer; no realloc, no runtime growth.

### 2) Deterministic Flight Camera
- Use fixed simulation step `1/120` for movement integration.
- Sample mouse every frame, accumulate deltas, consume in fixed ticks.
- Controls:
- `W/S`: forward/back
- `A/D`: strafe
- `Q/E`: down/up
- `Shift`: fixed speed multiplier
- Write updated camera header state into arena each frame slot.

### 3) M3: Multi-Brick Traversal
- Implement coarse DDA through a brick grid.
- Fixed loop caps:
- `MAX_GLOBAL_STEPS`
- `MAX_LOCAL_STEPS`
- On candidate brick hit: run local 4x4x4 traversal and bit test on `occLo/occHi`.
- Prefer active-mask style updates inside fixed-count loops.

### 4) M4: CSG Edit Ring in Arena
- Add fixed-capacity command ring in the arena.
- Command payload includes target brick index and edit mask.
- Apply in-place ops:
- union: `occ |= editOcc`
- subtract: `occ &= ~editOcc`
- No dynamic allocation; fixed max edits per frame.

### 5) M5: Visual Polish
- Keep current color grading pass.
- Add minimal material lookup from packed arena data (fixed table).
- Preserve deterministic control flow and fixed descriptor layout.

## Frame Pipeline Constraints (unchanged)
- One submit per frame.
- Semaphores for acquire/present ordering.
- Fence only as per-frame resource reuse gate.
- Descriptor and buffer bindings remain fixed.

## Guardrails
- No runtime container growth in frame/render path.
- No hidden allocation churn.
- Keep synchronization explicit and minimal.
- Prefer branch-minimized shader logic with fixed step ceilings.
