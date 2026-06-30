#!/usr/bin/env python3

import sys
from pathlib import Path


CONFORMANCE_ROOT = Path(__file__).resolve().parents[1]
CORPUS_ROOT = CONFORMANCE_ROOT / "corpus"
AST_EXPECTATION_ROOT = CONFORMANCE_ROOT / "expectations" / "ast"

ALLOWED_EXTRA_AST_CHECKS = {
    Path("00-dump-format/default-tree.check"),
    Path("00-dump-format/invalid-format.check"),
    Path("00-dump-format/json.check"),
    Path("00-dump-format/raw.check"),
}


def rel_files(root: Path, suffix: str) -> set[Path]:
    return {
        path.relative_to(root)
        for path in root.rglob(f"*{suffix}")
        if path.is_file()
    }


def print_examples(header: str, entries: list[Path]) -> None:
    if not entries:
        return

    print(header, file=sys.stderr)
    for path in entries[:50]:
        print(f"  - {path.as_posix()}", file=sys.stderr)
    if len(entries) > 50:
        print(f"  ... and {len(entries) - 50} more", file=sys.stderr)


def main() -> int:
    corpus_as_checks = {
        path.with_suffix(".check") for path in rel_files(CORPUS_ROOT, ".zom")
    }
    ast_checks = rel_files(AST_EXPECTATION_ROOT, ".check")

    missing = sorted(corpus_as_checks - ast_checks)
    unexpected = sorted(ast_checks - corpus_as_checks - ALLOWED_EXTRA_AST_CHECKS)

    if missing or unexpected:
        print("AST conformance coverage check failed.", file=sys.stderr)
        print_examples("Missing AST expectation files:", missing)
        print_examples("Unexpected AST expectation files:", unexpected)
        return 1

    print(
        "AST conformance coverage check passed "
        f"({len(corpus_as_checks)} corpus inputs, "
        f"{len(ast_checks)} AST expectations, "
        f"{len(ALLOWED_EXTRA_AST_CHECKS)} explicit extras)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
