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

# Function to add a language specification test
# Usage: add_language_test(test_name source_file expected_output)
function(add_language_test TEST_NAME SOURCE_FILE)
  set(TEST_FULL_NAME "language-${TEST_NAME}")

  # Create test that runs zomc on the source file
  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND ${CMAKE_BINARY_DIR}/products/zomlang/utils/zomc/zomc compile --dump-ast --format=json ${SOURCE_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  # Set test properties
  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "language;specification"
    TIMEOUT 30
  )
endfunction()

# Function to add a language test with expected output comparison
# Usage: add_language_test_with_output(test_name source_file expected_file)
function(add_language_test_with_output TEST_NAME SOURCE_FILE EXPECTED_FILE)
  set(TEST_FULL_NAME "language-${TEST_NAME}")

  # Create test script that compares output
  set(TEST_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}_test.sh")
  set(ACTUAL_OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}.actual")
  file(WRITE ${TEST_SCRIPT}
    "#!/bin/bash\n"
    "set -ex\n"
    "${CMAKE_BINARY_DIR}/products/zomlang/utils/zomc/zomc compile --dump-ast --format=json ${SOURCE_FILE} > \"${ACTUAL_OUTPUT_FILE}\" 2>&1\n"
    "if cmp -s \"${EXPECTED_FILE}\" \"${ACTUAL_OUTPUT_FILE}\"; then\n"
    "  rm \"${ACTUAL_OUTPUT_FILE}\"\n"
    "  exit 0\n"
    "else\n"
    "  echo \"Differences:\"\n"
    "  diff -u \"${EXPECTED_FILE}\" \"${ACTUAL_OUTPUT_FILE}\" || true\n"
    "  rm \"${ACTUAL_OUTPUT_FILE}\"\n"
    "  exit 1\n"
    "fi\n"
  )

  # Make script executable
  file(CHMOD ${TEST_SCRIPT} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND ${TEST_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "language;specification;output-comparison"
    TIMEOUT 30
  )
endfunction()

# Function to add a regression test
# Usage: add_regression_test(test_name source_file issue_number)
function(add_regression_test TEST_NAME SOURCE_FILE ISSUE_NUMBER)
  set(TEST_FULL_NAME "regression-${TEST_NAME}")

  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND ${CMAKE_BINARY_DIR}/products/zomlang/utils/zomc/zomc compile --dump-ast --format=json ${SOURCE_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "regression;issue-${ISSUE_NUMBER}"
    TIMEOUT 30
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

# Function to discover and add all .zom files in a directory as language tests
# Usage: add_language_tests_from_directory(directory_path)
function(add_language_tests_from_directory DIRECTORY_PATH)
  file(GLOB_RECURSE ZOM_FILES "${DIRECTORY_PATH}/*.zom")

  foreach(ZOM_FILE ${ZOM_FILES})
    # Get relative path from the directory
    file(RELATIVE_PATH REL_PATH "${DIRECTORY_PATH}" "${ZOM_FILE}")

    # Create test name from relative path
    string(REPLACE "/" "-" TEST_NAME "${REL_PATH}")
    string(REPLACE ".zom" "" TEST_NAME "${TEST_NAME}")

    # Check if there's an expected output file
    string(REPLACE ".zom" ".expected" EXPECTED_FILE "${ZOM_FILE}")

    if(EXISTS "${EXPECTED_FILE}")
      add_language_test_with_output("${TEST_NAME}" "${ZOM_FILE}" "${EXPECTED_FILE}")
    else()
      add_language_test("${TEST_NAME}" "${ZOM_FILE}")
    endif()
  endforeach()
endfunction()

set(ALL_TESTS
    ""
    CACHE INTERNAL "List of all test executables")
