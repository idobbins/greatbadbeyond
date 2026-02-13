cmake_minimum_required(VERSION 3.24)

if(WIN32)
    set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe"
      CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(greadbadbeyond LANGUAGES CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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

if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE
        NAMES glslc
        HINTS $ENV{VULKAN_SDK}/bin
    )
endif()

if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc (Vulkan SDK) is required to build shaders")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/forward_opaque.vert
    ${SHADER_SOURCE_DIR}/forward_opaque.frag
)

set(SHADER_OUTPUTS "")
foreach(shader ${SHADER_SOURCES})
    get_filename_component(shader_file ${shader} NAME)
    set(output_file ${SHADER_OUTPUT_DIR}/${shader_file}.spv)
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -o ${output_file} ${shader}
        DEPENDS ${shader}
        COMMENT "Compiling ${shader_file} to SPIR-V"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${output_file})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

find_program(UV_EXECUTABLE NAMES uv)
if(NOT UV_EXECUTABLE)
    message(FATAL_ERROR "uv is required for manifest code generation")
endif()

set(KENNEY_MANIFEST_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_kenney_manifest.sh)
set(KENNEY_MANIFEST_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/resources/external/kenney_asset_manifest.tsv)
set(KENNEY_MANIFEST_CODEGEN ${CMAKE_CURRENT_SOURCE_DIR}/tools/manifest_codegen/generate_manifest.py)
set(KENNEY_MANIFEST_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/manifest.h)
set(KENNEY_MANIFEST_PACK ${CMAKE_CURRENT_SOURCE_DIR}/resources/external/kenney_assets.pack)

add_custom_command(
    OUTPUT ${KENNEY_MANIFEST_HEADER} ${KENNEY_MANIFEST_PACK}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND /bin/bash ${KENNEY_MANIFEST_SCRIPT}
    COMMAND ${CMAKE_COMMAND} -E env UV_CACHE_DIR=/tmp/uv-cache ${UV_EXECUTABLE} run python ${KENNEY_MANIFEST_CODEGEN}
            --input ${KENNEY_MANIFEST_INPUT}
            --source-root ${CMAKE_CURRENT_SOURCE_DIR}/resources
            --header ${KENNEY_MANIFEST_HEADER}
            --blob ${KENNEY_MANIFEST_PACK}
    DEPENDS
        ${KENNEY_MANIFEST_INPUT}
        ${KENNEY_MANIFEST_SCRIPT}
        ${KENNEY_MANIFEST_CODEGEN}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_kenney_semantic_index.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_kenney_index.sh
    COMMENT "Generating manifest.h and Kenney manifest pack"
    VERBATIM
)

add_custom_target(kenney_manifest_codegen DEPENDS ${KENNEY_MANIFEST_HEADER} ${KENNEY_MANIFEST_PACK})

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/manifest_blob.cpp
  src/camera.cpp
  src/platform.cpp
  src/vulkan.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan)
add_dependencies(${PROJECT_NAME} shaders kenney_manifest_codegen)
target_precompile_headers(${PROJECT_NAME} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${KENNEY_MANIFEST_HEADER}>"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_CACHE_DIRECTORY="${SHADER_OUTPUT_DIR}"
    ASSET_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
