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
      "-DZOM_DESCRIPTOR_HEADER=${CMAKE_BINARY_DIR}/compiler/query/query-descriptor-inventory.generated.h")
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

# RFC 0044 O6/KR6.2: `zomc fmt` / `zomc fmt --check` end-to-end contract over the
# real binary. Frontend-only; no LLVM backend required.
add_test(
  NAME format-cli
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-fmt-cli.py"
    --zomc
    "$<TARGET_FILE:zomc>")
set_tests_properties(format-cli PROPERTIES LABELS "unittest;format;cli" TIMEOUT 60)
# RFC 0021 O5/KR5.1: with the LLVM backend built, binary emission is available
# (`zomc compile --emit=binary` writes an object), so the native-execution
# check runs the full compile-link-inspect-publish-execute path via
# --binary-available. Frontend-only builds keep the fail-closed assertions.
if(ZOM_ENABLE_LLVM_BACKEND)
  set(ZOM_NATIVE_EXECUTION_EXTRA_ARGS --binary-available)
else()
  set(ZOM_NATIVE_EXECUTION_EXTRA_ARGS)
endif()
add_test(
  NAME native-execution-cli
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-native-execution.py"
    --zomc
    "$<TARGET_FILE:zomc>"
    --manifest
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/installed-consumer/Zom.toml"
    ${ZOM_NATIVE_EXECUTION_EXTRA_ARGS})
set_tests_properties(native-execution-cli PROPERTIES LABELS "unittest;cli" TIMEOUT 120)

