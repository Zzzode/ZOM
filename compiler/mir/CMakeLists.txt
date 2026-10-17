set(MIR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/built-mir.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/cfg-utils.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/hir-node-resolver.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/loop-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/mir-builder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/mir-fn-builder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/build/recursive-mir-builder.cc
)

add_library(mir STATIC ${MIR_SRC})
