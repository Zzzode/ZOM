#!/usr/bin/env python3
"""Enforce source-backed core distribution and installed-consumer wiring."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = Path("products/zomcore")
TESTS = Path("products/zomlang/tests")
REQUIRED = (
    CORE / "src/core.zom",
    CORE / "src/core/marker.zom",
    CORE / "src/core/prelude.zom",
    CORE / "CMakeLists.txt",
    TESTS / "cmake/verify-core-source-install.cmake",
    TESTS / "cmake/verify-core-library-install-consumer.cmake",
    TESTS / "integration/core-library/installed-consumer/Zom.toml",
    TESTS / "integration/core-library/installed-consumer/src/main.zom",
)


def files() -> dict[Path, str]:
    return {path: (ROOT / path).read_text(encoding="utf-8") for path in REQUIRED if (ROOT / path).is_file()}


def check(values: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path in REQUIRED:
        if path not in values:
            errors.append(f"missing core-library artifact: {path}")
    core_cmake = values.get(CORE / "CMakeLists.txt", "")
    for source in ("src/core.zom", "src/core/marker.zom", "src/core/prelude.zom"):
        if source not in core_cmake:
            errors.append(f"{CORE / 'CMakeLists.txt'}: missing core source: {source}")
    test_cmake = (ROOT / TESTS / "CMakeLists.txt").read_text(encoding="utf-8")
    for marker in ("core-source-install-layout", "core-library-install-consumer"):
        if marker not in test_cmake:
            errors.append(f"{TESTS / 'CMakeLists.txt'}: missing installation gate: {marker}")
    session = (ROOT / "products/zomlang/compiler/driver/compiler-session.cc").read_text(encoding="utf-8")
    if "installVerifiedCoreDistribution" not in session:
        errors.append("CompilerSession: missing installed core-distribution admission")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    values = files()
    if args.self_test:
        mutated = dict(values)
        path = CORE / "CMakeLists.txt"
        mutated[path] = mutated.get(path, "").replace("src/core/prelude.zom", "src/core/missing.zom")
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        print("core-library architecture self-test passed")
        return 0
    errors = check(values)
    if errors:
        print("\n".join(errors))
        return 1
    print("core-library architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
