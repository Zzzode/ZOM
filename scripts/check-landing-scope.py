#!/usr/bin/env python3
"""Prove that one Git worktree or index matches an exact landing allowlist."""

from __future__ import annotations

import argparse
import importlib.util
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from types import ModuleType
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
FULL_OID = re.compile(r"[0-9a-f]{40}")
ALLOWED_STATUSES = frozenset({"A", "C", "D", "M", "R"})


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


class ScopeError(Exception):
    """One rejected landing-scope invariant."""

    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


@dataclass(frozen=True)
class Allowlist:
    raw: bytes
    paths: tuple[str, ...]


@dataclass(frozen=True)
class Fixture:
    root: Path
    start: str
    base_file: Path
    allowlist: Path


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
        raise ScopeError("git-failure", f"git {' '.join(arguments)} failed: {detail}")
    return completed


def repository_path(root: Path, path: Path, category: str) -> Path:
    absolute = path if path.is_absolute() else root / path
    try:
        return absolute.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise ScopeError(category, f"path is outside the repository: {path}") from error


def validate_entry(root: Path, entry: str) -> None:
    candidate = PurePosixPath(entry)
    repository_path(root, Path(entry), "allowlist-path-outside-repository")
    if (
        not entry
        or candidate.is_absolute()
        or "\\" in entry
        or any(part in {"", ".", ".."} for part in candidate.parts)
        or candidate.as_posix() != entry
    ):
        raise ScopeError("non-normalized-allowlist-path", f"path is not normalized: {entry!r}")


def read_allowlist(root: Path, path: Path) -> Allowlist:
    relative = repository_path(root, path, "allowlist-path-outside-repository")
    try:
        raw = (root / relative).read_bytes()
        text = raw.decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ScopeError("invalid-allowlist", f"cannot read UTF-8 allowlist {relative}: {error}")
    if not text or "\r" in text or not text.endswith("\n") or "\0" in text:
        raise ScopeError("invalid-allowlist", f"{relative} must be non-empty newline-terminated UTF-8")
    paths = tuple(text[:-1].split("\n"))
    if any(not entry for entry in paths):
        raise ScopeError("invalid-allowlist", f"{relative} contains an empty entry")
    if len(paths) != len(set(paths)):
        raise ScopeError("duplicate-allowlist-entry", f"{relative} contains duplicate entries")
    for entry in paths:
        validate_entry(root, entry)
    ordered = tuple(sorted(paths, key=lambda value: value.encode("utf-8")))
    if paths != ordered:
        raise ScopeError("unsorted-allowlist", f"{relative} is not newline-sorted")
    for entry in paths:
        ignored = git(root, "check-ignore", "--no-index", "-q", "--", entry, check=False)
        if ignored.returncode == 0:
            raise ScopeError("ignored-allowlist-entry", f"allowlist entry is ignored: {entry}")
        if ignored.returncode != 1:
            raise ScopeError("git-failure", f"git check-ignore failed for {entry}")
    return Allowlist(raw, paths)


def validate_base(root: Path, base_file: Path) -> str:
    try:
        base, _ = BASE_GATE.validate_base(root, base_file)
        return base
    except BASE_GATE.BaseValidationError as error:
        raise ScopeError(error.category, str(error)) from error


def head_oid(root: Path) -> str:
    return git(root, "rev-parse", "--verify", "HEAD^{commit}").stdout.decode("ascii").strip()


def validate_start(root: Path, start_ref: str) -> str:
    resolved = git(
        root, "rev-parse", "--verify", "--end-of-options", f"{start_ref}^{{commit}}", check=False
    )
    if resolved.returncode != 0:
        raise ScopeError("missing-start", f"start ref does not resolve to a commit: {start_ref}")
    oid = resolved.stdout.decode("ascii").strip()
    if FULL_OID.fullmatch(start_ref) is None or oid != start_ref:
        raise ScopeError("moving-start", f"start ref must be one full immutable commit id: {start_ref}")
    head = head_oid(root)
    if head != oid:
        raise ScopeError("start-not-head", f"HEAD {head} does not equal start ref {oid}")
    return oid


