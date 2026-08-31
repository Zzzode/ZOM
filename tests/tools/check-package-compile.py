#!/usr/bin/env python3
"""Exercise `zomc build` over a multi-module workspace and lock replay."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def write_package(root: Path, name: str, dependency: bool, multi_module: bool = False) -> None:
    (root / "src").mkdir(parents=True)
    main_source = "const value: i32 = 1;\n"
    if multi_module:
        main_source = "import app::child;\nfun answer() -> i32 { return 42; }\n"
        (root / "src" / "child.zom").write_text(
            "module child;\nfun childAnswer() -> i32 { return 7; }\n",
            encoding="utf-8",
        )
    (root / "src" / "lib.zom").write_text(main_source, encoding="utf-8")
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
            "build",
            "--manifest-path",
            str(workspace / "Zom.toml"),
            "--package",
            "app",
            "--lib",
            lock_flag,
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

    help_result = subprocess.run(
        [zomc, "build", "--help"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if help_result.returncode != 0:
        raise RuntimeError(f"zomc build help failed:\n{help_result.stdout}")
    if "--emit" in help_result.stdout or "--check" in help_result.stdout:
        raise RuntimeError(f"zomc build exposed compile-only options:\n{help_result.stdout}")

    with tempfile.TemporaryDirectory(prefix="zom-package-compile-") as temporary:
        workspace = Path(temporary)
        (workspace / "Zom.toml").write_text(
            '[workspace]\nmembers = ["app", "dep"]\n', encoding="utf-8"
        )
        write_package(workspace / "app", "app", True, multi_module=True)
        write_package(workspace / "dep", "dep", False)

        updated = run(zomc, workspace, "--update-lock")
        if updated.returncode != 0:
            raise RuntimeError(f"package update build failed:\n{updated.stdout}")
        if updated.stdout:
            raise RuntimeError(f"package update build was not silent:\n{updated.stdout}")
        lockfile = workspace / "Zom.lock"
        if not lockfile.is_file():
            raise RuntimeError("--update-lock did not publish Zom.lock")
        lock_text = lockfile.read_text(encoding="utf-8")
        if lock_text.count("[[package]]") != 2 or "[[package.dependency]]" not in lock_text:
            raise RuntimeError(f"resolved lock graph is incomplete:\n{lock_text}")

        locked = run(zomc, workspace, "--locked")
        if locked.returncode != 0:
            raise RuntimeError(f"locked replay build failed:\n{locked.stdout}")
        if locked.stdout:
            raise RuntimeError(f"locked replay build was not silent:\n{locked.stdout}")

    print("zomc build multi-module workspace and locked replay passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
