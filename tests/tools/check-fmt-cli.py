#!/usr/bin/env python3
"""RFC 0044 O6/KR6.2: `zomc fmt` and `zomc fmt --check` end-to-end contract.

Drives the real zomc binary over a temporary source tree and asserts the
deterministic formatter's CLI behavior:

  - `fmt --check` reports drift with a non-zero exit and writes nothing;
  - `fmt --check` on already-canonical source exits zero;
  - `fmt` rewrites a drifted source to its canonical form in place;
  - `fmt` is idempotent: re-running leaves canonical source unchanged and
    `fmt --check` then exits zero.

The normalizations exercised here are the ones RFC 0044 permits over the RFC
0023 lossless lexeme stream and that the formatter pipeline (proven by the
`lexeme-printer` unit tests) applies: trailing-whitespace strip, exactly one
final newline, and one space after a comma.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

# A source with trailing whitespace, no final newline, and a comma with no
# following space. Each is a normalization RFC 0044 permits.
DRIFTED = "let x = 1   \nf(a,b)"
# The canonical form the formatter must produce.
CANONICAL = "let x = 1\nf(a, b)\n"


def run_fmt(zomc: str, work: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [zomc, "fmt", *args],
        cwd=work,
        env=dict(os.environ),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    with tempfile.TemporaryDirectory(prefix="zom-fmt-cli-") as temporary:
        work = Path(temporary)

        # 1. --check on drifted source: non-zero exit, source untouched.
        drifted = work / "drifted.zom"
        drifted.write_text(DRIFTED, encoding="utf-8")
        result = run_fmt(zomc, work, "--check", "drifted.zom")
        if result.returncode == 0:
            raise RuntimeError(
                f"fmt --check accepted a drifted source (rc=0)\n{result.stdout}"
            )
        if drifted.read_text(encoding="utf-8") != DRIFTED:
            raise RuntimeError("fmt --check modified the source on disk")

        # 2. --check on canonical source: zero exit.
        clean = work / "clean.zom"
        clean.write_text(CANONICAL, encoding="utf-8")
        result = run_fmt(zomc, work, "--check", "clean.zom")
        if result.returncode != 0:
            raise RuntimeError(
                f"fmt --check rejected canonical source (rc={result.returncode})"
                f"\n{result.stdout}"
            )

        # 3. fmt in place rewrites the drifted source to canonical form.
        result = run_fmt(zomc, work, "drifted.zom")
        if result.returncode != 0:
            raise RuntimeError(
                f"fmt failed on a drifted source (rc={result.returncode})\n{result.stdout}"
            )
        formatted = drifted.read_text(encoding="utf-8")
        if formatted != CANONICAL:
            raise RuntimeError(
                f"fmt did not produce the canonical form\n"
                f"expected: {CANONICAL!r}\nactual:   {formatted!r}"
            )

        # 4. Idempotence: --check now passes, and a second fmt changes nothing.
        result = run_fmt(zomc, work, "--check", "drifted.zom")
        if result.returncode != 0:
            raise RuntimeError(
                f"fmt --check rejected freshly formatted source (rc={result.returncode})"
                f"\n{result.stdout}"
            )
        result = run_fmt(zomc, work, "drifted.zom")
        if result.returncode != 0 or drifted.read_text(encoding="utf-8") != CANONICAL:
            raise RuntimeError("fmt is not idempotent on canonical source")

    print("zomc fmt / fmt --check end-to-end contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
