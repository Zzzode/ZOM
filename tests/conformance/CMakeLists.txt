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

# Conformance tests are organized around a shared source corpus and separate
# runner/oracle layers. Only executable runners are registered with CTest.

unset(LIT_EXECUTABLE CACHE)
find_program(ZOM_LIT_SOURCE_EXECUTABLE NAMES lit NO_CACHE REQUIRED)
file(STRINGS "${ZOM_LIT_SOURCE_EXECUTABLE}" ZOM_LIT_LAUNCHER_FIRST_LINE LIMIT_COUNT 1)
if(ZOM_LIT_LAUNCHER_FIRST_LINE MATCHES "^#!(.+)$")
  set(ZOM_LIT_PYTHON "${CMAKE_MATCH_1}")
  if(NOT EXISTS "${ZOM_LIT_PYTHON}")
    message(FATAL_ERROR
      "The LLVM lit interpreter '${ZOM_LIT_PYTHON}' does not exist"
    )
  endif()
  set(ZOM_LIT_COMMAND "${ZOM_LIT_PYTHON}" "${ZOM_LIT_SOURCE_EXECUTABLE}")
else()
  set(ZOM_LIT_COMMAND "${ZOM_LIT_SOURCE_EXECUTABLE}")
endif()

execute_process(
  COMMAND ${ZOM_LIT_COMMAND} --version
  RESULT_VARIABLE ZOM_LIT_PROBE_RESULT
  ERROR_VARIABLE ZOM_LIT_PROBE_ERROR
  OUTPUT_QUIET
)
if(NOT ZOM_LIT_PROBE_RESULT EQUAL 0)
  string(STRIP "${ZOM_LIT_PROBE_ERROR}" ZOM_LIT_PROBE_ERROR)
  message(FATAL_ERROR
    "The discovered LLVM lit command is unusable: "
    "${ZOM_LIT_PROBE_ERROR}"
  )
endif()

add_subdirectory(runners/grammar)
add_subdirectory(runners/ast)
add_subdirectory(runners/diagnostics)

add_test(
  NAME ownership-determinism
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ownership-determinism.py
          --check --zomc "$<TARGET_FILE:zomc>"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ownership-determinism PROPERTIES
  LABELS "conformance;ownership;determinism"
  TIMEOUT 300
)

add_test(
  NAME ownership-determinism-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ownership-determinism.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ownership-determinism-negative PROPERTIES
  LABELS "conformance;ownership;determinism;negative"
  TIMEOUT 120
)

add_custom_target(check-conformance
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "conformance"
  COMMENT "Running all conformance tests"
)

add_custom_target(check-conformance-ast
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "conformance-ast"
  COMMENT "Running AST/FileCheck conformance tests"
)

add_custom_target(check-conformance-grammar
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "conformance-grammar"
  COMMENT "Running ANTLR grammar conformance tests"
)

add_custom_target(check-conformance-diagnostics
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "conformance-diagnostics"
  COMMENT "Running diagnostics/FileCheck conformance tests"
)

add_dependencies(check-conformance zomc)
add_dependencies(check-conformance-ast zomc)
add_dependencies(check-conformance-diagnostics zomc)
