set(IDENTITY_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/brand.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/identity-invariant.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/identity-diagnostic-adapter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/sorted-feature-set.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source-query-input.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source-snapshot.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/canonical-decoder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/canonical-encoder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/canonical-scalar.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/canonical-url.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-name.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-compound.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-core.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-decode.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-encode.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-records.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/identity-interner-set.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/impl-header.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/overload-header.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/build-script-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/compilation-unit-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/crate-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/definition-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/import-binding-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/module-resolution-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/package-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/key/source-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic/context-fingerprint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic/semantic-version.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto/sha256.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto/overload-header-digest.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/text/unicode-normalization.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/text/unicode-normalization-data.cc)

add_library(identity STATIC ${IDENTITY_SRC})
target_link_libraries(identity PUBLIC zc lexer)
