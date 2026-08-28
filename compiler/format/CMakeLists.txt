set(FORMAT_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/doc.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/doc-renderer.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source-edits.cc
)

add_library(format STATIC ${FORMAT_SRC})
