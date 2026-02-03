# Repository Guidelines

## Scope & Rule Sources
- Applies to all files in this repository unless a nested `AGENTS.md` overrides it.
- No Cursor rules found (`.cursor/rules/` and `.cursorrules` are absent).
- No Copilot instructions found (`.github/copilot-instructions.md` is absent).

## Project Structure & Module Organization
- `src/main.cpp` owns the application entry point; it spins up GLFW, calls into the Vulkan setup helpers, drives the event loop, and tears everything down.
- `src/callandor.h` is the shared header that centralizes type aliases, forward declarations, and cross-module APIs so source files include a single entry point.
- `src/config.h` captures compile-time configuration such as cache sizes, default window parameters, and log prefixes used across modules.
- `src/utils.h` implements inline logging helpers and the `Assert` utility that all translation units rely on.
- `src/platform.cpp` wraps GLFW setup, window creation/destruction, event polling, and the platform-side `PlatformData` cache; it also exposes the Vulkan instance extension discovery helper.
- `src/vulkan.cpp` performs all Vulkan bootstrapping: instance creation, surface management, physical device selection, logical device/queue creation, and feature queries.
- `src/camera.cpp` manages camera state, input handling, and camera parameter packing.
- `PERF.md` and other Markdown files track design notes and should be kept in sync with the active code paths.
- `CMakeLists.txt` configures the C++ toolchain, fetches GLFW, finds Vulkan, and builds the executable from the platform, camera, and Vulkan translation units.
- Generated `cmake-build-*` directories come from IDE builds; prefer fresh `build/<config>` folders for reproducible output.

## Build, Test, and Lint Commands
- Debug configure: `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug`.
- Debug build: `cmake --build build/debug` (also compiles shaders via the `shaders` target).
- Run the app: `./build/debug/callandor`.
- Release configure: `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release`.
- Release build: `cmake --build build/release`.
- Shader-only rebuild: `cmake --build build/debug --target shaders`.
- Clean reconfigure (optional): delete `build/<config>` and rerun CMake.
- Tests: none are defined yet; add targets under `tests/` and register with CTest.
- Run all tests (once added): `ctest --test-dir build/debug`.
- Run a single test (once added): `ctest --test-dir build/debug -R <test_name>`.
- Lint/format: no dedicated tooling configured; rely on compiler warnings.

## Tooling Notes
- CMake exports `compile_commands.json` under `build/<config>` for clangd/IDE integration.
- Treat new compiler warnings as regressions and fix them immediately.

## Shader Workflow
- Shader sources live under `resources/shaders`.
- When adding shaders, update `SHADER_SOURCES` in `CMakeLists.txt`.
- SPIR-V outputs land under `build/<config>/shaders`.
- `SHADER_CACHE_DIRECTORY` is set by CMake; keep shader paths relative.
- Rebuild shaders only with `cmake --build build/debug --target shaders`.

## Adding or Modifying Source Files
- Register new `.cpp` files in `CMakeLists.txt` `add_executable` sources.
- Prefer adding declarations to `callandor.h` so new APIs are discoverable.
- Keep new headers minimal and only when they are shared across modules.
- Avoid introducing new external dependencies without approval.

## Runtime / Environment Notes
- Vulkan SDK is required; ensure `glslc` is on `PATH` or `VULKAN_SDK` points to it.
- CMake configuration fails if `glslc` is missing.
- Debug builds enable `VK_LAYER_KHRONOS_validation`; make sure the layer is installed.
- Use Release builds to disable validation layers and debug checks.
- Vulkan 1.3-capable drivers are required (the code checks API version).
- macOS builds require the Vulkan portability subset (MoltenVK).
- If the validation layer is missing, `CreateInstance()` will assert early.

## API / Module Change Checklist
- Declare new public functions/types in `callandor.h`.
- Keep `Create*`/`Destroy*` pairs symmetric and called from `main.cpp`.
- Update `config.h` for new limits/defaults instead of hard-coded magic numbers.
- If render pipelines or shaders change, update `PERF.md` alongside code.

## Coding Style & Naming Conventions
- Target ISO C++23 with warnings enabled, but write code in the raylib-style C subset: four-space indentation, brace placement matching the current `.cpp` files, and no RTTI, exceptions, templates, or STL algorithms beyond the handful already in use.
- Use the primitive aliases in `callandor.h` (`u32`, `f32`, etc.) for engine-facing types.
- Prefer `std::array`, `std::span`, and `std::pmr::vector` for fixed-size or scratch data; avoid heap-owning abstractions.
- Do not add translation-unit `static` helper functions; every callable routine (even small helpers) must have external linkage, live in a `.cpp` file, and be declared in `callandor.h` so the header remains the authoritative map of engine capabilities.
- Global renderer state follows raylib's pattern: group related data inside translation-unit `static` structs (for example `Platform` and `Vulkan`) but surface the controlling functions via `callandor.h`.
- Includes order: `callandor.h`, local headers (`config.h`, `utils.h`), third-party headers, then standard headers; keep blank lines between groups.
- `using namespace std;` appears in existing files; follow local patterns and keep `std::` qualifiers when clarity matters.
- Always initialize variables, keep comments on the line before the code they explain (`// Describe the block`), include spaces after control flow keywords (`if (condition)`), and align braces vertically.
- Prefer trailing return syntax (`auto Foo() -> Type`) for non-void functions, but leave `void Foo()` in the traditional form so side-effect-only routines stand out when scanning declarations.
- Use `nullptr`, `VK_NULL_HANDLE`, and explicit zero-initialization braces to make intent obvious.
- Avoid trailing whitespace and keep headers sorted logically (`callandor.h`, then local headers, then system headers).

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

## Data & Memory Practices
- Prefer stack allocation or fixed-size caches; avoid `new`/`delete`.
- Use `static constexpr` for limits and keep cache sizes in `config.h`.
- Expose cached arrays with `std::span` instead of copying.
- Reset Vulkan handles to `VK_NULL_HANDLE` after destruction.
- Avoid recursion; use explicit loops and small helper structs.

## Error Handling & Logging
- Use `Assert` from `utils.h` for contract failures; it logs and exits.
- Use `LogInfo`, `LogWarn`, and `LogError` for runtime diagnostics to keep prefixes consistent.
- Check Vulkan return codes and propagate/handle `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` as in `MainLoop()`.

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
