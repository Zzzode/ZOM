#!/usr/bin/env python3
"""Verify current-state core-library documentation and publish a fixed report."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
DELETED_PATHS = (
    Path("docs/concurrency/zom-async-canonical-design.md"),
    Path("docs/design/runtime-ffi-examples.md"),
)
REQUIRED_INVENTORY = (
    Path("docs/overview.md"),
    Path("docs/spec/specification.md"),
    Path("docs/spec/chapters/13-modules-and-imports.md"),
    Path("docs/spec/chapters/14-memory-management.md"),
    Path("docs/spec/chapters/15-concurrency.md"),
    Path("docs/design/compiler-contracts.md"),
)
HISTORY_ROOTS = (Path("docs/rfc"), Path("docs/reports"))


def current_documents(root: Path) -> tuple[Path, ...]:
    return tuple(sorted(path.relative_to(root) for path in (root / "docs").rglob("*.md")))


def is_history(path: Path) -> bool:
    return any(path == history or history in path.parents for history in HISTORY_ROOTS)


def violations(root: Path) -> list[str]:
    errors: list[str] = []
    for path in REQUIRED_INVENTORY:
        if not (root / path).is_file():
            errors.append(f"missing fixed inventory document: {path}")
    for path in DELETED_PATHS:
        if (root / path).exists():
            errors.append(f"deleted document remains: {path}")
    for path in current_documents(root):
        if is_history(path):
            continue
        text = (root / path).read_text(encoding="utf-8")
        for deleted in DELETED_PATHS:
            if str(deleted) in text or deleted.name in text:
                errors.append(f"{path}: references deleted document: {deleted}")
    return errors


def report() -> str:
    lines = [
        "# Core Library Specification Alignment",
        "",
        "## Result",
        "",
        "The fixed current-state documentation inventory has zero deleted-document references.",
        "",
        "## Verified Inventory",
        "",
    ]
    lines.extend(f"- `{path}`" for path in REQUIRED_INVENTORY)
    lines.extend(("", "## Verification", "", "- Fixed inventory exists.",
                  "- Current-state documentation has no deleted-document links or text references.",
                  "- RFC and report decision history is excluded from current-state link checks.", ""))
    return "\n".join(lines)


def check(root: Path) -> int:
    errors = violations(root)
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("core-library specification alignment check passed")
    return 0


def self_test() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path in REQUIRED_INVENTORY:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("# Current\n", encoding="utf-8")
        if violations(root):
            print("error: clean temporary inventory failed")
            return 1
        restored = root / DELETED_PATHS[0]
        restored.parent.mkdir(parents=True, exist_ok=True)
        restored.write_text("# Restored\n", encoding="utf-8")
        if not violations(root):
            print("error: restored deleted document escaped")
            return 1
        restored.unlink()
        (root / REQUIRED_INVENTORY[0]).write_text(
            "[deleted](design/runtime-ffi-examples.md)\n", encoding="utf-8"
        )
        if not violations(root):
            print("error: deleted-document link escaped")
            return 1
    print("core-library specification alignment self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--verify-report", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if check(ROOT) != 0:
        return 1
    expected = report()
    if args.verify_report is not None:
        actual = args.verify_report.read_text(encoding="utf-8") if args.verify_report.is_file() else ""
        if actual != expected:
            print(f"error: report differs: {args.verify_report}")
            return 1
        print("core-library specification alignment report verified")
    if args.report is not None:
        args.report.write_text(expected, encoding="utf-8")
        print(f"wrote report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
