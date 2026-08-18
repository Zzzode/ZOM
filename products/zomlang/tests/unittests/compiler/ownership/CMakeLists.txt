add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

set_tests_properties(ownership-event-overlay-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-lineage-mutation-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
