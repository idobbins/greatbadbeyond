# Repository Guidelines

## Project Structure & Module Organization
- `src/runtime.h` owns `GlobalData`, shared macros, and the logging/assert declarations shared by all translation units.
- `src/main.c` keeps the application entry point (GLFW/window/input loop, frame stats) and delegates Vulkan work to the new modules.
- `src/vk_bootstrap.{c,h}` initialize and tear down the instance, device, queues, command pool/buffer, sync primitives, and VMA allocator.
- `src/vk_descriptors.{c,h}` create the descriptor layout/pool/set and provide `UpdateComputeDescriptorSet`.
- `src/vk_pipelines.{c,h}` load SPIR-V modules and build/destroy the compute and blit pipelines.
- `src/rt_resources.{c,h}` own swapchain-sized buffers, the gradient image, and the descriptor writes.
- `src/rt_frame.{c,h}` record the per-frame command buffer (compute dispatch + blit) and expose `RtUpdateSpawnArea`.
- `src/vk_swapchain.{c,h}` manage swapchain lifetime and call into resources/pipelines for rebuilds; they expose `CreateSwapchain`, `DestroySwapchain`, `RecreateSwapchain`, and `VulkanDrawFrame`.
- `shaders/bindings.inc.glsl` mirrors the C binding IDs from `src/shader_bindings.h` so GLSL includes stay in sync.
- `shaders/blit.glsl` and `shaders/compute.glsl` compile to SPIR-V under `build/*/shaders`; keep sources human-readable here.
- `CMakeLists.txt` configures the C17 toolchain, FetchContent for GLFW, shader compilation targets, and includes the new source files.
- Generated `cmake-build-*` directories come from IDE builds; prefer fresh `build/<config>` folders for reproducible output.

## Build, Test, and Development Commands
- `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug` configures a debug tree; rerun after dependency or toolchain changes.
- `cmake --build build/debug` builds the executable and triggers `compile_shaders`.
- `./build/debug/callandor` launches the renderer; verify the Vulkan runtime and GPU driver before running.
- `cmake --build build/debug --target compile_shaders` recompiles SPIR-V artifacts after shader edits.
- For optimized binaries, run `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release` followed by `cmake --build build/release`.

## Coding Style & Naming Conventions
- Target ISO C17 with warnings enabled; keep 4-space indentation and brace placement consistent with `src/main.c`.
- Prefer `static` functions for translation-unit helpers, uppercase snake case for macros/constants, and camelCase for functions and locals.
- Extend logging via `LogInfo`, `LogWarn`, and `LogError`; match the existing prefixes for searchable output.

Reference naming patterns (adapted from raylib) when introducing new symbols:

Code element | Convention | Example
--- | :---: | ---
Defines | ALL_CAPS | `#define PLATFORM_DESKTOP`
Macros | ALL_CAPS | `#define MIN(a,b) (((a)<(b))?(a):(b))`
Variables | lowerCase | `int screenWidth = 0;`, `float targetFrameTime = 0.016f;`
Local variables | lowerCase | `Vector2 playerPosition = { 0 };`
Global variables | lowerCase | `bool windowReady = false;`
Constants | lowerCase | `const int maxValue = 8;`
Pointers | MyType *pointer | `Texture2D *array = NULL;`
float values | always x.xf | `float gravity = 10.0f` (avoid `10.f`)
Operators | value1*value2 | `int product = value*6;`
Operators | value1/value2 | `int division = value/4;`
Operators | value1 + value2 | `int sum = value + 10;`
Operators | value1 - value2 | `int res = value - 5;`
Enum | TitleCase | `enum TextureFormat`
Enum members | ALL_CAPS | `PIXELFORMAT_UNCOMPRESSED_R8G8B8`
Struct | TitleCase | `struct Texture2D`, `struct Material`
Struct members | lowerCase | `texture.width`, `color.r`
Functions | TitleCase | `InitWindow()`, `LoadImageFromMemory()`
Function params | lowerCase | `width`, `height`
Ternary operator | (condition)? result1 : result2 | `printf("Value is 0: %s", (value == 0)? "yes" : "no");`

Global renderer state follows the raylib-style pattern: group buckets inside `GlobalData` use TitleCase names (for example `Glfw`, `Window`, `Vulkan`) and the singleton instance exposed in `main.c` is named `GLOBAL`.

Additional formatting guidance:
- Always initialize defined variables.
- Use four spaces, never tabs; avoid trailing whitespace.
- Place comments on the line before the code they describe, start with a capitalized sentence, omit trailing periods: `// This is a comment`.
- Include a space after control flow keywords: `if (condition)` and `while (!WindowShouldClose())`.
- Keep condition expressions in parentheses except for boolean literals: `if ((value > 1) && valueActive)`.
- Align opening and closing braces vertically.

```c
void SomeFunction()
{
    // TODO: Something helpful
}
```

## Files and Directories Naming
- Use `snake_case` for directories: `resources/models`, `resources/fonts`.
- Use `snake_case` for files: `main_title.png`, `cubicmap.png`, `sound.wav`.
- Avoid spaces or special characters in filesystem names.

## Resource Organization
- Group data files by loading phase and runtime usage to streamline asset streaming.
- Choose descriptive filenames so asset intent is clear without additional context.
- Example layout:

```
resources/audio/fx/long_jump.wav
resources/audio/music/main_theme.ogg
resources/screens/logo/logo.png
resources/screens/title/title.png
resources/screens/gameplay/background.png
resources/characters/player.png
resources/characters/enemy_slime.png
resources/common/font_arial.ttf
resources/common/gui.png
```

## Testing Guidelines
- Automated tests are not yet present; perform manual smoke tests by running the app with validation layers enabled.
- Watch stdout/stderr for `info:`, `warn:`, and `error:` prefixes; resolve warnings before merging.
- When introducing tests, place harnesses under `tests/` and register new CMake targets so they run alongside the build.

## Commit & Pull Request Guidelines
- Follow repository history: concise (<60 char) sentence-style summaries such as `full compute pipeline`; add bodies when more context helps.
- Reference related issues using `fixes #123` and describe shader or pipeline impacts explicitly.
- Pull requests should include a short change summary, configuration used (`VK_LAYER_KHRONOS_validation`, GPU/driver), test evidence (logs or screenshots), and call out shader artifacts or new assets.

## Security & Configuration Tips
- Install the Vulkan SDK to provide `glslc`; export `VULKAN_SDK` so CMake can locate headers and binaries.
- Keep shader paths relative and avoid committing user-specific absolute directories or credentials.
