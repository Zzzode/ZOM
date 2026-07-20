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

# ZomLang Compiler Test Suite
# This directory contains all tests for the ZomLang compiler

# Enable testing
enable_testing()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import yaml"
  RESULT_VARIABLE ZOM_PYYAML_IMPORT_RESULT
  ERROR_VARIABLE ZOM_PYYAML_IMPORT_ERROR
  OUTPUT_QUIET
)
if(NOT ZOM_PYYAML_IMPORT_RESULT EQUAL 0)
  string(STRIP "${ZOM_PYYAML_IMPORT_ERROR}" ZOM_PYYAML_IMPORT_ERROR)
  message(FATAL_ERROR
    "The configured Python interpreter '${Python3_EXECUTABLE}' cannot import PyYAML: "
    "${ZOM_PYYAML_IMPORT_ERROR}\n"
    "Install PyYAML for this exact interpreter with:\n"
    "  \"${Python3_EXECUTABLE}\" -m pip install PyYAML"
  )
endif()

# Unit Tests - Low-level component tests
add_subdirectory(unittests)

# Conformance runner orchestration
add_subdirectory(conformance)

# Performance Tests - Benchmarks and performance regression tests
if(ZOM_ENABLE_PERFORMANCE_TESTS)
  add_subdirectory(performance)
endif()

add_test(
  NAME package-invocation-cli
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-package-invocation.py"
    --zomc
    "$<TARGET_FILE:zomc>")
set_tests_properties(package-invocation-cli
                     PROPERTIES LABELS "unittest;package;cli" TIMEOUT 120)
add_test(
  NAME package-workspace-compile
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-package-compile.py"
    --zomc
    "$<TARGET_FILE:zomc>")
set_tests_properties(package-workspace-compile
                     PROPERTIES LABELS "integration;package;cli" TIMEOUT 60)

# Benchmarks - Frontend micro-benchmarks
add_subdirectory(benchmarks)

# Fuzzing - LibFuzzer targets for lexer and parser
add_subdirectory(fuzzing)

# Regression Tests - Tests for previously fixed bugs
add_subdirectory(regression)

# Custom test targets
add_custom_target(check-unit
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "unit-"
  COMMENT "Running unit tests"
)

add_custom_target(check-integration
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "integration-"
  COMMENT "Running integration tests"
)

add_custom_target(check-regression
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "regression-"
  COMMENT "Running regression tests"
)

add_custom_target(check-all
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  COMMENT "Running all tests"
)

# Add dependencies
add_dependencies(check-unit zomc)
add_dependencies(check-integration zomc)
add_dependencies(check-regression zomc)
add_dependencies(check-all zomc)
