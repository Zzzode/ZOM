set(OWNERSHIP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/ownership-event-overlay.cc
)

add_library(ownership STATIC ${OWNERSHIP_SRC})
