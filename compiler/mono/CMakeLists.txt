set(MONO_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/instance-cloner.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/instance-fixpoint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/instance-set.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/instance-set-builder.cc
)

add_library(mono STATIC ${MONO_SRC})
