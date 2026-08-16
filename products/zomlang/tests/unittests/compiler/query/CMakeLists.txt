add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES query identity basic zc)

set(QUERY_TEST_DESCRIPTOR_SCHEMA
  ${CMAKE_CURRENT_SOURCE_DIR}/query-test-descriptor-schema.def)
set(QUERY_TEST_DESCRIPTOR_HEADER
  ${CMAKE_CURRENT_BINARY_DIR}/query-test-descriptor-inventory.generated.h)
set(QUERY_DESCRIPTOR_SCHEMA
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/query/query-descriptor-schema.def)
set(QUERY_DESCRIPTOR_HEADER
  ${CMAKE_BINARY_DIR}/products/zomlang/compiler/query/query-descriptor-inventory.generated.h)
set(QUERY_DESCRIPTOR_GENERATOR
  ${CMAKE_SOURCE_DIR}/scripts/generate-query-descriptor-schema.py)

add_custom_command(
  OUTPUT ${QUERY_TEST_DESCRIPTOR_HEADER}
  COMMAND
    "${Python3_EXECUTABLE}" ${QUERY_DESCRIPTOR_GENERATOR}
      --production-schema ${QUERY_DESCRIPTOR_SCHEMA}
      --test-schema ${QUERY_TEST_DESCRIPTOR_SCHEMA}
      --production-output ${QUERY_DESCRIPTOR_HEADER}
      --test-output ${QUERY_TEST_DESCRIPTOR_HEADER}
  DEPENDS
    ${QUERY_DESCRIPTOR_HEADER}
    ${QUERY_DESCRIPTOR_SCHEMA}
    ${QUERY_TEST_DESCRIPTOR_SCHEMA}
    ${QUERY_DESCRIPTOR_GENERATOR}
  VERBATIM)

add_custom_target(query-test-descriptor-inventory
  DEPENDS ${QUERY_TEST_DESCRIPTOR_HEADER})

add_library(
  query-capability-production-fixture STATIC
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/active-definition-authority-query.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/core/query.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/incremental-binding-query-adapter.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/incremental-module-resolution-query.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/incremental-package-graph-query-input.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/module-graph-query-input.cc
  ${CMAKE_SOURCE_DIR}/products/zomlang/compiler/driver/module-graph-query.cc)
add_dependencies(
  query-capability-production-fixture
  query-test-descriptor-inventory
  check-vendored-dependencies)
target_compile_definitions(
  query-capability-production-fixture
  PRIVATE ZOM_QUERY_TEST_DESCRIPTOR_INVENTORY=1)
target_compile_options(
  query-capability-production-fixture
  PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${QUERY_TEST_DESCRIPTOR_HEADER}>")
target_link_libraries(
  query-capability-production-fixture
  PUBLIC frontend)
target_link_libraries(
  query-capability-test
  PRIVATE query-capability-production-fixture)

add_test(
  NAME query-descriptor-schema
  COMMAND
    "${Python3_EXECUTABLE}" ${QUERY_DESCRIPTOR_GENERATOR}
      --check
      --production-schema ${QUERY_DESCRIPTOR_SCHEMA}
      --test-schema ${QUERY_TEST_DESCRIPTOR_SCHEMA}
      --production-output ${QUERY_DESCRIPTOR_HEADER}
      --test-output ${QUERY_TEST_DESCRIPTOR_HEADER})
set_tests_properties(
  query-descriptor-schema
  PROPERTIES LABELS "unittest;query;architecture" TIMEOUT 30)

add_test(
  NAME query-descriptor-schema-negative
  COMMAND "${Python3_EXECUTABLE}" ${QUERY_DESCRIPTOR_GENERATOR} --self-test)
set_tests_properties(
  query-descriptor-schema-negative
  PROPERTIES LABELS "unittest;query;architecture" TIMEOUT 30)

foreach(target IN ITEMS
    query-capability-test
    query-concurrency-test
    query-database-test
    query-eviction-test
    query-observability-test
    query-red-green-test)
  add_dependencies(${target} query-test-descriptor-inventory)
  target_compile_definitions(${target} PRIVATE ZOM_QUERY_TEST_DESCRIPTOR_INVENTORY=1)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${QUERY_TEST_DESCRIPTOR_HEADER}>")
endforeach()
