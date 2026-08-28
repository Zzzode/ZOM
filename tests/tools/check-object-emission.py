#!/usr/bin/env python3
"""RFC 0021 O5/KR5.1: `zomc compile --emit=binary -o x.o` writes a native ELF object.

Run only when the LLVM backend is built. Compiles the scalar module-initializer
package fixture, then asserts the requested output path holds a non-empty ELF
relocatable object (magic 0x7f 'E' 'L' 'F').
"""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ELF_MAGIC = b"\x7fELF"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--bin", required=True)
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))
    manifest = str(Path(arguments.manifest).resolve(strict=True))

    with tempfile.TemporaryDirectory(prefix="zom-object-emission-") as temporary:
        work_directory = Path(temporary)
        output = work_directory / "out.o"
        result = subprocess.run(
            [
                zomc,
                "compile",
                "--manifest-path",
                manifest,
                "--package",
                arguments.package,
                "--bin",
                arguments.bin,
                "--emit",
                "binary",
                "-o",
                str(output),
            ],
            cwd=work_directory,
            env=dict(os.environ),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(f"object emission failed: rc={result.returncode}\n{result.stdout}")
        if not output.exists():
            raise RuntimeError("object emission reported success but wrote no file")
        data = output.read_bytes()
        if len(data) == 0:
            raise RuntimeError("object emission wrote an empty file")
        if data[:4] != ELF_MAGIC:
            raise RuntimeError(f"emitted object is not ELF: first bytes {data[:4]!r}")

    print("zomc compile --emit=binary wrote a native ELF object")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
