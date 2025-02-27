file(GLOB BASIC_SRC frontend.cc task-queue.cc)

add_library(basic STATIC ${BASIC_SRC})
