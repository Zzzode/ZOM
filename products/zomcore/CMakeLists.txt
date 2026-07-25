set(ZOMCORE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(ZOMCORE_BUILD_ROOT
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/zom/core")
set(ZOMCORE_BUILD_MANIFEST "${ZOMCORE_BUILD_ROOT}/Zom.toml")
set(ZOMCORE_BUILD_PRELUDE "${ZOMCORE_BUILD_ROOT}/src/prelude.zom")

add_custom_command(
  OUTPUT
    "${ZOMCORE_BUILD_MANIFEST}"
    "${ZOMCORE_BUILD_PRELUDE}"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory "${ZOMCORE_BUILD_ROOT}/src"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
    "${ZOMCORE_BUILD_MANIFEST}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${ZOMCORE_SOURCE_ROOT}/src/prelude.zom"
    "${ZOMCORE_BUILD_PRELUDE}"
  DEPENDS
    "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
    "${ZOMCORE_SOURCE_ROOT}/src/prelude.zom"
  COMMENT "Materializing the standard prelude distribution"
  VERBATIM)

add_custom_target(
  zomcore-distribution ALL
  DEPENDS
    "${ZOMCORE_BUILD_MANIFEST}"
    "${ZOMCORE_BUILD_PRELUDE}")

install(
  FILES "${ZOMCORE_SOURCE_ROOT}/Zom.toml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core")
install(
  FILES "${ZOMCORE_SOURCE_ROOT}/src/prelude.zom"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/zom/core/src")
