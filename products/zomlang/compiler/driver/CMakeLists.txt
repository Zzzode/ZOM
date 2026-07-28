set(DRIVER_SRC active-definition-authority-query.cc active-definition-authority-session.cc
               borrow-evidence.cc coherence-builder.cc compiler-session.cc
               contextual-binding-key.cc
               core-library-query-provider.cc core-library-query-verifier.cc crate-graph.cc
               imported-signature-view-projector.cc incremental-binding-query-adapter.cc
               incremental-module-resolution-query.cc
               incremental-package-graph-query-input.cc
               module-discovery.cc module-graph-query-input.cc module-graph-query.cc
               module-interface.cc
               module-interface-diagnostic-adapter.cc
               named-identity-inventory-query.cc named-item-query.cc owner-body-query.cc)

enable_language(C)
add_subdirectory(package/vendor)

add_custom_target(
  check-vendored-dependencies
  COMMAND "${Python3_EXECUTABLE}"
          ${CMAKE_SOURCE_DIR}/products/zomlang/tests/tools/check-vendored-dependencies.py
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
