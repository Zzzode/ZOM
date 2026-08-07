#!/usr/bin/env python3
"""Generate the RFC 0027 ownership coverage artifacts from the instrumented test."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build-coverage"
OUTPUT = BUILD / "coverage/ownership"
PROFILES = OUTPUT / "profiles"
EXPORT = OUTPUT / "llvm-cov-export.json"
REPORT = OUTPUT / "report.json"
MARKDOWN = OUTPUT / "report.md"
TEST = BUILD / "products/zomlang/tests/unittests/compiler/ownership/ownership-event-overlay-test"


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def unit_test_binaries() -> list[Path]:
    listing = subprocess.run(
        ["ctest", "--test-dir", str(BUILD), "-L", "unittest", "--show-only=json-v1"],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    payload = json.loads(listing.stdout)
    tests = payload.get("tests")
    if not isinstance(tests, list):
        raise ValueError("ctest unit-test listing has invalid tests payload")
    binaries: set[Path] = {TEST}
    for test in tests:
        if not isinstance(test, dict):
            continue
        command = test.get("command")
        if not isinstance(command, list) or not command or not isinstance(command[0], str):
            continue
        candidate = Path(command[0])
        if candidate.is_file():
            binaries.add(candidate)
    return sorted(binaries)


def line_summary(entry: dict[str, object]) -> tuple[int, int]:
    summary = entry.get("summary", {})
    lines = summary.get("lines", {}) if isinstance(summary, dict) else {}
    if not isinstance(lines, dict):
        return 0, 0
    return int(lines.get("covered", 0)), int(lines.get("count", 0))


def report_from_export(export: dict[str, object]) -> dict[str, object]:
    files: dict[str, dict[str, object]] = {}
    data = export.get("data", [])
    if not isinstance(data, list):
        raise ValueError("llvm-cov export has invalid data")
    for datum in data:
        if not isinstance(datum, dict):
            continue
        for entry in datum.get("files", []):
            if not isinstance(entry, dict):
                continue
            filename = entry.get("filename")
            if not isinstance(filename, str):
                continue
            covered, count = line_summary(entry)
            files[filename] = {"coveredLines": covered, "lineCount": count}
    covered = sum(int(item["coveredLines"]) for item in files.values())
    count = sum(int(item["lineCount"]) for item in files.values())
    return {
        "schema": "zom.rfc0027.ownership-coverage-report",
        "aggregate": {
            "coveredLines": covered,
            "lineCount": count,
            "linePercent": round(100 * covered / count, 2) if count else 0.0,
        },
        "files": dict(sorted(files.items())),
    }


def write_markdown(report: dict[str, object]) -> None:
    aggregate = report["aggregate"]
    assert isinstance(aggregate, dict)
    rows = [
        "# RFC 0027 Ownership Coverage",
        "",
        f"Aggregate line coverage: {aggregate['linePercent']:.2f}% "
        f"({aggregate['coveredLines']}/{aggregate['lineCount']}).",
        "",
        "| File | Covered lines | Total lines | Line coverage |",
        "| --- | ---: | ---: | ---: |",
    ]
    files = report["files"]
    assert isinstance(files, dict)
    for filename, entry in files.items():
        assert isinstance(entry, dict)
        covered = int(entry["coveredLines"])
        count = int(entry["lineCount"])
        percent = 100 * covered / count if count else 0.0
        rows.append(f"| `{filename}` | {covered} | {count} | {percent:.2f}% |")
    MARKDOWN.write_text("\n".join(rows) + "\n", encoding="utf-8")


def main() -> int:
    run(["cmake", "--preset", "coverage"])
    run(["cmake", "--build", "--preset", "coverage"])
    if not TEST.is_file():
        raise RuntimeError(f"missing ownership coverage test executable: {TEST}")
    binaries = unit_test_binaries()

    coverage_directory = BUILD / "coverage"
    for profile in coverage_directory.glob("*.profraw"):
        profile.unlink()
    if PROFILES.exists():
        shutil.rmtree(PROFILES)
    PROFILES.mkdir(parents=True)
    run(["ctest", "--preset", "coverageCollectionTests", "-L", "unittest"])

    raw_profiles = sorted(coverage_directory.glob("*.profraw"))
    if not raw_profiles:
        raise RuntimeError("coverage unit tests did not emit LLVM profiles")
    for profile in raw_profiles:
        shutil.move(str(profile), PROFILES / profile.name)
    raw_profiles = sorted(PROFILES.glob("*.profraw"))
    merged = PROFILES / "merged.profdata"
    run(["llvm-profdata", "merge", "-sparse", *map(str, raw_profiles), "-o", str(merged)])
    with EXPORT.open("w", encoding="utf-8") as handle:
        command = ["llvm-cov", "export", str(TEST), f"-instr-profile={merged}"]
        command.extend(f"-object={binary}" for binary in binaries if binary != TEST)
        subprocess.run(
            command,
            cwd=ROOT,
            stdout=handle,
            check=True,
        )
    export = json.loads(EXPORT.read_text(encoding="utf-8"))
    report = report_from_export(export)
    REPORT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(report)
    print(f"ownership coverage report: {REPORT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
