set(ZC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB BASE_SRC ${ZC_DIR}/base/*.cc)
file(GLOB CONCURRENT_SRC ${ZC_DIR}/concurrent/*.cc)
file(GLOB CONTAINERS_SRC ${ZC_DIR}/containers/*.cc)
file(GLOB MEMORY_SRC ${ZC_DIR}/memory/*.cc)
file(GLOB PARSE_SRC ${ZC_DIR}/parse/*.cc)
file(GLOB STRINGS_SRC ${ZC_DIR}/strings/*.cc)
file(GLOB UTILITY_SRC ${ZC_DIR}/utility/*.cc)
file(GLOB ZTEST_SRC ${ZC_DIR}/ztest/*.cc)

add_library(base STATIC ${BASE_SRC})
add_library(concurrent STATIC ${CONCURRENT_SRC})
add_library(containers STATIC ${CONTAINERS_SRC})
add_library(memory STATIC ${MEMORY_SRC})
add_library(parse STATIC ${PARSE_SRC})
add_library(strings STATIC ${STRINGS_SRC})
add_library(utility STATIC ${UTILITY_SRC})
add_library(ztest STATIC ${ZTEST_SRC})
add_library(
  zc STATIC
  $<TARGET_OBJECTS:base>
  $<TARGET_OBJECTS:concurrent>
  $<TARGET_OBJECTS:containers>
  $<TARGET_OBJECTS:memory>
  $<TARGET_OBJECTS:parse>
  $<TARGET_OBJECTS:strings>
  $<TARGET_OBJECTS:utility>
  $<TARGET_OBJECTS:ztest>)
