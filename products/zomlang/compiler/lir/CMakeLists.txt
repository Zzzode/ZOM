set(LIR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/lir-algebra-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/lir-module.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/lir-store.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/lir-stores.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/mir-to-lir.cc
)

add_library(lir STATIC ${LIR_SRC})
