add_ztest_unit_test(
  type-semantic-type-key-test
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-type-key-test.cc
  LIBRARIES type identity lexer
)
