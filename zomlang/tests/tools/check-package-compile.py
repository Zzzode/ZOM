#!/usr/bin/env python3
"""Exercise workspace resolution, lock update/replay, and package-root compilation."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def write_package(root: Path, name: str, dependency: bool) -> None:
    (root / "src").mkdir(parents=True)
    (root / "src" / "lib.zom").write_text("const value: i32 = 1;\n", encoding="utf-8")
    dependency_table = '\n[dependencies]\ndep = { path = "../dep" }\n' if dependency else ""
    (root / "Zom.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "1.0.0"\nedition = "2026"\n'
        + dependency_table,
        encoding="utf-8",
    )


def run(zomc: str, workspace: Path, lock_flag: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            zomc,
            "compile",
            "--manifest-path",
            str(workspace / "Zom.toml"),
            "--package",
            "app",
            "--lib",
            lock_flag,
            "--check",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parsed = parser.parse_args()
    zomc = str(Path(parsed.zomc).resolve(strict=True))

    with tempfile.TemporaryDirectory(prefix="zom-package-compile-") as temporary:
        workspace = Path(temporary)
        (workspace / "Zom.toml").write_text(
            '[workspace]\nmembers = ["app", "dep"]\n', encoding="utf-8"
        )
        write_package(workspace / "app", "app", True)
        write_package(workspace / "dep", "dep", False)

        updated = run(zomc, workspace, "--update-lock")
        if updated.returncode != 0:
            raise RuntimeError(f"package update compilation failed:\n{updated.stdout}")
        if updated.stdout:
            raise RuntimeError(f"package update check was not silent:\n{updated.stdout}")
        lockfile = workspace / "Zom.lock"
        if not lockfile.is_file():
            raise RuntimeError("--update-lock did not publish Zom.lock")
        lock_text = lockfile.read_text(encoding="utf-8")
        if lock_text.count("[[package]]") != 2 or "[[package.dependency]]" not in lock_text:
            raise RuntimeError(f"resolved lock graph is incomplete:\n{lock_text}")

        locked = run(zomc, workspace, "--locked")
        if locked.returncode != 0:
            raise RuntimeError(f"locked replay compilation failed:\n{locked.stdout}")
        if locked.stdout:
            raise RuntimeError(f"locked replay check was not silent:\n{locked.stdout}")

    print("package workspace compile and locked replay passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
