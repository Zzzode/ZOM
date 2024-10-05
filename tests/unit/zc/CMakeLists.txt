set(ZC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE TEST_SOURCES "${ZC_TEST_DIR}/base/*_test.cc"
     "${ZC_TEST_DIR}/parse/*_test.cc")

foreach(TEST_SOURCE ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  get_filename_component(TEST_DIR ${TEST_SOURCE} DIRECTORY)
  get_filename_component(TEST_DIR_NAME ${TEST_DIR} NAME)

  set(UNIQUE_TEST_NAME "${TEST_DIR_NAME}_${TEST_NAME}")

  message(STATUS "Test ${UNIQUE_TEST_NAME} added")
  add_executable(${UNIQUE_TEST_NAME} ${TEST_SOURCE})
  target_link_libraries(${UNIQUE_TEST_NAME} PRIVATE zc ztest)
  add_test(NAME ${UNIQUE_TEST_NAME} COMMAND ${UNIQUE_TEST_NAME})
endforeach()
