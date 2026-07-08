set(CHECKER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/checker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/body-checker.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/decl-signature.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/exhaustiveness.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query-cycle-detector.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-expr-utils.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/trait-resolver.cc
)

add_library(checker STATIC "${CHECKER_SRC}")
