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
# Usage: add_ztest_unit_test(test_name test_source [LIBRARIES ...] [SHARDS n])
#
# SHARDS n (optional): register n CTest entries over ONE test executable, each
# running a line-range slice of the file's ZC_TEST cases. Boundaries are read
# from the source at configure time and made seamless (every line belongs to
# exactly one shard), so newly inserted tests can never be silently skipped.
# Sharding exists because ztest runs cases serially inside one binary: a
# full-pipeline booting suite is one CTest slot and gains nothing from -j.
function(add_ztest_unit_test TEST_NAME TEST_SOURCE)
  cmake_parse_arguments(ZTEST "" "SHARDS" "LIBRARIES" ${ARGN})

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

  # Compute the shard set: seamless line ranges covering every ZC_TEST line.
  get_filename_component(TEST_SOURCE_BASENAME "${TEST_SOURCE}" NAME)
  set(ZTEST_SHARD_ENTRIES "${TEST_NAME}|1")
  if(DEFINED ZTEST_SHARDS AND ZTEST_SHARDS GREATER 1)
    file(STRINGS "${TEST_SOURCE}" ZTEST_SOURCE_LINES)
    set(ZTEST_CASE_LINES "")
    set(ZTEST_LINE_NO 0)
    foreach(ZTEST_LINE IN LISTS ZTEST_SOURCE_LINES)
      math(EXPR ZTEST_LINE_NO "${ZTEST_LINE_NO} + 1")
      if(ZTEST_LINE MATCHES "ZC_TEST[ \t]*\\(")
        list(APPEND ZTEST_CASE_LINES "${ZTEST_LINE_NO}")
      endif()
    endforeach()
    list(LENGTH ZTEST_CASE_LINES ZTEST_CASE_COUNT)
    if(ZTEST_CASE_COUNT GREATER 1)
      set(ZTEST_SHARD_ENTRIES "")
      math(EXPR ZTEST_SHARD_LIMIT "${ZTEST_SHARDS} - 1")
      foreach(ZTEST_SHARD_INDEX RANGE 0 ${ZTEST_SHARD_LIMIT})
        # Contiguous balanced blocks: shard s owns global case indices
        # [s*k, (s+1)*k). One line range per shard then tiles the file
        # seamlessly: each shard starts midway between its predecessor's last
        # case and its own first case, and the final shard runs to EOF, so a
        # test inserted anywhere belongs to exactly one shard.
        math(EXPR ZTEST_BLOCK_FIRST "${ZTEST_SHARD_INDEX} * ${ZTEST_CASE_COUNT} / ${ZTEST_SHARDS}")
        math(EXPR ZTEST_BLOCK_LAST
             "(${ZTEST_SHARD_INDEX} + 1) * ${ZTEST_CASE_COUNT} / ${ZTEST_SHARDS} - 1")
        if(ZTEST_BLOCK_FIRST LESS_EQUAL ZTEST_BLOCK_LAST)
          list(GET ZTEST_CASE_LINES ${ZTEST_BLOCK_FIRST} ZTEST_FIRST_CASE_LINE)
          if(ZTEST_BLOCK_FIRST EQUAL 0)
            set(ZTEST_RANGE_MIN 1)
          else()
            math(EXPR ZTEST_PREV_CASE_INDEX "${ZTEST_BLOCK_FIRST} - 1")
            list(GET ZTEST_CASE_LINES ${ZTEST_PREV_CASE_INDEX} ZTEST_PREV_CASE_LINE)
            math(EXPR ZTEST_RANGE_MIN
                 "(${ZTEST_PREV_CASE_LINE} + ${ZTEST_FIRST_CASE_LINE}) / 2 + 1")
          endif()
          math(EXPR ZTEST_NEXT_CASE_INDEX "${ZTEST_BLOCK_LAST} + 1")
          if(ZTEST_NEXT_CASE_INDEX LESS ZTEST_CASE_COUNT)
            list(GET ZTEST_CASE_LINES ${ZTEST_BLOCK_LAST} ZTEST_LAST_CASE_LINE)
            list(GET ZTEST_CASE_LINES ${ZTEST_NEXT_CASE_INDEX} ZTEST_NEXT_CASE_LINE)
            math(EXPR ZTEST_RANGE_MAX "(${ZTEST_LAST_CASE_LINE} + ${ZTEST_NEXT_CASE_LINE}) / 2")
          else()
            set(ZTEST_RANGE_MAX 2147483647)
          endif()
          list(APPEND ZTEST_SHARD_ENTRIES
               "${TEST_NAME}-shard|${ZTEST_SHARD_INDEX}|${ZTEST_RANGE_MIN}-${ZTEST_RANGE_MAX}")
        endif()
      endforeach()
      # One test total: a single all-file shard is cheaper than range filtering.
      list(LENGTH ZTEST_SHARD_ENTRIES ZTEST_SHARD_ENTRY_COUNT)
      if(ZTEST_SHARD_ENTRY_COUNT EQUAL 0)
        set(ZTEST_SHARD_ENTRIES "${TEST_NAME}|1")
      endif()
    endif()
  endif()

  foreach(ZTEST_SHARD_ENTRY IN LISTS ZTEST_SHARD_ENTRIES)
    string(REPLACE "|" ";" ZTEST_SHARD_FIELDS "${ZTEST_SHARD_ENTRY}")
    list(LENGTH ZTEST_SHARD_FIELDS ZTEST_SHARD_FIELD_COUNT)
    if(ZTEST_SHARD_FIELD_COUNT EQUAL 2)
      list(GET ZTEST_SHARD_FIELDS 0 ZTEST_CTEST_NAME)
      set(ZTEST_TEST_COMMAND "$<TARGET_FILE:${TEST_NAME}>")
    else()
      list(GET ZTEST_SHARD_FIELDS 0 ZTEST_CTEST_NAME)
      list(GET ZTEST_SHARD_FIELDS 1 ZTEST_SHARD_INDEX)
      list(GET ZTEST_SHARD_FIELDS 2 ZTEST_RANGE)
      set(ZTEST_CTEST_NAME "${ZTEST_CTEST_NAME}${ZTEST_SHARD_INDEX}")
      set(ZTEST_TEST_COMMAND "$<TARGET_FILE:${TEST_NAME}>;-f;${TEST_SOURCE_BASENAME}:${ZTEST_RANGE}")
    endif()

    add_test(NAME ${ZTEST_CTEST_NAME} COMMAND ${ZTEST_TEST_COMMAND})

    # Set test properties
    #
    # The default timeout covers every ztest that does not set its own. Sanitizer
    # builds are slow and the suite runs eight-way parallel, so a cap sized to an
    # unloaded host turns any external load into a spurious failure -- and because
    # the preset stops on first failure, one such timeout aborts the whole run.
    # Several suites measured 20-30s unloaded and still timed out at 60s under
    # contention, so the default is sized for the loaded case. A test that is
    # genuinely slow enough to need more sets TIMEOUT explicitly at its call site.
    set_tests_properties(${ZTEST_CTEST_NAME} PROPERTIES
      LABELS "${SPECIFIC_LABELS}"
      TIMEOUT 300
    )

    # Add coverage if enabled
    if(ZOM_ENABLE_COVERAGE)
      add_coverage_to_test(${ZTEST_CTEST_NAME})
      add_test_to_coverage(${ZTEST_CTEST_NAME})
    endif()
  endforeach()
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
# Usage: add_ztest_unit_tests_from_directory(directory_path
#          [LIBRARIES ...]
#          [SHARD_OVERRIDES source-basename.cc:shards ...])
#
# SHARD_OVERRIDES shards one large serial binary into multiple CTest entries;
# see add_ztest_unit_test for the semantics.
function(add_ztest_unit_tests_from_directory DIRECTORY_PATH)
  cmake_parse_arguments(ZTEST_DIR "" "" "LIBRARIES;SHARD_OVERRIDES" ${ARGN})

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

    set(ZTEST_FILE_SHARDS "")
    get_filename_component(TEST_BASENAME "${TEST_FILE}" NAME)
    foreach(ZTEST_OVERRIDE IN LISTS ZTEST_DIR_SHARD_OVERRIDES)
      string(REPLACE ":" ";" ZTEST_OVERRIDE_FIELDS "${ZTEST_OVERRIDE}")
      list(GET ZTEST_OVERRIDE_FIELDS 0 ZTEST_OVERRIDE_FILE)
      if(ZTEST_OVERRIDE_FILE STREQUAL TEST_BASENAME)
        list(GET ZTEST_OVERRIDE_FIELDS 1 ZTEST_FILE_SHARDS)
      endif()
    endforeach()

    if(ZTEST_FILE_SHARDS)
      add_ztest_unit_test("${UNIQUE_TEST_NAME}" "${TEST_FILE}"
                          LIBRARIES ${ZTEST_DIR_LIBRARIES}
                          SHARDS ${ZTEST_FILE_SHARDS})
    else()
      add_ztest_unit_test("${UNIQUE_TEST_NAME}" "${TEST_FILE}"
                          LIBRARIES ${ZTEST_DIR_LIBRARIES})
    endif()
  endforeach()
endfunction()

set(ALL_TESTS
    ""
    CACHE INTERNAL "List of all test executables")
