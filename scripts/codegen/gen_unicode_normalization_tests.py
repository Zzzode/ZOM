#!/usr/bin/env python3
"""Generate a compact NFC conformance oracle from Unicode NormalizationTest."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
from urllib.request import urlopen


DEFAULT_UCD_VERSION = "15.1.0"
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = ROOT / "products" / "zomlang" / "tests" / "unittests" / "compiler" / "identity"


def source_url(version: str) -> str:
    return f"https://www.unicode.org/Public/{version}/ucd/NormalizationTest.txt"


def load_text(version: str, input_path: Path | None) -> str:
    if input_path is not None:
        return input_path.read_text(encoding="utf-8")
    with urlopen(source_url(version), timeout=60) as response:
        return response.read().decode("utf-8")


def decode_column(value: str) -> bytes:
    return "".join(chr(int(code_point, 16)) for code_point in value.split()).encode("utf-8")


def parse_nfc_pairs(text: str) -> list[tuple[bytes, bytes]]:
    expected_by_input: dict[bytes, bytes] = {}
    for line in text.splitlines():
        content = line.split("#", 1)[0].strip()
        if not content or content.startswith("@"):
            continue
        columns = [part.strip() for part in content.split(";")]
        if len(columns) < 5:
            raise ValueError(f"invalid NormalizationTest row: {line}")
        c1, c2, c3, c4, c5 = [decode_column(value) for value in columns[:5]]
        for source, expected in (
            (c1, c2), (c2, c2), (c3, c2), (c4, c4), (c5, c4)
        ):
            previous = expected_by_input.setdefault(source, expected)
            if previous != expected:
                raise ValueError("NormalizationTest assigns conflicting NFC outputs")
    return list(expected_by_input.items())


def expected_digest(pairs: list[tuple[bytes, bytes]]) -> bytes:
    digest = hashlib.sha256()
    for source, expected in pairs:
        digest.update(struct.pack(">Q", len(source)))
        digest.update(source)
        digest.update(struct.pack(">Q", len(expected)))
        digest.update(expected)
    return digest.digest()


def format_cpp(contents: str, path: Path) -> str:
    completed = subprocess.run(
        ["clang-format", "-style=file", f"--assume-filename={path}"],
        input=contents,
        text=True,
        capture_output=True,
        check=True,
    )
    return completed.stdout


def render_header(version: str, input_count: int, byte_count: int, digest: bytes) -> str:
    digest_hex = digest.hex()
    return f'''// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.
//
// Contains test data derived from Unicode Character Database {version}.
// See third_party/unicode/LICENSE.txt and third_party/unicode/README.md.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"

namespace zomlang::compiler::identity::test {{

struct UnicodeNormalizationInput final {{
  uint32_t offset;
  uint16_t length;
}};

inline constexpr char kUnicodeNormalizationTestVersion[] = "{version}";
inline constexpr char kUnicodeNormalizationTestSource[] = "{source_url(version)}";
inline constexpr char kUnicodeNormalizationExpectedDigest[] = "{digest_hex}";
inline constexpr size_t kUnicodeNormalizationInputCount = {input_count};
inline constexpr size_t kUnicodeNormalizationInputByteCount = {byte_count};

extern const zc::ArrayPtr<const UnicodeNormalizationInput> UNICODE_NORMALIZATION_INPUTS;
extern const zc::ArrayPtr<const uint8_t> UNICODE_NORMALIZATION_INPUT_BYTES;

}}  // namespace zomlang::compiler::identity::test
'''


def render_cc(version: str, pairs: list[tuple[bytes, bytes]]) -> str:
    pooled = bytearray()
    entries: list[tuple[int, int]] = []
    for source, _ in pairs:
        entries.append((len(pooled), len(source)))
        pooled.extend(source)

    entry_rows = "\n".join(f"    {{{offset}, {length}}}," for offset, length in entries)
    byte_rows = "\n".join(f"    0x{value:02X}," for value in pooled)
    return f'''// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.
//
// Contains test data derived from Unicode Character Database {version}.
// See third_party/unicode/LICENSE.txt and third_party/unicode/README.md.

#include "unicode-normalization-conformance-data.h"

namespace zomlang::compiler::identity::test {{

// Generated by scripts/codegen/gen_unicode_normalization_tests.py.
// UCD version: {version}
// Source: {source_url(version)}

static constexpr UnicodeNormalizationInput UNICODE_NORMALIZATION_INPUTS_DATA[] = {{
{entry_rows}
}};

static constexpr uint8_t UNICODE_NORMALIZATION_INPUT_BYTES_DATA[] = {{
{byte_rows}
}};

static_assert(zc::size(UNICODE_NORMALIZATION_INPUTS_DATA) ==
              kUnicodeNormalizationInputCount);
static_assert(zc::size(UNICODE_NORMALIZATION_INPUT_BYTES_DATA) ==
              kUnicodeNormalizationInputByteCount);

constexpr zc::ArrayPtr<const UnicodeNormalizationInput> UNICODE_NORMALIZATION_INPUTS =
    zc::arrayPtr(UNICODE_NORMALIZATION_INPUTS_DATA);
constexpr zc::ArrayPtr<const uint8_t> UNICODE_NORMALIZATION_INPUT_BYTES =
    zc::arrayPtr(UNICODE_NORMALIZATION_INPUT_BYTES_DATA);

}}  // namespace zomlang::compiler::identity::test
'''


def write_if_changed(path: Path, contents: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == contents:
        return False
    path.write_text(contents, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ucd-version", default=DEFAULT_UCD_VERSION)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    pairs = parse_nfc_pairs(load_text(args.ucd_version, args.input))
    digest = expected_digest(pairs)
    byte_count = sum(len(source) for source, _ in pairs)
    header_path = args.output_dir / "unicode-normalization-conformance-data.h"
    cc_path = args.output_dir / "unicode-normalization-conformance-data.cc"
    header = format_cpp(
        render_header(args.ucd_version, len(pairs), byte_count, digest), header_path
    )
    implementation = format_cpp(render_cc(args.ucd_version, pairs), cc_path)

    if args.check:
        mismatches = [
            path for path, contents in [(header_path, header), (cc_path, implementation)]
            if not path.exists() or path.read_text(encoding="utf-8") != contents
        ]
        if mismatches:
            for path in mismatches:
                print(f"{path} differs from generated Unicode normalization tests")
            return 1
        print(
            f"unicode normalization tests ok: UCD {args.ucd_version}, "
            f"{len(pairs)} unique inputs, sha256 {digest.hex()}"
        )
        return 0

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_if_changed(header_path, header)
    write_if_changed(cc_path, implementation)
    print(
        f"generated Unicode normalization tests: UCD {args.ucd_version}, "
        f"{len(pairs)} unique inputs, sha256 {digest.hex()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
