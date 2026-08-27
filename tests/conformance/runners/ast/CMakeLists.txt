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

set(AST_EXPECTATION_ROOT
  ${CMAKE_SOURCE_DIR}/tests/conformance/expectations/ast
)

add_test(
  NAME conformance-ast-coverage
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/tests/conformance/tools/check-ast-coverage.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(conformance-ast-coverage PROPERTIES
  LABELS "conformance;conformance-ast;ast;coverage;specification"
  TIMEOUT 120
)

set(AST_TEST_ENV "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")
if(ZOM_ENABLE_COVERAGE)
  string(APPEND AST_TEST_ENV ";ZOM_ENABLE_COVERAGE=ON")
endif()

add_test(
  NAME conformance-ast
  COMMAND ${ZOM_LIT_COMMAND} -v -j 8 --timeout=90 ${AST_EXPECTATION_ROOT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
)

set_tests_properties(conformance-ast PROPERTIES
  LABELS "conformance;conformance-ast;ast;lit;filecheck;specification"
  TIMEOUT 900
  PROCESSORS 8
  ENVIRONMENT "${AST_TEST_ENV}"
)
