add_subdirectory(basic)
add_subdirectory(diagnostics)
add_subdirectory(driver)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(source)
add_subdirectory(typecheck)
add_subdirectory(zis)

add_library(
  frontend STATIC
  $<TARGET_OBJECTS:basic>
  $<TARGET_OBJECTS:diagnostics>
  $<TARGET_OBJECTS:driver>
  $<TARGET_OBJECTS:lexer>
  $<TARGET_OBJECTS:parser>
  $<TARGET_OBJECTS:source>
  $<TARGET_OBJECTS:typecheck>)
target_link_libraries(frontend PUBLIC zc)

set_target_include_directories(
  "${INCLUDE_DIRS}"
  frontend
  basic
  diagnostics
  driver
  lexer
  parser
  source
  typecheck)
