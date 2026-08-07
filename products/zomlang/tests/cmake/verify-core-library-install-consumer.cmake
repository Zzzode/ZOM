if(NOT DEFINED ZOM_BINARY_DIR OR NOT DEFINED ZOM_TEST_PREFIX OR
   NOT DEFINED ZOMC_FILE_NAME OR NOT DEFINED ZOM_CONSUMER_MANIFEST)
  message(FATAL_ERROR "Installed core consumer test inputs are incomplete")
endif()

file(REMOVE_RECURSE "${ZOM_TEST_PREFIX}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${ZOM_BINARY_DIR}" --prefix "${ZOM_TEST_PREFIX}"
  RESULT_VARIABLE ZOM_INSTALL_RESULT
  OUTPUT_VARIABLE ZOM_INSTALL_OUTPUT
  ERROR_VARIABLE ZOM_INSTALL_ERROR)
if(NOT ZOM_INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR "Installing the consumer distribution failed:\n${ZOM_INSTALL_OUTPUT}${ZOM_INSTALL_ERROR}")
endif()

set(ZOM_INSTALLED_COMPILER "${ZOM_TEST_PREFIX}/bin/${ZOMC_FILE_NAME}")
set(ZOM_INSTALLED_CORE "${ZOM_TEST_PREFIX}/share/zom/core/src/core.zom")
if(NOT EXISTS "${ZOM_INSTALLED_COMPILER}" OR NOT EXISTS "${ZOM_INSTALLED_CORE}")
  message(FATAL_ERROR "Installed consumer prerequisites are missing")
endif()

execute_process(
  COMMAND "${ZOM_INSTALLED_COMPILER}" compile --manifest-path "${ZOM_CONSUMER_MANIFEST}"
          --package installed_consumer --bin installed_consumer --dump-ast
  RESULT_VARIABLE ZOM_CONSUMER_RESULT
  OUTPUT_VARIABLE ZOM_CONSUMER_OUTPUT
  ERROR_VARIABLE ZOM_CONSUMER_ERROR)
if(NOT ZOM_CONSUMER_RESULT EQUAL 0)
  message(FATAL_ERROR "Installed consumer compilation failed:\n${ZOM_CONSUMER_OUTPUT}${ZOM_CONSUMER_ERROR}")
endif()
if(NOT ZOM_CONSUMER_OUTPUT MATCHES "SourceFile")
  message(FATAL_ERROR "Installed consumer did not publish an AST")
endif()

file(REMOVE_RECURSE "${ZOM_TEST_PREFIX}")
