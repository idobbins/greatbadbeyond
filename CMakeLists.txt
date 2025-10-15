cmake_minimum_required(VERSION 3.28)

# Windows RC must be set BEFORE project()
if(WIN32)
  set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe"
      CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(callandor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Always fetch GLFW (no docs/tests/examples)
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4)
FetchContent_MakeAvailable(glfw)

find_package(Vulkan REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Ban relative includes that climb out with ../ ---
file(GLOB_RECURSE CALLANDOR_CODE
  "${SRC_DIR}/*.c" "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp")
foreach(f IN LISTS CALLANDOR_CODE)
  # Lines like: #include "../foo"
  file(STRINGS "${f}" BAD_LINES REGEX "^[ \t]*#include[ \t]*[<\"]\\.\\./")
  if(BAD_LINES)
    message(FATAL_ERROR "Relative include '../' is not allowed in: ${f}\n${BAD_LINES}")
  endif()
endforeach()

# --- Forced manual unity: generate a single TU that includes main.cpp ---
set(UNITY_FILE ${CMAKE_CURRENT_BINARY_DIR}/unity_all.cpp)
file(WRITE  ${UNITY_FILE} "// Generated unity TU\n#include \"${SRC_DIR}/main.cpp\"\n")

add_executable(${PROJECT_NAME} ${UNITY_FILE})

# Allow '#include "types.cpp"' and '#include "vulkan/headers.cpp"' without '../'
target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan)

# Warnings
if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
