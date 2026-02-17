cmake_minimum_required(VERSION 3.24)

project(greatbadbeyond LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJC)
else()
    message(FATAL_ERROR "This GLFW-free build currently targets macOS only.")
endif()

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

set(GRADIENT_COMP_SOURCE ${SHADER_SOURCE_DIR}/gradient.comp)
set(GRADIENT_COMP_SPV ${SHADER_GENERATED_DIR}/gradient.comp.spv)
set(GRADIENT_COMP_HEADER ${SHADER_GENERATED_DIR}/gradient_comp_spv.h)

add_custom_command(
    OUTPUT ${GRADIENT_COMP_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_GENERATED_DIR}
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${GRADIENT_COMP_SOURCE} -o ${GRADIENT_COMP_SPV}
    DEPENDS ${GRADIENT_COMP_SOURCE}
    COMMENT "Compiling gradient.comp to SPIR-V"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GRADIENT_COMP_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${GRADIENT_COMP_SPV}
        -DOUTPUT_FILE=${GRADIENT_COMP_HEADER}
        -DSYMBOL_NAME=kGradientCompSpv
        -P ${SHADER_EMBED_SCRIPT}
    DEPENDS ${GRADIENT_COMP_SPV} ${SHADER_EMBED_SCRIPT}
    COMMENT "Embedding gradient.comp.spv into C header"
    VERBATIM
)

add_custom_target(embedded_shaders
    DEPENDS ${GRADIENT_COMP_HEADER}
)

add_executable(${PROJECT_NAME}
    src/main.c
    src/macos_platform.m
)

add_dependencies(${PROJECT_NAME} embedded_shaders)
target_include_directories(${PROJECT_NAME} PRIVATE ${SHADER_GENERATED_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    "-framework AppKit"
    "-framework QuartzCore"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
