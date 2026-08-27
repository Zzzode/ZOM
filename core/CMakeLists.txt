set(ZOMCORE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(ZOMCORE_BUILD_ROOT
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/zom/core/src")
set(ZOMCORE_BUILD_MODULE "${ZOMCORE_BUILD_ROOT}/core.zom")
set(ZOMCORE_BUILD_MARKER "${ZOMCORE_BUILD_ROOT}/core/marker.zom")
set(ZOMCORE_BUILD_PRELUDE "${ZOMCORE_BUILD_ROOT}/core/prelude.zom")
set(ZOMCORE_INVENTORY_GENERATOR
    "${CMAKE_SOURCE_DIR}/scripts/codegen/gen_core_library_inventory.py")
set(ZOMCORE_INVENTORY_OUTPUT
    "${CMAKE_BINARY_DIR}/generated/zom/core/core-library-inventory.inc")

add_custom_command(
  OUTPUT "${ZOMCORE_INVENTORY_OUTPUT}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${CMAKE_BINARY_DIR}/generated/zom/core"
  COMMAND "${Python3_EXECUTABLE}" "${ZOMCORE_INVENTORY_GENERATOR}"
          --source-root "${ZOMCORE_SOURCE_ROOT}/src"
          --output "${ZOMCORE_INVENTORY_OUTPUT}"
  DEPENDS
    "${ZOMCORE_INVENTORY_GENERATOR}"
    "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
  COMMENT "Generating the source-backed core library inventory"
  VERBATIM)

add_custom_target(generate-core-library-inventory
  DEPENDS "${ZOMCORE_INVENTORY_OUTPUT}")

add_custom_command(
  OUTPUT
    "${ZOMCORE_BUILD_MODULE}"
    "${ZOMCORE_BUILD_MARKER}"
    "${ZOMCORE_BUILD_PRELUDE}"
  COMMAND
    "${CMAKE_COMMAND}" -E rm -rf "${ZOMCORE_BUILD_ROOT}"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory "${ZOMCORE_BUILD_ROOT}/core"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
    "${ZOMCORE_BUILD_MODULE}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_BUILD_MARKER}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
    "${ZOMCORE_BUILD_PRELUDE}"
  DEPENDS
    "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
  COMMENT "Materializing the source-backed core library"
  VERBATIM)

add_custom_target(
  zomcore-distribution ALL
  DEPENDS
    "${ZOMCORE_BUILD_MODULE}"
    "${ZOMCORE_BUILD_MARKER}"
    "${ZOMCORE_BUILD_PRELUDE}")

install(
  FILES "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core/src")
install(
  FILES
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core/src/core")
