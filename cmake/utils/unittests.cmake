# Copyright (c) 2025 Zode.Z. All rights reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

# Test utility functions for ZomLang compiler tests

include(CTest)

# Function to add a ztest-based unit test
# Usage: add_ztest_unit_test(test_name test_source [LIBRARIES ...])
function(add_ztest_unit_test TEST_NAME TEST_SOURCE)
  cmake_parse_arguments(ZTEST "" "" "LIBRARIES" ${ARGN})

  # Create executable for the unit test
  add_executable(${TEST_NAME} ${TEST_SOURCE})

  # Link with ztest and specified libraries
  target_link_libraries(${TEST_NAME} PRIVATE ztest ${ZTEST_LIBRARIES})

  # Set include directories
  target_include_directories(${TEST_NAME} PRIVATE
    ${ZOM_ROOT}/libraries
    ${ZOM_ROOT}
  )

  # Compiler options
  target_compile_options(${TEST_NAME} PRIVATE -Wno-global-constructors)

  # Add as CTest
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

  # Determine specific labels based on test name structure
  set(SPECIFIC_LABELS "unittest")

  # Extract component labels from test name by splitting on '-'
  # For test names like "ast-dumper-test" or "lexer-lexer-test", extract the first part as component label
  string(REPLACE "-" ";" TEST_NAME_PARTS "${TEST_NAME}")
  list(GET TEST_NAME_PARTS 0 FIRST_COMPONENT)

  # Only add component label if it's not "test" and not empty
  if(FIRST_COMPONENT AND NOT FIRST_COMPONENT STREQUAL "test")
    set(SPECIFIC_LABELS "${SPECIFIC_LABELS};${FIRST_COMPONENT}")
  endif()

  # Set test properties
  set_tests_properties(${TEST_NAME} PROPERTIES
    LABELS "${SPECIFIC_LABELS}"
    TIMEOUT 60
  )

  # Add coverage if enabled
  if(ZOM_ENABLE_COVERAGE)
    add_coverage_to_test(${TEST_NAME})
    add_test_to_coverage(${TEST_NAME})
  endif()
endfunction()

# Function to add a regression test
# Usage: add_regression_test(test_name source_file issue_number)
function(add_regression_test TEST_NAME SOURCE_FILE ISSUE_NUMBER)
  set(TEST_FULL_NAME "regression-${TEST_NAME}")

  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND $<TARGET_FILE:zomc> compile --dump-ast ${SOURCE_FILE}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  set(TEST_ENV "")
  if(ZOM_ENABLE_COVERAGE)
    set(TEST_ENV
      "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/coverage/${TEST_FULL_NAME}.profraw"
      "ZC_CLEAN_SHUTDOWN=1"
    )
  endif()

  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "regression;issue-${ISSUE_NUMBER}"
    TIMEOUT 30
    ENVIRONMENT "${TEST_ENV}"
  )
endfunction()

# Function to add a performance test
# Usage: add_performance_test(test_name executable_target)
function(add_performance_test TEST_NAME EXECUTABLE_TARGET)
  set(TEST_FULL_NAME "performance-${TEST_NAME}")

  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND ${EXECUTABLE_TARGET}
  )

  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "performance;benchmark"
    TIMEOUT 300  # 5 minutes for performance tests
  )
endfunction()

# Function to discover and add all *-test.cc files in a directory as ztest unit tests
# Usage: add_ztest_unit_tests_from_directory(directory_path [LIBRARIES ...])
function(add_ztest_unit_tests_from_directory DIRECTORY_PATH)
  cmake_parse_arguments(ZTEST_DIR "" "" "LIBRARIES" ${ARGN})

  file(GLOB_RECURSE TEST_FILES "${DIRECTORY_PATH}/*-test.cc")

  foreach(TEST_FILE ${TEST_FILES})
    # Get relative path from the directory
    file(RELATIVE_PATH REL_PATH "${DIRECTORY_PATH}" "${TEST_FILE}")

    # Create test name from relative path
    get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)
    file(RELATIVE_PATH REL_DIR "${DIRECTORY_PATH}" "${TEST_FILE}")
    get_filename_component(REL_DIR "${REL_DIR}" DIRECTORY)
    string(REPLACE "/" "-" DIR_PREFIX "${REL_DIR}")
    if(DIR_PREFIX AND NOT DIR_PREFIX STREQUAL ".")
      set(UNIQUE_TEST_NAME "${DIR_PREFIX}-${TEST_NAME}")
    else()
      set(UNIQUE_TEST_NAME "${TEST_NAME}")
    endif()

    add_ztest_unit_test("${UNIQUE_TEST_NAME}" "${TEST_FILE}" LIBRARIES ${ZTEST_DIR_LIBRARIES})
  endforeach()
endfunction()

set(ALL_TESTS
    ""
    CACHE INTERNAL "List of all test executables")