def parse_name_status(root: Path, output: bytes) -> list[str]:
    fields = [field for field in output.split(b"\0") if field]
    paths: list[str] = []
    index = 0
    while index < len(fields):
        try:
            status = fields[index].decode("ascii")
        except UnicodeDecodeError as error:
            raise ScopeError("malformed-git-status", "Git emitted a non-ASCII status") from error
        index += 1
        kind = status[:1]
        if kind not in ALLOWED_STATUSES:
            raise ScopeError("unsupported-git-status", f"unsupported Git status: {status}")
        needed = 2 if kind in {"C", "R"} else 1
        if index + needed > len(fields):
            raise ScopeError("malformed-git-status", f"status {status} has missing paths")
        try:
            names = [fields[index + offset].decode("utf-8") for offset in range(needed)]
        except UnicodeDecodeError as error:
            raise ScopeError("non-utf8-git-path", "Git emitted a non-UTF-8 path") from error
        index += needed
        if kind == "R":
            paths.extend(names)
        elif kind == "C":
            paths.append(names[1])
        else:
            paths.append(names[0])
    for path in paths:
        validate_entry(root, path)
    return paths


def diff_paths(root: Path, cached: bool, against_head: bool = True) -> list[str]:
    arguments = ["diff"]
    if cached:
        arguments.append("--cached")
    arguments.extend(["--name-status", "-z", "--find-renames", "--find-copies-harder"])
    if against_head:
        arguments.append("HEAD")
    arguments.append("--")
    return parse_name_status(root, git(root, *arguments).stdout)


def untracked_paths(root: Path) -> list[str]:
    output = git(root, "ls-files", "--others", "--exclude-standard", "-z").stdout
    try:
        paths = [field.decode("utf-8") for field in output.split(b"\0") if field]
    except UnicodeDecodeError as error:
        raise ScopeError("non-utf8-git-path", "Git emitted a non-UTF-8 untracked path") from error
    for path in paths:
        validate_entry(root, path)
    return paths


def compare_scope(paths: list[str], allowlist: Allowlist) -> None:
    if len(paths) != len(set(paths)):
        raise ScopeError("duplicate-observed-path", "Git emitted one affected path more than once")
    ordered = tuple(sorted(paths, key=lambda value: value.encode("utf-8")))
    observed = "".join(f"{path}\n" for path in ordered).encode("utf-8")
    if observed != allowlist.raw:
        missing = sorted(set(allowlist.paths) - set(ordered))
        additional = sorted(set(ordered) - set(allowlist.paths))
        raise ScopeError(
            "scope-mismatch",
            f"landing scope differs: missing={missing}, additional={additional}",
        )


def check_scope(
    root: Path, mode: str, start_ref: str, allowlist_file: Path, base_file: Path
) -> int:
    base = validate_base(root, base_file)
    start = validate_start(root, start_ref)
    ancestor = git(root, "merge-base", "--is-ancestor", base, start, check=False)
    if ancestor.returncode != 0:
        raise ScopeError("base-not-start-ancestor", f"base {base} is not an ancestor of {start}")
    allowlist = read_allowlist(root, allowlist_file)
    if mode == "worktree":
        staged = diff_paths(root, True)
        if staged:
            raise ScopeError("non-empty-index", f"worktree check found staged paths: {staged}")
        paths = diff_paths(root, False) + untracked_paths(root)
    else:
        unstaged = diff_paths(root, False, against_head=False)
        if unstaged:
            raise ScopeError("unstaged-changes", f"index check found unstaged paths: {unstaged}")
        untracked = untracked_paths(root)
        if untracked:
            raise ScopeError("untracked-paths", f"index check found untracked paths: {untracked}")
        paths = diff_paths(root, True)
    compare_scope(paths, allowlist)
    if head_oid(root) != start:
        raise ScopeError("moving-start", "HEAD moved while the landing scope was checked")
    return len(allowlist.paths)


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def commit(root: Path, message: str) -> str:
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)
    return head_oid(root)


