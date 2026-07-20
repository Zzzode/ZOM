set(IR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-failure.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-identity.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target-registry.cc
)

add_library(ir STATIC ${IR_SRC})
