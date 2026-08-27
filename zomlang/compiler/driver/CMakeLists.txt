set(DRIVER_SRC
  session/compiler-session.cc
  graph/crate-graph.cc
  graph/module-discovery.cc
  interface/borrow-evidence.cc
  interface/coherence-builder.cc
  interface/imported-signature-view-projector.cc
  interface/module-interface.cc
  interface/module-interface-diagnostic-adapter.cc
  query/binding/active-definition-authority-query.cc
  query/binding/active-definition-authority-session.cc
  query/binding/active-identity-membership-query.cc
  query/binding/contextual-binding-key.cc
  query/binding/incremental-binding-query-adapter.cc
  query/binding/incremental-package-graph-query-input.cc
  query/binding/named-identity-inventory-query.cc
  query/binding/named-item-query.cc
  query/binding/owner-body-query.cc
  query/module-graph/incremental-module-resolution-query.cc
  query/module-graph/materialized-module-graph-query.cc
  query/module-graph/module-dependency-provenance-query.cc
  query/module-graph/module-graph-query.cc
  query/module-graph/module-graph-query-input.cc
  core/role-seed-failure.cc
  core/library.cc
  core/marker-authority.cc
  core/signature.cc
  core/query.cc
  core/verifier.cc)

enable_language(C)
add_subdirectory(package/vendor)

add_custom_target(
  check-vendored-dependencies
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/zomlang/tests/tools/check-vendored-dependencies.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking RFC 0012 vendored dependency inventory"
  VERBATIM)

add_library(driver STATIC ${DRIVER_SRC})
add_dependencies(
  driver
  check-vendored-dependencies
  zom_vendor_archive
  zom_vendor_sodium
  zom_vendor_zstd)
add_subdirectory(package)
