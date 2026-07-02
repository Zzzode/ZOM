set(PARSER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/declaration-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/expression-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-helpers.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-recovery.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/pattern-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/statement-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/token-cursor.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-parser.cc
)

add_library(parser STATIC ${PARSER_SRC})
