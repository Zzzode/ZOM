set(ZC_EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(parse_example parse/parse_example.cc)
add_executable(parse_example_non_zc parse/parse_example_non_zc.cc)

target_link_libraries(parse_example zc)
