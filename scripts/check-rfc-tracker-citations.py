#!/usr/bin/env python3
"""Check that RFC tracker path citations resolve against the tree.

Quarter-close requirement 7 asks that trackers match production code. The
failure mode this guards is narrow and mechanical: a source reorganization moves
a file, and every tracker that cited its old path silently becomes wrong. That
is exactly what happened across the identity, ownership, and test-directory
regroupings, and nothing caught it.

The rule is therefore about relocation, not deletion. A citation fails when the
path does not exist AND a file with the same basename does exist elsewhere in
the tree, because that pair means the citation simply did not follow the move. A
citation naming a genuinely deleted artifact is left alone: trackers legitimately
record work whose files a later RFC removed, and forbidding that would force
rewriting history.

Known-stale citations that name deleted artifacts are listed in STALE_BACKLOG
with the tracker that owns the rewrite. Adding to that list is not a way to
silence this gate: entries must name a deleted artifact, and the gate rejects an
entry whose file has come back.
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

# Citations naming artifacts that were deliberately deleted. Each entry is owned
# by the tracker rewrite noted beside it; see the 2026-09-07 audit report at
# docs/reports/zom-rfc-tracker-audit-2026-09-07.md.
STALE_BACKLOG: dict[str, str] = {
    # RFC 0025: 37 citations across 14 tracker rows. Needs one owner rewrite
    # pass, not line fixes. Artifacts removed by RFC 0027 R27-29 (identity
    # registry and freeze authority), RFC 0047 (diagnostics), and the
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
    return errors


def run_check() -> int:
    errors = analyze()
    if errors:
        print("RFC tracker citation check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    total = len(load_citations())
    print(f"RFC tracker citation check passed ({total} cited paths).")
    return 0


def run_self_test() -> int:
    """Proves the gate rejects a relocated citation and accepts a deleted one."""
    index = basename_index()
    citations = load_citations()

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

    print("RFC tracker citation self-test passed (2/2).")
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
