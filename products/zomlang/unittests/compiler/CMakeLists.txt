file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER SUBDIRS INCLUDE REGEX "^[^.].+$")

foreach (SUBDIR ${SUBDIRS})
  if (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR})
    file(GLOB SUBDIR_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/*-test.cc)

    foreach (TEST_SOURCE ${SUBDIR_TESTS})
      get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
      set(UNIQUE_TEST_NAME "${SUBDIR}-${TEST_NAME}")

      add_executable(${UNIQUE_TEST_NAME} ${TEST_SOURCE})
      target_link_libraries(${UNIQUE_TEST_NAME} PRIVATE frontend ztest)
      target_include_directories(${UNIQUE_TEST_NAME} PRIVATE ${ZOM_ROOT}/libraries ${ZOM_ROOT}/products)

      target_compile_options(${UNIQUE_TEST_NAME} PRIVATE -Wno-global-constructors)

      add_test(NAME ${UNIQUE_TEST_NAME} COMMAND ${UNIQUE_TEST_NAME})
      add_coverage_to_test(${UNIQUE_TEST_NAME})
      add_test_to_coverage(${UNIQUE_TEST_NAME})
    endforeach ()
  endif ()
endforeach ()
