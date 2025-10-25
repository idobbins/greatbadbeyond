# Render Roadmap

Linear sequence of work items that graduates the app from the current code (GLFW window + Vulkan device + swapchain/resize handling already implemented) to a compute path tracer that blits to a fullscreen triangle.

0. **Current Baseline (Done)**
   - GLFW context + window lifecycle, surface creation, and resize callbacks are wired.
   - Vulkan instance, debug messenger, surface, physical device, logical device, queues, and swapchain/image-view management exist with `RecreateSwapchain()` handling VK out-of-date/suboptimal cases.
   - Main loop currently only polls events; no command buffers, sync objects, or rendering work submit yet.

1. **Frame Infrastructure (Done)**
   - Implemented frame cache with two-frame overlap: per-frame command pool, buffer, fence, and image-available semaphore plus per-swapchain-image render-finished semaphores and ownership fences.
   - Added `RecordCommandBuffer(frame, imageIndex, clearColor)` using dynamic rendering to clear the current swapchain image with the configured color.
   - Main loop now runs acquire → record → submit → present every tick, recreating the swapchain on VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR or resize callbacks.

2. **Solid-Color Output (Done)**
   - Added GLSL fullscreen-triangle vertex/fragment shaders plus a CMake-driven `glslc` pipeline that compiles them into `build/*/shaders/*.spv` whenever the sources change.
   - The Vulkan backend now builds shader modules, a push-constant-only pipeline layout, and a dynamic-rendering graphics pipeline that re-creates itself alongside the swapchain.
   - `RecordCommandBuffer()` binds the fullscreen pipeline, pushes the frame color, and draws the triangle so every submission now renders a constant tint instead of relying on attachment clears.

3. **UV Gradient Pass (Done)**
   - Fullscreen vertex shader now emits per-vertex UVs scaled by live framebuffer resolution via the new `GradientParams` push constants, so attribute interpolation matches the visible surface even after swapchain recreation.
   - Fragment shader consumes those UVs, normalizes them against the current resolution, and outputs `vec4(uv, wave, 1.0)` where `wave` is derived from the push-constant time parameter, giving a clear animated gradient.
   - The Vulkan backend defines/pushes `GradientParams` every frame: `MainLoop()` fills resolution+time from `GetFramebufferSize()`/`glfwGetTime()`, `RecordCommandBuffer()` forwards them via `vkCmdPushConstants`, and the fullscreen pipeline layout now exposes the range to both shader stages.

4. **Compute Storage Image Plumbing**
   - Allocate a storage image matching the swapchain extent plus the associated image view and memory.
   - Define descriptor set layouts/pools binding the storage image so a compute pass can write into it.
   - Update the fullscreen pipeline to sample from this image instead of generating colors procedurally, setting the stage for the compute output blit.

5. **Minimal Compute Path Tracer**
   - Implement a basic compute shader that writes a flat color or gradient into the storage image via descriptors.
   - Sequence: dispatch compute → insert proper pipeline barriers → run fullscreen triangle to present the compute result.
   - Establish CPU-side parameters (camera structs, time) and route them through push constants or uniform buffers.

6. **Path Tracer Features**
   - Expand the compute shader into an actual path tracer (ray generation, scene description, accumulation).
   - Add accumulation buffers, random seed management, and camera controls (position/orientation, FOV) mapped to input events.
   - Integrate flight controls from platform input to manipulate the camera each frame, document the workflow in `PERF.md`, and ensure hot-resize plus swapchain recreation keeps accumulation/state coherent.
