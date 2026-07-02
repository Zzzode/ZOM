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

find_program(LIT_EXECUTABLE NAMES lit)
if(NOT LIT_EXECUTABLE)
  message(FATAL_ERROR "LLVM lit executable not found. Please install lit first")
endif()

set(AST_EXPECTATION_ROOT
  ${CMAKE_SOURCE_DIR}/products/zomlang/tests/conformance/expectations/ast
)

add_test(
  NAME conformance-ast-coverage
  COMMAND python3
          ${CMAKE_SOURCE_DIR}/products/zomlang/tests/conformance/tools/check-ast-coverage.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(conformance-ast-coverage PROPERTIES
  LABELS "conformance;conformance-ast;ast;coverage;specification"
  TIMEOUT 30
)

file(GLOB_RECURSE AST_EXPECTATION_FILES "${AST_EXPECTATION_ROOT}/*.check")
foreach(AST_EXPECTATION_FILE ${AST_EXPECTATION_FILES})
  file(RELATIVE_PATH REL_PATH "${AST_EXPECTATION_ROOT}" "${AST_EXPECTATION_FILE}")
  string(REPLACE "/" "-" TEST_NAME "${REL_PATH}")
  string(REPLACE ".check" "" TEST_NAME "${TEST_NAME}")
  set(TEST_FULL_NAME "lit-${TEST_NAME}")

  add_test(
    NAME ${TEST_FULL_NAME}
    COMMAND ${LIT_EXECUTABLE} -v --timeout=20 ${AST_EXPECTATION_FILE}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/products/zomlang/tests
  )

  set(TEST_ENV "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")
  if(ZOM_ENABLE_COVERAGE)
    string(APPEND TEST_ENV
      ";ZOM_ENABLE_COVERAGE=ON"
    )
  endif()

  set_tests_properties(${TEST_FULL_NAME} PROPERTIES
    LABELS "conformance;conformance-ast;ast;lit;filecheck;specification"
    TIMEOUT 30
    ENVIRONMENT "${TEST_ENV}"
  )
endforeach()
