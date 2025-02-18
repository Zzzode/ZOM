file(GLOB DIAGNOSTICS_SRC
  diagnostic.cc
  diagnostic-state.cc
  diagnostic-engine.cc
  diagnostic-types.cc
  diagnostic-consumer.cc
  in-flight-diagnostic.cc
  consoling-diagnostic-consumer.cc
)

add_library(diagnostics STATIC ${DIAGNOSTICS_SRC})
