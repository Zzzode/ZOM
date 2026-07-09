set(RUNTIME_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/panic.cc)

add_library(runtime STATIC ${RUNTIME_SRC})
target_link_libraries(runtime PUBLIC zc)
set_target_include_directories("${INCLUDE_DIRS}" runtime)
