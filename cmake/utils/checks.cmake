
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckLibraryExists)

check_function_exists(backtrace ZC_HAS_BACKTRACE)
check_symbol_exists(backtrace execinfo.h ZC_HAS_BACKTRACE_HEADER)

if (ZC_HAS_BACKTRACE OR ZC_HAS_BACKTRACE_HEADER)
  add_compile_definitions(ZC_HAS_BACKTRACE=1)

  # On Linux, backtrace() is in libc, but on some other systems it might be in libexecinfo
  check_library_exists(execinfo backtrace "" HAVE_LIBEXECINFO)
  if (HAVE_LIBEXECINFO)
    link_libraries(execinfo)
  endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    check_library_exists(dl dlopen "" HAVE_LIBDL)
    if(HAVE_LIBDL)
        add_compile_definitions(ZC_HAS_LIBDL=1)
        link_libraries(dl)
    endif()
endif()
