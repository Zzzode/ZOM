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

set(ZOM_QUERY_RUNTIME_NEGATIVE_CASES
  identity-token-construction:QueryDatabaseIdentityToken
  request-result-copy:QueryRequestResult
  request-result-clone:clone
  memo-base-observer:memoBase
  memo-base-cast:memoAs
  capability-published-construction:CapabilityPublished
  request-decoder-bridge:QueryRuntimeTestAccess
  database-identity-constructor:QueryDatabase
  database-allocator-constructor:QueryDatabase
  database-gate-constructor:QueryDatabase
  database-callback-constructor:QueryDatabase
  database-verifier-constructor:QueryDatabase
  memo-kind-mutation:kind
  memo-database-mutation:database
  memo-revision-mutation:revision)

foreach(case_entry IN LISTS ZOM_QUERY_RUNTIME_NEGATIVE_CASES)
  string(REPLACE ":" ";" case_fields "${case_entry}")
  list(GET case_fields 0 case_name)
  list(GET case_fields 1 forbidden_symbol)
  add_test(
    NAME query-runtime-negative-compile-${case_name}
    COMMAND
      "${CMAKE_COMMAND}"
      "-S${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect-compile-failure"
      "-B${CMAKE_CURRENT_BINARY_DIR}/negative-compile/query-runtime/${case_name}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DZOM_NEGATIVE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile-fail/query-runtime/${case_name}.cc"
      "-DZOM_FORBIDDEN_SYMBOL=${forbidden_symbol}"
      "-DZOM_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
      "-DZOM_BINARY_ROOT=${CMAKE_BINARY_DIR}"
      "-DZOM_DESCRIPTOR_HEADER=${CMAKE_BINARY_DIR}/zomlang/compiler/query/query-descriptor-inventory.generated.h")
  set_tests_properties(
    query-runtime-negative-compile-${case_name}
    PROPERTIES LABELS "unittest;query;architecture;compile-fail" TIMEOUT 60)
endforeach()

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
  NAME native-execution-unavailable-cli
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-native-execution.py"
    --zomc
    "$<TARGET_FILE:zomc>"
    --manifest
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/installed-consumer/Zom.toml")
set_tests_properties(native-execution-unavailable-cli PROPERTIES LABELS "unittest;cli" TIMEOUT 30)
add_test(
  NAME package-workspace-compile
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-package-compile.py"
    --zomc
    "$<TARGET_FILE:zomc>")
set_tests_properties(package-workspace-compile
                     PROPERTIES LABELS "integration;package;cli" TIMEOUT 60)

add_test(
  NAME debugger-helper-syntax
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-debugger-helpers.py")
set_tests_properties(debugger-helper-syntax PROPERTIES LABELS "unittest;debugger" TIMEOUT 30)

find_program(ZOM_GDB_EXECUTABLE NAMES gdb)
if(ZOM_GDB_EXECUTABLE)
  add_test(
    NAME gdb-helper
    COMMAND
      "${ZOM_GDB_EXECUTABLE}"
      --batch
      -ex "source ${CMAKE_SOURCE_DIR}/zomlang/tools/gdb/zomlang_gdb.py"
      -ex "help zomkind"
      -ex "help zominfo")
  set_tests_properties(gdb-helper PROPERTIES LABELS "unittest;debugger;gdb" TIMEOUT 30)
endif()

find_program(ZOM_LLDB_EXECUTABLE NAMES lldb)
if(ZOM_LLDB_EXECUTABLE)
  add_test(
    NAME lldb-helper
    COMMAND
      "${ZOM_LLDB_EXECUTABLE}"
      --batch
      -o "command script import ${CMAKE_SOURCE_DIR}/zomlang/tools/lldb/zomlang_lldb.py"
      -o "help zomkind"
      -o "help zominfo")
  set_tests_properties(lldb-helper PROPERTIES LABELS "unittest;debugger;lldb" TIMEOUT 30)
endif()

add_test(
  NAME core-source-install-layout
  COMMAND
    "${CMAKE_COMMAND}"
    "-DZOM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DZOM_PYTHON=${Python3_EXECUTABLE}"
    "-DZOM_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    "-DZOM_TEST_PREFIX=${CMAKE_BINARY_DIR}/test-prefixes/core-source-install-layout"
    "-DZOMC_FILE_NAME=$<TARGET_FILE_NAME:zomc>"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify-core-source-install.cmake")
set_tests_properties(
  core-source-install-layout
  PROPERTIES LABELS "integration;core;install" RUN_SERIAL TRUE TIMEOUT 120)
add_test(
  NAME core-library-install-consumer
  COMMAND
    "${CMAKE_COMMAND}"
    "-DZOM_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DZOM_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    "-DZOM_TEST_PREFIX=${CMAKE_BINARY_DIR}/test-prefixes/core-library-install-consumer"
    "-DZOMC_FILE_NAME=$<TARGET_FILE_NAME:zomc>"
    "-DZOM_CONSUMER_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/installed-consumer/Zom.toml"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify-core-library-install-consumer.cmake")
set_tests_properties(
  core-library-install-consumer
  PROPERTIES LABELS "integration;core;install" RUN_SERIAL TRUE TIMEOUT 120)

# RFC 0016 -- LLVM discovery-gate configure fixtures. Drives the real gate
# module (cmake/utils/llvm.cmake) through an isolated harness for the positive
# and every configure-negative case, asserting the stable repository-owned
# failure identifiers. The negative cases synthesize self-contained fake LLVM
# installs and need only cmake + python; the positive/ambient cases require the
# provisioned LLVM, forwarded only when ZOM_ENABLE_LLVM_BACKEND is ON.
if(ZOM_ENABLE_LLVM_BACKEND AND DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  set(ZOM_LLVM_GATE_REAL_DIR_ARG "--real-llvm-dir" "${LLVM_DIR}")
else()
  set(ZOM_LLVM_GATE_REAL_DIR_ARG "")
endif()
add_test(
  NAME llvm-discovery-gate
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-llvm-discovery-gate.py"
    "--harness-dir"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/llvm-discovery-gate"
    "--gate-module"
    "${CMAKE_SOURCE_DIR}/cmake/utils/llvm.cmake"
    ${ZOM_LLVM_GATE_REAL_DIR_ARG})
set_tests_properties(
  llvm-discovery-gate
  PROPERTIES LABELS "integration;llvm;rfc0016" RUN_SERIAL TRUE TIMEOUT 300)

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
