set(LSP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/json-value.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/json-parse.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/json-serialize.cc
)

add_library(lsp STATIC ${LSP_SRC})
# yyjson is linked PRIVATE: it is used only inside json-parse.cc and never appears
# in a public lsp header, so the dependency does not propagate to lsp's consumers.
target_link_libraries(lsp PUBLIC basic PRIVATE yyjson)
