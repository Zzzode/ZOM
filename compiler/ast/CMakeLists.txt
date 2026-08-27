set(AST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical-tree-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/dump.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/operator.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/schema-verifier.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/tree.cc
)

add_library(ast STATIC ${AST_SRC})
