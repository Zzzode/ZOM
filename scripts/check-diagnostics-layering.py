#!/usr/bin/env python3
# Copyright (c) 2026 Zode.Z. All rights reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Enforce the diagnostics layering rule from .codex/rules/cpp-zc.md.

Analysis libraries return typed failures and must not depend on
`DiagnosticEngine`. A dedicated adapter under `<subsystem>/diagnostics/`
translates those failures into `DiagID` diagnostics at the boundary.

The rule exists because emitting inside a memoized computation fails twice
over: the emitting body does not run on a cache hit, so the diagnostic
silently disappears on reuse, and a cached artifact carrying a diagnostic
cannot be reused at all. Keeping every reference in one reviewable place is
what makes a violation visible.

Run with --check to verify, or --self-test to exercise the classifier.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPILER = ROOT / "compiler"

SYMBOL = re.compile(r"\bDiagnosticEngine\b")

# The diagnostics subsystem owns the type. Everything below is about who may
# reference it from outside.
OWNER_PREFIX = "compiler/diagnostics/"

# `CompilerSession` owns the engine and calls every adapter; it is the boundary
# where typed failures become diagnostics, so it holds a reference by design.
SESSION_OWNER = frozenset(
    {
        "compiler/driver/session/compiler-session.h",
        "compiler/driver/session/compiler-session.cc",
    }
)

# Adapters that have not been moved into their subsystem's diagnostics/ group.
# Shrink this set by moving the file; never grow it. A new adapter goes
# straight into <subsystem>/diagnostics/ and needs no entry here.
#
# The package pair stays put: check-compiler-session-architecture.py exempts
# the `package` path segment from the strict driver surface and assertion
# rules, so moving these into driver/diagnostics/ would force that gate to be
# loosened. This gate already makes the boundary visible without that trade.
UNGROUPED_ADAPTERS = frozenset(
    {
        "compiler/binder/metadata/binding-metadata.h",
        "compiler/binder/metadata/binding-metadata.cc",
        "compiler/driver/package/package-diagnostic.h",
        "compiler/driver/package/package-diagnostic.cc",
    }
)


def is_adapter(rel: str) -> bool:
    """True when the path sits in a subsystem's diagnostics/ group."""
    return "/diagnostics/" in rel


def classify(rel: str) -> str | None:
    """Return a violation reason for `rel`, or None when the reference is allowed."""
    if rel.startswith(OWNER_PREFIX):
        return None
    if rel in SESSION_OWNER:
        return None
    if rel in UNGROUPED_ADAPTERS:
        return None
    if is_adapter(rel):
        return None
    return (
        "references DiagnosticEngine outside an adapter; return a typed failure "
        "and translate it in <subsystem>/diagnostics/"
    )


def sources() -> list[Path]:
    return sorted(
        path
        for pattern in ("*.h", "*.cc")
        for path in COMPILER.rglob(pattern)
        if path.is_file()
    )


def check() -> int:
    errors: list[str] = []
    seen_exempt: set[str] = set()

    for path in sources():
        rel = path.relative_to(ROOT).as_posix()
        if not SYMBOL.search(path.read_text(encoding="utf-8")):
            continue
        if rel in UNGROUPED_ADAPTERS:
            seen_exempt.add(rel)
        reason = classify(rel)
        if reason:
            errors.append(f"  - {rel}: {reason}")

    # A stale exemption is as much drift as a new violation.
    for rel in sorted(UNGROUPED_ADAPTERS - seen_exempt):
        errors.append(
            f"  - {rel}: listed as an ungrouped adapter but no longer references "
            "DiagnosticEngine; drop it from UNGROUPED_ADAPTERS"
        )

    if errors:
        print("Diagnostics layering check failed:")
        print("\n".join(errors))
        return 1

    print(
        "Diagnostics layering check passed "
        f"({len(UNGROUPED_ADAPTERS)} adapter(s) still outside a diagnostics/ group)."
    )
    return 0


def self_test() -> int:
    cases = [
        ("compiler/diagnostics/core/diagnostic-engine.h", None),
        ("compiler/driver/session/compiler-session.cc", None),
        ("compiler/ownership/diagnostics/ownership-diagnostic-adapter.cc", None),
        ("compiler/driver/package/package-diagnostic.cc", None),
        ("compiler/binder/metadata/binding-metadata.cc", None),
        ("compiler/checker/body/body-checker.cc", "violation"),
        ("compiler/binder/graph/parsed-module.cc", "violation"),
    ]
    failures = []
    for rel, expected in cases:
        actual = classify(rel)
        if (actual is None) != (expected is None):
            failures.append(f"  - {rel}: expected {expected!r}, got {actual!r}")
    if failures:
        print("Self-test failed:")
        print("\n".join(failures))
        return 1
    print(f"Self-test passed ({len(cases)} cases).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true")
    group.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return self_test() if args.self_test else check()


if __name__ == "__main__":
    sys.exit(main())
