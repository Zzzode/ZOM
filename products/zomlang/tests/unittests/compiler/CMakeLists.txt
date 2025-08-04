# Use the new ztest unit test discovery function
add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

# Add subdirectories for organized test structure
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER SUBDIRS INCLUDE REGEX "^[^.].+$")

foreach(SUBDIR ${SUBDIRS})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR})
    # Check if subdirectory has its own CMakeLists.txt
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
      add_subdirectory(${SUBDIR})
    else()
      # Use the discovery function for this subdirectory
      add_ztest_unit_tests_from_directory(
        ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}
        LIBRARIES frontend
      )
    endif()
  endif()
endforeach()
