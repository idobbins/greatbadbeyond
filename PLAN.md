# Forward Renderer Roadmap

This plan aligns the renderer with a simple, principled, world-space forward raster pipeline.
The current frame loop, swapchain lifecycle, and camera flow remain the backbone.

## North Star

- CPU-driven forward renderer.
- Data-oriented scene representation.
- Minimal passes and explicit data flow.
- Add complexity only when profiling proves it is needed.

## Immediate Scope Changes

Remove and unwind:

- Path tracing storage image, sampler, and related descriptor layout/pool/set.
- Compute pipelines and ReSTIR buffers.
- Compute dispatches and compute-to-graphics synchronization barriers.

Keep:

- `FrameOverlap` resources (command pool/buffer, semaphores, fences).
- Dynamic rendering for swapchain images.
- Camera module and input integration.

## Engine Architecture Targets

CPU-side data:

- `MeshGpu`: vertex/index buffers and counts.
- `MaterialGpu`: pipeline key and descriptor binding data.
- `RenderObject`: `meshId`, `materialId`, world transform, optional bounds.
- `FrameGlobals`: view/proj, camera position, time, exposure.
- `Light`: directional plus a bounded array of point lights.
- Scene containers: `std::vector<RenderObject>` persistent scene list, plus `std::vector<DrawItem>` rebuilt each frame or on scene changes.

`DrawItem` baseline fields:

- `pipelineKey`, `materialId`, `meshId`, `firstIndex`, `indexCount`, `instanceCount`, `firstInstance`.

GPU-side data:

- Per-frame globals UBO (set 0).
- Light buffer (SSBO preferred; UBO acceptable for very small limits).
- Per-object transform buffer (SSBO for scalable instancing).
- No bindless or descriptor indexing in v1.

## Rendering Passes

Pass 0 (required): forward opaque to swapchain image.

- Begin dynamic rendering with color attachment.
- Include depth attachment from day 1.
- Draw opaque items sorted by pipeline, then material, then mesh.

Pass 1 (later): forward transparent.

- Reuse forward pass setup.
- Back-to-front sort by depth.
- Blend-enabled pipeline state.

## Vulkan Integration Plan

Public API additions in `src/greadbadbeyond.h`:

- `void CreateScene();`
- `void DestroyScene();`
- `void CreateForwardRenderer();`
- `void DestroyForwardRenderer();`
- `void DrawFrameForward(u32 frameIndex, u32 imageIndex);`

`VulkanData` additions:

- `VkPipelineLayout forwardPipelineLayout;`
- `VkPipeline forwardPipeline;`
- `VkDescriptorSetLayout frameSetLayout;`
- `VkDescriptorPool frameDescriptorPool;`
- `std::array<VkDescriptorSet, FrameOverlap> frameDescriptorSets;`
- `std::array<VkBuffer, FrameOverlap> frameUniformBuffers;`
- `std::array<VkDeviceMemory, FrameOverlap> frameUniformMemory;`

Depth resources:

- `VkImage depthImage;`
- `VkDeviceMemory depthMemory;`
- `VkImageView depthView;`
- `VkFormat depthFormat;`

Command recording shape:

- Transition swapchain image to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`.
- Transition depth image to `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL`.
- Begin dynamic rendering with color + depth.
- Bind forward pipeline and frame descriptor set.
- Draw sorted `DrawItem` list.
- End rendering.
- Transition swapchain image to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`.

## Phased Execution

0. Baseline confirmation (done)
- Keep acquire-record-submit-present and resize recreation paths stable.

1. Compute/path-tracing de-scope
- Remove compute descriptors, pipelines, shader wiring, and dispatch logic.
- Keep fullscreen/clear fallback only if needed for bring-up.

2. Depth + first forward draw
- Create/destroy depth image resources with swapchain lifecycle.
- Add simple forward shader pair and draw one hardcoded triangle/mesh with depth test.

3. Mesh pipeline
- Add `Vertex` format, vertex/index buffer upload path, indexed draw call.
- Render one mesh from scene data.

4. Frame globals
- Add per-frame globals UBO.
- Feed camera view/projection + time every frame.

5. Object transforms + instancing
- Add transform SSBO.
- Support instance draws for objects sharing mesh/material.

6. Lighting v0
- Implement directional + small point-light array in forward shader.
- Keep shader small and explicit.

7. CPU batching and sorting
- Build `DrawItem` list from scene.
- Stable sort by pipeline/material/mesh before issuing draws.

8. Transparent path (optional)
- Add separate transparent draw list and blending pipeline.

9. Advanced lighting scale (only when profiling requires)
- Add clustered forward light lists.
- Gate this work on measured light-count bottlenecks.

## Guardrails

- Each extra pass must be justified by measured wins.
- Avoid hidden systems; features should stay explainable with a few structs and a single clear draw loop.
