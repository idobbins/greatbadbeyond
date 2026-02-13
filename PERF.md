# Forward Renderer Performance Notes

This document tracks performance decisions for the forward renderer direction.

## North Star

- CPU-driven forward raster pipeline.
- Explicit data flow and minimal passes.
- Measured complexity only.

## Current Baseline

- Vulkan instance/device/swapchain lifecycle is stable.
- Frame overlap + acquire/record/submit/present is stable.
- Dynamic rendering draws indexed forward geometry (single centered scene mesh).
- Preferred 2x MSAA resolves directly into the swapchain when supported by the GPU.
- Legacy compute rendering path has been removed from the active frame loop.

## Immediate Targets

1. Add depth attachment and depth test in the forward pass.
2. Draw indexed geometry (single mesh in center of world).
3. Add frame globals UBO (view/proj/time).
4. Add one directional light + small point-light array.

## CPU Data Shapes (v1)

- `MeshGpu`: vertex/index buffers + counts.
- `MaterialGpu`: pipeline/material bindings.
- `RenderObject`: meshId, materialId, transform.
- `DrawItem`: pipelineKey, materialId, meshId, firstIndex, indexCount, firstInstance, instanceCount.

## GPU Data Shapes (v1)

- Set 0: frame globals UBO.
- SSBO: object transforms.
- SSBO/UBO: light list.

## Draw Ordering

Opaque list is stable-sorted by:

1. pipeline
2. material
3. mesh

This minimizes pipeline and descriptor churn before adding more systems.

## Asset Bring-Up (Kenney)

For first scene bring-up, use one OBJ from:

- `resources/external/Kenney/3D assets/Prototype Kit/Models/OBJ format/shape-cube.obj`

Target behavior:

- Single centered model at world origin.
- Camera starts offset and looks toward origin.
- Scene assets resolve via generated manifest handles plus `kenney_assets.pack`.

Pack status (February 13, 2026):

- `asset count = 84,470`
- `alias count = 8,353`
- `mesh/image/audio/raw = 5,009 / 54,216 / 1,342 / 23,903`
- `compressed records = 74,369`
- `pack bytes = 1,314,807,875`
- Pack is memory-mapped at runtime; assets decompress into reusable scratch then upload via reusable staging buffer.

## Profiling Gates

Only add new systems when measurements justify them:

- Clustered forward lights: only if light count becomes a bottleneck.
- Extra passes: only if visual wins exceed measured cost.

## Guardrails

- No hidden renderer subsystems.
- Keep create/destroy order explicit.
- Keep swapchain recreation deterministic.
