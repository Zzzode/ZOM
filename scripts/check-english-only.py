#!/usr/bin/env python3
"""Reject East Asian scripts in text changed since the frozen implementation base."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASE_FILE = Path(
    "tests/coverage/implementation-series-base.txt"
)
BASE_RECORD = re.compile(rb"[0-9a-f]{40}\n")

# The project rule files quote forbidden scripts as counter-examples in the
# English-only and no-legacy-prose rules.  They are the sole tolerated
# exception and must never gain additional East Asian text.
EXEMPT_PATHS = frozenset({Path("AGENTS.md"), Path("CLAUDE.md")})

SCRIPT_RANGES = (
    ("Han", 0x2E80, 0x2FDF),
    ("Han", 0x3400, 0x4DBF),
    ("Han", 0x4E00, 0x9FFF),
    ("Han", 0xF900, 0xFAFF),
    ("Han", 0x20000, 0x2EE5F),
    ("Han", 0x30000, 0x323AF),
    ("Hiragana", 0x3040, 0x309F),
    ("Hiragana", 0x1B001, 0x1B11F),
    ("Hiragana", 0x1B150, 0x1B152),
    ("Katakana", 0x30A0, 0x30FF),
    ("Katakana", 0x31F0, 0x31FF),
    ("Katakana", 0xFF66, 0xFF9D),
    ("Katakana", 0x1B000, 0x1B000),
    ("Katakana", 0x1B120, 0x1B122),
    ("Hangul", 0x1100, 0x11FF),
    ("Hangul", 0x3130, 0x318F),
    ("Hangul", 0xA960, 0xA97F),
    ("Hangul", 0xAC00, 0xD7AF),
    ("Hangul", 0xD7B0, 0xD7FF),
    ("Hangul", 0xFFA0, 0xFFDC),
)


class BaseValidationError(Exception):
    """One rejected frozen-base invariant."""

    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


@dataclass(frozen=True)
class ChangedPath:
    status: str
    path: Path


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    column: int
    script: str
    codepoint: int


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
        raise BaseValidationError(
            "git-failure",
            f"git {' '.join(arguments)} failed: {detail}",
        )
    return completed


def repository_relative_path(root: Path, path: Path) -> Path:
    absolute = path if path.is_absolute() else root / path
    try:
        return absolute.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise BaseValidationError(
            "base-path-outside-repository",
            f"base file is outside the repository: {path}",
        ) from error


def read_base_record(root: Path, base_file: Path) -> tuple[str, Path]:
    relative = repository_relative_path(root, base_file)
    try:
        content = (root / relative).read_bytes()
    except OSError as error:
        raise BaseValidationError(
            "missing-base", f"cannot read frozen base file {relative}: {error}"
        ) from error
    if BASE_RECORD.fullmatch(content) is None:
        raise BaseValidationError(
            "malformed-base",
            f"{relative} must contain exactly forty lowercase hexadecimal bytes and newline",
        )
    return content[:-1].decode("ascii"), relative


def validate_base(root: Path, base_file: Path) -> tuple[str, Path]:
    base, relative = read_base_record(root, base_file)
    if git(root, "cat-file", "-e", f"{base}^{{commit}}", check=False).returncode != 0:
        raise BaseValidationError(
            "missing-base-commit", f"frozen base commit does not exist: {base}"
        )
    if git(root, "merge-base", "--is-ancestor", base, "HEAD", check=False).returncode != 0:
        raise BaseValidationError(
            "non-ancestor-base",
            f"frozen base is not an ancestor of HEAD: {base}",
        )

    additions = git(
        root,
        "log",
        "--diff-filter=A",
        "--format=%H",
        "--reverse",
        "--",
        relative.as_posix(),
    ).stdout.splitlines()
    if len(additions) != 1:
        raise BaseValidationError(
            "base-record-history",
            f"{relative} must have exactly one addition commit",
        )
    recording_commit = additions[0].decode("ascii")
    recorded_content = git(
        root, "show", f"{recording_commit}:{relative.as_posix()}"
    ).stdout
    current_content = (root / relative).read_bytes()
    if recorded_content != current_content:
        raise BaseValidationError(
            "moving-base",
            f"{relative} differs from its immutable recording commit",
        )
    parent_lines = git(
        root, "rev-list", "--parents", "-n", "1", recording_commit
    ).stdout.split()
    if len(parent_lines) != 2 or parent_lines[1].decode("ascii") != base:
        raise BaseValidationError(
            "base-record-parent",
            "frozen base must be the sole parent of its recording commit",
        )
    return base, relative


def changed_paths(root: Path, base: str) -> list[ChangedPath]:
    output = git(
        root,
        "diff",
        "--name-status",
        "-z",
        "--find-renames",
        "--find-copies-harder",
        "--diff-filter=ACMR",
        base,
        "HEAD",
        "--",
    ).stdout
    fields = [field for field in output.split(b"\0") if field]
    result: list[ChangedPath] = []
    index = 0
    while index < len(fields):
        status = fields[index].decode("ascii")
        index += 1
        kind = status[0]
        if kind in {"R", "C"}:
            if index + 1 >= len(fields):
                raise BaseValidationError(
                    "malformed-git-diff", "rename or copy record has missing paths"
                )
            index += 1
            destination = fields[index]
            index += 1
        else:
            if index >= len(fields):
                raise BaseValidationError(
                    "malformed-git-diff", "change record has no path"
                )
            destination = fields[index]
            index += 1
        result.append(
            ChangedPath(kind, Path(destination.decode("utf-8", errors="strict")))
        )
    return sorted(result, key=lambda change: change.path.as_posix())


def script_for(character: str) -> str | None:
    value = ord(character)
    for script, start, end in SCRIPT_RANGES:
        if start <= value <= end:
            return script
    return None


def read_text(path: Path) -> str | None:
    try:
        content = path.read_bytes()
    except OSError:
        return None
    if b"\0" in content:
        return None
    try:
        return content.decode("utf-8")
    except UnicodeDecodeError:
        return None


def scan_text(path: Path, text: str) -> list[Finding]:
    findings: list[Finding] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for column, character in enumerate(line, start=1):
            script = script_for(character)
            if script is not None:
                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=column,
                        script=script,
                        codepoint=ord(character),
                    )
                )
    return findings


def scan_changed_text(root: Path, changes: list[ChangedPath]) -> list[Finding]:
    findings: list[Finding] = []
    for change in changes:
        if change.path in EXEMPT_PATHS:
            continue
        text = read_text(root / change.path)
        if text is not None:
            findings.extend(scan_text(change.path, text))
    return findings


def print_findings(findings: list[Finding]) -> None:
    for finding in findings:
        print(
            f"{finding.path}:{finding.line}:{finding.column}: "
            f"{finding.script} character U+{finding.codepoint:04X}",
            file=sys.stderr,
        )


def run_check(root: Path, base_file: Path) -> int:
    try:
        base, _ = validate_base(root, base_file)
        changes = changed_paths(root, base)
    except BaseValidationError as error:
        print(f"English-only check failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    findings = scan_changed_text(root, changes)
    if findings:
        print_findings(findings)
        print(
            f"English-only check failed with {len(findings)} finding(s)",
            file=sys.stderr,
        )
        return 1
    print(f"English-only check passed ({len(changes)} changed text candidates)")
    return 0


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def commit(root: Path, message: str) -> str:
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)
    return git(root, "rev-parse", "HEAD").stdout.decode("ascii").strip()


def expect_base_failure(
    root: Path, base_file: Path, category: str
) -> bool:
    try:
        validate_base(root, base_file)
    except BaseValidationError as error:
        return error.category == category
    return False


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="zom-english-only-") as directory:
        root = Path(directory)
        git(root, "init", "-q")
        git(root, "config", "user.name", "ZOM Verification")
        git(root, "config", "user.email", "verification@example.invalid")
        write(root / "modified.txt", "stable modified source\n")
        write(root / "renamed.txt", "stable renamed source " * 32 + "\n")
        write(root / "copied.txt", "stable copied source " * 32 + "\n")
        old_base = commit(root, "old accepted state")
        write(root / "accepted.txt", "accepted\n")
        accepted_base = commit(root, "accepted synchronization")

        base_file = Path("coverage/implementation-series-base.txt")
        write(root / base_file, f"{accepted_base}\n")
        recording_commit = commit(root, "record implementation base")
        try:
            observed_base, _ = validate_base(root, base_file)
        except BaseValidationError as error:
            print(f"self-test rejected a valid base: {error}", file=sys.stderr)
            return 1
        if observed_base != accepted_base:
            print("self-test read the wrong valid base", file=sys.stderr)
            return 1

        original_record = (root / base_file).read_bytes()
        (root / base_file).write_bytes(b"A" * 40 + b"\n")
        if not expect_base_failure(root, base_file, "malformed-base"):
            print("self-test failed to reject a malformed base", file=sys.stderr)
            return 1
        write(root / base_file, f"{old_base}\n")
        if not expect_base_failure(root, base_file, "moving-base"):
            print("self-test failed to reject a moving base", file=sys.stderr)
            return 1

        (root / base_file).write_bytes(original_record)
        git(root, "switch", "-q", "-c", "foreign", old_base)
        write(root / "foreign.txt", "foreign\n")
        foreign = commit(root, "foreign history")
        git(root, "switch", "-q", "master")
        write(root / base_file, f"{foreign}\n")
        if not expect_base_failure(root, base_file, "non-ancestor-base"):
            print("self-test failed to reject a non-ancestor base", file=sys.stderr)
            return 1

        git(root, "reset", "--hard", "-q", recording_commit)
        write(root / "added.txt", chr(0x6C49) + "\n")
        write(root / "modified.txt", "stable modified source\n" + chr(0x3042) + "\n")
        (root / "renamed.txt").rename(root / "renamed-new.txt")
        with (root / "renamed-new.txt").open("a", encoding="utf-8") as stream:
            stream.write(chr(0x30A2) + "\n")
        copied = (root / "copied.txt").read_text(encoding="utf-8")
        write(root / "copied-new.txt", copied + chr(0xD55C) + "\n")
        commit(root, "mutate changed text")

        changes = changed_paths(root, accepted_base)
        statuses = {change.status for change in changes}
        if not {"A", "M", "R", "C"}.issubset(statuses):
            print(
                f"self-test did not exercise the complete change matrix: {sorted(statuses)}",
                file=sys.stderr,
            )
            return 1
        findings = scan_changed_text(root, changes)
        scripts = {finding.script for finding in findings}
        if scripts != {"Han", "Hiragana", "Katakana", "Hangul"}:
            print(
                f"self-test did not reject every required script: {sorted(scripts)}",
                file=sys.stderr,
            )
            return 1

    print("English-only self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject East Asian scripts in implementation-series text changes"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--base-file", type=Path, default=DEFAULT_BASE_FILE)
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    return run_check(ROOT, arguments.base_file)


if __name__ == "__main__":
    raise SystemExit(main())
