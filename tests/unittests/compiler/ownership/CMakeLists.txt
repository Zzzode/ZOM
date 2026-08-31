add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

set_tests_properties(ownership-event-overlay-test PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
set_tests_properties(ownership-lineage-mutation-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-drop-elaboration-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-linear-source-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-capture-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-loop-flow-test PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
set_tests_properties(ownership-region-membership-test PROPERTIES TIMEOUT 300)
set_tests_properties(ownership-region-outlives-test PROPERTIES TIMEOUT 300)
