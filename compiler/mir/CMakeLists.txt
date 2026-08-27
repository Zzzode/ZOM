set(MIR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/built-mir.cc
)

add_library(mir STATIC ${MIR_SRC})
