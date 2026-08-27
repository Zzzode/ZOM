set(CHECKER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/checker-identity-authority.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/module-interface-contract.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/operator-kind.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/body/body-checker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/body/marker-proof.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/borrow/borrow-interface.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/borrow/borrow-interface-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/checker-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/checker-diagnostic-id.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/checked-facts-repository.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/coherence-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/cross-module-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/dispatch-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/scalar-literal-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/facts/signature-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/inference/checked-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/inference/inference-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/inference/inference-recovery-context.cc
)

add_library(checker STATIC "${CHECKER_SRC}")
