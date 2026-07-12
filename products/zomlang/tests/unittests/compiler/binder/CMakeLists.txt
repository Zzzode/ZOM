add_ztest_unit_test("binder-test" "binder-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
add_ztest_unit_test("binding-input-test" "binding-input-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
add_test(NAME binder-architecture
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-binder-architecture.py --check)
set_tests_properties(binder-architecture PROPERTIES LABELS "binding;architecture")
add_test(NAME binder-architecture-negative
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-binder-architecture.py --self-test)
set_tests_properties(binder-architecture-negative PROPERTIES LABELS "binding;architecture")
add_ztest_unit_test("decl-collector-test" "decl-collector-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
add_ztest_unit_test("definition-inventory-test" "definition-inventory-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
add_ztest_unit_test("name-resolver-test" "name-resolver-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
add_ztest_unit_test("import-resolver-test" "import-resolver-test.cc" LIBRARIES binder identity diagnostics symbol lexer ast source basic)
