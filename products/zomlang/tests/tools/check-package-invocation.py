#!/usr/bin/env python3
"""Check every RFC 0012 pre-request invocation diagnostic through the real CLI."""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
import time
from pathlib import Path


ANSI = re.compile(r"\x1b\[[0-9;]*m")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parsed = parser.parse_args()
    zomc = str(Path(parsed.zomc).resolve(strict=True))

    cases = {
        "manifest-not-found": ["--package", "app", "--lib"],
        "invalid-manifest-path": [
            "--package",
            "app",
            "--lib",
            "--manifest-path",
            "Other.toml",
        ],
        "missing-package-selection": ["--lib"],
        "duplicate-package-selection": [
            "--package",
            "app",
            "--package",
            "other",
            "--lib",
        ],
        "missing-target-selection": ["--package", "app"],
        "duplicate-target-selection": ["--package", "app", "--lib", "--lib"],
        "positional-source-argument": ["main.zom"],
        "invalid-feature-list": [
            "--package",
            "app",
            "--lib",
            "--features",
            "a,,b",
        ],
        "conflicting-lock-mode": [
            "--package",
            "app",
            "--lib",
            "--locked",
            "--update-lock",
        ],
        "unknown-target-profile": [
            "--package",
            "app",
            "--lib",
            "--target",
            "missing",
        ],
        "invalid-panic-strategy": [
            "--package",
            "app",
            "--lib",
            "--panic",
            "explode",
        ],
    }

    with tempfile.TemporaryDirectory(prefix="zom-invocation-") as temporary:
        cwd = Path(temporary)
        for issue, arguments in cases.items():
            result = subprocess.run(
                [zomc, "compile", *arguments],
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )
            output = ANSI.sub("", result.stdout)
            expected = f"[ZOM7016]: Package invocation is invalid ({issue})"
            if result.returncode == 0 or expected not in output:
                raise RuntimeError(
                    f"{issue}: expected failing {expected!r}, got rc={result.returncode}:\n{output}"
                )
            if temporary in output or "main.zom" in output or "Other.toml" in output:
                raise RuntimeError(f"{issue}: diagnostic leaked rejected input: {output}")

        workspace = cwd / "workspace"
        invocation = cwd / "invocation"
        workspace.mkdir()
        invocation.mkdir()
        (workspace / "Zom.toml").write_text(
            '[package]\nname = "isolation"\nversion = "0.0.0"\n'
            'edition = "2026"\n\n[lib]\nname = "isolation"\npath = "main.zom"\n',
            encoding="utf-8",
        )
        (workspace / "main.zom").write_text("let value: i32 = 1;\n", encoding="utf-8")
        (workspace / "snapshot-padding.bin").write_bytes(bytes(8 * 1024 * 1024))

        process = subprocess.Popen(
            [
                zomc,
                "compile",
                "--manifest-path",
                str(workspace / "Zom.toml"),
                "--package",
                "isolation",
                "--lib",
                "--dump-dispatch",
            ],
            cwd=invocation,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        polluted_invocation = False
        while process.poll() is None:
            polluted_invocation = polluted_invocation or any(invocation.glob(".zc-tmp.*"))
            time.sleep(0.001)
        output, _ = process.communicate(timeout=30)
        normalized_output = ANSI.sub("", output)
        expected_boundary = "[ZOM9928]: Internal checker required fact is missing"
        if process.returncode == 0 or expected_boundary not in normalized_output:
            raise RuntimeError(
                "source snapshot isolation did not reach the fail-closed signature boundary:"
                f"\n{normalized_output}"
            )
        if polluted_invocation:
            raise RuntimeError("source snapshot staging polluted the invocation directory")
        if any(cwd.glob(".zc-tmp.*")):
            raise RuntimeError("source snapshot staging was not cleaned after compilation")

    print("all package invocation diagnostics passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
