add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

target_sources(
  text-unicode-normalization-test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/text/unicode-normalization-conformance-data.cc)
