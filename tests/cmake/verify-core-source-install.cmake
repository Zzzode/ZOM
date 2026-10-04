if(NOT DEFINED ZOM_BINARY_DIR OR NOT DEFINED ZOM_PYTHON OR NOT DEFINED ZOM_SOURCE_ROOT OR
   NOT DEFINED ZOM_TEST_PREFIX OR NOT DEFINED ZOMC_FILE_NAME)
  message(FATAL_ERROR "Core source install test inputs are incomplete")
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
      "Installing the core source layout failed:\n${ZOM_INSTALL_OUTPUT}${ZOM_INSTALL_ERROR}")
endif()

set(ZOM_INSTALLED_CORE "${ZOM_TEST_PREFIX}/share/zom/core/src/core.zom")
set(ZOM_INSTALLED_MARKER
    "${ZOM_TEST_PREFIX}/share/zom/core/src/core/marker.zom")
set(ZOM_INSTALLED_PRELUDE
    "${ZOM_TEST_PREFIX}/share/zom/core/src/core/prelude.zom")
set(ZOM_INSTALLED_MANIFEST "${ZOM_TEST_PREFIX}/share/zom/core/Zom.toml")
set(ZOM_INSTALLED_COMPILER "${ZOM_TEST_PREFIX}/bin/${ZOMC_FILE_NAME}")
set(ZOM_INSTALLED_GDB "${ZOM_TEST_PREFIX}/share/zom/debuggers/gdb/zomlang_gdb.py")
set(ZOM_INSTALLED_LLDB "${ZOM_TEST_PREFIX}/share/zom/debuggers/lldb/zomlang_lldb.py")

foreach(
    ZOM_REQUIRED_FILE
    IN ITEMS
       "${ZOM_INSTALLED_COMPILER}"
       "${ZOM_INSTALLED_CORE}"
       "${ZOM_INSTALLED_MARKER}"
       "${ZOM_INSTALLED_PRELUDE}"
       "${ZOM_INSTALLED_MANIFEST}"
       "${ZOM_INSTALLED_GDB}"
       "${ZOM_INSTALLED_LLDB}")
  if(NOT EXISTS "${ZOM_REQUIRED_FILE}")
    message(FATAL_ERROR "Installed distribution is missing ${ZOM_REQUIRED_FILE}")
  endif()
endforeach()

foreach(
    ZOM_DEBUGGER
    IN ITEMS
       "gdb/zomlang_gdb.py"
       "lldb/zomlang_lldb.py")
  file(SHA256 "${ZOM_SOURCE_ROOT}/tools/${ZOM_DEBUGGER}" ZOM_SOURCE_DIGEST)
  file(SHA256 "${ZOM_TEST_PREFIX}/share/zom/debuggers/${ZOM_DEBUGGER}" ZOM_INSTALLED_DIGEST)
  if(NOT ZOM_SOURCE_DIGEST STREQUAL ZOM_INSTALLED_DIGEST)
    message(FATAL_ERROR "Installed debugger helper bytes differ for ${ZOM_DEBUGGER}")
  endif()
endforeach()

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
  "share/zom/core/src/core.zom"
  "share/zom/core/src/core/marker.zom"
  "share/zom/core/src/core/prelude.zom"
  "share/zom/debuggers/gdb/zomlang_gdb.py"
  "share/zom/debuggers/lldb/zomlang_lldb.py")
list(SORT ZOM_EXPECTED_FILES)
if(NOT ZOM_INSTALLED_FILES STREQUAL ZOM_EXPECTED_FILES)
  message(
    FATAL_ERROR
      "Installed distribution file set differs: ${ZOM_INSTALLED_FILES}")
endif()

foreach(ZOM_DEBUGGER_HELPER IN ITEMS "${ZOM_INSTALLED_GDB}" "${ZOM_INSTALLED_LLDB}")
  execute_process(
    COMMAND "${ZOM_PYTHON}" -m py_compile "${ZOM_DEBUGGER_HELPER}"
    RESULT_VARIABLE ZOM_PYTHON_COMPILE_RESULT
    OUTPUT_VARIABLE ZOM_PYTHON_COMPILE_OUTPUT
    ERROR_VARIABLE ZOM_PYTHON_COMPILE_ERROR)
  if(NOT ZOM_PYTHON_COMPILE_RESULT EQUAL 0)
    message(
      FATAL_ERROR
        "Installed debugger helper syntax check failed for ${ZOM_DEBUGGER_HELPER}:\n"
        "${ZOM_PYTHON_COMPILE_OUTPUT}${ZOM_PYTHON_COMPILE_ERROR}")
  endif()
endforeach()

file(REMOVE_RECURSE "${ZOM_TEST_PREFIX}")
