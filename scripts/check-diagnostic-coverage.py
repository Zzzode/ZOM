#!/usr/bin/env python3

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTIC_ROOT = ROOT / "compiler/diagnostics"
PRODUCTION_ROOTS = (
    ROOT / "compiler",
    ROOT / "utils",
)
TEST_ROOT = ROOT / "tests"
RESERVATIONS = ROOT / "tests/coverage/diagnostic-reservations.json"
DEFINITION_PATTERN = re.compile(r"DIAG\(\s*(\d+)\s*,\s*([A-Za-z][A-Za-z0-9_]*)")
REFERENCE_PATTERN = re.compile(
    r"(?:DiagID|[A-Za-z][A-Za-z0-9_]*SourceDiagnostic|CheckerErrorId|"
    r"CheckerWarningId|CheckerNoteId)::([A-Za-z][A-Za-z0-9_]*)"
)
CODE_PATTERN = re.compile(r"ZOM(\d{4})")


@dataclass(frozen=True)
class Definition:
    code: int
    path: Path


@dataclass(frozen=True)
class Reservation:
    diagnostic: str
    tracking: Path


def listed_files(root: Path, patterns: tuple[str, ...]):
    suffixes = {pattern.removeprefix("*") for pattern in patterns}
    relative_root = root.relative_to(ROOT)
    result = subprocess.run(
        (
            "git",
            "-C",
            str(ROOT),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            str(relative_root),
        ),
        check=True,
        capture_output=True,
        text=True,
    )
    files = (ROOT / line for line in result.stdout.splitlines())
    return sorted(path for path in files if path.suffix in suffixes and path.is_file())


def source_files(root: Path):
    return listed_files(root, ("*.cc", "*.h", "*.inc"))


def load_definitions() -> dict[str, Definition]:
    definitions: dict[str, Definition] = {}
    codes: dict[int, str] = {}
    for path in sorted(DIAGNOSTIC_ROOT.glob("defs/diagnostics-*.def")):
        for code_text, name in DEFINITION_PATTERN.findall(path.read_text()):
            code = int(code_text)
            if name in definitions:
                raise ValueError(f"duplicate diagnostic name {name}")
            if code in codes:
                raise ValueError(f"duplicate diagnostic code ZOM{code:04d}: {codes[code]} and {name}")
            definitions[name] = Definition(code, path.relative_to(ROOT))
            codes[code] = name
    return definitions


def load_emitted() -> set[str]:
    emitted: set[str] = set()
    for root in PRODUCTION_ROOTS:
        for path in source_files(root):
            if path.parent == DIAGNOSTIC_ROOT:
                continue
            emitted.update(REFERENCE_PATTERN.findall(path.read_text(errors="ignore")))
    return emitted


def load_asserted(definitions: dict[str, Definition]) -> set[str]:
    asserted: set[str] = set()
    asserted_codes: set[int] = set()
    for path in source_files(TEST_ROOT):
        text = path.read_text(errors="ignore")
        asserted.update(REFERENCE_PATTERN.findall(text))
        asserted_codes.update(int(code) for code in CODE_PATTERN.findall(text))
    for path in listed_files(TEST_ROOT, ("*.check", "*.zom", "*.yml", "*.md")):
        text = path.read_text(errors="ignore")
        asserted.update(REFERENCE_PATTERN.findall(text))
        asserted_codes.update(int(code) for code in CODE_PATTERN.findall(text))
    asserted.update(name for name, value in definitions.items() if value.code in asserted_codes)
    return asserted


def load_reservations() -> list[Reservation]:
    data = json.loads(RESERVATIONS.read_text())
    return [
        Reservation(item["diagnostic"], Path(item["tracking"]))
        for item in data.get("reservations", [])
    ]


