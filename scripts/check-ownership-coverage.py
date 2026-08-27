#!/usr/bin/env python3
"""Validate RFC 0027 ownership coverage for changed compiler implementations."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "tests/coverage/implementation-series-base.txt"
EXEMPTIONS = ROOT / "tests/coverage/ownership-exemptions.json"
BASELINE = ROOT / "tests/coverage/ownership-coverage-baseline.json"
REPORT = ROOT / "build-coverage/coverage/ownership/report.json"
MINIMUM_LINE_PERCENT = 70.0
BASELINE_SCHEMA = "zom.rfc0027.ownership-coverage-baseline"
FULL_OID = re.compile(r"[0-9a-f]{40}")


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
            "compiler",
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


def load_baseline() -> dict[str, object]:
    value = json.loads(BASELINE.read_text(encoding="utf-8"))
    load_baseline_from_value(value)
    base = BASE.read_text(encoding="ascii").strip()
    if value["baseRevision"] != base:
        raise ValueError("ownership coverage baseline does not match the frozen implementation base")
    return value


def evaluate(
    report: dict[str, object],
    changed: set[str],
    exemptions: dict[str, str],
    baseline: dict[str, object] | None = None,
) -> list[str]:
    errors: list[str] = []
    if report.get("schema") != "zom.rfc0027.ownership-coverage-report":
        return ["invalid ownership coverage report schema"]
    files = report.get("files")
    aggregate = report.get("aggregate")
    if not isinstance(files, dict) or not isinstance(aggregate, dict):
        return ["invalid ownership coverage report payload"]
    uncovered: list[str] = []
    warnings: list[str] = []
    covered_total = 0
    line_total = 0
    per_file_baselines = {}
    if baseline is not None:
        per_file_baselines = baseline.get("perFileBaselines", {})
        if not isinstance(per_file_baselines, dict):
            per_file_baselines = {}
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
        tracked = per_file_baselines.get(path)
        if isinstance(tracked, dict) and isinstance(tracked.get("linePercent"), (int, float)):
            if round(percent, 2) + 1e-9 < tracked["linePercent"]:
                uncovered.append(
                    f"{path}: {percent:.2f}% regressed below baseline {tracked['linePercent']:.2f}%"
                )
            elif percent < MINIMUM_LINE_PERCENT:
                warnings.append(
                    f"{path}: {percent:.2f}% below {MINIMUM_LINE_PERCENT:.0f}% target "
                    f"(baseline {tracked['linePercent']:.2f}%, non-regression enforced)"
                )
        elif percent < MINIMUM_LINE_PERCENT:
            uncovered.append(f"{path}: {percent:.2f}% < {MINIMUM_LINE_PERCENT:.0f}%")
    errors.extend(uncovered)
    for warning in warnings:
        print(f"warning: {warning}")
    if not line_total:
        errors.append("no non-exempt changed compiler implementation has coverage")
    elif 100 * covered_total / line_total < MINIMUM_LINE_PERCENT:
        errors.append(
            f"aggregate changed-source coverage regressed below {MINIMUM_LINE_PERCENT:.0f}%: "
            f"{100 * covered_total / line_total:.2f}%"
        )
    if baseline is not None:
        baseline_percent = baseline["aggregate"]["linePercent"]
        current_percent = aggregate.get("linePercent")
        if not isinstance(current_percent, (int, float)):
            errors.append("ownership coverage report aggregate percent is invalid")
        elif current_percent + 1e-9 < baseline_percent:
            errors.append(
                f"aggregate ownership coverage regressed below baseline: "
                f"{current_percent:.2f}% < {baseline_percent:.2f}%"
            )
    return errors


def self_test() -> int:
    tracked_path = "compiler/ownership/ownership-event-overlay.cc"
    clean_path = "compiler/ownership/ownership-finalizer.cc"
    changed = {tracked_path, clean_path}
    report = {
        "schema": "zom.rfc0027.ownership-coverage-report",
        "aggregate": {"linePercent": 78.75},
        "files": {
            str(ROOT / tracked_path): {"coveredLines": 69, "lineCount": 100},
            str(ROOT / clean_path): {"coveredLines": 100, "lineCount": 100},
        },
    }
    if not evaluate(report, changed, {}):
        print("ownership coverage self-test escaped")
        return 1
    report["files"][str(ROOT / tracked_path)] = {"coveredLines": 70, "lineCount": 100}
    if evaluate(report, changed, {}):
        print("ownership coverage self-test rejected valid threshold")
        return 1
    baseline = {
        "schema": BASELINE_SCHEMA,
        "aggregate": {"coveredLines": 7875, "lineCount": 10000, "linePercent": 78.75},
        "perFileBaselines": {
            tracked_path: {"coveredLines": 67, "lineCount": 100, "linePercent": 67.0},
        },
        "llvmCovExportSha256": "a" * 64,
        "headRevision": "b" * 40,
        "baseRevision": "c" * 40,
    }
    if evaluate(report, changed, {}, baseline):
        print("ownership coverage self-test rejected a non-regressed aggregate")
        return 1
    report["aggregate"]["linePercent"] = 78.74
    if not evaluate(report, changed, {}, baseline):
        print("ownership coverage self-test escaped an aggregate regression")
        return 1
    report["aggregate"]["linePercent"] = 78.75
    report["files"][str(ROOT / tracked_path)] = {"coveredLines": 66, "lineCount": 100}
    if not evaluate(report, changed, {}, baseline):
        print("ownership coverage self-test escaped a per-file baseline regression")
        return 1
    report["files"][str(ROOT / tracked_path)] = {"coveredLines": 68, "lineCount": 100}
    if evaluate(report, changed, {}, baseline):
        print("ownership coverage self-test rejected a non-regressed tracked file")
        return 1
    untracked = "compiler/ownership/ownership-verifier.cc"
    changed.add(untracked)
    report["files"][str(ROOT / untracked)] = {"coveredLines": 50, "lineCount": 100}
    if not evaluate(report, changed, {}, baseline):
        print("ownership coverage self-test escaped an untracked below-floor file")
        return 1
    try:
        load_baseline_from_value(
            {
                "schema": BASELINE_SCHEMA,
                "aggregate": {"coveredLines": 7875, "lineCount": 10000, "linePercent": 78.0},
                "llvmCovExportSha256": "a" * 64,
                "headRevision": "b" * 40,
                "baseRevision": "c" * 40,
            }
        )
    except ValueError:
        pass
    else:
        print("ownership coverage self-test accepted an inconsistent baseline")
        return 1
    try:
        load_baseline_from_value(
            {
                "schema": BASELINE_SCHEMA,
                "aggregate": {"coveredLines": 7875, "lineCount": 10000, "linePercent": 78.75},
                "perFileBaselines": {
                    tracked_path: {"coveredLines": 70, "lineCount": 100, "linePercent": 70.0},
                },
                "llvmCovExportSha256": "a" * 64,
                "headRevision": "b" * 40,
                "baseRevision": "c" * 40,
            }
        )
    except ValueError:
        pass
    else:
        print("ownership coverage self-test accepted a baseline entry at the floor")
        return 1
    print("ownership coverage self-test passed")
    return 0


def load_baseline_from_value(value: dict[str, object]) -> dict[str, object]:
    if value.get("schema") != BASELINE_SCHEMA:
        raise ValueError("invalid ownership coverage baseline schema")
    aggregate = value.get("aggregate")
    if not isinstance(aggregate, dict):
        raise ValueError("ownership coverage baseline must contain an aggregate object")
    covered = aggregate.get("coveredLines")
    count = aggregate.get("lineCount")
    percent = aggregate.get("linePercent")
    if (
        not isinstance(covered, int)
        or not isinstance(count, int)
        or not isinstance(percent, (int, float))
        or covered < 0
        or count <= 0
        or not 0 <= percent <= 100
    ):
        raise ValueError("ownership coverage baseline aggregate is invalid")
    if round(100 * covered / count, 2) != percent:
        raise ValueError("ownership coverage baseline aggregate percent is inconsistent")
    digest = value.get("llvmCovExportSha256")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise ValueError("ownership coverage baseline export digest is invalid")
    for key in ("headRevision", "baseRevision"):
        revision = value.get(key)
        if not isinstance(revision, str) or FULL_OID.fullmatch(revision) is None:
            raise ValueError(f"ownership coverage baseline {key} is invalid")
    per_file = value.get("perFileBaselines", {})
    if not isinstance(per_file, dict):
        raise ValueError("ownership coverage baseline per-file entries must be an object")
    for path, entry in per_file.items():
        if not isinstance(path, str) or not path.startswith("compiler/"):
            raise ValueError(f"ownership coverage baseline path is invalid: {path!r}")
        if not isinstance(entry, dict):
            raise ValueError(f"ownership coverage baseline entry is invalid: {path}")
        file_covered = entry.get("coveredLines")
        file_count = entry.get("lineCount")
        file_percent = entry.get("linePercent")
        if (
            not isinstance(file_covered, int)
            or not isinstance(file_count, int)
            or not isinstance(file_percent, (int, float))
            or file_covered < 0
            or file_count <= 0
            or not 0 <= file_percent < MINIMUM_LINE_PERCENT
        ):
            raise ValueError(f"ownership coverage baseline entry is invalid: {path}")
        if round(100 * file_covered / file_count, 2) != file_percent:
            raise ValueError(f"ownership coverage baseline percent is inconsistent: {path}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    revision = base_revision()
    report = json.loads(REPORT.read_text(encoding="utf-8"))
    baseline = load_baseline()
    errors = evaluate(report, changed_sources(revision), load_exemptions(), baseline)
    if errors:
        print("\n".join(errors))
        return 1
    print("ownership coverage check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
