if (ZOM_ENABLE_ADDRESS_SANITIZER)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)

  # GCC on Linux does not automatically link libpthread when using ASAN
  if (UNIX
    AND NOT APPLE
    AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-lpthread)
  endif ()
endif ()

if (ZOM_ENABLE_UNDEFINED_SANITIZER)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif ()

if (ZOM_ENABLE_WERROR)
  message(STATUS "Enable Werror")
  if (MSVC)
    add_compile_options(/WX)
  else ()
    add_compile_options(-Werror)
  endif ()
endif ()

if (ZOM_ENABLE_WALL)
  message(STATUS "Enable Wall")
  if (MSVC)
    add_compile_options(/W4)
  else ()
    add_compile_options(-Wall -Wextra)
  endif ()
endif ()

if (ZOM_DISABLE_GLOB_CTOR)
  message(
    STATUS
    "Disable declare global or static variables with dynamic constructors")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wglobal-constructors)
  endif ()
endif ()

if (ZOM_ENABLE_UNITTESTS)
  message(STATUS "Enable Unitttests")
  enable_testing()
endif()

if(ZOM_ENABLE_COVERAGE)
  if(ZOM_ENABLE_ADDRESS_SANITIZER OR ZOM_ENABLE_UNDEFINED_SANITIZER)
    message(FATAL_ERROR "Coverage cannot be used with sanitizers")
  endif()

  message(STATUS "Enable Coverage")

  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARN "This compiler is not supported for zom")
    add_compile_options(--coverage)
    add_link_options(--coverage)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping -fno-inline)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
  endif()
endif()

add_compile_options(-Wno-sign-compare -Wno-unused-parameter)
