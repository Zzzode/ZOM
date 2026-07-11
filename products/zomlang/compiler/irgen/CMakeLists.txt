set(IRGEN_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/error-union-layout.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-dump.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/lowering.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target-data-layout.cc)

add_library(irgen STATIC ${IRGEN_SRC})
