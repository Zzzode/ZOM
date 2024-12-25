function(set_target_include_directories include_dir)
  foreach (TARGET ${ARGN})
    if (TARGET ${TARGET})
      target_include_directories(${TARGET} PRIVATE ${include_dir})
    else ()
      message(WARNING "Target '${TARGET}' does not exist. Skipping include directory setup.")
    endif ()
  endforeach ()
endfunction()


function(check_include_prefixes)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs DIRECTORIES PREFIXES)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(SCRIPT_PATH ${ZOM_ROOT}/scripts/check_includes.py)

  if (NOT EXISTS ${SCRIPT_PATH})
    message(FATAL_ERROR "Script not found: ${SCRIPT_PATH}")
  endif ()

  if (NOT ARG_DIRECTORIES)
    message(FATAL_ERROR "At least one DIRECTORY is required")
  endif ()

  if (NOT ARG_PREFIXES)
    message(FATAL_ERROR "At least one PREFIX is required")
  endif ()

  list(LENGTH ARG_DIRECTORIES DIR_COUNT)
  list(LENGTH ARG_PREFIXES PREFIX_COUNT)

  if (NOT DIR_COUNT EQUAL PREFIX_COUNT)
    message(FATAL_ERROR "Number of DIRECTORIES must match number of PREFIXES")
  endif ()

  set(COMMAND_ARGS ${SCRIPT_PATH})
  foreach (DIR PREFIX IN ZIP_LISTS ARG_DIRECTORIES ARG_PREFIXES)
    list(APPEND COMMAND_ARGS ${DIR} ${PREFIX})
  endforeach ()

  execute_process(
    COMMAND python3 ${COMMAND_ARGS}
    OUTPUT_VARIABLE INVALID_INCLUDES
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if (INVALID_INCLUDES)
    message(FATAL_ERROR "Invalid include format found:\n${INVALID_INCLUDES}")
  endif ()
endfunction()
