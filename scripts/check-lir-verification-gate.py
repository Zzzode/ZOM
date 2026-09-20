#!/usr/bin/env python3
"""Gate that the production binary path actually runs both RFC 0053 stages.

The LIR structural verifier and the MIR-to-LIR translation validator must run
on every `zomc compile --emit=binary` path before LLVM translation. A bypass
(`if (false)`, a deleted call, or a return ahead of the check) is a silent
regression that no behavioral test detects if the happy path still emits an
object. This gate pins the two production call sites and their fail-closed
projection to the internal incident rail.

It deliberately checks source markers rather than behavior: the behavioral
half (forced finding -> internal incident, non-zero exit, no output object) is
covered by the `object-emission-cli` incident case. Together they make the
stages non-removable without a CI failure.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZOMC = ROOT / "utils" / "zomc" / "zomc.cc"

# Each requirement is a literal that must appear in the production emitter.
REQUIRED_MARKERS = (
    # The structural stage runs ...
    "zom.lir-verification.structural-stage",
    "lir::LirStructuralVerifier::verify(lirModule)",
    # ... and its finding reaches the internal incident rail.
    "lir::projectLirVerificationIncident",
    # The translation stage runs ...
    "zom.lir-verification.translation-stage",
    "lir::TranslationValidator::validate(presentedMir.asPtr(), lirModule, types)",
)


def missing_in(text: str) -> list[str]:
    return [marker for marker in REQUIRED_MARKERS if marker not in text]


def run_check() -> list[str]:
    if not ZOMC.is_file():
        return [f"missing production emitter source: {ZOMC}"]
    return missing_in(ZOMC.read_text(encoding="utf-8"))


def run_self_test() -> list[str]:
    # A text that has every marker except one must be detected, and the real
    # source must pass.
    all_markers = "\n".join(REQUIRED_MARKERS)
    dropped = all_markers.replace(REQUIRED_MARKERS[0], "")
    failures = []
    if not missing_in(dropped):
        failures.append("gate failed to detect a missing structural-stage marker")
    if run_check():
        failures.append("gate reported missing markers in the real production source")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.check and not args.self_test:
        parser.error("one of --check or --self-test is required")

    if args.self_test:
        failures = run_self_test()
        if failures:
            for failure in failures:
                print(failure, file=sys.stderr)
            return 1
        print("self-test passed")
        return 0

    failures = run_check()
    if failures:
        for failure in failures:
            print(f"LIR verification gate failed:\n  - {failure}", file=sys.stderr)
        return 1
    print("LIR verification production-stage gate passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
