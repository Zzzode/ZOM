set(PARSER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-helpers.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-recovery.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/token-cursor.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/syntax/declaration-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/syntax/expression-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/syntax/pattern-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/syntax/statement-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/syntax/type-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query/canonical-parsed-source.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query/parse-source-query.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query/parse-source-query-verifier.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query/effective-source-query.cc
)

add_library(parser STATIC ${PARSER_SRC})
