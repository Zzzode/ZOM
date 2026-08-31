#!/usr/bin/env python3
"""RFC 0021 O5: `zomc compile --emit=binary -o x.o` writes a native ELF object.

Run only when the LLVM backend is built. Compiles each requested package bin,
then asserts the requested output path holds a non-empty ELF relocatable object
(magic 0x7f 'E' 'L' 'F'). Every `--case manifest::package::bin` triple exercises
a distinct MIR -> LIR lowering slice:

- the scalar module-initializer (RFC 0021 KR2.4), and
- the boolean-conditional diamond `Function` (KR5.2 C1).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ELF_MAGIC = b"\x7fELF"


def emit_case(zomc: str, manifest: str, package: str, binary: str) -> None:
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-object-emission-") as temporary:
        work_directory = Path(temporary)
        output = work_directory / "out.o"
        result = subprocess.run(
            [
                zomc,
                "compile",
                "--manifest-path",
                resolved_manifest,
                "--package",
                package,
                "--bin",
                binary,
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
            raise RuntimeError(
                f"object emission failed for {package}: rc={result.returncode}\n{result.stdout}"
            )
        if not output.exists():
            raise RuntimeError(f"object emission for {package} reported success but wrote no file")
        data = output.read_bytes()
        if len(data) == 0:
            raise RuntimeError(f"object emission for {package} wrote an empty file")
        if data[:4] != ELF_MAGIC:
            raise RuntimeError(f"emitted object for {package} is not ELF: first bytes {data[:4]!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    # Each case is "manifest::package::bin"; every case must emit an ELF object.
    parser.add_argument("--case", action="append", required=True)
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    for case in arguments.case:
        parts = case.split("::")
        if len(parts) != 3:
            raise RuntimeError(f"malformed --case (want manifest::package::bin): {case}")
        manifest, package, binary = parts
        emit_case(zomc, manifest, package, binary)

    print("zomc compile --emit=binary wrote a native ELF object for every case")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
