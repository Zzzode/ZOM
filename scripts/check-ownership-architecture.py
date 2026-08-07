#!/usr/bin/env python3
"""Enforce retained ownership-overlay leases and production publication."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OVERLAY = Path("products/zomlang/compiler/ownership/ownership-event-overlay.cc")
SESSION = Path("products/zomlang/compiler/driver/compiler-session.cc")
TEST = Path("products/zomlang/tests/unittests/compiler/ownership/ownership-event-overlay-test.cc")
TEST_CMAKE = Path("products/zomlang/tests/conformance/CMakeLists.txt")
REQUIRED = (OVERLAY, SESSION, TEST, TEST_CMAKE)


def files() -> dict[Path, str]:
    return {
        path: (ROOT / path).read_text(encoding="utf-8")
        for path in REQUIRED
        if (ROOT / path).is_file()
    }


def check(values: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path in REQUIRED:
        if path not in values:
            errors.append(f"missing ownership architecture artifact: {path}")

    overlay = values.get(OVERLAY, "")
    for marker in (
        "driver::module_graph_query::CheckerBoundModuleView boundModule;",
        "builtMir.retainBoundModule()",
        "OwnershipEventOverlayBuilder::build(",
        "OwnershipEventOverlayVerifier::verify(",
    ):
        if marker not in overlay:
            errors.append(f"{OVERLAY}: missing required retained-overlay contract: {marker}")

    session = values.get(SESSION, "")
    for marker in (
        "stagedOwnershipEventOverlays.add(zc::mv(verifiedOwnership).takeVerified());",
        "impl->ownershipEventOverlays = zc::mv(stagedOwnershipEventOverlays);",
        "ownershipEventOverlays.clear();",
        "builtMirModules.clear();",
    ):
        if marker not in session:
            errors.append(f"{SESSION}: missing ownership publication contract: {marker}")
    overlay_release = session.find("ownershipEventOverlays.clear();")
    mir_release = session.find("builtMirModules.clear();")
    if overlay_release != -1 and mir_release != -1 and overlay_release > mir_release:
        errors.append(f"{SESSION}: ownership overlays must release before retained MIR modules")

    test = values.get(TEST, "")
    for marker in (
        "Ownership event overlay verifier rejects a tampered function slot count",
        "Ownership event overlay verifier rejects a tampered slot role",
        "Ownership event overlay verifier rejects a foreign event owner",
        "CompilerSession publishes verified ownership event overlays",
        "Ownership event overlay production revision matches the independent function oracle",
    ):
        if marker not in test:
            errors.append(f"{TEST}: missing ownership mutation or production test: {marker}")

    cmake = values.get(TEST_CMAKE, "")
    for marker in ("ownership-architecture", "ownership-architecture-negative"):
        if marker not in cmake:
            errors.append(f"{TEST_CMAKE}: missing ownership architecture gate: {marker}")
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
        mutated[OVERLAY] = mutated.get(OVERLAY, "").replace(
            "builtMir.retainBoundModule()", "builtMir.detachedBoundModule()", 1
        )
        if not check(mutated):
            print("ownership architecture self-test escaped")
            return 1
        print("ownership architecture self-test passed")
        return 0
    errors = check(values)
    if errors:
        print("\n".join(errors))
        return 1
    print("ownership architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
