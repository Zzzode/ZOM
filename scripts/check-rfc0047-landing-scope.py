#!/usr/bin/env python3
"""Validate RFC 0047's exact implementation path and status allowlist."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath

import yaml


ROOT = Path(__file__).resolve().parents[1]
ROUTING = ROOT / "docs/rfc/tracking/0047-implementation-routing.yml"
ALLOWLIST = ROOT / "tests/coverage/rfc-0047-diagnostics-landing-files.txt"
ACCEPTED_ROUTING_SHA256 = "d53fdf81ef7cd682d0f49ee0d891b0d5318d151ef67738cc9f97171d67b09276"
ACCEPTED_BASE_OID = "7a970bdde3d6922a321ad94a76d1cac21c5b556c"
FULL_OID_LENGTH = 40
PRE_BASE_TASKS = frozenset({"R47-20A", "R47-20B"})
STATUS_TO_GIT = {"create": "A", "generate": "A", "modify": "M", "delete": "D"}


class ScopeError(Exception):
    """One rejected exact-scope invariant."""


def run_git(root: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", *arguments], cwd=root, check=False, capture_output=True
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ScopeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout


def validate_path(value: str) -> None:
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or "\\" in value
        or any(part in {"", ".", ".."} for part in path.parts)
        or path.as_posix() != value
    ):
        raise ScopeError(f"non-normalized repository path: {value!r}")


def routing_entries(
    routing: Path = ROUTING, expected_sha256: str = ACCEPTED_ROUTING_SHA256
) -> tuple[tuple[str, str], ...]:
    try:
        raw = routing.read_bytes()
        document = yaml.safe_load(raw)
    except (OSError, UnicodeDecodeError, yaml.YAMLError) as error:
        raise ScopeError(f"cannot read routing artifact: {error}") from error
    if hashlib.sha256(raw).hexdigest() != expected_sha256:
        raise ScopeError("routing artifact differs from the accepted RFC 0047 snapshot")
    if not isinstance(document, dict) or not isinstance(document.get("entries"), list):
        raise ScopeError("routing artifact must contain an entries sequence")
    entries: list[tuple[str, str]] = []
    for item in document["entries"]:
        if not isinstance(item, dict):
            raise ScopeError("routing entry must be a mapping")
        task = item.get("task")
        if task in PRE_BASE_TASKS:
            continue
        path = item.get("path")
        status = item.get("status")
        if not isinstance(path, str) or status not in STATUS_TO_GIT:
            raise ScopeError(f"invalid routing entry: {item!r}")
        validate_path(path)
        entries.append((STATUS_TO_GIT[status], path))
    if len(entries) != len({path for _, path in entries}):
        raise ScopeError("routing contains duplicate implementation paths")
    return tuple(sorted(entries, key=lambda item: item[1].encode("utf-8")))


def canonical_lines(entries: tuple[tuple[str, str], ...]) -> bytes:
    return "".join(f"{status}\t{path}\n" for status, path in entries).encode("utf-8")


def validate_allowlist(entries: tuple[tuple[str, str], ...], allowlist: Path = ALLOWLIST) -> None:
    try:
        actual = allowlist.read_bytes()
    except OSError as error:
        raise ScopeError(f"cannot read generated allowlist: {error}") from error
    expected = canonical_lines(entries)
    if actual != expected:
        raise ScopeError("generated landing allowlist differs from accepted routing")


def validate_base(root: Path, base: str, accepted_base: str = ACCEPTED_BASE_OID) -> None:
    if len(base) != FULL_OID_LENGTH or any(ch not in "0123456789abcdef" for ch in base):
        raise ScopeError("--base must be one full lowercase commit id")
    resolved = run_git(root, "rev-parse", "--verify", f"{base}^{{commit}}").decode().strip()
    if resolved != base:
        raise ScopeError("--base must resolve byte-for-byte to the supplied commit id")
    if base != accepted_base:
        raise ScopeError(
            f"--base must equal the accepted RFC 0047 implementation base {accepted_base}"
        )
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", base, "HEAD"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        raise ScopeError(f"base {base} is not an ancestor of HEAD")


def parse_name_status(output: bytes) -> dict[str, str]:
    fields = [field for field in output.split(b"\0") if field]
    observed: dict[str, str] = {}
    index = 0
    while index < len(fields):
        status = fields[index].decode("ascii")
        index += 1
        kind = status[:1]
        if kind not in {"A", "D", "M"}:
            raise ScopeError(f"unsupported Git status {status}; renames and copies are forbidden")
        if index >= len(fields):
            raise ScopeError(f"Git status {status} has no path")
        path = fields[index].decode("utf-8")
        index += 1
        validate_path(path)
        if path in observed:
            raise ScopeError(f"duplicate observed path: {path}")
        observed[path] = kind
    return observed


def diff(root: Path, base: str, *extra: str) -> dict[str, str]:
    return parse_name_status(
        run_git(root, "diff", *extra, "--name-status", "-z", "--no-renames", base, "--")
    )


def untracked(root: Path) -> dict[str, str]:
    paths = [
        value.decode("utf-8")
        for value in run_git(root, "ls-files", "--others", "--exclude-standard", "-z").split(b"\0")
        if value
    ]
    for path in paths:
        validate_path(path)
    return {path: "A" for path in paths}


def reject_submodules(root: Path, paths: set[str]) -> None:
    for path in sorted(paths):
        output = run_git(root, "ls-files", "--stage", "--", path).decode("utf-8")
        if any(line.startswith("160000 ") for line in output.splitlines()):
            raise ScopeError(f"submodule path is forbidden in RFC 0047 scope: {path}")


def merge(left: dict[str, str], right: dict[str, str]) -> dict[str, str]:
    result = dict(left)
    for path, status in right.items():
        if path in result and result[path] != status:
            raise ScopeError(f"conflicting observed statuses for {path}")
        result[path] = status
    return result


def require_exact(label: str, observed: dict[str, str], expected: dict[str, str]) -> None:
    if observed == expected:
        return
    missing = sorted(set(expected) - set(observed))
    additional = sorted(set(observed) - set(expected))
    wrong = sorted(
        f"{path}: expected {expected[path]}, observed {observed[path]}"
        for path in set(expected) & set(observed)
        if expected[path] != observed[path]
    )
    raise ScopeError(
        f"{label} scope mismatch: missing={missing}, additional={additional}, wrong={wrong}"
    )


def check(
    root: Path,
    base: str,
    routing: Path = ROUTING,
    allowlist: Path = ALLOWLIST,
    expected_routing_sha256: str = ACCEPTED_ROUTING_SHA256,
    accepted_base: str = ACCEPTED_BASE_OID,
) -> int:
    validate_base(root, base, accepted_base)
    entries = routing_entries(routing, expected_routing_sha256)
    validate_allowlist(entries, allowlist)
    expected = {path: status for status, path in entries}
    head = parse_name_status(run_git(root, "diff", "--name-status", "-z", "--no-renames", base, "HEAD", "--"))
    index = parse_name_status(run_git(root, "diff", "--cached", "--name-status", "-z", "--no-renames", base, "--"))
    worktree = merge(
        parse_name_status(run_git(root, "diff", "--name-status", "-z", "--no-renames", base, "--")),
        untracked(root),
    )
    require_exact("base-to-HEAD", head, expected)
    require_exact("base-to-index", index, expected)
    require_exact("base-to-worktree", worktree, expected)
    reject_submodules(root, set(expected))
    return len(expected)


def git_ok(root: Path, *arguments: str) -> None:
    subprocess.run(["git", *arguments], cwd=root, check=True, capture_output=True)


def write(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def expect_error(label: str, action) -> None:
    try:
        action()
    except ScopeError:
        return
    raise ScopeError(f"self-test mutation was accepted: {label}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="zom-rfc0047-scope-") as directory:
        root = Path(directory)
        git_ok(root, "init", "-q")
        git_ok(root, "config", "user.name", "ZOM Verification")
        git_ok(root, "config", "user.email", "verification@example.invalid")
        write(root / "existing.txt", "before\n")
        write(root / "deleted.txt", "delete\n")
        write(root / "guard.txt", "guard\n")
        write(
            root / "routing.yml",
            "entries:\n"
            "  - {path: created.txt, status: create, task: T, owner: verification}\n"
            "  - {path: deleted.txt, status: delete, task: T, owner: verification}\n"
            "  - {path: existing.txt, status: modify, task: T, owner: verification}\n",
        )
        write(root / "allowlist.txt", "A\tcreated.txt\nD\tdeleted.txt\nM\texisting.txt\n")
        git_ok(root, "add", "-A")
        git_ok(root, "commit", "-q", "-m", "base")
        base = run_git(root, "rev-parse", "HEAD").decode().strip()
        write(root / "existing.txt", "after\n")
        (root / "deleted.txt").unlink()
        write(root / "created.txt", "created\n")
        expected = {"created.txt": "A", "deleted.txt": "D", "existing.txt": "M"}
        observed = merge(
            parse_name_status(run_git(root, "diff", "--name-status", "-z", "--no-renames", base, "--")),
            untracked(root),
        )
        require_exact("positive", observed, expected)
        expect_error("missing path", lambda: require_exact("missing", observed, {"existing.txt": "M"}))
        expect_error("wrong status", lambda: require_exact("status", observed, {**expected, "existing.txt": "A"}))
        expect_error("moving base", lambda: validate_base(root, "HEAD"))
        expect_error("short base", lambda: validate_base(root, base[:12]))
        routing = root / "routing.yml"
        allowlist = root / "allowlist.txt"
        routing_digest = hashlib.sha256(routing.read_bytes()).hexdigest()
        git_ok(root, "add", "created.txt", "deleted.txt", "existing.txt")
        git_ok(root, "commit", "-q", "-m", "implementation")
        assert check(root, base, routing, allowlist, routing_digest, base) == 3
        write(root / "guard.txt", "unstaged\n")
        expect_error(
            "unstaged drift",
            lambda: check(root, base, routing, allowlist, routing_digest, base),
        )
        git_ok(root, "restore", "guard.txt")
        write(root / "stray.txt", "untracked\n")
        expect_error(
            "untracked drift",
            lambda: check(root, base, routing, allowlist, routing_digest, base),
        )
        (root / "stray.txt").unlink()
        write(root / "allowlist.txt", "A\tcreated.txt\nM\tdeleted.txt\nM\texisting.txt\n")
        expect_error(
            "allowlist status drift",
            lambda: check(root, base, routing, allowlist, routing_digest, base),
        )
        write(root / "allowlist.txt", "A\tcreated.txt\nD\tdeleted.txt\nM\texisting.txt\n")
        expect_error(
            "routing hash drift",
            lambda: check(root, base, routing, allowlist, "0" * 64, base),
        )
    print("RFC 0047 landing-scope self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write-allowlist", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--base")
    args = parser.parse_args()
    try:
        if args.self_test:
            if args.base is not None:
                parser.error("--self-test does not accept --base")
            self_test()
            return 0
        if args.write_allowlist:
            if args.base is not None:
                parser.error("--write-allowlist does not accept --base")
            entries = routing_entries()
            ALLOWLIST.parent.mkdir(parents=True, exist_ok=True)
            ALLOWLIST.write_bytes(canonical_lines(entries))
            print(f"Wrote RFC 0047 landing allowlist ({len(entries)} exact paths)")
            return 0
        if args.base is None:
            parser.error("--check requires --base")
        count = check(ROOT, args.base)
        print(f"RFC 0047 landing scope passed ({count} exact paths)")
        return 0
    except ScopeError as error:
        print(f"RFC 0047 landing scope failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
