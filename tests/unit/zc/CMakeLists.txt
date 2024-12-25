set(ZC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

check_include_prefixes(DIRECTORIES ${ZC_TEST_DIR} PREFIXES "src/zc")

file(GLOB_RECURSE TEST_SOURCES ${ZC_TEST_DIR}/*-test.cc)

foreach (TEST_SOURCE ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  get_filename_component(TEST_DIR ${TEST_SOURCE} DIRECTORY)
  get_filename_component(TEST_DIR_NAME ${TEST_DIR} NAME)

  set(UNIQUE_TEST_NAME "${TEST_DIR_NAME}-${TEST_NAME}")

  message(STATUS "Test ${UNIQUE_TEST_NAME} added")
  add_executable(${UNIQUE_TEST_NAME} EXCLUDE_FROM_ALL ${TEST_SOURCE})
  target_link_libraries(${UNIQUE_TEST_NAME} PRIVATE zc ztest)
  target_include_directories(${UNIQUE_TEST_NAME} PRIVATE ${ZOM_ROOT})
  add_test(NAME ${UNIQUE_TEST_NAME} COMMAND ${UNIQUE_TEST_NAME})
endforeach ()
