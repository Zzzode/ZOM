#!/usr/bin/env python3
"""RFC 0016 LLVM discovery-gate configure fixtures.

Drives the real repository gate module (cmake/utils/llvm.cmake) through the
fixture harness (tests/cmake/llvm-discovery-gate) once per
case, each in an isolated build directory. The positive case asserts the
recorded provenance evidence; every negative case asserts the exact stable
repository-owned failure identifier and that the harness never reaches the
post-gate admission marker (i.e. it fails before any compiler target could be
generated).

Negative cases that must survive earlier checks synthesize a self-contained
fake LLVM install (a hand-written LLVMConfig.cmake plus a real llvm-config
shell script), so each identifier is exercised in isolation without a second
real LLVM toolchain.
"""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ADMIT_MARKER = "ZOM-CMAKE-LLVM-GATE-FIXTURE: admitted"

REQUIRED_COMPONENTS = [
    "Core",
    "Support",
    "Target",
    "TargetParser",
    "MC",
    "CodeGen",
    "AsmParser",
    "AsmPrinter",
    "BitWriter",
    "X86",
    "AArch64",
]

# Component -> library names the fake config maps and (unless dropped) imports.
FAKE_COMPONENT_LIBS = {
    "Core": ["LLVMCore"],
    "Support": ["LLVMSupport"],
    "Target": ["LLVMTarget"],
    "TargetParser": ["LLVMTargetParser"],
    "MC": ["LLVMMC"],
    "CodeGen": ["LLVMCodeGen"],
    "AsmParser": ["LLVMAsmParser"],
    "AsmPrinter": ["LLVMAsmPrinter"],
    "BitWriter": ["LLVMBitWriter"],
    "X86": ["LLVMX86CodeGen", "LLVMX86Info"],
    "AArch64": ["LLVMAArch64CodeGen", "LLVMAArch64Info"],
}


def make_fake_install(
    root: Path,
    *,
    package_version: str = "22.1.8",
    config_version: str = "22.1.8",
    cmake_dir_override: str | None = None,
    install_prefix_override: str | None = None,
    tools_binary_dir_override: str | None = None,
    config_prefix_override: str | None = None,
    config_cmakedir_override: str | None = None,
    omit_component: str | None = None,
    targets: str = "X86;AArch64",
    llvm_config_present: bool = True,
) -> Path:
    """Create a synthetic LLVM install; return the LLVMConfig.cmake directory."""
    cmake_dir = root / "lib" / "cmake" / "llvm"
    bin_dir = root / "bin"
    cmake_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)

    llvm_cmake_dir = cmake_dir_override if cmake_dir_override is not None else str(cmake_dir)
    install_prefix = install_prefix_override if install_prefix_override is not None else str(root)
    tools_binary_dir = (
        tools_binary_dir_override if tools_binary_dir_override is not None else str(bin_dir)
    )

    imported = []
    mapped = []
    for component in REQUIRED_COMPONENTS:
        libs = FAKE_COMPONENT_LIBS[component]
        mapped.append(f'  if(component STREQUAL "{component}")')
        mapped.append(f'    list(APPEND ${{out_var}} {";".join(libs)})')
        mapped.append("  endif()")
        if component == omit_component:
            continue
        for lib in libs:
            imported.append(f"if(NOT TARGET {lib})")
            imported.append(f"  add_library({lib} INTERFACE IMPORTED)")
            imported.append("endif()")

    config = f"""# Synthetic LLVMConfig.cmake for the ZOM RFC 0016 gate fixtures.
set(LLVM_PACKAGE_VERSION "{package_version}")
set(LLVM_CMAKE_DIR "{llvm_cmake_dir}")
set(LLVM_INSTALL_PREFIX "{install_prefix}")
set(LLVM_TOOLS_BINARY_DIR "{tools_binary_dir}")
set(LLVM_TARGETS_TO_BUILD "{targets}")

{os.linesep.join(imported)}

macro(llvm_map_components_to_libnames out_var)
  set(${{out_var}} "")
  foreach(component ${{ARGN}})
{os.linesep.join(mapped)}
  endforeach()
endmacro()
"""
    (cmake_dir / "LLVMConfig.cmake").write_text(config)

    if llvm_config_present:
        config_prefix = config_prefix_override if config_prefix_override is not None else install_prefix
        config_cmakedir = (
            config_cmakedir_override if config_cmakedir_override is not None else llvm_cmake_dir
        )
        script = f"""#!/bin/sh
case "$1" in
  --prefix) echo "{config_prefix}" ;;
  --cmakedir) echo "{config_cmakedir}" ;;
  --version) echo "{config_version}" ;;
  *) echo "" ;;
esac
"""
        llvm_config = bin_dir / "llvm-config"
        llvm_config.write_text(script)
        llvm_config.chmod(llvm_config.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)

    return cmake_dir


