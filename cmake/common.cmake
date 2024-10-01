if(ZOM_ENABLE_ADDRESS_SANITIZER)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)

  # GCC on Linux does not automatically link libpthread when using ASAN
  if(UNIX
     AND NOT APPLE
     AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-lpthread)
  endif()
endif()

if(ZOM_ENABLE_UNDEFINED_SANITIZER)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endif()

if(ZOM_ENABLE_WERROR)
  message(STATUS "Enable Werror")
  if(MSVC)
    add_compile_options(/WX)
  else()
    add_compile_options(-Werror)
  endif()
endif()

if(ZOM_ENABLE_WALL)
  message(STATUS "Enable Wall")
  if(MSVC)
    add_compile_options(/W4)
  else()
    add_compile_options(-Wall -Wextra)
  endif()
endif()

if(ZOM_DISABLE_GLOB_CTOR)
  message(
    STATUS
      "Disable declare global or static variables with dynamic constructors")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wglobal-constructors)
  endif()
endif()
