cmake_minimum_required(VERSION 3.25.0)

project("ZOM" CXX)

set(VERSION 0.0.1-dev)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)

set(ZOM_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(ZOM_CMAKE_DIR ${ZOM_ROOT}/cmake)
set(INCLUDE_DIRS ${ZOM_ROOT}/products ${ZOM_ROOT}/libraries)

include(${ZOM_CMAKE_DIR}/utils/options.cmake)
include(${ZOM_CMAKE_DIR}/utils/common.cmake)
include(${ZOM_CMAKE_DIR}/utils/checks.cmake)
include(${ZOM_CMAKE_DIR}/utils/functions.cmake)
include(${ZOM_CMAKE_DIR}/utils/unittests.cmake)
if(ZOM_ENABLE_COVERAGE)
  include(${ZOM_CMAKE_DIR}/utils/coverage.cmake)
endif()

add_subdirectory(libraries)
add_subdirectory(products)
add_subdirectory(examples)

if(ZOM_ENABLE_COVERAGE)
  create_coverage_target(NAME coverage TARGETS ${ALL_TESTS} EXCLUDES
                         ".*\-test\\.cc")
endif()
