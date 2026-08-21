set(OWNERSHIP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/ownership-event-overlay.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ownership-checked-mir.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/drop-elaborated-mir.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/ownership-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source-suppression.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/surface-admission.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/inputs.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/flow.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/init.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/borrow-source.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/loans.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/ownership-facts-codec.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/ownership-facts-revision.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/paths.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/resources.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/regions.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/refs.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/states.cc
)

add_library(ownership STATIC ${OWNERSHIP_SRC})
