set(BINDER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/binding-metadata.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/binding-skeleton.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/binding-input.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/binding-verifier.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/binder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/decl-collector.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/definition-identity-map.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/definition-inventory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/definition-site.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/frozen-definition-inventory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/import-resolver.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/name-resolver.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/parsed-module.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/scope-arena.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/utilities.cc)

add_library(binder STATIC ${BINDER_SRC})
