set(ZC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB BASE_SRC ${ZC_DIR}/base/*.cc)
file(GLOB CONTAINERS_SRC ${ZC_DIR}/containers/*.cc)
file(GLOB MEMORY_SRC ${ZC_DIR}/memory/*.cc)
file(GLOB STRINGS_SRC ${ZC_DIR}/strings/*.cc)

add_library(base STATIC ${BASE_SRC})
add_library(containers STATIC ${CONTAINERS_SRC})
add_library(memory STATIC ${MEMORY_SRC})
add_library(strings STATIC ${STRINGS_SRC})


add_library(zc STATIC $<TARGET_OBJECTS:base> $<TARGET_OBJECTS:containers> $<TARGET_OBJECTS:memory> $<TARGET_OBJECTS:strings>)