def run_gate(harness_dir: Path, gate_module: Path, build_dir: Path, llvm_dir: str | None, env=None):
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    command = [
        "cmake",
        "-S",
        str(harness_dir),
        "-B",
        str(build_dir),
        f"-DZOM_GATE_MODULE={gate_module}",
    ]
    if llvm_dir is not None:
        command.append(f"-DLLVM_DIR={llvm_dir}")
    proc = subprocess.run(command, capture_output=True, text=True, env=env)
    return proc.returncode, proc.stdout + proc.stderr


def expect_negative(name: str, identifier: str, rc: int, output: str) -> bool:
    ok = True
    if rc == 0:
        print(f"FAIL [{name}]: configure succeeded but should have failed")
        ok = False
    if identifier not in output:
        print(f"FAIL [{name}]: expected identifier '{identifier}' not in configure output")
        ok = False
    if ADMIT_MARKER in output:
        print(f"FAIL [{name}]: gate reached post-admission marker (target would generate)")
        ok = False
    if ok:
        print(f"PASS [{name}]: {identifier}")
    return ok


def expect_positive(name: str, rc: int, output: str) -> bool:
    ok = True
    if rc != 0:
        print(f"FAIL [{name}]: positive configure failed")
        ok = False
    required_records = [
        "ZOM-CMAKE-LLVM-GATE: LLVM discovery gate passed",
        "requested LLVM_DIR",
        "resolved LLVM_DIR",
        "LLVM_CMAKE_DIR",
        "LLVM_INSTALL_PREFIX",
        "LLVM_TOOLS_BINARY_DIR",
        "llvm-config",
        "llvm-config --prefix",
        "llvm-config --cmakedir",
        "llvm-config --version      = 22.1.8",
        "LLVM_PACKAGE_VERSION       = 22.1.8",
        "mapped component libraries",
        "LLVM_TARGETS_TO_BUILD",
    ]
    for record in required_records:
        if record not in output:
            print(f"FAIL [{name}]: positive record '{record}' missing from configure output")
            ok = False
    if ADMIT_MARKER not in output:
        print(f"FAIL [{name}]: positive configure did not reach admission marker")
        ok = False
    if ok:
        print(f"PASS [{name}]: gate admitted and recorded provenance evidence")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--harness-dir", required=True, type=Path)
    parser.add_argument("--gate-module", required=True, type=Path)
    parser.add_argument(
        "--real-llvm-dir",
        default=None,
        help="Directory containing the provisioned LLVMConfig.cmake for the positive fixture.",
    )
    parser.add_argument("--work-dir", default=None, type=Path)
    args = parser.parse_args()

    harness_dir = args.harness_dir.resolve()
    gate_module = args.gate_module.resolve()

    if shutil.which("cmake") is None:
        print("SKIP: cmake not found on PATH")
        return 0

    work_parent = args.work_dir or Path(tempfile.mkdtemp(prefix="zom-llvm-gate-"))
    work_parent.mkdir(parents=True, exist_ok=True)

    all_ok = True

    def build_dir(name: str) -> Path:
        return work_parent / f"build-{name}"

    def fake_dir(name: str) -> Path:
        d = work_parent / f"fake-{name}"
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        return d

    # Positive fixture (only when a real provisioned LLVM is supplied).
    if args.real_llvm_dir:
        rc, out = run_gate(harness_dir, gate_module, build_dir("positive"), args.real_llvm_dir)
        all_ok &= expect_positive("positive", rc, out)
    else:
        print("SKIP [positive]: no --real-llvm-dir supplied")

    # DIR-REQUIRED: LLVM_DIR unset.
    rc, out = run_gate(harness_dir, gate_module, build_dir("dir-required"), None)
    all_ok &= expect_negative("dir-required", "ZOM-CMAKE-LLVM-DIR-REQUIRED", rc, out)

    # DIR-REQUIRED: LLVM_DIR empty.
    rc, out = run_gate(harness_dir, gate_module, build_dir("dir-empty"), "")
    all_ok &= expect_negative("dir-empty", "ZOM-CMAKE-LLVM-DIR-REQUIRED", rc, out)

    # DIR-INVALID: nonexistent directory.
    rc, out = run_gate(
        harness_dir, gate_module, build_dir("dir-missing"), str(work_parent / "does-not-exist")
    )
    all_ok &= expect_negative("dir-missing", "ZOM-CMAKE-LLVM-DIR-INVALID", rc, out)

    # DIR-INVALID: existing directory without LLVMConfig.cmake.
    empty = fake_dir("no-config")
    rc, out = run_gate(harness_dir, gate_module, build_dir("dir-no-config"), str(empty))
    all_ok &= expect_negative("dir-no-config", "ZOM-CMAKE-LLVM-DIR-INVALID", rc, out)

    # DIR-INVALID (ambient fallback): a bogus LLVM_DIR while a compatible LLVM is
    # deliberately reachable through CMAKE_PREFIX_PATH and the environment must
    # still fail DIR-INVALID before find_package can publish any LLVM variables.
    if args.real_llvm_dir:
        ambient_env = dict(os.environ)
        real_prefix = str(Path(args.real_llvm_dir).resolve().parents[2])
        ambient_env["CMAKE_PREFIX_PATH"] = real_prefix
        ambient_env["LLVM_DIR"] = args.real_llvm_dir
        rc, out = run_gate(
            harness_dir,
            gate_module,
            build_dir("dir-ambient"),
            str(work_parent / "ambient-bogus"),
            env=ambient_env,
        )
        all_ok &= expect_negative("dir-ambient", "ZOM-CMAKE-LLVM-DIR-INVALID", rc, out)
    else:
        print("SKIP [dir-ambient]: no --real-llvm-dir supplied")

    # VERSION: LLVM_PACKAGE_VERSION not exactly 22.1.8 (config agrees, so
    # provenance and CONFIG-VERSION pass; VERSION is the first to fail).
    d = fake_dir("wrong-version")
    cmake_dir = make_fake_install(d, package_version="21.0.0", config_version="21.0.0")
    rc, out = run_gate(harness_dir, gate_module, build_dir("wrong-version"), str(cmake_dir))
    all_ok &= expect_negative("wrong-version", "ZOM-CMAKE-LLVM-VERSION", rc, out)

    # CONFIG-VERSION: package version 22.1.8 but llvm-config disagrees.
    d = fake_dir("config-version")
    cmake_dir = make_fake_install(d, package_version="22.1.8", config_version="22.1.7")
    rc, out = run_gate(harness_dir, gate_module, build_dir("config-version"), str(cmake_dir))
    all_ok &= expect_negative("config-version", "ZOM-CMAKE-LLVM-CONFIG-VERSION", rc, out)

    # COMPONENT: a required component is unavailable.
    d = fake_dir("component")
    cmake_dir = make_fake_install(d, omit_component="AsmPrinter")
    rc, out = run_gate(harness_dir, gate_module, build_dir("component"), str(cmake_dir))
    all_ok &= expect_negative("component", "ZOM-CMAKE-LLVM-COMPONENT", rc, out)

    # TARGET: X86/AArch64 absent from inventory.
    d = fake_dir("target")
    cmake_dir = make_fake_install(d, targets="X86")
    rc, out = run_gate(harness_dir, gate_module, build_dir("target"), str(cmake_dir))
    all_ok &= expect_negative("target", "ZOM-CMAKE-LLVM-TARGET", rc, out)

    # PROVENANCE: mismatched LLVM_CMAKE_DIR.
    d = fake_dir("prov-cmakedir")
    cmake_dir = make_fake_install(d, cmake_dir_override=str(d))
    rc, out = run_gate(harness_dir, gate_module, build_dir("prov-cmakedir"), str(cmake_dir))
    all_ok &= expect_negative("prov-cmakedir", "ZOM-CMAKE-LLVM-PROVENANCE", rc, out)

    # PROVENANCE: false llvm-config --prefix.
    d = fake_dir("prov-prefix")
    cmake_dir = make_fake_install(d, config_prefix_override="/nonexistent/false-prefix")
    rc, out = run_gate(harness_dir, gate_module, build_dir("prov-prefix"), str(cmake_dir))
    all_ok &= expect_negative("prov-prefix", "ZOM-CMAKE-LLVM-PROVENANCE", rc, out)

    # PROVENANCE: false llvm-config --cmakedir.
    d = fake_dir("prov-configcmakedir")
    cmake_dir = make_fake_install(d, config_cmakedir_override="/nonexistent/false-cmakedir")
    rc, out = run_gate(harness_dir, gate_module, build_dir("prov-configcmakedir"), str(cmake_dir))
    all_ok &= expect_negative("prov-configcmakedir", "ZOM-CMAKE-LLVM-PROVENANCE", rc, out)

    # PROVENANCE: missing ${LLVM_TOOLS_BINARY_DIR}/llvm-config.
    d = fake_dir("prov-noconfig")
    cmake_dir = make_fake_install(d, llvm_config_present=False)
    rc, out = run_gate(harness_dir, gate_module, build_dir("prov-noconfig"), str(cmake_dir))
    all_ok &= expect_negative("prov-noconfig", "ZOM-CMAKE-LLVM-PROVENANCE", rc, out)

    # PROVENANCE: mismatched LLVM_TOOLS_BINARY_DIR (points elsewhere).
    d = fake_dir("prov-toolsdir")
    other = fake_dir("prov-toolsdir-other")
    cmake_dir = make_fake_install(d, tools_binary_dir_override=str(other))
    rc, out = run_gate(harness_dir, gate_module, build_dir("prov-toolsdir"), str(cmake_dir))
    all_ok &= expect_negative("prov-toolsdir", "ZOM-CMAKE-LLVM-PROVENANCE", rc, out)

    if args.work_dir is None:
        shutil.rmtree(work_parent, ignore_errors=True)

    if all_ok:
        print("ALL LLVM discovery-gate fixtures passed")
        return 0
    print("LLVM discovery-gate fixtures FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
