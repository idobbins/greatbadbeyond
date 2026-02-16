cmake_minimum_required(VERSION 3.24)

project(greadbadbeyond LANGUAGES CXX)

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
    GIT_TAG 3.4
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
    message(FATAL_ERROR "glslc (Vulkan SDK) is required to build embedded shaders")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders)
set(SHADER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
set(SHADER_EMBED_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.cmake)

set(TRIANGLE_COMP_SOURCE ${SHADER_SOURCE_DIR}/triangle.comp)
set(TRIANGLE_COMP_SPV ${SHADER_GENERATED_DIR}/triangle.comp.spv)
set(TRIANGLE_COMP_HEADER ${SHADER_GENERATED_DIR}/triangle_comp_spv.h)

add_custom_command(
    OUTPUT ${TRIANGLE_COMP_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_GENERATED_DIR}
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} -O ${TRIANGLE_COMP_SOURCE} -o ${TRIANGLE_COMP_SPV}
    DEPENDS ${TRIANGLE_COMP_SOURCE}
    COMMENT "Compiling triangle.comp to SPIR-V"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TRIANGLE_COMP_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${TRIANGLE_COMP_SPV}
        -DOUTPUT_FILE=${TRIANGLE_COMP_HEADER}
        -DSYMBOL_NAME=kTriangleCompSpv
        -P ${SHADER_EMBED_SCRIPT}
    DEPENDS ${TRIANGLE_COMP_SPV} ${SHADER_EMBED_SCRIPT}
    COMMENT "Embedding triangle.comp.spv into C++ header"
    VERBATIM
)

add_custom_target(embedded_shaders
    DEPENDS ${TRIANGLE_COMP_HEADER}
)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

add_dependencies(${PROJECT_NAME} embedded_shaders)
target_include_directories(${PROJECT_NAME} PRIVATE ${SHADER_GENERATED_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
