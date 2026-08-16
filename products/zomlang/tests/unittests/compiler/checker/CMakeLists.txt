add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

set_tests_properties(body-body-checker-test PROPERTIES TIMEOUT 180)
set_tests_properties(inference-checked-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(facts-dispatch-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(facts-signature-facts-test PROPERTIES TIMEOUT 180)
set_tests_properties(body-marker-proof-test PROPERTIES TIMEOUT 600)

target_link_libraries(body-marker-proof-test PRIVATE checker)
