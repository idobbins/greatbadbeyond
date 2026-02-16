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

## Change Policy
- New code should preserve the explicit/minimal style.
- Do not add broad robustness layers by default.
- If safety checks are requested, add them in the smallest possible surface area.
- Any deviation from this ethos should be called out in the change summary.
