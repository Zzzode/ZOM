set(ZC_EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(parse_example parse/parse_example.cc)

target_link_libraries(parse_example zc)
