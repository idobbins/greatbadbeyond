cmake_minimum_required(VERSION 3.24)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BUILD_DIR "${REPO_ROOT}/build")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${REPO_ROOT}" -B "${BUILD_DIR}"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr
)

if(configure_stdout)
    message("${configure_stdout}")
endif()

if(configure_stderr)
    message("${configure_stderr}")
endif()

if(configure_result)
    message(FATAL_ERROR "CMake configure failed with code ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)

if(build_stdout)
    message("${build_stdout}")
endif()

if(build_stderr)
    message("${build_stderr}")
endif()

if(build_result)
    message(FATAL_ERROR "Build failed with code ${build_result}")
endif()

if(WIN32)
    set(EXECUTABLE "${BUILD_DIR}/callandor.exe")
else()
    set(EXECUTABLE "${BUILD_DIR}/callandor")
endif()

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

execute_process(
  COMMAND "${EXECUTABLE}"
  WORKING_DIRECTORY "${BUILD_DIR}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)

if(run_stdout)
    message("${run_stdout}")
endif()

if(run_stderr)
    message("${run_stderr}")
endif()

if(run_result)
    message(FATAL_ERROR "Executable exited with code ${run_result}")
endif()
