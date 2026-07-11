set(BINDER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/binder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/decl-collector.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/definition-inventory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/import-resolver.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/name-resolver.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/utilities.cc)

add_library(binder STATIC ${BINDER_SRC})
