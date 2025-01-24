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
