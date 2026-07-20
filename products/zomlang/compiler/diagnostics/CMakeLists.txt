file(GLOB DIAGNOSTICS_SRC
  diagnostic.cc
  diagnostic-fact.cc
  diagnostic-fact-buffer.cc
  diagnostic-materializer.cc
  diagnostic-state.cc
  diagnostic-engine.cc
  diagnostic-types.cc
  diagnostic-text.cc
  diagnostic-consumer.cc
  in-flight-diagnostic.cc
  consoling-diagnostic-consumer.cc
)

add_library(diagnostics STATIC ${DIAGNOSTICS_SRC})
