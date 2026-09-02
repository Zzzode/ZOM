#!/usr/bin/env python3
"""Reject whitespace errors and conflict markers in the implementation-series diff."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import ModuleType


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASE_FILE = Path(
    "tests/coverage/implementation-series-base.txt"
)
CONFLICT_MARKER = re.compile(r"^(?:<{7}|={7}|>{7}|\|{7})(?:\s|$)")

# Trees excluded from the hygiene scan: vendored upstream third-party source
# (whose whitespace we do not own and must not modify, matching check-format.py's
# exclusions) and agent runtime session memory. These are git pathspecs appended
# after the `--` separator.
EXCLUDED_PATHSPECS = (
    ":(exclude)thirdparty/**",
    ":(exclude).codex/**",
)


def load_base_gate() -> ModuleType:
    path = ROOT / "scripts/check-english-only.py"
    spec = importlib.util.spec_from_file_location("zom_check_english_only", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load frozen-base gate from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


BASE_GATE = load_base_gate()


class HygieneError(Exception):
    """One rejected diff-hygiene invariant."""

    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


def git(
    root: Path, *arguments: str, check: bool = True
) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        ["git", *arguments],
        cwd=root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise HygieneError("git-failure", f"git {' '.join(arguments)} failed: {detail}")
    return completed


def changed_paths(root: Path, base: str) -> list[Path]:
    output = git(
        root,
        "diff",
        "--name-only",
        "--diff-filter=ACMR",
        base,
        "HEAD",
        "--",
        *EXCLUDED_PATHSPECS,
    ).stdout
    return [Path(field.decode("utf-8")) for field in output.splitlines() if field]


def check_whitespace(root: Path, base: str) -> None:
    completed = git(
        root, "diff", "--check", base, "HEAD", "--", *EXCLUDED_PATHSPECS, check=False
    )
    if completed.returncode != 0:
        detail = completed.stdout.decode("utf-8", errors="replace").strip()
        raise HygieneError(
            "whitespace-error",
            f"implementation-series diff contains whitespace errors:\n{detail}",
        )


def scan_conflict_markers(root: Path, paths: list[Path]) -> list[str]:
    findings: list[str] = []
    for path in paths:
        absolute = root / path
        try:
            content = absolute.read_bytes()
        except OSError:
            continue
        if b"\0" in content:
            continue
        try:
            text = content.decode("utf-8")
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(text.splitlines(), start=1):
            if CONFLICT_MARKER.match(line):
                findings.append(f"{path}:{line_number}: conflict marker {line[:7]!r}")
    return findings


def run_check(root: Path, base_file: Path) -> int:
    try:
        base, _ = BASE_GATE.validate_base(root, base_file)
        check_whitespace(root, base)
        findings = scan_conflict_markers(root, changed_paths(root, base))
    except BASE_GATE.BaseValidationError as error:
        print(f"Diff-hygiene check failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    except HygieneError as error:
        print(f"Diff-hygiene check failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    if findings:
        for finding in findings:
            print(finding, file=sys.stderr)
        print(
            f"Diff-hygiene check failed with {len(findings)} conflict-marker finding(s)",
            file=sys.stderr,
        )
        return 1
    print("Diff-hygiene check passed")
    return 0


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def commit(root: Path, message: str) -> str:
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)
    return git(root, "rev-parse", "HEAD").stdout.decode("ascii").strip()


def expect_failure(label: str, category: str, action) -> None:
    try:
        action()
    except (HygieneError, BASE_GATE.BaseValidationError) as error:
        if error.category == category:
            return
        raise HygieneError(
            "self-test-wrong-failure",
            f"{label} produced {error.category}, expected {category}: {error}",
        ) from error
    raise HygieneError("self-test-missed-failure", f"{label} was not rejected")


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="zom-diff-hygiene-") as directory:
        root = Path(directory)
        git(root, "init", "-q")
        git(root, "config", "user.name", "ZOM Verification")
        git(root, "config", "user.email", "verification@example.invalid")
        write(root / "stable.txt", "stable source\n")
        base = commit(root, "accepted implementation base")
        base_file = Path("coverage/implementation-series-base.txt")
        write(root / base_file, f"{base}\n")
        commit(root, "record immutable implementation base")

        write(root / "clean.txt", "clean addition\n")
        commit(root, "clean change")
        try:
            run_check(root, base_file)
        except SystemExit as exit:
            if exit.code != 0:
                print("self-test rejected a clean implementation series", file=sys.stderr)
                return 1

        write(root / "trailing.txt", "trailing whitespace \n")
        commit(root, "whitespace regression")
        expect_failure(
            "trailing whitespace",
            "whitespace-error",
            lambda: check_whitespace(root, base),
        )

        write(root / "trailing.txt", "clean again\n")
        commit(root, "fix whitespace")
        write(root / "conflict.txt", "before\n<<<<<<< HEAD\nours\n=======\ntheirs\n>>>>>>> branch\nafter\n")
        commit(root, "conflict marker")
        markers = scan_conflict_markers(root, [Path("conflict.txt")])
        if not markers:
            print("self-test did not detect a conflict marker", file=sys.stderr)
            return 1

        write(root / "conflict.txt", "resolved\n")
        commit(root, "resolve conflict")
        write(root / base_file, "not-a-commit\n")
        expect_failure(
            "malformed base",
            "malformed-base",
            lambda: BASE_GATE.validate_base(root, base_file),
        )

    print("Diff-hygiene self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject whitespace errors and conflict markers in the implementation series"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--base-file", type=Path, default=DEFAULT_BASE_FILE)
    arguments = parser.parse_args()
    if arguments.self_test:
        try:
            return run_self_test()
        except HygieneError as error:
            print(f"Diff-hygiene self-test failed [{error.category}]: {error}", file=sys.stderr)
            return 1
    return run_check(ROOT, arguments.base_file)


if __name__ == "__main__":
    raise SystemExit(main())
