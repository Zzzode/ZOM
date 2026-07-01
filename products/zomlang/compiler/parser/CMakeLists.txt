set(PARSER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/token-cursor.cc
)

add_library(parser STATIC ${PARSER_SRC})
