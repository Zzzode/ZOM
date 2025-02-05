file(GLOB DIAGNOSTICS_SRC diagnostic.cc diagnostic-state.cc)

add_library(diagnostics STATIC ${DIAGNOSTICS_SRC})
