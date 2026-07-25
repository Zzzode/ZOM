if(NOT DEFINED ZOM_BINARY_DIR OR NOT DEFINED ZOM_TEST_PREFIX OR
   NOT DEFINED ZOMC_FILE_NAME)
  message(FATAL_ERROR "Standard prelude install test inputs are incomplete")
endif()

file(REMOVE_RECURSE "${ZOM_TEST_PREFIX}")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --install "${ZOM_BINARY_DIR}" --prefix "${ZOM_TEST_PREFIX}"
  RESULT_VARIABLE ZOM_INSTALL_RESULT
  OUTPUT_VARIABLE ZOM_INSTALL_OUTPUT
  ERROR_VARIABLE ZOM_INSTALL_ERROR)
if(NOT ZOM_INSTALL_RESULT EQUAL 0)
  message(
    FATAL_ERROR
      "Installing the standard prelude layout failed:\n${ZOM_INSTALL_OUTPUT}${ZOM_INSTALL_ERROR}")
endif()

set(ZOM_INSTALLED_MANIFEST "${ZOM_TEST_PREFIX}/share/zom/core/Zom.toml")
set(ZOM_INSTALLED_PRELUDE
    "${ZOM_TEST_PREFIX}/share/zom/core/src/prelude.zom")
set(ZOM_INSTALLED_COMPILER "${ZOM_TEST_PREFIX}/bin/${ZOMC_FILE_NAME}")

foreach(
    ZOM_REQUIRED_FILE
    IN ITEMS
       "${ZOM_INSTALLED_COMPILER}"
       "${ZOM_INSTALLED_MANIFEST}"
       "${ZOM_INSTALLED_PRELUDE}")
  if(NOT EXISTS "${ZOM_REQUIRED_FILE}")
    message(FATAL_ERROR "Installed distribution is missing ${ZOM_REQUIRED_FILE}")
  endif()
endforeach()

file(SIZE "${ZOM_INSTALLED_MANIFEST}" ZOM_MANIFEST_SIZE)
file(SHA256 "${ZOM_INSTALLED_MANIFEST}" ZOM_MANIFEST_DIGEST)
if(NOT ZOM_MANIFEST_SIZE EQUAL 108 OR
   NOT ZOM_MANIFEST_DIGEST STREQUAL
       "3ec3417bca606a7cfbb588b7e177202ade5dcdec48cdff13ba6aea474000ab74")
  message(FATAL_ERROR "Installed standard prelude manifest bytes differ")
endif()

file(SIZE "${ZOM_INSTALLED_PRELUDE}" ZOM_PRELUDE_SIZE)
file(SHA256 "${ZOM_INSTALLED_PRELUDE}" ZOM_PRELUDE_DIGEST)
if(NOT ZOM_PRELUDE_SIZE EQUAL 52 OR
   NOT ZOM_PRELUDE_DIGEST STREQUAL
       "a05fc153f772f0075ed4c8dd9d8affeecb3f01ea674786047e31778f439833a3")
  message(FATAL_ERROR "Installed standard prelude source bytes differ")
endif()

file(
  GLOB_RECURSE ZOM_INSTALLED_FILES
  LIST_DIRECTORIES false
  RELATIVE "${ZOM_TEST_PREFIX}"
  "${ZOM_TEST_PREFIX}/*")
list(SORT ZOM_INSTALLED_FILES)
set(
  ZOM_EXPECTED_FILES
  "bin/${ZOMC_FILE_NAME}"
  "share/zom/core/Zom.toml"
  "share/zom/core/src/prelude.zom")
list(SORT ZOM_EXPECTED_FILES)
if(NOT ZOM_INSTALLED_FILES STREQUAL ZOM_EXPECTED_FILES)
  message(
    FATAL_ERROR
      "Installed distribution file set differs: ${ZOM_INSTALLED_FILES}")
endif()

file(REMOVE_RECURSE "${ZOM_TEST_PREFIX}")
