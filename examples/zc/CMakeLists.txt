set(ZC_EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(parse-example parse/parse-example.cc)
add_executable(parse-example-non-zc parse/parse-example-non-zc.cc)

target_link_libraries(parse-example zc)
