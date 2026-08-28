set(IR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/error-union-layout-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/error-union-layout-revision.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/executable-manifest-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/executable-publication.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/host-execution-profile.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-failure.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ir-identity.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/link-plan-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/linker-invocation.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/target-registry.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/toolchain-discovery.cc
)

add_library(ir STATIC ${IR_SRC})
