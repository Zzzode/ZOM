file(GLOB DIAGNOSTICS_SRC
  diagnostic.cc
  diagnostic-fact.cc
  source-diagnostic-draft-buffer.cc
  diagnostic-materializer.cc
  diagnostic-state.cc
  diagnostic-engine.cc
  diagnostic-types.cc
  diagnostic-text.cc
  toolchain-module-root-argument.cc
  diagnostic-consumer.cc
  in-flight-diagnostic.cc
  consoling-diagnostic-consumer.cc
)

add_library(diagnostics STATIC ${DIAGNOSTICS_SRC})
