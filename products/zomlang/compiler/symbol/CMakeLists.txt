set(SYMBOL_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/symbol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/symbol-denotation.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-symbol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/value-symbol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/symbol-table.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/scope.cc)

add_library(symbol STATIC ${SYMBOL_SRC})
