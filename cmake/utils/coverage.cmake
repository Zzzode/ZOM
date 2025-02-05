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

    add_custom_target(
      ${COVERAGE_NAME}_generate_html_report
      COMMAND
        ${XCRUN} llvm-cov show ${COVERAGE_TARGETS}
        -instr-profile=${COVERAGE_DIR}/coverage.profdata -format=html
        -output-dir=${COVERAGE_DIR}/html
        -ignore-filename-regex="${COVERAGE_EXCLUDES}"
        -path-equivalence=${CMAKE_SOURCE_DIR},. ${CMAKE_SOURCE_DIR}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generating HTML coverage report for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_generate_html_report
                     ${COVERAGE_NAME}_merge_profdata)

    add_custom_target(
      ${COVERAGE_NAME}_generate_text_report
      COMMAND
        ${XCRUN} llvm-cov report ${COVERAGE_TARGETS}
        -instr-profile=${COVERAGE_DIR}/coverage.profdata
        -ignore-filename-regex="${COVERAGE_EXCLUDES}"
        -path-equivalence=${CMAKE_SOURCE_DIR},. ${CMAKE_SOURCE_DIR} >
        ${CMAKE_BINARY_DIR}/coverage/coverage.txt
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generating text coverage report for ${COVERAGE_NAME}")
    add_dependencies(${COVERAGE_NAME}_generate_text_report
                     ${COVERAGE_NAME}_merge_profdata)

    add_custom_target(
      ${COVERAGE_NAME}
      DEPENDS ${COVERAGE_NAME}_create_dir
              ${COVERAGE_NAME}_run_tests
              ${COVERAGE_NAME}_merge_profdata
              ${COVERAGE_NAME}_generate_html_report
              ${COVERAGE_NAME}_generate_text_report
      COMMENT "Generating full coverage report for ${COVERAGE_NAME}")
  else()
    message(
      WARNING
        "Coverage target '${COVERAGE_NAME}' already exists. Skipping creation.")
  endif()
endfunction()

function(add_test_to_coverage TEST_NAME)
  list(APPEND ALL_TESTS $<TARGET_FILE:${TEST_NAME}>)
  set(ALL_TESTS
      ${ALL_TESTS}
      CACHE INTERNAL "List of all test executables" FORCE)
endfunction()
