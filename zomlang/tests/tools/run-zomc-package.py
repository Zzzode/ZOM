#!/usr/bin/env python3
"""Run one corpus source through the package-only zomc compile contract."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile


def run_compiler(command: list[str]) -> int:
    completed = subprocess.run(command, check=False)
    if completed.returncode >= 0:
        return completed.returncode

    signal_number = -completed.returncode
    try:
        signal_name = signal.Signals(signal_number).name
    except ValueError:
        signal_name = "UNKNOWN"
    print(
        f"zomc terminated by signal {signal_name} ({signal_number})",
        file=sys.stderr,
    )
    return 128 + signal_number


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument("args", nargs=argparse.REMAINDER)
    parsed = parser.parse_args()

    arguments = list(parsed.args)
    if not arguments or arguments[0] != "compile":
        return run_compiler([parsed.zomc, *arguments])

    source_indexes = [
        index
        for index, argument in enumerate(arguments[1:], start=1)
        if not argument.startswith("-")
        and argument.endswith(".zom")
        and (index == 1 or arguments[index - 1] not in {"--output", "-o"})
    ]
    if len(source_indexes) != 1:
        return run_compiler([parsed.zomc, *arguments])

    source_index = source_indexes[0]
    source_argument = Path(arguments.pop(source_index))
    source = source_argument.resolve(strict=True)
    target_relative = Path(source.name) if source_argument.is_absolute() else source_argument
    if target_relative.is_absolute() or ".." in target_relative.parts:
        target_relative = Path(source.name)
    with tempfile.TemporaryDirectory(prefix="zom-corpus-package-") as temporary:
        package_root = Path(temporary)
        target_source = package_root / target_relative
        target_source.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target_source)
        (package_root / "Zom.toml").write_text(
            '[package]\nname = "conformance"\nversion = "0.0.0"\n'
            'edition = "2026"\n\n[lib]\n'
            f'name = "conformance"\npath = "{target_relative.as_posix()}"\n',
            encoding="utf-8",
        )
        command = [
            parsed.zomc,
            *arguments,
            "--manifest-path",
            os.fspath(package_root / "Zom.toml"),
            "--package",
            "conformance",
            "--lib",
        ]
        return run_compiler(command)


if __name__ == "__main__":
    sys.exit(main())
