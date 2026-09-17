set(MIR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/built-mir.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/mir-builder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/mir-fn-builder.cc
)

add_library(mir STATIC ${MIR_SRC})
