cmake_minimum_required(VERSION 3.28)

if(WIN32)
    set(CMAKE_RC_COMPILER "C:/Program Files/LLVM/bin/llvm-rc.exe" CACHE FILEPATH "LLVM resource compiler" FORCE)
endif()

project(callandor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)

# Configure GLFW build options before fetching
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

find_package(Vulkan REQUIRED COMPONENTS glslc)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw Vulkan::Vulkan)