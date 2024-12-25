cmake_minimum_required(VERSION 3.19.0)

project(COMPILER)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)

set(ZOM_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(ZOM_CMAKE_DIR ${ZOM_ROOT}/cmake)

include(${ZOM_CMAKE_DIR}/options.cmake)
include(${ZOM_CMAKE_DIR}/common.cmake)
include(${ZOM_CMAKE_DIR}/checks.cmake)
include(${ZOM_CMAKE_DIR}/functions.cmake)

add_subdirectory(src)
add_subdirectory(examples)

enable_testing()
add_subdirectory(tests)
