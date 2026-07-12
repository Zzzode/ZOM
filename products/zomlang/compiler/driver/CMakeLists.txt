file(GLOB DRIVER_SRC compiler-session.cc)

enable_language(C)
add_subdirectory(package/vendor)

add_custom_target(
  check-vendored-dependencies
  COMMAND python3
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
