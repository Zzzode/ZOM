add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

# RFC 0043 D3b: the invoke-linker test execs the linker driver by a snapshot file
# descriptor (execveat AT_EMPTY_PATH), which cannot run a `#!` script, so the fake
# driver must be a real compiled ELF. Build it as a native helper and pass its
# path to the test. Gated on Linux, where the exec-by-descriptor path exists.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  enable_language(C)
  add_executable(
    zom-fake-linker-fixture
    ${ZOM_ROOT}/tests/fixtures/fake-linker-fixture.c)
  set_target_properties(
    zom-fake-linker-fixture
    PROPERTIES C_STANDARD 11
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)
  zom_disable_coverage_for_target(zom-fake-linker-fixture)
  # The fixture is exec'd as an ordinary child; keep sanitizers off it so its
  # process image is a plain ELF the descriptor-exec path can run cleanly.
  target_compile_options(zom-fake-linker-fixture PRIVATE -fno-sanitize=all)
  target_link_options(zom-fake-linker-fixture PRIVATE -fno-sanitize=all)
  if(TARGET invoke-linker-test)
    add_dependencies(invoke-linker-test zom-fake-linker-fixture)
    target_compile_definitions(
      invoke-linker-test
      PRIVATE
        ZOM_FAKE_LINKER_FIXTURE="$<TARGET_FILE:zom-fake-linker-fixture>")
  endif()
endif()
