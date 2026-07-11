add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

target_sources(
  unicode-normalization-test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unicode-normalization-conformance-data.cc)
