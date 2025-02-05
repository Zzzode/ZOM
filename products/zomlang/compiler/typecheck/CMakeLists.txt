file(GLOB TYPECHECK_SRC "*.cc")

add_library(typecheck STATIC "${TYPECHECK_SRC}")