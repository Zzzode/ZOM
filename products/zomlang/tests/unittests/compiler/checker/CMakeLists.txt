add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

set_tests_properties(body-checker-test PROPERTIES TIMEOUT 180)
set_tests_properties(checked-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(dispatch-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(signature-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(marker-proof-test PROPERTIES TIMEOUT 600)

target_link_libraries(marker-proof-test PRIVATE checker)
