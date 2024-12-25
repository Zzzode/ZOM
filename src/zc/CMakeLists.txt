set(ZC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

check_include_prefixes(DIRECTORIES ${ZC_DIR} PREFIXES "src/zc")

file(GLOB ZC_ASYNC_SRC ${ZC_DIR}/async/*.cc)
file(GLOB ZC_CORE_SRC ${ZC_DIR}/core/*.cc)
file(GLOB ZC_HTTP_SRC ${ZC_DIR}/http/*.cc)
file(GLOB ZC_PARSE_SRC ${ZC_DIR}/parse/*.cc)
file(GLOB ZC_TLS_SRC ${ZC_DIR}/tls/*.cc)
file(GLOB ZC_ZIP_SRC ${ZC_DIR}/zip/*.cc)
file(GLOB ZC_ZTEST_SRC ${ZC_DIR}/ztest/*.cc)

add_library(async STATIC ${ZC_ASYNC_SRC})
add_library(core STATIC ${ZC_CORE_SRC})
add_library(http STATIC ${ZC_HTTP_SRC})
add_library(parse STATIC ${ZC_PARSE_SRC})
add_library(tls STATIC ${ZC_TLS_SRC})
add_library(zip STATIC ${ZC_ZIP_SRC})
add_library(ztest STATIC ${ZC_ZTEST_SRC})
add_library(
  zc STATIC
  $<TARGET_OBJECTS:async>
  $<TARGET_OBJECTS:core>
  $<TARGET_OBJECTS:http>
  $<TARGET_OBJECTS:parse>
  $<TARGET_OBJECTS:tls>
  $<TARGET_OBJECTS:zip>
  $<TARGET_OBJECTS:ztest>)

set_target_include_directories(${ZOM_ROOT} async core http parse tls zip ztest)
