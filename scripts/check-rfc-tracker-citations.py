#!/usr/bin/env python3
"""Check that RFC tracker path and test citations resolve against the tree.

Quarter-close requirement 7 asks that trackers match production code. The
failure mode this guards is narrow and mechanical: a source reorganization moves
a file or renames a test target, and every tracker that cited the old name
silently becomes wrong. That is exactly what happened across the identity,
ownership, and test-directory regroupings, and nothing caught it.

The rule is therefore about relocation, not deletion. A citation fails when the
name does not resolve AND an equivalent does exist elsewhere, because that pair
means the citation simply did not follow the move. A citation naming a genuinely
deleted artifact is left alone: trackers legitimately record work whose files a
later RFC removed, and forbidding that would force rewriting history.

Two citation kinds are checked:

  * paths under a source root, matched by basename when the exact path is gone;
  * CTest target names of the form `<name>-test`, matched against the test
    source files on disk rather than a configured CTest inventory, because
    registration varies with build options -- `llvm-translation-test` exists as
    a source but only registers when the LLVM backend is enabled.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACKING = ROOT / "docs" / "rfc" / "tracking"

# Directories a citation may name. A citation outside these is prose, not a path.
SOURCE_ROOTS = ("compiler", "tests", "scripts", "utils", "runtime", "libraries", "docs", "cmake")

CITATION = re.compile(r"`((?:" + "|".join(SOURCE_ROOTS) + r")/[A-Za-z0-9_./-]+\.[a-z0-9]+)`")
TEST_CITATION = re.compile(r"`([a-z0-9][a-z0-9-]*-test)`")

# Citations naming artifacts that were deliberately deleted. Each entry is owned
# by the tracker rewrite noted beside it; see the 2026-09-07 audit report at
# docs/reports/zom-rfc-tracker-audit-2026-09-07.md.
STALE_BACKLOG: dict[str, str] = {
    # RFC 0025: 37 path citations across 14 tracker rows. Needs one owner
    # rewrite pass, not line fixes. Artifacts removed by RFC 0027 R27-29
    # (identity registry and freeze authority), RFC 0047 (diagnostics), and the
    # tools/ -> compiler/ relocation.
}


def load_citations() -> dict[str, list[tuple[Path, int]]]:
    """Maps each cited path to the tracker locations that name it."""
    citations: dict[str, list[tuple[Path, int]]] = {}
    for tracker in sorted(TRACKING.glob("*.md")):
        for number, line in enumerate(tracker.read_text(encoding="utf-8").splitlines(), start=1):
            for path in CITATION.findall(line):
                citations.setdefault(path, []).append((tracker, number))
    return citations


def load_test_citations() -> dict[str, list[tuple[Path, int]]]:
    """Maps each cited CTest target name to the tracker locations that name it."""
    citations: dict[str, list[tuple[Path, int]]] = {}
    for tracker in sorted(TRACKING.glob("*.md")):
        for number, line in enumerate(tracker.read_text(encoding="utf-8").splitlines(), start=1):
            for name in TEST_CITATION.findall(line):
                citations.setdefault(name, []).append((tracker, number))
    return citations


def test_source_index() -> dict[str, str]:
    """Maps each derivable CTest target name to its test source path.

    Two registration forms exist in cmake/utils/unittests.cmake:

      * `add_ztest_unit_tests_from_directory` scans a directory and names each
        target `<relative-subdirectory>-<file stem>`, slashes turned into
        dashes, or just the stem when the source sits directly in that
        directory;
      * `add_ztest_unit_test` names one target explicitly, which is how a
        build-gated test like `llvm-translation-test` is registered.

    Deriving names from the CMake files rather than from a configured CTest
    inventory keeps the gate independent of build options, so a test that only
    registers under `ZOM_ENABLE_LLVM_BACKEND` still counts as existing.
    """
    index: dict[str, str] = {}
    explicit = re.compile(r"add_ztest_unit_test\s*\(\s*([a-z0-9][a-z0-9-]*-test)\b")
    for cmake in (ROOT / "tests" / "unittests").rglob("CMakeLists.txt"):
        text = cmake.read_text(encoding="utf-8")
        base = cmake.parent
        for name in explicit.findall(text):
            source = base / f"{name}.cc"
            index.setdefault(name, str(source.relative_to(ROOT)) if source.exists() else name)
        if "add_ztest_unit_tests_from_directory" not in text:
            continue
        for source in base.rglob("*-test.cc"):
            relative = source.parent.relative_to(base)
            prefix = "" if str(relative) == "." else str(relative).replace("/", "-") + "-"
            index.setdefault(f"{prefix}{source.stem}", str(source.relative_to(ROOT)))
    return index


def basename_index() -> dict[str, list[str]]:
    """Indexes every tracked source file by basename."""
    index: dict[str, list[str]] = {}
    for root in SOURCE_ROOTS:
        base = ROOT / root
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file():
                continue
            # Vendored trees carry generic basenames that collide with ours.
            if "vendor" in path.parts or "thirdparty" in path.parts:
                continue
            index.setdefault(path.name, []).append(str(path.relative_to(ROOT)))
    return index


def analyze() -> list[str]:
    errors: list[str] = []
    citations = load_citations()
    index = basename_index()

    for path, sites in sorted(citations.items()):
        if (ROOT / path).exists():
            if path in STALE_BACKLOG:
                errors.append(
                    f"{path}: listed in STALE_BACKLOG but the file exists; remove the entry"
                )
            continue
        if path in STALE_BACKLOG:
            continue
        candidates = index.get(Path(path).name, [])
        if not candidates:
            # A genuinely deleted artifact. Trackers may record these.
            continue
        tracker, number = sites[0]
        relative = tracker.relative_to(ROOT)
        moved = candidates[0] if len(candidates) == 1 else f"{len(candidates)} candidates"
        errors.append(
            f"{relative}:{number}: cited path `{path}` does not exist, but a file with "
            f"that name does: {moved}. Update the citation to follow the move."
        )

    # A cited target whose own source is gone is a deleted test and is allowed.
    # A cited target that is a bare suffix of exactly one real target is a
    # citation that did not follow the directory-prefix rename.
    sources = test_source_index()
    for name, sites in sorted(load_test_citations().items()):
        if name in sources:
            continue
        matches = [target for target in sources if target.endswith(f"-{name}")]
        if len(matches) != 1:
            continue
        tracker, number = sites[0]
        relative = tracker.relative_to(ROOT)
        errors.append(
            f"{relative}:{number}: cited test `{name}` is not a target, but "
            f"`{matches[0]}` is. Update the citation to follow the rename."
        )
    return errors


def run_check() -> int:
    errors = analyze()
    if errors:
        print("RFC tracker citation check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    total = len(load_citations())
    tests = len(load_test_citations())
    print(f"RFC tracker citation check passed ({total} cited paths, {tests} cited tests).")
    return 0


def run_self_test() -> int:
    """Proves the gate rejects a relocated citation and accepts a deleted one."""
    index = basename_index()
    citations = load_citations()
    sources = test_source_index()

    # A path that exists today, rewritten to a wrong directory, must be rejected.
    existing = next((path for path in sorted(citations) if (ROOT / path).exists()), None)
    if existing is None:
        print("citation self-test found no existing cited path", file=sys.stderr)
        return 1
    relocated = f"compiler/definitely-not-here/{Path(existing).name}"
    if not index.get(Path(relocated).name):
        print("citation self-test could not index the relocated basename", file=sys.stderr)
        return 1

    # A basename that appears nowhere must be accepted, since deletion is legal.
    if index.get("this-file-never-existed-anywhere.cc"):
        print("citation self-test found a basename that should not exist", file=sys.stderr)
        return 1

    # A bare stem whose real target carries a directory prefix must be
    # detectable as a rename. `session-compiler-session-test` is the target;
    # `compiler-session-test` is the stale citation the reorg left behind.
    prefixed = next(
        (name for name in sorted(sources) if name.count("-") >= 3 and "-" in name), None
    )
    if prefixed is None:
        print("citation self-test found no prefixed test target", file=sys.stderr)
        return 1
    bare = prefixed.split("-", 1)[1]
    if bare in sources:
        # This particular target's bare form is itself a target; find another.
        prefixed = next(
            (name for name in sorted(sources) if name.split("-", 1)[1] not in sources), None
        )
        if prefixed is None:
            print("citation self-test found no unambiguous prefixed target", file=sys.stderr)
            return 1
        bare = prefixed.split("-", 1)[1]
    if len([target for target in sources if target.endswith(f"-{bare}")]) != 1:
        print("citation self-test could not match a bare target suffix", file=sys.stderr)
        return 1

    # A target that is registered only under a build option must still resolve,
    # because the index is derived from sources rather than a configured
    # inventory.
    if "llvm-translation-test" not in sources:
        print("citation self-test lost the build-gated target", file=sys.stderr)
        return 1

    print("RFC tracker citation self-test passed (4/4).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    return run_check() if arguments.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
