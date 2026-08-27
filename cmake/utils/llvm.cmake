# RFC 0016 -- LLVM build and CI contract (fail-closed LLVM discovery gate).
#
# This module implements the exact "LLVM build and CI contract" defined in
# docs/rfc/0016-context-bound-target-registry-verification.md. It is included
# from the top-level CMakeLists.txt ONLY when ZOM_ENABLE_LLVM_BACKEND is ON.
# When the option is OFF (the default) no LLVM package is searched or linked and
# the frontend-only build is completely unaffected.
#
# When enabled, this gate is fail-closed: it rejects an unset/empty, invalid,
# non-provenant, wrong-version, incomplete-component, or wrong-target LLVM
# install before any compiler target is generated. Every failure reports a
# stable repository-owned identifier (asserted by the fixtures) rather than
# vendor prose:
#
#   ZOM-CMAKE-LLVM-DIR-REQUIRED     LLVM_DIR unset or empty.
#   ZOM-CMAKE-LLVM-DIR-INVALID      LLVM_DIR missing or lacks LLVMConfig.cmake.
#   ZOM-CMAKE-LLVM-PROVENANCE       Resolved package/tool paths are not the
#                                   byte-identical canonical request chain.
#   ZOM-CMAKE-LLVM-VERSION          LLVM_PACKAGE_VERSION is not exactly 22.1.8.
#   ZOM-CMAKE-LLVM-CONFIG-VERSION   llvm-config --version disagrees with it.
#   ZOM-CMAKE-LLVM-COMPONENT        A required component is unavailable.
#   ZOM-CMAKE-LLVM-TARGET           X86 or AArch64 is absent from the inventory.
#
# LLVM linkage itself belongs in compiler/backend/** (not yet implemented); this
# module performs only discovery, provenance verification, and a component/target
# smoke check. No frontend target links LLVM.

# The pinned dependency. This is NOT find_package(LLVM 22 ...): LLVM 22.1's
# official version file treats a bare "22" request as incompatible "22.0".
set(ZOM_LLVM_REQUIRED_VERSION "22.1.8")

# The exact component set required by the target-aware backend contract.
set(ZOM_LLVM_REQUIRED_COMPONENTS
    Core
    Support
    Target
    TargetParser
    MC
    CodeGen
    AsmParser
    AsmPrinter
    BitWriter
    X86
    AArch64)

# The required installed target inventory.
set(ZOM_LLVM_REQUIRED_TARGETS X86 AArch64)

# ---------------------------------------------------------------------------
# 1. Reject an unset or empty LLVM_DIR before any package search.
# ---------------------------------------------------------------------------
if(NOT DEFINED LLVM_DIR OR "${LLVM_DIR}" STREQUAL "")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-DIR-REQUIRED: ZOM_ENABLE_LLVM_BACKEND is ON but LLVM_DIR is "
    "unset or empty. Pass -DLLVM_DIR=<llvm>/lib/cmake/llvm explicitly; the build "
    "never synthesizes a host path or performs an ambient LLVM search.")
endif()

# ---------------------------------------------------------------------------
# 2. Snapshot and canonicalize the requested directory; require it and its
#    LLVMConfig.cmake to exist.
# ---------------------------------------------------------------------------
file(REAL_PATH "${LLVM_DIR}" ZOM_REQUESTED_LLVM_DIR EXPAND_TILDE)

if(NOT IS_DIRECTORY "${ZOM_REQUESTED_LLVM_DIR}"
   OR NOT EXISTS "${ZOM_REQUESTED_LLVM_DIR}/LLVMConfig.cmake")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-DIR-INVALID: LLVM_DIR '${LLVM_DIR}' (canonical "
    "'${ZOM_REQUESTED_LLVM_DIR}') is not a directory containing LLVMConfig.cmake. "
    "No ambient LLVM install, CMAKE_PREFIX_PATH entry, environment hint, or user "
    "package registry may substitute for the explicitly requested directory.")
endif()

# ---------------------------------------------------------------------------
# 3. Search for the package ONLY at the requested directory, with no default
#    paths, package registry, or ambient search.
# ---------------------------------------------------------------------------
find_package(LLVM REQUIRED CONFIG
             PATHS "${ZOM_REQUESTED_LLVM_DIR}"
             NO_DEFAULT_PATH)

# ---------------------------------------------------------------------------
# 4. Canonicalize the resolved provenance chain and require byte-identical
#    equality with the snapshotted request.
# ---------------------------------------------------------------------------
file(REAL_PATH "${LLVM_DIR}" ZOM_RESOLVED_LLVM_DIR EXPAND_TILDE)
file(REAL_PATH "${LLVM_CMAKE_DIR}" ZOM_RESOLVED_LLVM_CMAKE_DIR EXPAND_TILDE)
file(REAL_PATH "${LLVM_INSTALL_PREFIX}" ZOM_RESOLVED_LLVM_INSTALL_PREFIX EXPAND_TILDE)
file(REAL_PATH "${LLVM_TOOLS_BINARY_DIR}" ZOM_RESOLVED_LLVM_TOOLS_BINARY_DIR EXPAND_TILDE)

