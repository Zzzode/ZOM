set(CST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/lexeme-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/lexeme-stream-builder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parse-eligibility.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parser-event-stream.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/recovery-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/recovery-stream-builder.cc
)

add_library(cst STATIC ${CST_SRC})
