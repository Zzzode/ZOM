# Add subdirectories for organized test structure
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER SUBDIRS INCLUDE REGEX "^[^.].+$")

# Handle subdirectories with their own CMakeLists.txt
foreach(SUBDIR ${SUBDIRS})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
      add_subdirectory(${SUBDIR})
    endif()
  endif()
endforeach()

# Only add tests from current directory (not subdirectories)
# to avoid duplicate registration with subdirectory CMakeLists.txt
file(GLOB CURRENT_DIR_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/*-test.cc")
foreach(TEST_FILE ${CURRENT_DIR_TESTS})
  get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)
  add_ztest_unit_test("${TEST_NAME}" "${TEST_FILE}" LIBRARIES frontend)
endforeach()
