set(IDE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-snapshot.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/snapshot-diagnostic.cc
)

add_library(ide STATIC ${IDE_SRC})
target_link_libraries(ide PUBLIC identity diagnostics)
