set(FORMAT_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/doc.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/doc-renderer.cc
)

add_library(format STATIC ${FORMAT_SRC})
