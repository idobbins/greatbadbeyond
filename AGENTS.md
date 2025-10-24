# Repository Guidelines

## Project Structure & Module Organization
- `src/main.cpp` owns the application entry point; it spins up GLFW, calls into the Vulkan setup helpers, drives the event loop, and tears everything down.
- `src/callandor.h` is the shared header that centralizes type aliases, forward declarations, and cross-module APIs so source files include a single entry point.
- `src/config.h` captures compile-time configuration such as cache sizes, default window parameters, and log prefixes used across modules.
- `src/utils.h` implements inline logging helpers and the `Assert` utility that all translation units rely on.
- `src/platform.cpp` wraps GLFW setup, window creation/destruction, event polling, and the platform-side `PlatformData` cache; it also exposes the Vulkan instance extension discovery helper.
- `src/vulkan.cpp` performs all Vulkan bootstrapping: instance creation, surface management, physical device selection, logical device/queue creation, and feature queries.
- `PERF.md` and other Markdown files track design notes and should be kept in sync with the active code paths.
- `CMakeLists.txt` configures the C++ toolchain, fetches GLFW, finds Vulkan, and builds the executable from the platform and Vulkan translation units.
- Generated `cmake-build-*` directories come from IDE builds; prefer fresh `build/<config>` folders for reproducible output.

## Build, Test, and Development Commands
- `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug` configures a debug tree; rerun after dependency or toolchain changes.
- `cmake --build build/debug` compiles the executable with the GLFW and Vulkan dependencies hydrated by CMake.
- `./build/debug/callandor` launches the renderer; make sure the Vulkan runtime and GPU driver are present on the machine first.
- For optimized binaries, run `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release` followed by `cmake --build build/release`.

## Coding Style & Naming Conventions
- Target ISO C++23 with warnings enabled, but write code in the raylib-style C subset: four-space indentation, brace placement matching the current `.cpp` files, and no RTTI, exceptions, templates, or STL algorithms beyond the handful already in use.
- Do not add translation-unit `static` helper functions; every callable routine (even small helpers) must have external linkage, live in a `.cpp` file, and be declared in `callandor.h` so the header remains the authoritative map of engine capabilities.
- Reach for the small set of approved C++ features already present (`std::span`, `std::array`, `std::pmr::vector`, structured bindings) only when they simplify the C-style flow; avoid adding heavy abstractions or heap-owning types.
- Logging flows through `LogInfo`, `LogWarn`, and `LogError` in `utils.h`; reuse them for consistent prefixes.
- Always initialize variables, keep comments on the line before the code they explain (`// Describe the block`), include spaces after control flow keywords (`if (condition)`), and align braces vertically.

Reference naming patterns (adapted from raylib):

Code element | Convention | Example
--- | :---: | ---
Defines | ALL_CAPS | `#define PLATFORM_DESKTOP`
Macros | ALL_CAPS | `#define MIN(a,b) (((a)<(b))?(a):(b))`
Variables | lowerCase | `int screenWidth = 0;`, `float targetFrameTime = 0.016f;`
Local variables | lowerCase | `Vector2 playerPosition = { 0 };`
Global state buckets | TitleCase members inside structs | `Platform.Window.ready`
Constants | lowerCase `constexpr` | `constexpr int swapchainImageCount = 3;`
Pointers | MyType *pointer | `GLFWwindow *handle = nullptr;`
float values | always x.xf | `float gravity = 10.0f`
Operators | value1*value2 | `int product = value*6;`
Operators | value1/value2 | `int division = value/4;`
Operators | value1 + value2 | `int sum = value + 10;`
Operators | value1 - value2 | `int res = value - 5;`
Enum types | TitleCase | `enum TextureFormat`
Enum members | ALL_CAPS | `PIXELFORMAT_UNCOMPRESSED_R8G8B8`
Struct types | TitleCase | `struct VulkanData`
Struct members | lowerCase | `config.debug`
Functions | TitleCase | `CreateVulkan()`, `PollEvents()`
Function params | lowerCase | `width`, `height`
Ternary operator | (condition)? result1 : result2 | `value = (ready)? active : idle;`

Additional formatting guidance:
- Global renderer state follows raylib's pattern: group related data inside translation-unit `static` structs (for example `Platform` and `Vulkan`) but surface the controlling functions via `callandor.h` so the single header still captures the behavior.
- Use `nullptr`, `VK_NULL_HANDLE`, and explicit zero-initialization braces to make intent obvious.
- Avoid trailing whitespace and keep headers sorted logically (`callandor.h`, then local headers, then system headers).

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
