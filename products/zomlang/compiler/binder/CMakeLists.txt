set(BINDER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/binder.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/utilities.cc)

add_library(binder STATIC ${BINDER_SRC})
