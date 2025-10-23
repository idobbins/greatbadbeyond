cmake_minimum_required(VERSION 3.28)

if(WIN32)
    set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe"
            CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(callandor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- GLFW ---
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

# --- Vulkan Memory Allocator (VMA) ---
FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

find_package(Vulkan REQUIRED)

# --- Executable ---
add_executable(${PROJECT_NAME}
        src/main.cpp
)

# --- C++ Module(s) ---
target_sources(${PROJECT_NAME}
        PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES
        FILES
        src/assert.ixx
        src/glfw.ixx
        src/platform.ixx
        src/window.ixx
)

# --- Includes & Linking ---
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan VulkanMemoryAllocator)

# Newer versions of clang seem to not have dedicated clang++
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # CLion may invoke clang instead of clang++, so add libc++/c++abi explicitly.
    find_library(CPP_LIB c++)
    if(CPP_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CPP_LIB})
    endif()

    find_library(CPPABI_LIB c++abi)
    if(CPPABI_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CPPABI_LIB})
    endif()
endif()

# --- Compiler warnings ---
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
