add_subdirectory(basic)
add_subdirectory(diagnostics)
add_subdirectory(driver)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(source)
add_subdirectory(symbol)
add_subdirectory(checker)
add_subdirectory(ast)
add_subdirectory(trace)
add_subdirectory(binder)

add_library(
  frontend STATIC
  $<TARGET_OBJECTS:ast>
  $<TARGET_OBJECTS:binder>
  $<TARGET_OBJECTS:basic>
  $<TARGET_OBJECTS:checker>
  $<TARGET_OBJECTS:diagnostics>
  $<TARGET_OBJECTS:driver>
  $<TARGET_OBJECTS:lexer>
  $<TARGET_OBJECTS:parser>
  $<TARGET_OBJECTS:source>
  $<TARGET_OBJECTS:symbol>
  $<TARGET_OBJECTS:trace>)
target_link_libraries(frontend PUBLIC zc)

set_target_include_directories(
  "${INCLUDE_DIRS}"
  ast
  basic
  binder
  checker
  diagnostics
  driver
  lexer
  parser
  source
  symbol
  trace)
