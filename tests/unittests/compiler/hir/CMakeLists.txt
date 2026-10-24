add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

# The suite drives every case through a real CompilerSession under ASan, so its
# wall clock grows with each added case and had already reached ~1200s before
# the conditional-local-write cases landed. Keep a bound well above the
# observed cost so growth shows up as a real failure, not a timeout.
set_tests_properties(hir-module-test PROPERTIES TIMEOUT 2400 RUN_SERIAL TRUE)
