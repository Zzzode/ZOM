add_ztest_unit_tests_from_directory(
  ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARIES frontend
)

# RFC 0043 D3b/D4: the invoke-linker test execs the linker driver by a snapshot
# file descriptor (execveat AT_EMPTY_PATH), which cannot run a `#!` script, so the
# fake driver must be a real compiled ELF. The plan carries no argument surface, so
# the driver's behavior is selected at compile time (-DZOM_FAKE_LINKER_MODE): build
# seven ELF variants and pass each path to the test. Modes 0-2 cover the link
# outcome (success / partial / no-output); modes 3-6 exercise the D4 output
# structural invariants (symlink / empty / directory / hardlink). Gated on Linux,
# where the exec-by-descriptor path exists.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  enable_language(C)

  function(zom_add_fake_linker_variant TARGET MODE DEFINE_NAME)
    add_executable(${TARGET} ${ZOM_ROOT}/tests/fixtures/fake-linker-fixture.c)
    set_target_properties(
      ${TARGET}
      PROPERTIES C_STANDARD 11
                 C_STANDARD_REQUIRED ON
                 C_EXTENSIONS OFF)
    zom_disable_coverage_for_target(${TARGET})
    # The fixture is exec'd as an ordinary child; keep sanitizers off it so its
    # process image is a plain ELF the descriptor-exec path can run cleanly.
    target_compile_options(${TARGET} PRIVATE -fno-sanitize=all -DZOM_FAKE_LINKER_MODE=${MODE})
    target_link_options(${TARGET} PRIVATE -fno-sanitize=all)
    if(TARGET invoke-linker-test)
      add_dependencies(invoke-linker-test ${TARGET})
      target_compile_definitions(
        invoke-linker-test PRIVATE ${DEFINE_NAME}="$<TARGET_FILE:${TARGET}>")
    endif()
  endfunction()

  zom_add_fake_linker_variant(zom-fake-linker-success 0 ZOM_FAKE_LINKER_SUCCESS)
  zom_add_fake_linker_variant(zom-fake-linker-partial 1 ZOM_FAKE_LINKER_PARTIAL)
  zom_add_fake_linker_variant(zom-fake-linker-no-output 2 ZOM_FAKE_LINKER_NO_OUTPUT)
  zom_add_fake_linker_variant(zom-fake-linker-symlink 3 ZOM_FAKE_LINKER_SYMLINK)
  zom_add_fake_linker_variant(zom-fake-linker-empty 4 ZOM_FAKE_LINKER_EMPTY)
  zom_add_fake_linker_variant(zom-fake-linker-directory 5 ZOM_FAKE_LINKER_DIRECTORY)
  zom_add_fake_linker_variant(zom-fake-linker-hardlink 6 ZOM_FAKE_LINKER_HARDLINK)
endif()
