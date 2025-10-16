cmake_minimum_required(VERSION 3.24)

if(WIN32)
    set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe"
      CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(callandor LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

find_package(Vulkan REQUIRED)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc executable not found. Ensure the Vulkan SDK is installed and glslc is on PATH.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(COMPUTE_SHADER_SOURCE ${SHADER_SOURCE_DIR}/compute.glsl)
set(BLIT_SHADER_SOURCE ${SHADER_SOURCE_DIR}/blit.glsl)

set(COMPUTE_SHADER_SPV ${SHADER_OUTPUT_DIR}/compute.spv)
set(BLIT_VERTEX_SHADER_SPV ${SHADER_OUTPUT_DIR}/blit.vert.spv)
set(BLIT_FRAGMENT_SHADER_SPV ${SHADER_OUTPUT_DIR}/blit.frag.spv)

add_custom_command(
    OUTPUT ${COMPUTE_SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute -o ${COMPUTE_SHADER_SPV} ${COMPUTE_SHADER_SOURCE}
    DEPENDS ${COMPUTE_SHADER_SOURCE}
    COMMENT "Compiling compute.glsl"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BLIT_VERTEX_SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -DVERTEX_SHADER -fshader-stage=vertex -o ${BLIT_VERTEX_SHADER_SPV} ${BLIT_SHADER_SOURCE}
    DEPENDS ${BLIT_SHADER_SOURCE}
    COMMENT "Compiling blit.glsl (vertex)"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BLIT_FRAGMENT_SHADER_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} -DFRAGMENT_SHADER -fshader-stage=fragment -o ${BLIT_FRAGMENT_SHADER_SPV} ${BLIT_SHADER_SOURCE}
    DEPENDS ${BLIT_SHADER_SOURCE}
    COMMENT "Compiling blit.glsl (fragment)"
    VERBATIM
)

add_custom_target(compile_shaders
    DEPENDS
        ${COMPUTE_SHADER_SPV}
        ${BLIT_VERTEX_SHADER_SPV}
        ${BLIT_FRAGMENT_SHADER_SPV}
)

add_executable(${PROJECT_NAME}
  src/main.c
)

add_dependencies(${PROJECT_NAME} compile_shaders)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    glfw
    Vulkan::Vulkan
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(${PROJECT_NAME}
  PRIVATE
    VULKAN_SHADER_DIRECTORY="${SHADER_OUTPUT_DIR}"
)
