set(TYPE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-type-store.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/semantic-type-key.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/type-substitution.cc
)

add_library(type STATIC ${TYPE_SRC})
