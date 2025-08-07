function(add_coverage_to_test TEST_NAME)
  set_tests_properties(
    ${TEST_NAME}
    PROPERTIES
      ENVIRONMENT
      "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/coverage/${TEST_NAME}.profraw")
endfunction()

function(create_coverage_target)
  cmake_parse_arguments(COVERAGE "" "NAME" "TARGETS;EXCLUDES" ${ARGN})

  if(NOT TARGET ${COVERAGE_NAME})
    if(APPLE)
      set(XCRUN "xcrun")
    else()
      set(XCRUN "")
    endif()

    set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")

    add_custom_target(
      ${COVERAGE_NAME}_create_dir
      COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Creating coverage directory for ${COVERAGE_NAME}")

    add_custom_target(
      ${COVERAGE_NAME}_run_tests
      COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG>
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Running tests for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_run_tests ${COVERAGE_NAME}_create_dir)

    add_custom_target(
      ${COVERAGE_NAME}_merge_profdata
      COMMAND ${XCRUN} llvm-profdata merge -sparse ${COVERAGE_DIR}/*.profraw -o
              ${COVERAGE_DIR}/coverage.profdata
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Merging coverage data for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_merge_profdata ${COVERAGE_NAME}_run_tests)

    # Convert target names to target files
    # Separate executables and libraries for llvm-cov
    set(COVERAGE_EXECUTABLE_FILES "")
    foreach(TARGET_NAME ${COVERAGE_TARGETS})
      get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
      if(TARGET_TYPE STREQUAL "EXECUTABLE")
        list(APPEND COVERAGE_EXECUTABLE_FILES $<TARGET_FILE:${TARGET_NAME}>)
      endif()
    endforeach()

    # Construct llvm-cov binary arguments: [BIN] [-object BIN]...
    # Use the first executable as the main target, others as -object
    list(GET COVERAGE_EXECUTABLE_FILES 0 MAIN_EXECUTABLE)
    set(COVERAGE_TARGET_FILES "${MAIN_EXECUTABLE}")
    list(LENGTH COVERAGE_EXECUTABLE_FILES EXEC_COUNT)
    if(EXEC_COUNT GREATER 1)
      list(SUBLIST COVERAGE_EXECUTABLE_FILES 1 -1 OTHER_EXECUTABLES)
      foreach(EXEC ${OTHER_EXECUTABLES})
        list(APPEND COVERAGE_TARGET_FILES "-object" "${EXEC}")
      endforeach()
    endif()

    # Build exclude patterns
    set(COVERAGE_EXCLUDE_PATTERNS ".*-test\.cc")
    if(COVERAGE_EXCLUDES)
      list(APPEND COVERAGE_EXCLUDE_PATTERNS ${COVERAGE_EXCLUDES})
    endif()
     
    # Join exclude patterns with | for regex OR
    string(REPLACE ";" "|" COVERAGE_EXCLUDES "${COVERAGE_EXCLUDE_PATTERNS}")
    # Escape the pattern for shell
    set(COVERAGE_EXCLUDES "'${COVERAGE_EXCLUDES}'")

    # llvm-cov export [options] -instr-profile PROFILE [BIN] [-object BIN]… [-sources] [SOURCE]…
    set(SOURCES "-sources ${CMAKE_SOURCE_DIR}")
    add_custom_target(
      ${COVERAGE_NAME}_generate_html_report
      COMMAND
        ${XCRUN} llvm-cov show -format=html
        -instr-profile=${COVERAGE_DIR}/coverage.profdata
        ${COVERAGE_TARGET_FILES}
        -ignore-filename-regex=${COVERAGE_EXCLUDES}
        -output-dir=${COVERAGE_DIR}/html
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating HTML coverage report for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_generate_html_report
                     ${COVERAGE_NAME}_merge_profdata)

    add_custom_target(
      ${COVERAGE_NAME}_generate_text_report
      COMMAND
        ${XCRUN} llvm-cov report -format=text
        ${COVERAGE_TARGET_FILES}
        -instr-profile=${COVERAGE_DIR}/coverage.profdata
        -ignore-filename-regex=${COVERAGE_EXCLUDES}
        > ${COVERAGE_DIR}/coverage.txt
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating text coverage report for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_generate_text_report
                     ${COVERAGE_NAME}_merge_profdata)

    # Generate lcov format for codecov compatibility
    add_custom_target(
      ${COVERAGE_NAME}_generate_lcov_report
      COMMAND
        ${XCRUN} llvm-cov export -format=lcov
        ${COVERAGE_TARGET_FILES}
        -instr-profile=${COVERAGE_DIR}/coverage.profdata
        -ignore-filename-regex=${COVERAGE_EXCLUDES}
        > ${COVERAGE_DIR}/coverage.lcov
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating lcov coverage report for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_generate_lcov_report
                     ${COVERAGE_NAME}_merge_profdata)

    add_custom_target(
      ${COVERAGE_NAME}
      DEPENDS ${COVERAGE_NAME}_create_dir
              ${COVERAGE_NAME}_run_tests
              ${COVERAGE_NAME}_merge_profdata
              ${COVERAGE_NAME}_generate_html_report
              ${COVERAGE_NAME}_generate_text_report
              ${COVERAGE_NAME}_generate_lcov_report
      COMMENT "Generating full coverage report for ${COVERAGE_NAME}")
  else()
    message(
      WARNING
        "Coverage target '${COVERAGE_NAME}' already exists. Skipping creation.")
  endif()
endfunction()

function(add_test_to_coverage TEST_NAME)
  get_property(ALL_TESTS_PROP GLOBAL PROPERTY ALL_TESTS_GLOBAL)
  list(APPEND ALL_TESTS_PROP ${TEST_NAME})
  set_property(GLOBAL PROPERTY ALL_TESTS_GLOBAL "${ALL_TESTS_PROP}")
endfunction()
