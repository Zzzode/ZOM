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
  NAME lit-exec-root
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-lit-exec-root.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(lit-exec-root PROPERTIES
  LABELS "conformance;lit;architecture"
  TIMEOUT 120
)

add_test(
  NAME lit-exec-root-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-lit-exec-root.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(lit-exec-root-negative PROPERTIES
  LABELS "conformance;lit;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME parser-coverage
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-parser-coverage.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(parser-coverage PROPERTIES
  LABELS "conformance;parser;coverage;specification"
  TIMEOUT 120
)

add_test(
  NAME lexer-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-lexer-architecture.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(lexer-architecture PROPERTIES
  LABELS "conformance;lexer;specification"
  TIMEOUT 120
)

add_test(
  NAME spec-alignment
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-spec-alignment.py --check
          --report ${CMAKE_BINARY_DIR}/reports/spec-alignment/rfc0027.json
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(spec-alignment PROPERTIES
  LABELS "conformance;specification;architecture"
  TIMEOUT 120
)

add_test(
  NAME spec-alignment-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-spec-alignment.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(spec-alignment-negative PROPERTIES
  LABELS "conformance;specification;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME ast-generated-schema
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/codegen/gen_ast.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
  NAME compiler-session-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-compiler-session-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(compiler-session-architecture PROPERTIES
  LABELS "conformance;compiler-session;architecture"
  TIMEOUT 120
)

add_test(
  NAME compiler-session-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-compiler-session-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(compiler-session-architecture-negative PROPERTIES
  LABELS "conformance;compiler-session;architecture;negative"
  TIMEOUT 300
)

add_test(
  NAME checker-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-checker-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(checker-architecture PROPERTIES
  LABELS "conformance;checker;architecture"
  TIMEOUT 120
)

add_test(
  NAME checker-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-checker-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
  NAME impl-source-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-impl-source-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(impl-source-architecture PROPERTIES
  LABELS "conformance;parser;binder;checker;architecture;specification"
  TIMEOUT 120
)

add_test(
  NAME impl-source-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-impl-source-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(impl-source-architecture-negative PROPERTIES
  LABELS "conformance;parser;binder;checker;architecture;negative;specification"
  TIMEOUT 120
)

set_tests_properties(checker-architecture-negative PROPERTIES
  LABELS "conformance;checker;architecture;negative"
  TIMEOUT 600
)

add_test(
  NAME canonical-header-syntax-schema
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/scripts/generate-canonical-header-syntax-schema.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(canonical-header-syntax-schema PROPERTIES
  LABELS "conformance;identity;generated;architecture"
  TIMEOUT 120
)

add_test(
  NAME canonical-header-syntax-schema-negative
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/scripts/generate-canonical-header-syntax-schema.py
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(canonical-header-syntax-schema-negative PROPERTIES
  LABELS "conformance;identity;generated;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME identity-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-identity-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(identity-architecture PROPERTIES
  LABELS "conformance;identity;architecture"
  TIMEOUT 120
)

add_test(
  NAME identity-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-identity-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(identity-architecture-negative PROPERTIES
  LABELS "conformance;identity;architecture;negative"
  TIMEOUT 300
)

add_test(
  NAME core-library-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-core-library-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(core-library-architecture PROPERTIES
  LABELS "conformance;core;architecture"
  TIMEOUT 120
)

add_test(
  NAME core-library-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-core-library-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(core-library-architecture-negative PROPERTIES
  LABELS "conformance;core;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME ownership-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ownership-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ownership-architecture PROPERTIES
  LABELS "conformance;ownership;architecture"
  TIMEOUT 120
)

add_test(
  NAME ownership-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ownership-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ownership-architecture-negative PROPERTIES
  LABELS "conformance;ownership;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME ownership-coverage-checker-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ownership-coverage.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ownership-coverage-checker-negative PROPERTIES
  LABELS "conformance;ownership;coverage;negative"
  TIMEOUT 120
)

add_test(
  NAME english-only
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-english-only.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(english-only PROPERTIES
  LABELS "conformance;english-only;architecture"
  TIMEOUT 120
)

add_test(
  NAME english-only-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-english-only.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(english-only-negative PROPERTIES
  LABELS "conformance;english-only;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME diff-hygiene
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-diff-hygiene.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(diff-hygiene PROPERTIES
  LABELS "conformance;diff-hygiene;architecture"
  TIMEOUT 120
)

add_test(
  NAME diff-hygiene-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-diff-hygiene.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(diff-hygiene-negative PROPERTIES
  LABELS "conformance;diff-hygiene;architecture;negative"
  TIMEOUT 120
)

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

add_test(
  NAME incremental-query-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-incremental-query-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(incremental-query-architecture PROPERTIES
  LABELS "conformance;query;incremental;architecture"
  TIMEOUT 120
)

add_test(
  NAME incremental-query-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-incremental-query-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(incremental-query-architecture-negative PROPERTIES
  LABELS "conformance;query;incremental;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME incremental-query-benchmark-runner
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/run-incremental-query-benchmarks.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(incremental-query-benchmark-runner PROPERTIES
  LABELS "conformance;query;incremental;benchmark"
  TIMEOUT 30
)

add_test(
  NAME vendored-dependencies
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/tests/tools/check-vendored-dependencies.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(vendored-dependencies PROPERTIES
  LABELS "conformance;package;vendor;architecture"
  TIMEOUT 120
)

add_test(
  NAME vendored-dependencies-negative
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/tests/tools/check-vendored-dependencies.py
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(vendored-dependencies-negative PROPERTIES
  LABELS "conformance;package;vendor;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME package-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-package-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-architecture PROPERTIES
  LABELS "conformance;package;architecture"
  TIMEOUT 120
)

add_test(
  NAME package-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-package-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-architecture-negative PROPERTIES
  LABELS "conformance;package;architecture;negative"
  TIMEOUT 300
)

add_test(
  NAME package-generated-oracles
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/codegen/gen_package_oracles.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-generated-oracles PROPERTIES
  LABELS "conformance;package;generated;architecture"
  TIMEOUT 180
)

add_test(
  NAME package-generated-oracles-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/codegen/gen_package_oracles.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(package-generated-oracles-negative PROPERTIES
  LABELS "conformance;package;generated;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME ir-architecture
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ir-architecture.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ir-architecture PROPERTIES
  LABELS "conformance;ir;diagnostics;architecture"
  TIMEOUT 120
)

add_test(
  NAME ir-architecture-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-ir-architecture.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(ir-architecture-negative PROPERTIES
  LABELS "conformance;ir;diagnostics;architecture;negative"
  TIMEOUT 120
)

add_test(
  NAME diagnostic-coverage
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-diagnostic-coverage.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(diagnostic-coverage PROPERTIES
  LABELS "conformance;diagnostics;coverage;architecture"
  TIMEOUT 120
)

add_test(
  NAME diagnostic-coverage-negative
  COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/check-diagnostic-coverage.py --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_tests_properties(diagnostic-coverage-negative PROPERTIES
  LABELS "conformance;diagnostics;coverage;architecture;negative"
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

add_dependencies(check-conformance zomc)
add_dependencies(check-conformance-ast zomc)
add_dependencies(check-conformance-diagnostics zomc)
