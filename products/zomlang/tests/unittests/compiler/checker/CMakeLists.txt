add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

set_tests_properties(body-checker-test PROPERTIES TIMEOUT 120)
set_tests_properties(dispatch-facts-test PROPERTIES TIMEOUT 120)
set_tests_properties(marker-proof-test PROPERTIES TIMEOUT 300)

target_link_libraries(marker-proof-test PRIVATE checker)
