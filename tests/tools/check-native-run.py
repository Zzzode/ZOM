#!/usr/bin/env python3
"""RFC 0043 O5/KR5.4: `zomc run` executes an entry-compatible native program.

Run only when the LLVM backend is built. A program whose sole function folds to
the reserved no-argument i32 `zom.module_init` entry (the aggregate field-return
fixture) links and executes end to end, exiting with the returned value (42). A
program whose object keeps a parameterized symbol (the boolean-conditional
diamond fixture) is object-only, so `zomc run` fails closed on it. This is the
first end-to-end run assertion; it deliberately avoids the separate
check-native-execution.py contract.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LINK_FAILED = "Native linking or publication failed."


def run(zomc: str, manifest: str, package: str, binary: str) -> tuple[int, str]:
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-native-run-") as temporary:
        result = subprocess.run(
            [
                zomc,
                "run",
                "--manifest-path",
                resolved_manifest,
                "--package",
                package,
                "--bin",
                binary,
            ],
            cwd=Path(temporary),
            env=dict(os.environ),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        return result.returncode, ANSI.sub("", result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument("--run-manifest", required=True)
    parser.add_argument("--run-package", required=True)
    parser.add_argument("--run-bin", required=True)
    parser.add_argument("--run-exit", required=True, type=int)
    parser.add_argument("--reject-manifest", required=True)
    parser.add_argument("--reject-package", required=True)
    parser.add_argument("--reject-bin", required=True)
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    # Positive: the entry-compatible program runs to completion with the expected
    # exit code and no linking/publication failure.
    exit_code, output = run(zomc, arguments.run_manifest, arguments.run_package, arguments.run_bin)
    if LINK_FAILED in output:
        raise RuntimeError(f"native run reported a link failure:\nrc={exit_code}\n{output}")
    if exit_code != arguments.run_exit:
        raise RuntimeError(
            f"native run exit mismatch: expected {arguments.run_exit}, got {exit_code}\n{output}"
        )

    # Negative: the object-only program has no zom.module_init entry, so run fails
    # closed rather than executing.
    reject_code, reject_output = run(
        zomc, arguments.reject_manifest, arguments.reject_package, arguments.reject_bin
    )
    if reject_code == arguments.run_exit and LINK_FAILED not in reject_output:
        raise RuntimeError(
            "object-only program unexpectedly ran instead of failing closed:"
            f"\nrc={reject_code}\n{reject_output}"
        )

    print("zomc run executed the entry-compatible program and rejected the object-only program")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
