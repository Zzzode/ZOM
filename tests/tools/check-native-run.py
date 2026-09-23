#!/usr/bin/env python3
"""RFC 0043 O5/KR5.4: `zomc run` executes an entry-compatible native program.

Run only when the LLVM backend is built. A program whose entry folds to the
reserved no-argument i32 `zom.module_init` symbol links and executes end to
end, exiting with the returned value. A program whose object keeps a
parameterized symbol (the boolean-conditional diamond fixture) is object-only,
so `zomc run` fails closed on it.

One or more positive programs are named with repeated
``--run-case manifest::package::binary::exit-code`` entries; the script asserts
each one runs with its exact exit code. A single object-only reject case is
named with ``--reject-case``.
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


def parse_case(spec: str) -> tuple[str, str, str, int]:
    parts = spec.split("::")
    if len(parts) != 4:
        raise ValueError(f"run case must be manifest::package::binary::exit, got: {spec!r}")
    manifest, package, binary, exit_text = parts
    return manifest, package, binary, int(exit_text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument(
        "--run-case",
        action="append",
        required=True,
        metavar="manifest::package::binary::exit",
        help="Entry-compatible program and its expected exit code; repeatable.",
    )
    parser.add_argument(
        "--reject-case",
        required=True,
        metavar="manifest::package::binary",
        help="Object-only program that zomc run must reject.",
    )
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    # Positive: every entry-compatible program runs to completion with its exact
    # expected exit code and no linking/publication failure.
    positive_cases = [parse_case(spec) for spec in arguments.run_case]
    for manifest, package, binary, expected_exit in positive_cases:
        exit_code, output = run(zomc, manifest, package, binary)
        if LINK_FAILED in output:
            raise RuntimeError(f"native run reported a link failure:\nrc={exit_code}\n{output}")
        if exit_code != expected_exit:
            raise RuntimeError(
                f"native run exit mismatch for {package}: expected {expected_exit}, "
                f"got {exit_code}\n{output}"
            )

    # Negative: the object-only program has no zom.module_init entry, so run fails
    # closed rather than executing.
    reject_parts = arguments.reject_case.split("::")
    if len(reject_parts) != 3:
        raise ValueError(
            f"reject case must be manifest::package::binary, got: {arguments.reject_case!r}"
        )
    reject_manifest, reject_package, reject_binary = reject_parts
    reject_code, reject_output = run(zomc, reject_manifest, reject_package, reject_binary)
    if reject_code == 0 and LINK_FAILED not in reject_output:
        raise RuntimeError(
            "object-only program unexpectedly ran instead of failing closed:"
            f"\nrc={reject_code}\n{reject_output}"
        )

    print("zomc run executed every entry-compatible program and rejected the object-only program")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
