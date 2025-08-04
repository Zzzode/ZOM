# AST-specific unit tests

# Add AST unit tests with specific libraries
add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)