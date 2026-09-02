set(LSP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/json-value.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/json-parse.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/json-serialize.cc
)

add_library(lsp STATIC ${LSP_SRC})
target_link_libraries(lsp PUBLIC basic)
