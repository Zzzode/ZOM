#!/usr/bin/env python3
"""RFC 0021 O5: `zomc compile --emit=binary -o x.o` writes a native ELF object.

Run only when the LLVM backend is built. Compiles each requested package bin,
then asserts the requested output path holds a non-empty ELF relocatable object
(magic 0x7f 'E' 'L' 'F'). Every `--case manifest::package::bin` triple exercises
a distinct MIR -> LIR lowering slice:

- the scalar module-initializer (RFC 0021 KR2.4), and
- the boolean-conditional diamond `Function` (KR5.2 C1).

Every `--reject-case manifest::package::bin` triple exercises a shape the object
selector must fail closed on: the emission must exit non-zero and leave no output
object on disk. These pin the selector's fail-closed branches (a too-large
function count, an ambiguous caller pair, a non-leaf third function) so a future
change cannot silently start emitting an object for a shape outside a lowering
slice.

Every `--string-case manifest::package::bin::expected` triple additionally
verifies the emitted object's `.rodata` section contains the expected UTF-8
bytes (followed by a NUL terminator).  This pins the string-literal-return slice
end to end: the compiler must copy the canonical bytes into a global constant,
not merely produce a page-aligned pointer whose low bits happen to match an exit
code.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ELF_MAGIC = b"\x7fELF"

# Matches the hex-byte column of `objdump -s` output, e.g.
# " 0000 68656c6c 6f00                        hello."
_OBJDUMP_HEX = re.compile(r"^\s*[0-9a-f]+\s+((?:[0-9a-f]{2,4}\s+)+)")


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


def string_case(zomc: str, manifest: str, package: str, binary: str, expected: str) -> None:
    """Emit an object and assert its .rodata contains the expected UTF-8 + NUL."""
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-object-emission-str-") as temporary:
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

        # Dump .rodata and parse the hex bytes.  objdump -s prints up to 16
        # bytes per line in four space-separated groups; the ASCII column is
        # unreliable for verification (non-printable bytes show as '.'), so we
        # compare against the decoded hex payload instead.
        dump = subprocess.run(
            ["objdump", "-s", "-j", ".rodata", str(output)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if dump.returncode != 0:
            raise RuntimeError(
                f"objdump .rodata failed for {package}: rc={dump.returncode}\n{dump.stdout}"
            )
        rodata = bytearray()
        for line in dump.stdout.splitlines():
            match = _OBJDUMP_HEX.match(line)
            if match:
                for group in match.group(1).split():
                    rodata.extend(bytes.fromhex(group))
        needle = expected.encode("utf-8") + b"\x00"
        if needle not in rodata:
            raise RuntimeError(
                f".rodata for {package} missing expected string {expected!r}\n"
                f"rodata bytes: {bytes(rodata)!r}"
            )


def reject_case(zomc: str, manifest: str, package: str, binary: str) -> None:
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-object-emission-reject-") as temporary:
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
        if result.returncode == 0:
            raise RuntimeError(
                f"object emission for {package} unexpectedly succeeded on a fail-closed shape\n"
                f"{result.stdout}"
            )
        if output.exists():
            raise RuntimeError(
                f"object emission for {package} failed but left an output artifact on disk"
            )


def incident_case(zomc: str, manifest: str, package: str, binary: str) -> None:
    # RFC 0053: a forced LIR verification finding must abort emission through the
    # internal incident rail (phase ir + fingerprint), non-zero exit, and no
    # output object -- never an ordinary operational error string.
    resolved_manifest = str(Path(manifest).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="zom-object-emission-incident-") as temporary:
        work_directory = Path(temporary)
        output = work_directory / "out.o"
        env = dict(os.environ)
        env["ZOM_FORCE_LIR_VERIFICATION_FAULT"] = "1"
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
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            raise RuntimeError(
                f"forced LIR verification fault did not fail emission for {package}\n"
                f"{result.stdout}"
            )
        if output.exists():
            raise RuntimeError(
                f"LIR verification failure for {package} left an output artifact on disk"
            )
        if "internal compiler error" not in result.stdout or "phase: ir" not in result.stdout:
            raise RuntimeError(
                f"LIR verification failure was not routed to the internal incident rail\n"
                f"{result.stdout}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zomc", required=True)
    # Each case is "manifest::package::bin"; every case must emit an ELF object.
    parser.add_argument("--case", action="append", required=True)
    # Each reject-case is "manifest::package::bin"; every one must fail closed
    # (non-zero exit, no output object) because its shape is outside every
    # lowering slice.
    parser.add_argument("--reject-case", action="append", default=[])
    # Each incident-case is "manifest::package::bin"; with the LIR verification
    # fault-injection hook set it must surface an internal incident with no
    # output object.
    parser.add_argument("--incident-case", action="append", default=[])
    # Each string-case is "manifest::package::bin::expected"; the emitted
    # object's .rodata must contain the expected UTF-8 bytes + NUL.
    parser.add_argument("--string-case", action="append", default=[])
    arguments = parser.parse_args()
    zomc = str(Path(arguments.zomc).resolve(strict=True))

    for case in arguments.case:
        parts = case.split("::")
        if len(parts) != 3:
            raise RuntimeError(f"malformed --case (want manifest::package::bin): {case}")
        manifest, package, binary = parts
        emit_case(zomc, manifest, package, binary)

    for case in arguments.reject_case:
        parts = case.split("::")
        if len(parts) != 3:
            raise RuntimeError(f"malformed --reject-case (want manifest::package::bin): {case}")
        manifest, package, binary = parts
        reject_case(zomc, manifest, package, binary)

    for case in arguments.incident_case:
        parts = case.split("::")
        if len(parts) != 3:
            raise RuntimeError(f"malformed --incident-case (want manifest::package::bin): {case}")
        manifest, package, binary = parts
        incident_case(zomc, manifest, package, binary)

    for case in arguments.string_case:
        parts = case.split("::")
        if len(parts) != 4:
            raise RuntimeError(
                f"malformed --string-case (want manifest::package::bin::expected): {case}"
            )
        manifest, package, binary, expected = parts
        string_case(zomc, manifest, package, binary, expected)

    print("zomc compile --emit=binary wrote a native ELF object for every case")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
