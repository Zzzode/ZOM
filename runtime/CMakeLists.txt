set(RUNTIME_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/panic.cc)

add_library(runtime STATIC ${RUNTIME_SRC})
target_link_libraries(runtime PUBLIC zc)
set_target_include_directories("${INCLUDE_DIRS}" runtime)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  enable_language(ASM)
  add_library(zom-runtime-entry OBJECT entry-linux-x86_64.S)
  zom_disable_coverage_for_target(zom-runtime-entry)
  target_compile_options(zom-runtime-entry PRIVATE -fno-sanitize=all)
endif()
