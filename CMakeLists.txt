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

FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG        v1.3.1
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

if(NOT TARGET ZLIB::ZLIB)
    if(TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    else()
        message(FATAL_ERROR "ZLIB target not found after FetchContent")
    endif()
endif()

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
    ${SHADER_SOURCE_DIR}/shadow_depth.vert
    ${SHADER_SOURCE_DIR}/sky.vert
    ${SHADER_SOURCE_DIR}/sky.frag
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

set(KENNEY_MANIFEST_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/generated/manifest.h)
set(KENNEY_MANIFEST_PACK ${CMAKE_CURRENT_SOURCE_DIR}/resources/external/kenney_assets.pack)

if(NOT EXISTS ${KENNEY_MANIFEST_HEADER})
    message(FATAL_ERROR
        "Missing generated manifest header: ${KENNEY_MANIFEST_HEADER}\n"
        "Run: uv run greatbadbeyond all")
endif()

if(NOT EXISTS ${KENNEY_MANIFEST_PACK})
    message(WARNING
        "Missing runtime pack file: ${KENNEY_MANIFEST_PACK}\n"
        "Run: uv run greatbadbeyond all")
endif()

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/manifest_blob.cpp
  src/camera.cpp
  src/platform.cpp
  src/vulkan.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan ZLIB::ZLIB)
add_dependencies(${PROJECT_NAME} shaders)
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