# With the backend built, `zomc compile --emit=binary` writes a native ELF
# object for the scalar module-initializer fixture, the KR5.2 C1
# boolean-conditional diamond fixture, the KR5.2 C2 reducible while-loop fixture,
# the KR5.2 C3 comparison-driven conditional fixture, the KR5.2 C4 same-module
# direct-call fixture, the KR5.2 C5 single-argument direct-call fixture, the
# gap#2 aggregate field-return fixture, the shared-receiver method fixtures, the
# mutating-receiver call fixture, the receiver field-arithmetic fixture, and
# the discarded unit-receiver-call fixture. Gated on the backend so the
# frontend-only lane never requires LLVM.
if(ZOM_ENABLE_LLVM_BACKEND)
  add_test(
    NAME object-emission-cli
    COMMAND
      ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-object-emission.py"
      --zomc
      "$<TARGET_FILE:zomc>"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/installed-consumer/Zom.toml::installed_consumer::installed_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-consumer/Zom.toml::conditional_consumer::conditional_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-bool-consumer/Zom.toml::match_bool_consumer::match_bool_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-expr-consumer/Zom.toml::match_expr_consumer::match_expr_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-expr-multi-arm-consumer/Zom.toml::match_expr_multi_arm_consumer::match_expr_multi_arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-default-consumer/Zom.toml::match_default_consumer::match_default_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/int-patterns-2arm-consumer/Zom.toml::int_patterns_2arm_consumer::int_patterns_2arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-int-3arm-consumer/Zom.toml::match_int_3arm_consumer::match_int_3arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-guards-consumer/Zom.toml::match_guards_consumer::match_guards_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/loop-consumer/Zom.toml::loop_consumer::loop_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/equality-consumer/Zom.toml::equality_consumer::equality_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call-consumer/Zom.toml::call_consumer::call_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/callarg-consumer/Zom.toml::callarg_consumer::callarg_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call2arg-consumer/Zom.toml::call2arg_consumer::call2arg_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call-leaf-consumer/Zom.toml::call_leaf_consumer::call_leaf_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/aggregate-consumer/Zom.toml::aggregate_consumer::aggregate_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/multifield-aggregate-consumer/Zom.toml::multifield_aggregate_consumer::multifield_aggregate_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/scalar-local-call-consumer/Zom.toml::scalar_local_call_consumer::scalar_local_call_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/leading-local-comparison-consumer/Zom.toml::leading_local_comparison_consumer::leading_local_comparison_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-inference-consumer/Zom.toml::binary_inference_consumer::binary_inference_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-consumer/Zom.toml::receiver_consumer::receiver_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-field-consumer/Zom.toml::receiver_field_consumer::receiver_field_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-arg-consumer/Zom.toml::receiver_arg_consumer::receiver_arg_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-consumer/Zom.toml::receiver_mut_consumer::receiver_mut_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-fieldwrite-consumer/Zom.toml::receiver_mut_fieldwrite_consumer::receiver_mut_fieldwrite_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-fieldread-consumer/Zom.toml::receiver_mut_fieldread_consumer::receiver_mut_fieldread_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-local-consumer/Zom.toml::receiver_local_consumer::receiver_local_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-conditional-consumer/Zom.toml::receiver_conditional_consumer::receiver_conditional_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-conditional-comp-consumer/Zom.toml::receiver_conditional_comp_consumer::receiver_conditional_comp_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-selfcall-consumer/Zom.toml::receiver_selfcall_consumer::receiver_selfcall_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/arithmetic-consumer/Zom.toml::arithmetic_consumer::arithmetic_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/method-arithmetic-consumer/Zom.toml::method_arithmetic_consumer::method_arithmetic_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-field-arithmetic-consumer/Zom.toml::receiver_field_arithmetic_consumer::receiver_field_arithmetic_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-void-call-consumer/Zom.toml::receiver_void_call_consumer::receiver_void_call_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-local-consumer/Zom.toml::binary_write_local_consumer::binary_write_local_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-reversed-consumer/Zom.toml::binary_write_reversed_consumer::binary_write_reversed_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-subtraction-consumer/Zom.toml::binary_write_subtraction_consumer::binary_write_subtraction_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-multiply-consumer/Zom.toml::binary_write_multiply_consumer::binary_write_multiply_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/nested-binary-write-consumer/Zom.toml::nested_binary_write_consumer::nested_binary_write_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/multi-compound-assign-consumer/Zom.toml::multi_compound_assign_consumer::multi_compound_assign_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-increment-consumer/Zom.toml::postfix_increment_consumer::postfix_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-decrement-consumer/Zom.toml::postfix_decrement_consumer::postfix_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-increment-consumer/Zom.toml::prefix_increment_consumer::prefix_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-decrement-consumer/Zom.toml::prefix_decrement_consumer::prefix_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-return-increment-consumer/Zom.toml::prefix_return_increment_consumer::prefix_return_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-return-decrement-consumer/Zom.toml::prefix_return_decrement_consumer::prefix_return_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-let-increment-consumer/Zom.toml::prefix_let_increment_consumer::prefix_let_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-let-decrement-consumer/Zom.toml::prefix_let_decrement_consumer::prefix_let_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-return-increment-consumer/Zom.toml::postfix_return_increment_consumer::postfix_return_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-return-decrement-consumer/Zom.toml::postfix_return_decrement_consumer::postfix_return_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-let-increment-consumer/Zom.toml::postfix_let_increment_consumer::postfix_let_increment_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-let-decrement-consumer/Zom.toml::postfix_let_decrement_consumer::postfix_let_decrement_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/dyn-erase-consumer/Zom.toml::dyn_erase_consumer::dyn_erase_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-value-consumer/Zom.toml::enum_value_consumer::enum_value_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-discriminants-consumer/Zom.toml::enum_discriminants_consumer::enum_discriminants_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-tuple-variants-consumer/Zom.toml::enum_tuple_variants_consumer::enum_tuple_variants_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-match-2variant-consumer/Zom.toml::enum_match_2variant_consumer::enum_match_2variant_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-match-3arm-consumer/Zom.toml::enum_match_3arm_consumer::enum_match_3arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirtualization-consumer/Zom.toml::devirtualization_consumer::devirtualization_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirt-two-impl-consumer/Zom.toml::devirt_two_impl_consumer::devirt_two_impl_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirt-multi-call-consumer/Zom.toml::devirt_multi_call_consumer::devirt_multi_call_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-methods-consumer/Zom.toml::enum_methods_consumer::enum_methods_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-ops-consumer/Zom.toml::string_ops_consumer::string_ops_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-ops-consumer/Zom.toml::float_ops_consumer::float_ops_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-compare-consumer/Zom.toml::float_compare_consumer::float_compare_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-arith-cond-consumer/Zom.toml::float_arith_cond_consumer::float_arith_cond_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-arith-arm-consumer/Zom.toml::conditional_arith_arm_consumer::conditional_arith_arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-local-arm-consumer/Zom.toml::conditional_local_arm_consumer::conditional_local_arm_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-consumer/Zom.toml::string_concat_consumer::string_concat_consumer"
      --case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-let-consumer/Zom.toml::string_concat_let_consumer::string_concat_let_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-fourfn-consumer/Zom.toml::reject_fourfn_consumer::reject_fourfn_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-twocaller-consumer/Zom.toml::reject_twocaller_consumer::reject_twocaller_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-nonleaf-consumer/Zom.toml::reject_nonleaf_consumer::reject_nonleaf_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-arithmetic-pow-consumer/Zom.toml::reject_arithmetic_pow_consumer::reject_arithmetic_pow_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/dyn-erase-read-consumer/Zom.toml::dyn_erase_read_consumer::dyn_erase_read_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-match-nonexhaustive-consumer/Zom.toml::enum_match_nonexhaustive_consumer::enum_match_nonexhaustive_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/bool-match-nonexhaustive-consumer/Zom.toml::bool_match_nonexhaustive_consumer::bool_match_nonexhaustive_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-unary-literal-consumer/Zom.toml::reject_unary_literal_consumer::reject_unary_literal_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-match-pattern-consumer/Zom.toml::reject_match_pattern_consumer::reject_match_pattern_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-number-dot-consumer/Zom.toml::reject_number_dot_consumer::reject_number_dot_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-struct-update-consumer/Zom.toml::reject_struct_update_consumer::reject_struct_update_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/reject-for-break-only-consumer/Zom.toml::reject_for_break_only_consumer::reject_for_break_only_consumer"
      --incident-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/installed-consumer/Zom.toml::installed_consumer::installed_consumer"
      --string-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-return-consumer/Zom.toml::string_return_consumer::string_return_consumer::hello"
      --string-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-consumer/Zom.toml::string_concat_consumer::string_concat_consumer::hello world"
      --string-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-let-consumer/Zom.toml::string_concat_let_consumer::string_concat_let_consumer::hello world")
  set_tests_properties(object-emission-cli PROPERTIES LABELS "unittest;cli;llvm" TIMEOUT 180)
endif()

# RFC 0043 O5/KR5.4: with the LLVM backend built, `zomc run` executes an
# entry-compatible native program end to end. The aggregate field-return fixture
# folds to the reserved no-argument i32 zom.module_init entry and exits 42; the
# boolean-conditional diamond fixture keeps a parameterized symbol and is
# object-only, so run fails closed on it. Gated on the backend so the frontend-only
# lane never registers a native-run test.
if(ZOM_ENABLE_LLVM_BACKEND)
  add_test(
    NAME native-run-cli
    COMMAND
      ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-native-run.py"
      --zomc
      "$<TARGET_FILE:zomc>"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/aggregate-consumer/Zom.toml::aggregate_consumer::aggregate_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/multifield-aggregate-consumer/Zom.toml::multifield_aggregate_consumer::multifield_aggregate_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/scalar-local-call-consumer/Zom.toml::scalar_local_call_consumer::scalar_local_call_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call-consumer/Zom.toml::call_consumer::call_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/callarg-consumer/Zom.toml::callarg_consumer::callarg_consumer::7"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call2arg-consumer/Zom.toml::call2arg_consumer::call2arg_consumer::3"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/call-leaf-consumer/Zom.toml::call_leaf_consumer::call_leaf_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/leading-local-comparison-consumer/Zom.toml::leading_local_comparison_consumer::leading_local_comparison_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-inference-consumer/Zom.toml::binary_inference_consumer::binary_inference_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-consumer/Zom.toml::receiver_consumer::receiver_consumer::7"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-field-consumer/Zom.toml::receiver_field_consumer::receiver_field_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-arg-consumer/Zom.toml::receiver_arg_consumer::receiver_arg_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-consumer/Zom.toml::receiver_mut_consumer::receiver_mut_consumer::7"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-fieldwrite-consumer/Zom.toml::receiver_mut_fieldwrite_consumer::receiver_mut_fieldwrite_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-fieldread-consumer/Zom.toml::receiver_mut_fieldread_consumer::receiver_mut_fieldread_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-mut-fieldarith-consumer/Zom.toml::receiver_mut_fieldarith_consumer::receiver_mut_fieldarith_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-local-consumer/Zom.toml::receiver_local_consumer::receiver_local_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-conditional-consumer/Zom.toml::receiver_conditional_consumer::receiver_conditional_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-conditional-comp-consumer/Zom.toml::receiver_conditional_comp_consumer::receiver_conditional_comp_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-selfcall-consumer/Zom.toml::receiver_selfcall_consumer::receiver_selfcall_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/arithmetic-consumer/Zom.toml::arithmetic_consumer::arithmetic_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/method-arithmetic-consumer/Zom.toml::method_arithmetic_consumer::method_arithmetic_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-field-arithmetic-consumer/Zom.toml::receiver_field_arithmetic_consumer::receiver_field_arithmetic_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/receiver-void-call-consumer/Zom.toml::receiver_void_call_consumer::receiver_void_call_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-local-consumer/Zom.toml::binary_write_local_consumer::binary_write_local_consumer::2"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-reversed-consumer/Zom.toml::binary_write_reversed_consumer::binary_write_reversed_consumer::2"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-subtraction-consumer/Zom.toml::binary_write_subtraction_consumer::binary_write_subtraction_consumer::1"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/binary-write-multiply-consumer/Zom.toml::binary_write_multiply_consumer::binary_write_multiply_consumer::9"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/nested-binary-write-consumer/Zom.toml::nested_binary_write_consumer::nested_binary_write_consumer::4"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/multi-compound-assign-consumer/Zom.toml::multi_compound_assign_consumer::multi_compound_assign_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-increment-consumer/Zom.toml::postfix_increment_consumer::postfix_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-decrement-consumer/Zom.toml::postfix_decrement_consumer::postfix_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-increment-consumer/Zom.toml::prefix_increment_consumer::prefix_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-decrement-consumer/Zom.toml::prefix_decrement_consumer::prefix_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-return-increment-consumer/Zom.toml::prefix_return_increment_consumer::prefix_return_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-return-decrement-consumer/Zom.toml::prefix_return_decrement_consumer::prefix_return_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-let-increment-consumer/Zom.toml::prefix_let_increment_consumer::prefix_let_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/prefix-let-decrement-consumer/Zom.toml::prefix_let_decrement_consumer::prefix_let_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-return-increment-consumer/Zom.toml::postfix_return_increment_consumer::postfix_return_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-return-decrement-consumer/Zom.toml::postfix_return_decrement_consumer::postfix_return_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-let-increment-consumer/Zom.toml::postfix_let_increment_consumer::postfix_let_increment_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/postfix-let-decrement-consumer/Zom.toml::postfix_let_decrement_consumer::postfix_let_decrement_consumer::40"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-return-consumer/Zom.toml::string_return_consumer::string_return_consumer::0"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/for-loop-consumer/Zom.toml::for_loop_consumer::for_loop_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/for-loop-accumulator-consumer/Zom.toml::for_loop_accumulator_consumer::for_loop_accumulator_consumer::45"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/if-guarded-break-consumer/Zom.toml::if_guarded_break_consumer::if_guarded_break_consumer::10"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/for-loop-multi-write-consumer/Zom.toml::for_loop_multi_write_consumer::for_loop_multi_write_consumer::10"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/for-loop-break-consumer/Zom.toml::for_loop_break_consumer::for_loop_break_consumer::0"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/for-loop-continue-consumer/Zom.toml::for_loop_continue_consumer::for_loop_continue_consumer::45"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/nested-loops-consumer/Zom.toml::nested_loops_consumer::nested_loops_consumer::9"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/dyn-erase-consumer/Zom.toml::dyn_erase_consumer::dyn_erase_consumer::42"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-bool-consumer/Zom.toml::match_bool_consumer::match_bool_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-expr-consumer/Zom.toml::match_expr_consumer::match_expr_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-expr-multi-arm-consumer/Zom.toml::match_expr_multi_arm_consumer::match_expr_multi_arm_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-default-consumer/Zom.toml::match_default_consumer::match_default_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/int-patterns-2arm-consumer/Zom.toml::int_patterns_2arm_consumer::int_patterns_2arm_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-int-3arm-consumer/Zom.toml::match_int_3arm_consumer::match_int_3arm_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/match-guards-consumer/Zom.toml::match_guards_consumer::match_guards_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-value-consumer/Zom.toml::enum_value_consumer::enum_value_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-discriminants-consumer/Zom.toml::enum_discriminants_consumer::enum_discriminants_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-tuple-variants-consumer/Zom.toml::enum_tuple_variants_consumer::enum_tuple_variants_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-match-2variant-consumer/Zom.toml::enum_match_2variant_consumer::enum_match_2variant_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-match-3arm-consumer/Zom.toml::enum_match_3arm_consumer::enum_match_3arm_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirtualization-consumer/Zom.toml::devirtualization_consumer::devirtualization_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirt-two-impl-consumer/Zom.toml::devirt_two_impl_consumer::devirt_two_impl_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/devirt-multi-call-consumer/Zom.toml::devirt_multi_call_consumer::devirt_multi_call_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/enum-methods-consumer/Zom.toml::enum_methods_consumer::enum_methods_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-ops-consumer/Zom.toml::string_ops_consumer::string_ops_consumer::5"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-ops-consumer/Zom.toml::float_ops_consumer::float_ops_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-compare-consumer/Zom.toml::float_compare_consumer::float_compare_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-arith-cond-consumer/Zom.toml::float_arith_cond_consumer::float_arith_cond_consumer::41"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/float-cast-fold-consumer/Zom.toml::float_cast_fold_consumer::float_cast_fold_consumer::1"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-consumer/Zom.toml::string_concat_consumer::string_concat_consumer::0"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/string-concat-let-consumer/Zom.toml::string_concat_let_consumer::string_concat_let_consumer::0"
      --run-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-local-arm-consumer/Zom.toml::conditional_local_arm_consumer::conditional_local_arm_consumer::5"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-consumer/Zom.toml::conditional_consumer::conditional_consumer"
      --reject-case
      "${CMAKE_CURRENT_SOURCE_DIR}/integration/core-library/conditional-arith-arm-consumer/Zom.toml::conditional_arith_arm_consumer::conditional_arith_arm_consumer")
  set_tests_properties(native-run-cli PROPERTIES LABELS "unittest;cli;llvm" TIMEOUT 120)
endif()
add_test(
  NAME package-workspace-build
  COMMAND
    ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-package-compile.py"
    --zomc
    "$<TARGET_FILE:zomc>")
set_tests_properties(package-workspace-build
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
      -ex "source ${CMAKE_SOURCE_DIR}/tools/gdb/zomlang_gdb.py"
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
      -o "command script import ${CMAKE_SOURCE_DIR}/tools/lldb/zomlang_lldb.py"
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
