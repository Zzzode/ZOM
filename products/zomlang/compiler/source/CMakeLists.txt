set(SOURCE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/core-distribution.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/core-source-admission.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/core-source-catalog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/location.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/manager.cc)

add_library(source STATIC ${SOURCE_SRC})
