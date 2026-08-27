
add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

add_test(
  NAME pubgrub-scenarios-oracle
  COMMAND
    ${Python3_EXECUTABLE}
    "${ZOM_ROOT}/zomlang/tests/tools/check-pubgrub-scenarios.py"
    --corpus
    "${ZOM_ROOT}/zomlang/tests/fixtures/pubgrub-scenarios.json"
    --replay-source
    "${CMAKE_CURRENT_SOURCE_DIR}/package/package-resolver-test.cc")
set_tests_properties(pubgrub-scenarios-oracle
                     PROPERTIES LABELS "unittest;package" TIMEOUT 30)

set_tests_properties(session-compiler-session-package-test
                     PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)

set_tests_properties(session-compiler-session-test
                     PROPERTIES TIMEOUT 300)

set_tests_properties(query-binding-active-definition-authority-session-test
                     PROPERTIES TIMEOUT 180)

set_tests_properties(interface-borrow-evidence-test
                     PROPERTIES TIMEOUT 180)

target_compile_definitions(
  package-lockfile-test
  PRIVATE
    ZOM_LOCKFILE_GOLDEN="${ZOM_ROOT}/zomlang/tests/fixtures/Zom.lock.golden")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
  enable_language(C)
  add_executable(
    zom-linux-sandbox-runtime-fixture
    ${ZOM_ROOT}/zomlang/tests/fixtures/linux-sandbox-runtime-fixture.c)
  set_target_properties(
    zom-linux-sandbox-runtime-fixture
    PROPERTIES C_STANDARD 11
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)
  zom_disable_coverage_for_target(zom-linux-sandbox-runtime-fixture)
  target_compile_options(
    zom-linux-sandbox-runtime-fixture
    PRIVATE -ffreestanding -fPIE -fno-asynchronous-unwind-tables
            -fno-stack-protector -fno-sanitize=all)
  target_link_options(
    zom-linux-sandbox-runtime-fixture
    PRIVATE -nostdlib -static-pie -Wl,--build-id=none -Wl,-e,_start
            -Wl,-z,noexecstack -fno-sanitize=all)
  add_dependencies(package-linux-native-sandbox-test zom-linux-sandbox-runtime-fixture)
  target_compile_definitions(
    package-linux-native-sandbox-test
    PRIVATE
      ZOM_LINUX_SANDBOX_RUNTIME_FIXTURE="$<TARGET_FILE:zom-linux-sandbox-runtime-fixture>")

  if(ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS)
    add_executable(
      package-linux-native-sandbox-integration-test
      ${CMAKE_CURRENT_SOURCE_DIR}/package/linux-native-sandbox-integration.cc)
    target_link_libraries(
      package-linux-native-sandbox-integration-test PRIVATE ztest frontend)
    target_include_directories(
      package-linux-native-sandbox-integration-test
      PRIVATE ${ZOM_ROOT}/libraries ${ZOM_ROOT})
    target_compile_options(
      package-linux-native-sandbox-integration-test PRIVATE -Wno-global-constructors)
    target_compile_definitions(
      package-linux-native-sandbox-integration-test
      PRIVATE
        ZOM_LINUX_SANDBOX_RUNTIME_FIXTURE="$<TARGET_FILE:zom-linux-sandbox-runtime-fixture>")
    add_dependencies(
      package-linux-native-sandbox-integration-test zom-linux-sandbox-runtime-fixture)
    add_test(
      NAME linux-native-sandbox-integration
      COMMAND $<TARGET_FILE:package-linux-native-sandbox-integration-test>)
    set_tests_properties(
      linux-native-sandbox-integration
      PROPERTIES LABELS "integration;package;linux;privileged;release"
                 TIMEOUT 180)
  endif()
endif()
