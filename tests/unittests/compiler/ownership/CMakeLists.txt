add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
  SHARD_OVERRIDES
    ownership-event-overlay-test.cc:8
    ownership-lineage-mutation-test.cc:6
)

# Shard boundaries are derived at configure time; one executable, many CTest
# entries, so these full-pipeline suites run their cases concurrently with the
# rest of -j instead of as one RUN_SERIAL slot.
foreach(ZTEST_SHARD_INDEX RANGE 0 7)
  set_tests_properties(overlay-ownership-event-overlay-test-shard${ZTEST_SHARD_INDEX}
                       PROPERTIES TIMEOUT 900)
endforeach()
foreach(ZTEST_SHARD_INDEX RANGE 0 5)
  set_tests_properties(overlay-ownership-lineage-mutation-test-shard${ZTEST_SHARD_INDEX}
                       PROPERTIES TIMEOUT 900)
endforeach()
set_tests_properties(overlay-ownership-drop-elaboration-test PROPERTIES TIMEOUT 1800)
set_tests_properties(facts-ownership-linear-source-test PROPERTIES TIMEOUT 1800)
set_tests_properties(facts-ownership-capture-test PROPERTIES TIMEOUT 1800)
set_tests_properties(facts-ownership-loop-flow-test PROPERTIES TIMEOUT 1800)
set_tests_properties(facts-ownership-region-membership-test PROPERTIES TIMEOUT 300)
set_tests_properties(facts-ownership-region-outlives-test PROPERTIES TIMEOUT 300)
