set(IR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/ir-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/ir-failure.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-identity.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/layout/error-union-layout-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/layout/error-union-layout-revision.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/link/invoke-linker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/link/link-plan-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/publication/executable-inspector.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/publication/executable-manifest-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/publication/executable-publication.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target/host-execution-profile.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target/target-registry.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target/toolchain-discovery.cc
)

add_library(ir STATIC ${IR_SRC})
