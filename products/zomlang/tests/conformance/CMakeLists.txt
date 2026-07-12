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
add_subdirectory(runners/diagnostics)
add_subdirectory(runners/ir)

add_test(
  NAME parser-coverage
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-parser-coverage.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(parser-coverage PROPERTIES
  LABELS "conformance;parser;coverage;specification"
  TIMEOUT 120
)

add_test(
  NAME lexer-architecture
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-lexer-architecture.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(lexer-architecture PROPERTIES
  LABELS "conformance;lexer;specification"
  TIMEOUT 120
)

add_test(
  NAME ast-generated-schema
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/codegen/gen_ast.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
  NAME compiler-session-architecture
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-compiler-session-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(compiler-session-architecture PROPERTIES
  LABELS "conformance;compiler-session;architecture"
  TIMEOUT 120
)

add_test(
  NAME compiler-session-architecture-negative
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-compiler-session-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(compiler-session-architecture-negative PROPERTIES
  LABELS "conformance;compiler-session;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME identity-architecture
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-identity-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(identity-architecture PROPERTIES
  LABELS "conformance;identity;architecture"
  TIMEOUT 120
)

add_test(
  NAME identity-architecture-negative
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-identity-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(identity-architecture-negative PROPERTIES
  LABELS "conformance;identity;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME vendored-dependencies
  COMMAND python3
          ${CMAKE_SOURCE_DIR}/products/zomlang/tests/tools/check-vendored-dependencies.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(vendored-dependencies PROPERTIES
  LABELS "conformance;package;vendor;architecture"
  TIMEOUT 120
)

add_test(
  NAME vendored-dependencies-negative
  COMMAND python3
          ${CMAKE_SOURCE_DIR}/products/zomlang/tests/tools/check-vendored-dependencies.py
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(vendored-dependencies-negative PROPERTIES
  LABELS "conformance;package;vendor;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME package-architecture
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-package-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-architecture PROPERTIES
  LABELS "conformance;package;architecture"
  TIMEOUT 120
)

add_test(
  NAME package-architecture-negative
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-package-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-architecture-negative PROPERTIES
  LABELS "conformance;package;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME ir-diagnostic-boundary
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ir-diagnostic-boundary.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ir-diagnostic-boundary PROPERTIES
  LABELS "conformance;ir;diagnostics;architecture"
  TIMEOUT 120
)

add_test(
  NAME ir-diagnostic-boundary-negative
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ir-diagnostic-boundary.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ir-diagnostic-boundary-negative PROPERTIES
  LABELS "conformance;ir;diagnostics;architecture;negative"
  TIMEOUT 120
)

set_tests_properties(ast-generated-schema PROPERTIES
  LABELS "conformance;ast;generated;specification"
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

add_custom_target(check-conformance-ir
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "conformance-ir"
  COMMENT "Running IR/FileCheck conformance tests"
)

add_dependencies(check-conformance zomc)
add_dependencies(check-conformance-ast zomc)
add_dependencies(check-conformance-diagnostics zomc)
add_dependencies(check-conformance-ir zomc)