def analyze(
    definitions: dict[str, Definition],
    emitted: set[str],
    asserted: set[str],
    reservations: list[Reservation],
) -> list[str]:
    errors: list[str] = []
    reserved: dict[str, Reservation] = {}
    for reservation in reservations:
        if reservation.diagnostic in reserved:
            errors.append(f"duplicate reservation for {reservation.diagnostic}")
            continue
        reserved[reservation.diagnostic] = reservation
        if reservation.diagnostic not in definitions:
            errors.append(f"reservation names undefined diagnostic {reservation.diagnostic}")
            continue
        if reservation.diagnostic in emitted:
            errors.append(f"stale reservation for emitted diagnostic {reservation.diagnostic}")
        tracking = ROOT / reservation.tracking
        if not tracking.is_file():
            errors.append(
                f"reservation {reservation.diagnostic} has missing tracking path "
                f"{reservation.tracking}"
            )
            continue
        definition_text = (ROOT / definitions[reservation.diagnostic].path).read_text()
        if f"Tracking: {reservation.tracking}" not in definition_text:
            errors.append(
                f"reservation {reservation.diagnostic} is not documented at its definition site"
            )

    defined_names = set(definitions)
    for name in sorted(emitted - defined_names):
        if name not in {"Name", "diagnosticId", "fromDiagnosticId"}:
            errors.append(f"production references undefined diagnostic {name}")
    for name in sorted(defined_names - emitted - set(reserved)):
        value = definitions[name]
        errors.append(f"ZOM{value.code:04d} {name} is defined but neither emitted nor reserved")
    for name in sorted((emitted & defined_names) - asserted):
        value = definitions[name]
        errors.append(f"ZOM{value.code:04d} {name} is emitted but never asserted by a test")
    return errors


def load_baseline():
    definitions = load_definitions()
    return definitions, load_emitted(), load_asserted(definitions), load_reservations()


def run_check() -> int:
    try:
        baseline = load_baseline()
    except (OSError, ValueError, json.JSONDecodeError, KeyError) as error:
        print(f"diagnostic coverage input error: {error}", file=sys.stderr)
        return 1
    errors = analyze(*baseline)
    if errors:
        print("diagnostic coverage check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    definitions, emitted, _, reservations = baseline
    print(
        "diagnostic coverage check passed "
        f"({len(definitions)} defined, {len(emitted & set(definitions))} emitted, "
        f"{len(reservations)} tracked reservations)."
    )
    return 0


def run_self_test() -> int:
    try:
        definitions, emitted, asserted, reservations = load_baseline()
    except (OSError, ValueError, json.JSONDecodeError, KeyError) as error:
        print(f"diagnostic coverage input error: {error}", file=sys.stderr)
        return 1
    baseline_errors = analyze(definitions, emitted, asserted, reservations)
    if baseline_errors:
        print("diagnostic coverage self-test baseline failed:", file=sys.stderr)
        for error in baseline_errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []

    injected = dict(definitions)
    injected["InjectedDeadDiagnostic"] = Definition(9999, Path("injected.def"))
    if not any("defined but neither emitted nor reserved" in error for error in analyze(
        injected, emitted, asserted, reservations
    )):
        failures.append("dead diagnostic mutation was accepted")

    if not any("undefined diagnostic InjectedEmission" in error for error in analyze(
        definitions, emitted | {"InjectedEmission"}, asserted, reservations
    )):
        failures.append("undefined emission mutation was accepted")

    asserted_removed = set(asserted)
    asserted_target = next(iter(sorted(emitted & set(definitions))))
    asserted_removed.discard(asserted_target)
    if asserted_target not in asserted:
        failures.append("baseline has no asserted emission for mutation")
    elif not any("never asserted by a test" in error for error in analyze(
        definitions, emitted, asserted_removed, reservations
    )):
        failures.append("untested emission mutation was accepted")

    duplicated = list(reservations)
    duplicated.append(reservations[0])
    if not any("duplicate reservation" in error for error in analyze(
        definitions, emitted, asserted, duplicated
    )):
        failures.append("duplicate reservation mutation was accepted")

    if failures:
        print("diagnostic coverage self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("diagnostic coverage negative fixtures passed (4/4).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return run_check() if args.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
