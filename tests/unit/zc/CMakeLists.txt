set(ZC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE TEST_SOURCES "${ZC_TEST_DIR}/base/*_test.cc")

foreach(TEST_SOURCE ${TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  message(STATUS "Test ${TEST_NAME} added")
  add_executable(${TEST_NAME} ${TEST_SOURCE})
  target_link_libraries(${TEST_NAME} PRIVATE zc ztest)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
