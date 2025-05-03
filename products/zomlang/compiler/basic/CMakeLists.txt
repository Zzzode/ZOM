set(BASIC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/frontend.cc ${CMAKE_CURRENT_SOURCE_DIR}/thread-pool.cc)

add_library(basic STATIC ${BASIC_SRC})
