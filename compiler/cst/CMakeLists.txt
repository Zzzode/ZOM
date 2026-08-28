set(CST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/lexeme-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/recovery-codec.cc
)

add_library(cst STATIC ${CST_SRC})
