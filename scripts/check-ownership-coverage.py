#!/usr/bin/env python3
"""Validate RFC 0027 ownership coverage for changed compiler implementations."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "products/zomlang/tests/coverage/implementation-series-base.txt"
EXEMPTIONS = ROOT / "products/zomlang/tests/coverage/ownership-exemptions.json"
REPORT = ROOT / "build-coverage/coverage/ownership/report.json"
MINIMUM_LINE_PERCENT = 70.0


def base_revision() -> str:
    value = BASE.read_text(encoding="ascii")
    if re.fullmatch(r"[0-9a-f]{40}\n", value) is None:
        raise ValueError(f"invalid frozen implementation base: {BASE}")
    revision = value.strip()
    if subprocess.run(["git", "merge-base", "--is-ancestor", revision, "HEAD"], cwd=ROOT).returncode:
        raise ValueError(f"frozen implementation base is not a HEAD ancestor: {revision}")
    return revision


def changed_sources(revision: str) -> set[str]:
    result = subprocess.run(
        [
            "git",
            "diff",
            "--name-only",
            "--diff-filter=AM",
            revision,
            "--",
            "products/zomlang/compiler",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return {
        path
        for path in result.stdout.splitlines()
        if path.endswith(".cc") and not path.endswith("-test.cc")
    }


def load_exemptions() -> dict[str, str]:
    value = json.loads(EXEMPTIONS.read_text(encoding="utf-8"))
    if value.get("schema") != "zom.rfc0027.ownership-coverage-exemptions":
        raise ValueError("invalid ownership coverage exemption schema")
    entries = value.get("entries")
    if not isinstance(entries, list):
        raise ValueError("ownership coverage exemptions must contain an entries array")
    exemptions: dict[str, str] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("ownership coverage exemption entry must be an object")
        path = entry.get("path")
        reason = entry.get("reason")
        if not isinstance(path, str) or not isinstance(reason, str) or not reason.strip():
            raise ValueError("ownership coverage exemption requires path and reason")
        if path in exemptions:
            raise ValueError(f"duplicate ownership coverage exemption: {path}")
        exemptions[path] = reason
    return exemptions


def evaluate(report: dict[str, object], changed: set[str], exemptions: dict[str, str]) -> list[str]:
    errors: list[str] = []
    if report.get("schema") != "zom.rfc0027.ownership-coverage-report":
        return ["invalid ownership coverage report schema"]
    files = report.get("files")
    aggregate = report.get("aggregate")
    if not isinstance(files, dict) or not isinstance(aggregate, dict):
        return ["invalid ownership coverage report payload"]
    uncovered: list[str] = []
    covered_total = 0
    line_total = 0
    for path in sorted(changed):
        if path in exemptions:
            continue
        entry = files.get(str(ROOT / path))
        if not isinstance(entry, dict):
            uncovered.append(f"{path}: no coverage entry")
            continue
        covered = entry.get("coveredLines")
        count = entry.get("lineCount")
        if not isinstance(covered, int) or not isinstance(count, int) or count <= 0:
            uncovered.append(f"{path}: invalid line coverage")
            continue
        percent = 100 * covered / count
        covered_total += covered
        line_total += count
        if percent < MINIMUM_LINE_PERCENT:
            uncovered.append(f"{path}: {percent:.2f}% < {MINIMUM_LINE_PERCENT:.0f}%")
    errors.extend(uncovered)
    if not line_total:
        errors.append("no non-exempt changed compiler implementation has coverage")
    elif 100 * covered_total / line_total < MINIMUM_LINE_PERCENT:
        errors.append(
            f"aggregate changed-source coverage regressed below {MINIMUM_LINE_PERCENT:.0f}%: "
            f"{100 * covered_total / line_total:.2f}%"
        )
    return errors


def self_test() -> int:
    changed = {"products/zomlang/compiler/ownership/ownership-event-overlay.cc"}
    report = {
        "schema": "zom.rfc0027.ownership-coverage-report",
        "aggregate": {},
        "files": {str(ROOT / next(iter(changed))): {"coveredLines": 69, "lineCount": 100}},
    }
    if not evaluate(report, changed, {}):
        print("ownership coverage self-test escaped")
        return 1
    report["files"][str(ROOT / next(iter(changed)))] = {"coveredLines": 70, "lineCount": 100}
    if evaluate(report, changed, {}):
        print("ownership coverage self-test rejected valid threshold")
        return 1
    print("ownership coverage self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    revision = base_revision()
    report = json.loads(REPORT.read_text(encoding="utf-8"))
    errors = evaluate(report, changed_sources(revision), load_exemptions())
    if errors:
        print("\n".join(errors))
        return 1
    print("ownership coverage check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