if(NOT "${ZOM_RESOLVED_LLVM_DIR}" STREQUAL "${ZOM_REQUESTED_LLVM_DIR}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: resolved LLVM_DIR '${ZOM_RESOLVED_LLVM_DIR}' is "
    "not the byte-identical canonical requested directory "
    "'${ZOM_REQUESTED_LLVM_DIR}'.")
endif()

if(NOT "${ZOM_RESOLVED_LLVM_CMAKE_DIR}" STREQUAL "${ZOM_REQUESTED_LLVM_DIR}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: canonical LLVM_CMAKE_DIR "
    "'${ZOM_RESOLVED_LLVM_CMAKE_DIR}' is not byte-identical to the requested "
    "directory '${ZOM_REQUESTED_LLVM_DIR}'.")
endif()

# ---------------------------------------------------------------------------
# 5. The only introspection executable is the canonical
#    ${LLVM_TOOLS_BINARY_DIR}/llvm-config. Never find_program(llvm-config) and
#    never accept a PATH executable.
# ---------------------------------------------------------------------------
set(ZOM_LLVM_CONFIG_EXECUTABLE "${ZOM_RESOLVED_LLVM_TOOLS_BINARY_DIR}/llvm-config")

if(NOT EXISTS "${ZOM_LLVM_CONFIG_EXECUTABLE}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: introspection executable "
    "'${ZOM_LLVM_CONFIG_EXECUTABLE}' does not exist under the resolved "
    "LLVM_TOOLS_BINARY_DIR.")
endif()

file(REAL_PATH "${ZOM_LLVM_CONFIG_EXECUTABLE}" ZOM_LLVM_CONFIG_REAL EXPAND_TILDE)
get_filename_component(ZOM_LLVM_CONFIG_PARENT "${ZOM_LLVM_CONFIG_REAL}" DIRECTORY)
file(REAL_PATH "${ZOM_LLVM_CONFIG_PARENT}" ZOM_LLVM_CONFIG_PARENT EXPAND_TILDE)

if(NOT "${ZOM_LLVM_CONFIG_PARENT}" STREQUAL "${ZOM_RESOLVED_LLVM_TOOLS_BINARY_DIR}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: llvm-config canonical parent "
    "'${ZOM_LLVM_CONFIG_PARENT}' is not the resolved LLVM_TOOLS_BINARY_DIR "
    "'${ZOM_RESOLVED_LLVM_TOOLS_BINARY_DIR}'.")
endif()

# --prefix must canonicalize to exactly LLVM_INSTALL_PREFIX.
execute_process(
  COMMAND "${ZOM_LLVM_CONFIG_EXECUTABLE}" --prefix
  OUTPUT_VARIABLE ZOM_LLVM_CONFIG_PREFIX_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE ZOM_LLVM_CONFIG_PREFIX_RC)
if(NOT ZOM_LLVM_CONFIG_PREFIX_RC EQUAL 0)
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: '${ZOM_LLVM_CONFIG_EXECUTABLE} --prefix' failed "
    "(exit ${ZOM_LLVM_CONFIG_PREFIX_RC}).")
endif()
file(REAL_PATH "${ZOM_LLVM_CONFIG_PREFIX_RAW}" ZOM_LLVM_CONFIG_PREFIX EXPAND_TILDE)
if(NOT "${ZOM_LLVM_CONFIG_PREFIX}" STREQUAL "${ZOM_RESOLVED_LLVM_INSTALL_PREFIX}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: 'llvm-config --prefix' canonical result "
    "'${ZOM_LLVM_CONFIG_PREFIX}' is not the resolved LLVM_INSTALL_PREFIX "
    "'${ZOM_RESOLVED_LLVM_INSTALL_PREFIX}'.")
endif()

# --cmakedir must canonicalize to exactly LLVM_CMAKE_DIR / resolved LLVM_DIR /
# the requested directory.
execute_process(
  COMMAND "${ZOM_LLVM_CONFIG_EXECUTABLE}" --cmakedir
  OUTPUT_VARIABLE ZOM_LLVM_CONFIG_CMAKEDIR_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE ZOM_LLVM_CONFIG_CMAKEDIR_RC)
if(NOT ZOM_LLVM_CONFIG_CMAKEDIR_RC EQUAL 0)
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: '${ZOM_LLVM_CONFIG_EXECUTABLE} --cmakedir' failed "
    "(exit ${ZOM_LLVM_CONFIG_CMAKEDIR_RC}).")
endif()
file(REAL_PATH "${ZOM_LLVM_CONFIG_CMAKEDIR_RAW}" ZOM_LLVM_CONFIG_CMAKEDIR EXPAND_TILDE)
if(NOT "${ZOM_LLVM_CONFIG_CMAKEDIR}" STREQUAL "${ZOM_REQUESTED_LLVM_DIR}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: 'llvm-config --cmakedir' canonical result "
    "'${ZOM_LLVM_CONFIG_CMAKEDIR}' is not the requested/resolved LLVM_DIR "
    "'${ZOM_REQUESTED_LLVM_DIR}'.")
