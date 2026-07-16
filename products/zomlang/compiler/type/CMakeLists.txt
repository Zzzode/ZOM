set(TYPE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/constraint-set.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-algebra.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/primitive-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/function-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/tuple-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/object-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/array-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/named-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-var.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/error-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/interface-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/union-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/intersection-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/reference-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/raw-pointer-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-type-store.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-type-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/existential-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/associated-type.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/coercion.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-scheme.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-env.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-canonical-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/unification.cc)

add_library(type STATIC ${TYPE_SRC})
