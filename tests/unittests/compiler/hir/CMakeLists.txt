add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
  SHARD_OVERRIDES
    hir-module-test.cc:8
)

# The suite drives every case through a real CompilerSession under ASan; it is
# split into eight shards at configure time so cases run concurrently with the
# rest of -j instead of as one ~1200s serial slot.
foreach(ZTEST_SHARD_INDEX RANGE 0 7)
  set_tests_properties(hir-module-test-shard${ZTEST_SHARD_INDEX}
                       PROPERTIES TIMEOUT 900)
endforeach()
