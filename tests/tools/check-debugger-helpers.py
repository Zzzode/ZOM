#!/usr/bin/env python3
"""Check debugger helper source syntax without requiring a debugger installation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPERS = (
    ROOT / "tools/gdb/zomlang_gdb.py",
    ROOT / "tools/lldb/zomlang_lldb.py",
)


def main() -> int:
    for helper in HELPERS:
        source = helper.read_text(encoding="utf-8")
        compile(source, str(helper), "exec")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
