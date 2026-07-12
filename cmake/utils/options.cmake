# Project options

option(ZOM_ENABLE_ADDRESS_SANITIZER
       "Enable Address Sanitizer (-fsanitize=address)" OFF)
option(ZOM_ENABLE_UNDEFINED_SANITIZER
       "Enable Undefined Behavior Sanitizer (-fsanitize=undefined)" OFF)
option(ZOM_ENABLE_WERROR "Treat warnings as errors (-Werror)" ON)
option(ZOM_ENABLE_WALL "Enable most warnings (-Wall -Wextra)" ON)
option(ZOM_DISABLE_GLOB_CTOR
       "Disable declare global or static variables with dynamic constructors"
       ON)
option(BUILD_STATIC_LIB "Build ZOM as a static library" ON)
option(BUILD_CLI "Build ZOM CLI" ON)
option(ZOM_ENABLE_UNITTESTS "Enable ZOM unittests" ON)
option(ZOM_ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(
  ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS
  "Build and register privileged Linux production sandbox integration tests"
  OFF)

if(ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS)
  if(NOT ZOM_ENABLE_UNITTESTS)
    message(
      FATAL_ERROR
        "ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS requires ZOM_ENABLE_UNITTESTS=ON"
    )
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(
      FATAL_ERROR
        "ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS requires a Linux build host")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
    message(
      FATAL_ERROR
        "ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS supports only x86-64 and AArch64"
    )
  endif()
endif()