endif()

execute_process(
  COMMAND "${ZOM_LLVM_CONFIG_EXECUTABLE}" --version
  OUTPUT_VARIABLE ZOM_LLVM_CONFIG_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE ZOM_LLVM_CONFIG_VERSION_RC)
if(NOT ZOM_LLVM_CONFIG_VERSION_RC EQUAL 0)
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-PROVENANCE: '${ZOM_LLVM_CONFIG_EXECUTABLE} --version' failed "
    "(exit ${ZOM_LLVM_CONFIG_VERSION_RC}).")
endif()

# ---------------------------------------------------------------------------
# 6. Require LLVM_PACKAGE_VERSION and llvm-config --version to equal exactly
#    22.1.8.
# ---------------------------------------------------------------------------
if(NOT "${LLVM_PACKAGE_VERSION}" STREQUAL "${ZOM_LLVM_REQUIRED_VERSION}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-VERSION: LLVM_PACKAGE_VERSION '${LLVM_PACKAGE_VERSION}' is not "
    "the pinned '${ZOM_LLVM_REQUIRED_VERSION}'.")
endif()

if(NOT "${ZOM_LLVM_CONFIG_VERSION}" STREQUAL "${LLVM_PACKAGE_VERSION}")
  message(FATAL_ERROR
    "ZOM-CMAKE-LLVM-CONFIG-VERSION: 'llvm-config --version' "
    "'${ZOM_LLVM_CONFIG_VERSION}' disagrees with LLVM_PACKAGE_VERSION "
    "'${LLVM_PACKAGE_VERSION}'.")
endif()

# ---------------------------------------------------------------------------
# 7. Require every component to map to a real imported LLVM target.
# ---------------------------------------------------------------------------
set(ZOM_LLVM_RESOLVED_COMPONENT_LIBS "")
foreach(component IN LISTS ZOM_LLVM_REQUIRED_COMPONENTS)
  llvm_map_components_to_libnames(ZOM_LLVM_COMPONENT_LIBS "${component}")
  if("${ZOM_LLVM_COMPONENT_LIBS}" STREQUAL "")
    message(FATAL_ERROR
      "ZOM-CMAKE-LLVM-COMPONENT: required component '${component}' maps to no "
      "library in this LLVM install.")
  endif()
  foreach(lib IN LISTS ZOM_LLVM_COMPONENT_LIBS)
    if(NOT TARGET ${lib})
      message(FATAL_ERROR
        "ZOM-CMAKE-LLVM-COMPONENT: required component '${component}' maps to "
        "'${lib}', which is not an available LLVM target in this install.")
    endif()
  endforeach()
  list(APPEND ZOM_LLVM_RESOLVED_COMPONENT_LIBS ${ZOM_LLVM_COMPONENT_LIBS})
endforeach()

# ---------------------------------------------------------------------------
# 8. Require both X86 and AArch64 in the installed target inventory.
# ---------------------------------------------------------------------------
foreach(required_target IN LISTS ZOM_LLVM_REQUIRED_TARGETS)
  if(NOT "${required_target}" IN_LIST LLVM_TARGETS_TO_BUILD)
    message(FATAL_ERROR
      "ZOM-CMAKE-LLVM-TARGET: required target '${required_target}' is absent from "
      "the installed inventory '${LLVM_TARGETS_TO_BUILD}'.")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Provenance evidence (positive-fixture record).
# ---------------------------------------------------------------------------
message(STATUS "ZOM-CMAKE-LLVM-GATE: LLVM discovery gate passed")
message(STATUS "  requested LLVM_DIR         = ${ZOM_REQUESTED_LLVM_DIR}")
message(STATUS "  resolved LLVM_DIR          = ${ZOM_RESOLVED_LLVM_DIR}")
message(STATUS "  LLVM_CMAKE_DIR             = ${ZOM_RESOLVED_LLVM_CMAKE_DIR}")
message(STATUS "  LLVM_INSTALL_PREFIX        = ${ZOM_RESOLVED_LLVM_INSTALL_PREFIX}")
message(STATUS "  LLVM_TOOLS_BINARY_DIR      = ${ZOM_RESOLVED_LLVM_TOOLS_BINARY_DIR}")
message(STATUS "  llvm-config                = ${ZOM_LLVM_CONFIG_REAL}")
message(STATUS "  llvm-config --prefix       = ${ZOM_LLVM_CONFIG_PREFIX}")
message(STATUS "  llvm-config --cmakedir     = ${ZOM_LLVM_CONFIG_CMAKEDIR}")
message(STATUS "  llvm-config --version      = ${ZOM_LLVM_CONFIG_VERSION}")
message(STATUS "  LLVM_PACKAGE_VERSION       = ${LLVM_PACKAGE_VERSION}")
message(STATUS "  required components        = ${ZOM_LLVM_REQUIRED_COMPONENTS}")
message(STATUS "  mapped component libraries = ${ZOM_LLVM_RESOLVED_COMPONENT_LIBS}")
message(STATUS "  LLVM_TARGETS_TO_BUILD      = ${LLVM_TARGETS_TO_BUILD}")
