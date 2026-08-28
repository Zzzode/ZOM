#!/usr/bin/env python3
"""Ensure unavailable native execution never reports success or emits artifacts."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path


ANSI = re.compile(r"\x1b\[[0-9;]*m")
RUN_ERROR = "The run command requires native code generation."
BINARY_ERROR = "[ZOM6007]: Binary emission is not implemented"


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


def require_rejection(command: list[str], cwd: Path, profile_path: Path, expected: str) -> None:
    exit_code, output = run_command(command, cwd, profile_path)
    if exit_code == 0 or expected not in output:
        raise RuntimeError(
            "native execution did not reject the unavailable operation:"
            f"\nrc={exit_code}\n{output}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument("--manifest", required=True)
    # Set when the LLVM backend is built: binary emission is then available
    # (it requires -o and produces an object), so the ZOM6007 rejection no
    # longer holds. The `run` rejection still holds because linking is not
    # implemented. Default (backend off) keeps the original assertions.
    parser.add_argument("--binary-available", action="store_true")
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))
    manifest = str(Path(arguments.manifest).resolve(strict=True))

    with tempfile.TemporaryDirectory(prefix="zom-native-execution-") as temporary:
        work_directory = Path(temporary)
        with tempfile.TemporaryDirectory(prefix="zom-native-profile-") as profile_temporary:
            profile_path = Path(profile_temporary) / "native-execution.profraw"
            require_rejection([zomc, "run"], work_directory, profile_path, RUN_ERROR)
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
                    ],
                    work_directory,
                    profile_path,
                    BINARY_ERROR,
                )
                if any(work_directory.iterdir()):
                    raise RuntimeError("unavailable native execution created an artifact")

    print("native execution rejects unavailable operations without artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
