# Core library build-tree materialization and installation.
#
# The core library is a standard ZOM package under core/ with its own
# Zom.toml manifest. This module copies the package into the build tree
# so the compiler can find it relative to the executable, and installs
# it alongside the compiler for end-user distributions.

set(ZOMCORE_SOURCE_ROOT "${ZOM_ROOT}/core")
set(ZOMCORE_BUILD_ROOT
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/zom/core")
set(ZOMCORE_BUILD_MANIFEST "${ZOMCORE_BUILD_ROOT}/Zom.toml")
set(ZOMCORE_BUILD_MODULE "${ZOMCORE_BUILD_ROOT}/src/core.zom")
set(ZOMCORE_BUILD_MARKER "${ZOMCORE_BUILD_ROOT}/src/core/marker.zom")
set(ZOMCORE_BUILD_PRELUDE "${ZOMCORE_BUILD_ROOT}/src/core/prelude.zom")

add_custom_command(
  OUTPUT
    "${ZOMCORE_BUILD_MANIFEST}"
    "${ZOMCORE_BUILD_MODULE}"
    "${ZOMCORE_BUILD_MARKER}"
    "${ZOMCORE_BUILD_PRELUDE}"
  COMMAND
    "${CMAKE_COMMAND}" -E rm -rf "${ZOMCORE_BUILD_ROOT}"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory "${ZOMCORE_BUILD_ROOT}/src/core"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
    "${ZOMCORE_BUILD_MANIFEST}"
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
    "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
    "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
  COMMENT "Materializing the source-backed core library"
  VERBATIM)

add_custom_target(
  zomcore-distribution ALL
  DEPENDS
    "${ZOMCORE_BUILD_MANIFEST}"
    "${ZOMCORE_BUILD_MODULE}"
    "${ZOMCORE_BUILD_MARKER}"
    "${ZOMCORE_BUILD_PRELUDE}")

install(
  FILES "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core")
install(
  FILES "${ZOMCORE_SOURCE_ROOT}/src/core.zom"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core/src")
install(
  FILES
    "${ZOMCORE_SOURCE_ROOT}/src/core/marker.zom"
    "${ZOMCORE_SOURCE_ROOT}/src/core/prelude.zom"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core/src/core")
