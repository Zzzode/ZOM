# Add subdirectories for organized test structure
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER SUBDIRS INCLUDE REGEX "^[^.].+$")

# First, handle subdirectories with their own CMakeLists.txt
foreach(SUBDIR ${SUBDIRS})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
      add_subdirectory(${SUBDIR})
    endif()
  endif()
endforeach()

# Then, use the discovery function for the current directory
# This will automatically handle subdirectories without CMakeLists.txt
add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)
