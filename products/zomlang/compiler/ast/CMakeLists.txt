set(AST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/ast.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/dumper.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/factory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/expression.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/operator.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/statement.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/module.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type.cc)

add_library(ast STATIC ${AST_SRC})