def make_fixture(parent: Path, name: str, intervening: bool = False) -> Fixture:
    root = parent / name
    root.mkdir()
    git(root, "init", "-q")
    git(root, "config", "user.name", "ZOM Verification")
    git(root, "config", "user.email", "verification@example.invalid")
    write(root / ".gitignore", "ignored.txt\n")
    write(root / "modified.txt", "before\n")
    write(root / "rename-old.txt", "rename source " * 32 + "\n")
    write(root / "deleted.txt", "delete me\n")
    write(root / "extra.txt", "copy source " * 32 + "\n")
    base = commit(root, "accepted implementation base")
    if intervening:
        write(root / "intervening.txt", "intervening\n")
        commit(root, "intervening commit")
    base_file = Path("coverage/implementation-series-base.txt")
    write(root / base_file, f"{base}\n")
    start = commit(root, "record immutable implementation base")
    return Fixture(root, start, base_file, Path("coverage/landing-files.txt"))


def write_allowlist(fixture: Fixture, paths: list[str]) -> None:
    ordered = sorted(paths, key=lambda value: value.encode("utf-8"))
    write(fixture.root / fixture.allowlist, "".join(f"{path}\n" for path in ordered))


def prepare_landing(fixture: Fixture, staged: bool = False) -> list[str]:
    write(fixture.root / "modified.txt", "after\n")
    (fixture.root / "rename-old.txt").rename(fixture.root / "rename-new.txt")
    (fixture.root / "deleted.txt").unlink()
    write(fixture.root / "added.txt", "added\n")
    write(fixture.root / "copied.txt", (fixture.root / "extra.txt").read_text(encoding="utf-8"))
    paths = [
        fixture.allowlist.as_posix(),
        "added.txt",
        "copied.txt",
        "deleted.txt",
        "modified.txt",
        "rename-new.txt",
        "rename-old.txt",
    ]
    write_allowlist(fixture, paths)
    if staged:
        git(fixture.root, "add", "-A")
    return paths


def expect_failure(label: str, category: str, action: Callable[[], object]) -> None:
    try:
        action()
    except ScopeError as error:
        if error.category == category:
            return
        raise ScopeError(
            "self-test-wrong-failure",
            f"{label} produced {error.category}, expected {category}: {error}",
        ) from error
    raise ScopeError("self-test-missed-failure", f"{label} was not rejected")


