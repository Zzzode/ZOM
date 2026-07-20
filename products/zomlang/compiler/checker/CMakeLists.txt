set(CHECKER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/body-checker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/borrow-interface.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/borrow-interface-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/checked-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/checked-facts-repository.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/checker-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/checker-diagnostic-id.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/coherence-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/cross-module-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatch-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/inference-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/inference-recovery-context.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/marker-proof.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/module-interface-contract.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/operator-kind.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/scalar-literal-facts.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/signature-facts.cc
)

add_library(checker STATIC "${CHECKER_SRC}")
