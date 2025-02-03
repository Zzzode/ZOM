file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER SUBDIRS INCLUDE REGEX "^[^.].+$")

set(ALL_TESTS "")
set(ALL_SUBDIR_TARGETS "")

foreach (SUBDIR ${SUBDIRS})
  if (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR})
    file(GLOB SUBDIR_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/*-test.cc)

    set(SUBDIR_ALL_TESTS "")

    foreach (TEST_SOURCE ${SUBDIR_TESTS})
      get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
      set(UNIQUE_TEST_NAME "${SUBDIR}-${TEST_NAME}")

      add_executable(${UNIQUE_TEST_NAME} EXCLUDE_FROM_ALL ${TEST_SOURCE})
      target_link_libraries(${UNIQUE_TEST_NAME} PRIVATE zc frontend)
      target_include_directories(${UNIQUE_TEST_NAME} PRIVATE ${ZOM_ROOT}/libraries ${ZOM_ROOT}/products)

      target_compile_options(${UNIQUE_TEST_NAME} PRIVATE -Wno-global-constructors)

      add_test(NAME ${UNIQUE_TEST_NAME} COMMAND ${UNIQUE_TEST_NAME})

      list(APPEND SUBDIR_ALL_TESTS ${UNIQUE_TEST_NAME})
      list(APPEND ALL_TESTS ${UNIQUE_TEST_NAME})
    endforeach ()

    if (SUBDIR_ALL_TESTS)
      set(SUBDIR_TARGET_NAME "zomlangTests_${SUBDIR}")
      add_custom_target(${SUBDIR_TARGET_NAME} DEPENDS ${SUBDIR_ALL_TESTS})
      list(APPEND ALL_SUBDIR_TARGETS ${SUBDIR_TARGET_NAME})
    endif ()
  endif ()
endforeach ()

add_custom_target(zomlangCompilerTests DEPENDS ${ALL_SUBDIR_TARGETS})
