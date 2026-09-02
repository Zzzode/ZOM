#!/usr/bin/env python3
"""Provision the locked LLVM toolchain from source.

Reads the repository LLVM source lock (thirdparty/llvm/llvm-lock.json), shallow-
clones the locked tag, configures an X86;AArch64 Release with every optional
dependency disabled, builds, and installs to a prefix (default
$HOME/toolchains/llvm22). It then verifies that the install reports the locked
version, both required targets, and every required component.

The command is idempotent: if a valid install already exists at the prefix
(llvm-config --version matches the lock, both targets present, and every
component resolves) it prints 'already provisioned' and exits 0 without cloning
or building. Use --check-only to run just that verification against an existing
prefix.

The locked source -- not a system package -- is the source of truth. See
thirdparty/llvm/README.md and the RFC 0016 LLVM build and CI contract.
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "thirdparty" / "llvm" / "llvm-lock.json"
DEFAULT_PREFIX = Path.home() / "toolchains" / "llvm22"
DEFAULT_WORK_DIR = Path.home() / "toolchains" / "src"

# Optional dependencies disabled so the install matches the locked, portable
# build (system-libs reduced to -lrt -ldl -lm on the host of record).
OPTIONAL_DEPS_OFF = [
    "LLVM_ENABLE_ZLIB",
    "LLVM_ENABLE_ZSTD",
    "LLVM_ENABLE_LIBXML2",
    "LLVM_ENABLE_Z3_SOLVER",
    "LLVM_ENABLE_LIBEDIT",
    "LLVM_ENABLE_TERMINFO",
]


def load_lock() -> dict:
    with LOCK.open(encoding="utf-8") as handle:
        return json.load(handle)


def llvm_config_path(prefix: Path) -> Path:
    return prefix / "bin" / "llvm-config"


def run_config(llvm_config: Path, *args: str) -> str | None:
    try:
        proc = subprocess.run(
            [str(llvm_config), *args],
            capture_output=True,
            text=True,
        )
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.strip()


def verify_install(prefix: Path, lock: dict) -> tuple[bool, str]:
    """Return (ok, message). ok=True means the prefix satisfies the lock."""
    llvm_config = llvm_config_path(prefix)
    if not llvm_config.exists():
        return False, f"no llvm-config at {llvm_config}"

    version = run_config(llvm_config, "--version")
    if version is None:
        return False, f"'{llvm_config} --version' failed"
    if version != lock["version"]:
        return False, f"version '{version}' does not match locked '{lock['version']}'"

    targets = run_config(llvm_config, "--targets-built")
    if targets is None:
        return False, f"'{llvm_config} --targets-built' failed"
    built = set(targets.split())
    missing_targets = [t for t in lock["targets"] if t not in built]
    if missing_targets:
        return False, f"missing target(s) {missing_targets} (built: '{targets}')"

    # Every required component must resolve to at least one library name.
    libs = run_config(llvm_config, "--libs", *lock["components"])
    if libs is None or libs == "":
        return False, "one or more required components did not resolve via --libs"

    return True, (
        f"version {version}, targets {sorted(built & set(lock['targets']))}, "
        f"{len(lock['components'])} components resolved"
    )


def provision(prefix: Path, work_dir: Path, jobs: int, lock: dict) -> int:
    if shutil.which("git") is None:
        print("ERROR: git not found on PATH", file=sys.stderr)
        return 2
    if shutil.which("cmake") is None:
        print("ERROR: cmake not found on PATH", file=sys.stderr)
        return 2

    tag = lock["tag"]
    src = work_dir / "llvm-project"
    work_dir.mkdir(parents=True, exist_ok=True)

    if src.exists():
        shutil.rmtree(src)

    print(f"Cloning {lock['source']} tag {tag} (shallow) into {src}")
    clone = subprocess.run(
        [
            "git",
            "clone",
            "--depth",
            "1",
            "--branch",
            tag,
            lock["source"],
            str(src),
        ]
    )
    if clone.returncode != 0:
        print(f"ERROR: shallow clone of {tag} failed", file=sys.stderr)
        return 1

    build_dir = src / "build"
    configure = [
        "cmake",
        "-S",
        str(src / "llvm"),
        "-B",
        str(build_dir),
        "-G",
        "Ninja" if shutil.which("ninja") else "Unix Makefiles",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={prefix}",
        f"-DLLVM_TARGETS_TO_BUILD={';'.join(lock['targets'])}",
        "-DLLVM_INCLUDE_TESTS=OFF",
        "-DLLVM_INCLUDE_EXAMPLES=OFF",
        "-DLLVM_INCLUDE_BENCHMARKS=OFF",
    ]
    configure += [f"-D{flag}=OFF" for flag in OPTIONAL_DEPS_OFF]

    print(f"Configuring LLVM Release ({';'.join(lock['targets'])}) with optional deps OFF")
    if subprocess.run(configure).returncode != 0:
        print("ERROR: cmake configure failed", file=sys.stderr)
        return 1

    print(f"Building and installing to {prefix} with {jobs} job(s)")
    build = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "install", "-j", str(jobs)]
    )
    if build.returncode != 0:
        print("ERROR: build/install failed", file=sys.stderr)
        return 1

    ok, message = verify_install(prefix, lock)
    if not ok:
        print(f"ERROR: post-install verification failed: {message}", file=sys.stderr)
        return 1
    print(f"provisioned: {message}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prefix",
        type=Path,
        default=DEFAULT_PREFIX,
        help=f"Install prefix (default: {DEFAULT_PREFIX}).",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=DEFAULT_WORK_DIR,
        help=f"Scratch directory for the source clone and build (default: {DEFAULT_WORK_DIR}).",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=multiprocessing.cpu_count(),
        help="Parallel build jobs (default: host CPU count).",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="Only verify the prefix against the lock; never clone or build.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rebuild even if a valid install already exists at the prefix.",
    )
    args = parser.parse_args()

    lock = load_lock()
    prefix = args.prefix.expanduser()

    ok, message = verify_install(prefix, lock)

    if args.check_only:
        if ok:
            print(f"already provisioned: {prefix} ({message})")
            return 0
        print(f"not provisioned: {prefix} ({message})", file=sys.stderr)
        return 1

    if ok and not args.force:
        print(f"already provisioned: {prefix} ({message})")
        return 0

    if not ok:
        print(f"provisioning required: {prefix} ({message})")

    return provision(prefix, args.work_dir.expanduser(), args.jobs, lock)


if __name__ == "__main__":
    sys.exit(main())
