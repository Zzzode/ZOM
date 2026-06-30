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

add_subdirectory(runners/grammar)
add_subdirectory(runners/ast)

add_test(
  NAME parser-coverage
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-parser-coverage.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(parser-coverage PROPERTIES
  LABELS "conformance;parser;coverage;specification"
  TIMEOUT 10
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

add_dependencies(check-conformance zomc)
add_dependencies(check-conformance-ast zomc)
