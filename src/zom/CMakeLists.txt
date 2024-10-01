add_subdirectory(diagnostics)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(source)
add_subdirectory(typecheck)
add_subdirectory(zis)

file(GLOB COMPILER_SRC pipeline.cc)

add_library(compiler STATIC ${COMPILER_SRC})
target_link_libraries(compiler PUBLIC lexer parser source typecheck)
