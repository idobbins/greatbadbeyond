# Minimal Render Loop Plan

1. **Surface & Swapchain Policy**
   - Implement `CreateSwapchain()`, `RecreateSwapchain()`, and `DestroySwapchain()` so they consume the surface capability helpers, clamp the extent to the GLFW framebuffer size, prefer `VK_FORMAT_B8G8R8A8_SRGB`, and pick `VK_PRESENT_MODE_MAILBOX_KHR` with FIFO fallback.
   - Route window resize/minimized signals from `platform.cpp` so swapchain recreation happens on `VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`, or size changes.

2. **Swapchain Images & Views**
   - Enumerate swapchain images and create one `VkImageView` per image, exposing helpers that return the spans stored in the Vulkan module.
   - Define a lightweight “frame” cache containing image view references, command buffer handles, and sync primitives so the renderer can iterate `MaxFramesInFlight`.

3. **Command Infrastructure**
   - Flesh out the command pool/command buffer helpers: one graphics-capable command pool, one primary command buffer per frame, and helpers to reset/record them every frame.
   - Add `RecordCommandBuffer(frame, imageIndex, clearColor)` that begins dynamic rendering against the swapchain image view, binds the fullscreen pipeline, and issues a single triangle draw.

4. **Synchronization Objects**
   - Implement `CreateSemaphore()`, `CreateFence()`, and their destroy counterparts to allocate per-frame image-available and render-finished semaphores plus fences for CPU/GPU pacing.
   - Integrate sync objects into the main loop: wait/reset fence → acquire image → submit graphics work → signal/present.

5. **Pipeline & Shaders**
   - Author a fullscreen-triangle vertex shader and a fragment shader that outputs a hardcoded color (SPIR-V blobs under `resources/shaders/`).
   - Create a minimal pipeline layout (no descriptors yet) and a graphics pipeline that leverages dynamic rendering with the chosen swapchain format.
   - Document how this pipeline will later sample the compute shader’s storage image so the design aligns with the future full-screen blit path.

6. **Main Loop Integration**
   - After device creation, initialize swapchain, image views, frame cache, command buffers, sync objects, shaders, and pipeline; register teardown order for shutdown.
   - Update `MainLoop()`/Vulkan driver to execute the acquire → record → submit → present sequence each frame, handling `VK_ERROR_OUT_OF_DATE_KHR` by recreating the swapchain stack.

7. **Future-Proofing for Compute Path Tracer**
   - Add placeholders for a storage image (same extent as swapchain) and descriptor layout slots so the compute path tracer can write into it before the fullscreen pass samples it.
   - Capture the intended compute → blit flow and state ownership in `PERF.md` so future work can extend this minimal renderer without rearchitecting.
