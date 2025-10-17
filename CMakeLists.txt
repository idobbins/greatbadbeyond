cmake_minimum_required(VERSION 3.24)

if(WIN32)
    set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe"
      CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(callandor LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

find_package(Vulkan REQUIRED)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc executable not found. Ensure the Vulkan SDK is installed and glslc is on PATH.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(COMPUTE_SHADER_SOURCE ${SHADER_SOURCE_DIR}/compute.glsl)
set(BLIT_SHADER_SOURCE ${SHADER_SOURCE_DIR}/blit.glsl)

set(SPHERES_INIT_SPV       ${SHADER_OUTPUT_DIR}/spheres_init.spv)
set(PRIMARY_INTERSECT_SPV  ${SHADER_OUTPUT_DIR}/primary_intersect.spv)
set(SHADE_SHADOW_SPV       ${SHADER_OUTPUT_DIR}/shade_shadow.spv)
set(BLIT_VERTEX_SHADER_SPV ${SHADER_OUTPUT_DIR}/blit.vert.spv)
set(BLIT_FRAGMENT_SHADER_SPV ${SHADER_OUTPUT_DIR}/blit.frag.spv)

add_custom_command(
    OUTPUT ${SPHERES_INIT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -I${SHADER_SOURCE_DIR} -fshader-stage=compute -DKERNEL_SPHERES_INIT -o ${SPHERES_INIT_SPV} ${COMPUTE_SHADER_SOURCE}
    DEPENDS ${COMPUTE_SHADER_SOURCE}
    COMMENT "Compiling spheres_init"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PRIMARY_INTERSECT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -I${SHADER_SOURCE_DIR} -fshader-stage=compute -DKERNEL_PRIMARY_INTERSECT -o ${PRIMARY_INTERSECT_SPV} ${COMPUTE_SHADER_SOURCE}
    DEPENDS ${COMPUTE_SHADER_SOURCE}
    COMMENT "Compiling primary_intersect"
    VERBATIM
)

add_custom_command(
    OUTPUT ${SHADE_SHADOW_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -I${SHADER_SOURCE_DIR} -fshader-stage=compute -DKERNEL_SHADE_SHADOW -o ${SHADE_SHADOW_SPV} ${COMPUTE_SHADER_SOURCE}
    DEPENDS ${COMPUTE_SHADER_SOURCE}
    COMMENT "Compiling shade_shadow"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BLIT_VERTEX_SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -I${SHADER_SOURCE_DIR} -DVERTEX_SHADER -fshader-stage=vertex -o ${BLIT_VERTEX_SHADER_SPV} ${BLIT_SHADER_SOURCE}
    DEPENDS ${BLIT_SHADER_SOURCE}
    COMMENT "Compiling blit.glsl (vertex)"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BLIT_FRAGMENT_SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -I${SHADER_SOURCE_DIR} -DFRAGMENT_SHADER -fshader-stage=fragment -o ${BLIT_FRAGMENT_SHADER_SPV} ${BLIT_SHADER_SOURCE}
    DEPENDS ${BLIT_SHADER_SOURCE}
    COMMENT "Compiling blit.glsl (fragment)"
    VERBATIM
)

add_custom_target(compile_shaders
    DEPENDS
        ${SPHERES_INIT_SPV}
        ${PRIMARY_INTERSECT_SPV}
        ${SHADE_SHADOW_SPV}
        ${BLIT_VERTEX_SHADER_SPV}
        ${BLIT_FRAGMENT_SHADER_SPV}
)

add_executable(${PROJECT_NAME}
  src/main.c
  src/vk_bootstrap.c
  src/vk_descriptors.c
  src/vk_pipelines.c
  src/rt_resources.c
  src/rt_frame.c
  src/vk_swapchain.c
  src/vma_impl.cpp
)

add_dependencies(${PROJECT_NAME} compile_shaders)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    glfw
    Vulkan::Vulkan
)

if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

target_include_directories(${PROJECT_NAME}
  PRIVATE
    ${vma_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(src/vma_impl.cpp
      PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-missing-field-initializers;-Wno-unused-function")
elseif(MSVC)
    set_source_files_properties(src/vma_impl.cpp
      PROPERTIES
        COMPILE_OPTIONS "/wd4100;/wd4189;/wd4505")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(${PROJECT_NAME}
  PRIVATE
    VULKAN_SHADER_DIRECTORY="${SHADER_OUTPUT_DIR}"
    VMA_NULLABLE=
    VMA_NOT_NULL=
    VMA_NULLABLE_NON_DISPATCHABLE=
    VMA_NOT_NULL_NON_DISPATCHABLE=
)
