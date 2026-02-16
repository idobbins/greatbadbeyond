# AGENTS.md

## Purpose
This project is an austere Vulkan compute renderer.
The primary goal is deterministic, explicit behavior with minimal runtime variability and minimal code surface.

## Core Ethos
- Determinism over convenience.
- Explicit control over hidden abstraction.
- Fixed-size/static structures over dynamic/runtime growth.
- Minimal branching in CPU code and shader code.
- Keep the code short, direct, and mechanically obvious.

## Hard Constraints
- No runtime container growth (`std::vector`, maps, dynamic graphs) in the render path.
- Prefer compile-time capacities (`constexpr` + `std::array`).
- Avoid defensive/runtime safety scaffolding unless explicitly requested.
- No exception-based flow; no implicit fallback systems.
- No multi-path feature negotiation unless required by the task.
- No runtime heap allocation churn in the frame loop.
- Prefer one app-owned GPU data arena buffer with fixed internal offsets/packing.

## Vulkan Style Rules
- Use plain Vulkan handles and explicit lifetime management.
- Keep synchronization explicit and minimal.
- Prefer one queue, one command buffer strategy unless requirements force otherwise.
- Prefer one in-flight frame for simplest deterministic behavior.
- Avoid optional abstractions that hide ordering or resource ownership.
- Keep descriptor layouts simple and fixed.

## Shader Rules
- Favor branchless expressions (`step`, `mix`, arithmetic masking) over control-flow branches.
- Keep descriptor bindings stable and explicit.
- Keep workgroup geometry explicit and intentional.
- Avoid dynamic shader permutation systems.
- Prefer packed `uint` storage and explicit unpack in shader for scene data.

## Current Architecture
- Compute-only pipeline, no graphics pipeline.
- Direct write to acquired swapchain image from compute shader.
- One descriptor set with fixed bindings.
- `binding 0`: storage image target (swapchain image view, updated per frame).
- `binding 1`: storage buffer (`dataBuffer`) for packed scene data.
- One app-owned GPU data buffer (`dataBuffer` + `dataBufferMemory`) with custom packing.
- One-time scene upload to `dataBuffer` at init via direct map/write.
- One command buffer and one fence for explicit frame sequencing.

## Jebrim Alignment Status
- Current state is partially aligned:
- Aligned:
- Branch-minimized shader logic (`step`/`mix` style).
- Single packed GPU scene buffer (`uint`-oriented storage).
- Fixed-capacity/static app-side structures.
- Not yet aligned:
- CPU blocks twice per frame on fence waits (GPU feed is not continuous).
- Descriptor `binding 0` is updated every frame for swap image selection.
- Workgroup size is `1x1x1` (deterministic but low-throughput).
- Direct storage writes to swapchain images may not be portable across all MoltenVK targets.

## Alignment Plan
- Goal: keep deterministic behavior while reducing CPU stalls and descriptor churn.
- Phase 1 (low risk):
- Move to timeline where CPU does not hard-wait after every submit.
- Keep fixed resources; no dynamic allocations.
- Success criteria: frame loop has no per-frame blocking wait unless swapchain requires recovery.
- Phase 2 (low-medium risk):
- Remove per-frame image descriptor writes by switching to compute output buffer/image that is bound once.
- Copy/resolve to acquired swapchain image in transfer pass.
- Success criteria: no `vkUpdateDescriptorSets` in frame loop.
- Phase 3 (low risk):
- Tune workgroup geometry to hardware-friendly fixed tiles (for example `8x8`) while preserving deterministic execution.
- Success criteria: fixed dispatch math with no data-dependent control flow.
- Phase 4 (conditional portability fallback):
- If direct swapchain storage writes fail on any MoltenVK target, use pre-bound offscreen storage target plus explicit copy to present image.
- Success criteria: same packed-buffer pipeline and deterministic behavior across all supported Apple devices.
- Guardrails:
- Preserve one app-owned packed GPU arena buffer as the data source.
- Keep control flow explicit and mostly branchless in shader and frame code.
- Do not add broad runtime capability negotiation unless explicitly requested.

## Code Structure Guidelines
- Keep initialization linear and local.
- Keep the frame loop short and predictable.
- Avoid helper proliferation; add helpers only when they reduce total complexity.
- Prefer fewer objects and fewer transitions.
- If a feature adds complexity, document why it is required.

## Portability and Toolchain Notes
- MSVC designated initializers must follow declaration order for Vulkan structs.
- Keep initializer ordering compliant with strict compilers.
- Keep shader embedding and build wiring simple and single-purpose.
- MoltenVK support is required.
- Keep `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` on Apple.
- Keep `VK_KHR_portability_enumeration` instance extension on Apple.
- Keep `VK_KHR_portability_subset` device extension on Apple.
- If swapchain storage-image writes fail on a target, add fallback only then (do not preemptively add multi-path code).

## Change Policy
- New code should preserve the explicit/minimal style.
- Do not add broad robustness layers by default.
- If safety checks are requested, add them in the smallest possible surface area.
- Any deviation from this ethos should be called out in the change summary.
