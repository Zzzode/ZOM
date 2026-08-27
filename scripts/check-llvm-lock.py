#!/usr/bin/env python3
"""Check the LLVM source lock against the CMake discovery gate.

Verifies that third_party/llvm/llvm-lock.json cannot drift from
cmake/utils/llvm.cmake: the lock's version must equal ZOM_LLVM_REQUIRED_VERSION,
its targets must equal ZOM_LLVM_REQUIRED_TARGETS, and its components must equal
ZOM_LLVM_REQUIRED_COMPONENTS. This composes with (does not duplicate) the
fail-closed configure gate: the gate validates a live install, this validates
the repository lock that dev provisioning and CI build from.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "third_party" / "llvm" / "llvm-lock.json"
GATE = ROOT / "cmake" / "utils" / "llvm.cmake"


def parse_cmake_set(text: str, name: str) -> list[str]:
    """Return the whitespace-separated tokens of a set(<name> ...) block."""
    match = re.search(rf"set\(\s*{re.escape(name)}\s+(.*?)\)", text, re.DOTALL)
    if not match:
        raise SystemExit(f"FAIL: {name} not found in {GATE}")
    body = match.group(1)
    tokens = re.findall(r'"([^"]*)"|(\S+)', body)
    return [quoted or bare for quoted, bare in tokens if (quoted or bare)]


def main() -> int:
    with LOCK.open(encoding="utf-8") as handle:
        lock = json.load(handle)
    gate_text = GATE.read_text(encoding="utf-8")

    ok = True

    required_version = parse_cmake_set(gate_text, "ZOM_LLVM_REQUIRED_VERSION")
    if required_version != [lock["version"]]:
        print(
            f"FAIL: lock version {lock['version']!r} != "
            f"ZOM_LLVM_REQUIRED_VERSION {required_version}"
        )
        ok = False

    required_targets = parse_cmake_set(gate_text, "ZOM_LLVM_REQUIRED_TARGETS")
    if required_targets != lock["targets"]:
        print(
            f"FAIL: lock targets {lock['targets']} != "
            f"ZOM_LLVM_REQUIRED_TARGETS {required_targets}"
        )
        ok = False

    required_components = parse_cmake_set(gate_text, "ZOM_LLVM_REQUIRED_COMPONENTS")
    if required_components != lock["components"]:
        print(
            f"FAIL: lock components {lock['components']} != "
            f"ZOM_LLVM_REQUIRED_COMPONENTS {required_components}"
        )
        ok = False

    if not ok:
        return 1
    print(
        f"PASS: lock version {lock['version']}, targets {lock['targets']}, "
        f"{len(lock['components'])} components match cmake/utils/llvm.cmake"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
