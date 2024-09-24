set(ZC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB BASE_SRC ${ZC_DIR}/base/*.cc)
# file(GLOB UTILITY_SRC ${ZC_DIR}/utility/*.cc)
file(GLOB MEMORY_SRC ${ZC_DIR}/memory/*.cc)

add_library(base STATIC ${BASE_SRC})
# add_library(utility STATIC ${UTILITY_SRC})
add_library(memory STATIC ${MEMORY_SRC})

add_library(zc STATIC $<TARGET_OBJECTS:base> $<TARGET_OBJECTS:memory>)
