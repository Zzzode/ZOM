file(GLOB_RECURSE DIAGNOSTICS_SRC
  core/*.cc
  consumer/*.cc
  fact/*.cc
  text/*.cc
  toolchain/*.cc
)

add_library(diagnostics STATIC ${DIAGNOSTICS_SRC})
