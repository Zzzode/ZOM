set(QUERY_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/query-database.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/query-types.cc)

add_library(query STATIC ${QUERY_SRC})
target_link_libraries(query PUBLIC zc basic)
target_include_directories(query PUBLIC ${INCLUDE_DIRS})