def run_fixture_check(fixture: Fixture, mode: str, start: str | None = None) -> int:
    return check_scope(
        fixture.root,
        mode,
        fixture.start if start is None else start,
        fixture.allowlist,
        fixture.base_file,
    )


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="zom-landing-scope-") as directory:
        parent = Path(directory)
        positive = make_fixture(parent, "positive")
        prepare_landing(positive)
        run_fixture_check(positive, "worktree")
        git(positive.root, "add", "-A")
        run_fixture_check(positive, "index")

        malformed = make_fixture(parent, "malformed-base")
        write(malformed.root / malformed.base_file, "not-a-commit\n")
        expect_failure("malformed base", "malformed-base", lambda: run_fixture_check(malformed, "worktree"))

        moving_base = make_fixture(parent, "moving-base")
        write(moving_base.root / moving_base.base_file, f"{moving_base.start}\n")
        expect_failure("moving base", "moving-base", lambda: run_fixture_check(moving_base, "worktree"))

        bad_parent = make_fixture(parent, "base-parent", intervening=True)
        expect_failure(
            "base recording parent",
            "base-record-parent",
            lambda: run_fixture_check(bad_parent, "worktree"),
        )

        nonancestor = make_fixture(parent, "nonancestor")
        tree = git(nonancestor.root, "rev-parse", "HEAD^{tree}").stdout.decode("ascii").strip()
        foreign = git(nonancestor.root, "commit-tree", tree, "-m", "foreign root").stdout.decode("ascii").strip()
        git(nonancestor.root, "reset", "--hard", "-q", foreign)
        expect_failure(
            "non-ancestor base",
            "non-ancestor-base",
            lambda: run_fixture_check(nonancestor, "worktree", foreign),
        )

        starts = make_fixture(parent, "starts")
        branch = git(starts.root, "symbolic-ref", "--short", "HEAD").stdout.decode("utf-8").strip()
        expect_failure("moving start", "moving-start", lambda: run_fixture_check(starts, "worktree", branch))
        expect_failure("missing start", "missing-start", lambda: run_fixture_check(starts, "worktree", "0" * 40))
        base = (starts.root / starts.base_file).read_text(encoding="ascii").strip()
        expect_failure("start not HEAD", "start-not-head", lambda: run_fixture_check(starts, "worktree", base))

        missing = make_fixture(parent, "missing-entry")
        paths = prepare_landing(missing)
        write_allowlist(missing, [path for path in paths if path != "modified.txt"])
        expect_failure("missing allowlist entry", "scope-mismatch", lambda: run_fixture_check(missing, "worktree"))

        additional = make_fixture(parent, "additional-entry")
        paths = prepare_landing(additional)
        write_allowlist(additional, paths + ["not-changed.txt"])
        expect_failure("additional allowlist entry", "scope-mismatch", lambda: run_fixture_check(additional, "worktree"))

        drift = make_fixture(parent, "staged-drift")
        prepare_landing(drift, staged=True)
        write(drift.root / "extra.txt", "staged drift\n")
        git(drift.root, "add", "extra.txt")
        expect_failure("staged-set drift", "scope-mismatch", lambda: run_fixture_check(drift, "index"))

        unstaged = make_fixture(parent, "unstaged")
        prepare_landing(unstaged, staged=True)
        write(unstaged.root / "modified.txt", "unstaged\n")
        expect_failure("unstaged change", "unstaged-changes", lambda: run_fixture_check(unstaged, "index"))

        untracked = make_fixture(parent, "untracked")
        prepare_landing(untracked, staged=True)
        write(untracked.root / "stray.txt", "stray\n")
        expect_failure("untracked path", "untracked-paths", lambda: run_fixture_check(untracked, "index"))

        duplicate = make_fixture(parent, "duplicate")
        paths = prepare_landing(duplicate)
        write(duplicate.root / duplicate.allowlist, "".join(f"{path}\n" for path in sorted(paths + [paths[0]])))
        expect_failure("duplicate entry", "duplicate-allowlist-entry", lambda: run_fixture_check(duplicate, "worktree"))

        escape = make_fixture(parent, "escape")
        paths = prepare_landing(escape)
        write_allowlist(escape, paths + ["../outside"])
        expect_failure(
            "path escape",
            "allowlist-path-outside-repository",
            lambda: run_fixture_check(escape, "worktree"),
        )

        normalized = make_fixture(parent, "normalized")
        paths = prepare_landing(normalized)
        write_allowlist(
            normalized,
            ["./modified.txt" if path == "modified.txt" else path for path in paths],
        )
        expect_failure(
            "non-normalized path",
            "non-normalized-allowlist-path",
            lambda: run_fixture_check(normalized, "worktree"),
        )

        ignored = make_fixture(parent, "ignored")
        paths = prepare_landing(ignored)
        write_allowlist(ignored, paths + ["ignored.txt"])
        expect_failure(
            "ignored entry",
            "ignored-allowlist-entry",
            lambda: run_fixture_check(ignored, "worktree"),
        )
    print("Landing-scope self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check-worktree", action="store_const", const="worktree", dest="mode")
    mode.add_argument("--check-index", action="store_const", const="index", dest="mode")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--start-ref")
    parser.add_argument("--allowlist", type=Path)
    parser.add_argument("--base-file", type=Path)
    arguments = parser.parse_args()
    if arguments.self_test:
        if arguments.start_ref is not None or arguments.allowlist is not None or arguments.base_file is not None:
            parser.error("--self-test does not accept check-mode arguments")
        try:
            return run_self_test()
        except ScopeError as error:
            print(f"Landing-scope self-test failed [{error.category}]: {error}", file=sys.stderr)
            return 1
    if arguments.start_ref is None or arguments.allowlist is None or arguments.base_file is None:
        parser.error("check modes require --start-ref, --allowlist, and --base-file")
    try:
        count = check_scope(
            ROOT, arguments.mode, arguments.start_ref, arguments.allowlist, arguments.base_file
        )
    except ScopeError as error:
        print(f"Landing-scope check failed [{error.category}]: {error}", file=sys.stderr)
        return 1
    print(f"Landing-scope {arguments.mode} check passed ({count} exact paths)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
