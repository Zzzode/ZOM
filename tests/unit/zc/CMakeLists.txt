set(ZC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

check_include_prefixes(DIRECTORIES ${ZC_TEST_DIR} PREFIXES "zc")

# 获取所有子目录，除了 zip
file(
  GLOB TEST_SUBDIRS
  RELATIVE ${ZC_TEST_DIR}
  ${ZC_TEST_DIR}/*)
list(REMOVE_ITEM TEST_SUBDIRS "zip")

# 获取常规测试源文件，排除 zip 目录
set(REGULAR_TEST_SOURCES "")
foreach(SUBDIR ${TEST_SUBDIRS})
  file(GLOB_RECURSE SUBDIR_SOURCES "${ZC_TEST_DIR}/${SUBDIR}/*-test.cc")
  list(APPEND REGULAR_TEST_SOURCES ${SUBDIR_SOURCES})
endforeach()

# 如果 ZC_HAS_BROTLI 被定义且为真，则包含 zip 目录下的测试文件
if(DEFINED ZC_HAS_BROTLI AND ZC_HAS_BROTLI)
  file(GLOB_RECURSE ZIP_TEST_SOURCES ${ZC_TEST_DIR}/zip/*-test.cc)
  message(STATUS "Including ZIP tests due to ZC_HAS_BROTLI being set")
else()
  message(STATUS "Skipping ZIP tests as ZC_HAS_BROTLI is not set")
endif()

# 合并所有测试源文件
set(ALL_TEST_SOURCES ${REGULAR_TEST_SOURCES} ${ZIP_TEST_SOURCES})

set(ALL_TESTS)

foreach(TEST_SOURCE ${ALL_TEST_SOURCES})
  get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
  get_filename_component(TEST_DIR ${TEST_SOURCE} DIRECTORY)
  get_filename_component(TEST_DIR_NAME ${TEST_DIR} NAME)

  # 检查是否为平台特定测试
  set(IS_PLATFORM_SPECIFIC FALSE)
  if(TEST_NAME MATCHES "win32")
    set(IS_PLATFORM_SPECIFIC TRUE)
    if(NOT WIN32)
      message(STATUS "Skipping Windows-specific test: ${TEST_NAME}")
      continue()
    endif()
  elseif(TEST_NAME MATCHES "unix")
    set(IS_PLATFORM_SPECIFIC TRUE)
    if(NOT UNIX)
      message(STATUS "Skipping Unix-specific test: ${TEST_NAME}")
      continue()
    endif()
  endif()

  set(UNIQUE_TEST_NAME "${TEST_DIR_NAME}-${TEST_NAME}")

  message(STATUS "Test ${UNIQUE_TEST_NAME} added")
  add_executable(${UNIQUE_TEST_NAME} EXCLUDE_FROM_ALL ${TEST_SOURCE})
  target_link_libraries(${UNIQUE_TEST_NAME} PRIVATE zc ztest)
  target_include_directories(${UNIQUE_TEST_NAME} PRIVATE ${ZOM_ROOT}/tests/unit/zc ${ZOM_ROOT}/libraries)

  target_compile_options(${UNIQUE_TEST_NAME} PRIVATE -Wno-global-constructors)

  add_test(NAME ${UNIQUE_TEST_NAME} COMMAND ${UNIQUE_TEST_NAME})

  list(APPEND ALL_TESTS ${UNIQUE_TEST_NAME})
endforeach()

add_custom_target(buildAllTests DEPENDS ${ALL_TESTS})
