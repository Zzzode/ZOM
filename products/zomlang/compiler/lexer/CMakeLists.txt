set(LEXER_SRC 
  ${CMAKE_CURRENT_SOURCE_DIR}/lexer.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/token.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/unicode-data.cc
)

add_library(lexer STATIC ${LEXER_SRC})
