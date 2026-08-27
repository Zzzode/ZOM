set(HIR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/checked-module.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/hir-module.cc
)

add_library(hir STATIC ${HIR_SRC})
