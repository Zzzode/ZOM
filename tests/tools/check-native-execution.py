#!/usr/bin/env python3
"""Ensure unavailable native operations reject structurally and leak no artifacts.

This check owns only the rejection, artifact-leak, and exit-code semantics of the
native operations; the end-to-end real-execution positive (an entry-compatible
program exits with its value) is covered independently by check-native-run.py.

Two structured diagnostic codes are the stable anchors, never English prose:

- `ZOM7016` (package invocation invalid): an underspecified `zomc run` with no
  package selection is rejected before any native work, in every build.
- `ZOM6007` (binary emission not implemented): with the LLVM backend off,
  `zomc compile --emit=binary` is rejected. With the backend on this path
  succeeds, so the emit rejection is asserted only in the backend-off build.

Every asserted rejection must exit non-zero, carry its code, and leave no
artifact in the working directory.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path


ANSI = re.compile(r"\x1b\[[0-9;]*m")
RUN_REJECTION_CODE = "ZOM7016"
EMIT_REJECTION_CODE = "ZOM6007"


def run_command(command: list[str], cwd: Path, profile_path: Path) -> tuple[int, str]:
    environment = dict(os.environ)
    environment["LLVM_PROFILE_FILE"] = str(profile_path)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    return result.returncode, ANSI.sub("", result.stdout)


def require_rejection(command: list[str], cwd: Path, profile_path: Path, code: str) -> None:
    exit_code, output = run_command(command, cwd, profile_path)
    if exit_code == 0 or code not in output:
        raise RuntimeError(
            "native operation did not reject with the expected structured code:"
            f"\nexpected={code}\nrc={exit_code}\n{output}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument("--manifest", required=True)
    # Set when the LLVM backend is built: binary emission is then available, so
    # the ZOM6007 emit rejection no longer holds. The underspecified-run
    # rejection (ZOM7016) holds in every build. Default (backend off) also
    # asserts the emit rejection.
    parser.add_argument("--binary-available", action="store_true")
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))
    manifest = str(Path(arguments.manifest).resolve(strict=True))

    with tempfile.TemporaryDirectory(prefix="zom-native-execution-") as temporary:
        work_directory = Path(temporary)
        with tempfile.TemporaryDirectory(prefix="zom-native-profile-") as profile_temporary:
            profile_path = Path(profile_temporary) / "native-execution.profraw"

            # Underspecified `run` (no package selection) is rejected in every
            # build before any native work.
            require_rejection([zomc, "run"], work_directory, profile_path, RUN_REJECTION_CODE)

            # Backend off: binary emission is rejected. installed-consumer is used
            # only as the emit target; its exit value is irrelevant here.
            if not arguments.binary_available:
                require_rejection(
                    [
                        zomc,
                        "compile",
                        "--manifest-path",
                        manifest,
                        "--package",
                        "installed_consumer",
                        "--bin",
                        "installed_consumer",
                        "--emit",
                        "binary",
                    ],
                    work_directory,
                    profile_path,
                    EMIT_REJECTION_CODE,
                )

            # A rejected native operation never leaves an artifact behind, in
            # either build configuration.
            if any(work_directory.iterdir()):
                raise RuntimeError("a rejected native operation created an artifact")

    print("native operations reject structurally without leaking artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
