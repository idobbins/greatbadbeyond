cmake_minimum_required(VERSION 3.25)
project(callandor)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(SHADERS
  ${CMAKE_SOURCE_DIR}/shaders/compute.comp
  ${CMAKE_SOURCE_DIR}/shaders/fullscreen.vert
  ${CMAKE_SOURCE_DIR}/shaders/blit.frag
)

set(SPV_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPV_DIR})

set(SPV_OUTPUTS)
foreach(SRC ${SHADERS})
  get_filename_component(NAME ${SRC} NAME)
  set(OUT ${SPV_DIR}/${NAME}.spv)
  add_custom_command(
    OUTPUT ${OUT}
    COMMAND $<TARGET_FILE:Vulkan::glslc>
            --target-env=vulkan1.2
            $<$<CONFIG:Debug>:-g>
            -O
            -o ${OUT} ${SRC}
    DEPENDS ${SRC}
    COMMENT "glslc ${NAME}"
    VERBATIM
    COMMAND_EXPAND_LISTS
  )
  list(APPEND SPV_OUTPUTS ${OUT})
endforeach()

add_custom_target(shaders DEPENDS ${SPV_OUTPUTS})

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan glfw)
add_dependencies(${PROJECT_NAME} shaders)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${SPV_DIR} $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
)
