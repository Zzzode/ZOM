#!/usr/bin/env python3
"""Generate lexer Unicode identifier tables from UCD DerivedCoreProperties."""

from __future__ import annotations

import argparse
from pathlib import Path
from urllib.request import urlopen


DEFAULT_UCD_VERSION = "15.1.0"
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = ROOT / "products" / "zomlang" / "compiler" / "lexer"


def ucd_url(version: str) -> str:
    return f"https://www.unicode.org/Public/{version}/ucd/DerivedCoreProperties.txt"


def load_derived_core_properties(version: str, input_path: Path | None) -> str:
    if input_path is not None:
        return input_path.read_text(encoding="utf-8")
    with urlopen(ucd_url(version), timeout=30) as response:
        return response.read().decode("utf-8")


def parse_ranges(text: str) -> dict[str, list[tuple[int, int]]]:
    properties: dict[str, list[tuple[int, int]]] = {
        "ID_Start": [],
        "ID_Continue": [],
        "Other_ID_Start": [],
        "Other_ID_Continue": [],
    }

    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue

        code_range, property_name = [part.strip() for part in line.split(";", 1)]
        property_name = property_name.split()[0]
        if property_name not in properties:
            continue

        if ".." in code_range:
            start_text, end_text = code_range.split("..", 1)
        else:
            start_text = code_range
            end_text = code_range
        properties[property_name].append((int(start_text, 16), int(end_text, 16)))

    return properties


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[tuple[int, int]] = []
    for start, end in sorted(ranges):
        if merged and start <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def identifier_ranges(properties: dict[str, list[tuple[int, int]]]) -> tuple[
    list[tuple[int, int]], list[tuple[int, int]]
]:
    id_start = merge_ranges(properties["ID_Start"] + properties["Other_ID_Start"])
    id_part = merge_ranges(
        properties["ID_Continue"]
        + properties["Other_ID_Continue"]
        + properties["ID_Start"]
        + properties["Other_ID_Start"]
    )
    return id_start, id_part


def range_literal(start: int, end: int) -> str:
    return f"{{0x{start:04X}, 0x{end:04X}}},"


def format_range_array(name: str, comment: str, ranges: list[tuple[int, int]]) -> str:
    lines = [comment, f"static constexpr UnicodeRange {name}[] = {{"]
    for index in range(0, len(ranges), 4):
        chunk = ranges[index:index + 4]
        cells = [range_literal(start, end).ljust(19) for start, end in chunk]
        lines.append("    " + " ".join(cells).rstrip())
    lines.append("};")
    return "\n".join(lines)


def render_header(version: str, start_count: int, part_count: int) -> str:
    source = ucd_url(version)
    return f"""// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/common.h"

namespace zomlang {{
namespace compiler {{
namespace lexer {{

/// \\brief Unicode code point range for efficient range checking
struct UnicodeRange {{
  uint32_t start;
  uint32_t end;
}};

/// \\brief Unicode Character Database release used for identifier tables.
inline constexpr char kUnicodeIdentifierDataVersion[] = "{version}";

/// \\brief UCD property file used by scripts/codegen/gen_unicode_data.py.
inline constexpr char kUnicodeIdentifierDataSource[] =
    "{source}";

inline constexpr size_t kUnicodeIdentifierStartRangeCount = {start_count};
inline constexpr size_t kUnicodeIdentifierPartRangeCount = {part_count};

/// \\brief Corresponds to the ID_Start and Other_ID_Start property
extern const zc::ArrayPtr<const UnicodeRange> ID_START_RANGES;

/// \\brief Corresponds to ID_Continue, Other_ID_Continue, plus ID_Start and Other_ID_Start
extern const zc::ArrayPtr<const UnicodeRange> ID_PART_RANGES;

/// \\brief Check if a code point is in ID_Start and Other_ID_Start category
/// \\param codePoint The Unicode code point to check
/// \\return true if the code point can start an identifier
bool isIdStart(uint32_t codePoint);

/// \\brief Check if a code point is in ID_Continue, Other_ID_Continue, plus ID_Start and
/// Other_ID_Start category
/// \\param codePoint The Unicode code point to check
/// \\return true if the code point can continue an identifier
bool isIdPart(uint32_t codePoint);

/// \\brief Check if a code point is in a given range of Unicode code points
/// \\param codePoint The Unicode code point to check
/// \\param ranges The range of Unicode code points to check against
/// \\return true if the code point is in the range
bool isInUnicodeRange(uint32_t codePoint, const zc::ArrayPtr<const UnicodeRange>& ranges);

}}  // namespace lexer
}}  // namespace compiler
}}  // namespace zomlang
"""


def render_cc(version: str, start_ranges: list[tuple[int, int]],
              part_ranges: list[tuple[int, int]]) -> str:
    source = ucd_url(version)
    start_array = format_range_array(
        "ID_START_RANGES_DATA",
        "// Corresponds to the ID_Start and Other_ID_Start property",
        start_ranges,
    )
    part_array = format_range_array(
        "ID_PART_RANGES_DATA",
        "// Corresponds to ID_Continue, Other_ID_Continue, plus ID_Start and Other_ID_Start",
        part_ranges,
    )
    return f"""// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/lexer/unicode-data.h"

namespace zomlang {{
namespace compiler {{
namespace lexer {{

// Generated by scripts/codegen/gen_unicode_data.py.
// UCD version: {version}
// Source: {source}

{start_array}

{part_array}

static_assert(sizeof(ID_START_RANGES_DATA) / sizeof(ID_START_RANGES_DATA[0]) ==
              kUnicodeIdentifierStartRangeCount);
static_assert(sizeof(ID_PART_RANGES_DATA) / sizeof(ID_PART_RANGES_DATA[0]) ==
              kUnicodeIdentifierPartRangeCount);

constexpr zc::ArrayPtr<const UnicodeRange> ID_START_RANGES = zc::arrayPtr(ID_START_RANGES_DATA);

constexpr zc::ArrayPtr<const UnicodeRange> ID_PART_RANGES = zc::arrayPtr(ID_PART_RANGES_DATA);

bool isIdStart(const uint32_t codePoint) {{ return isInUnicodeRange(codePoint, ID_START_RANGES); }}

bool isIdPart(const uint32_t codePoint) {{ return isInUnicodeRange(codePoint, ID_PART_RANGES); }}

bool isInUnicodeRange(const uint32_t codePoint, const zc::ArrayPtr<const UnicodeRange>& ranges) {{
  size_t left = 0;
  size_t right = ranges.size();

  while (left < right) {{
    size_t mid = left + (right - left) / 2;
    const auto& range = ranges[mid];

    if (codePoint < range.start) {{
      right = mid;
    }} else if (codePoint > range.end) {{
      left = mid + 1;
    }} else {{
      return true;  // codePoint is within range
    }}
  }}

  return false;
}}

}}  // namespace lexer
}}  // namespace compiler
}}  // namespace zomlang
"""


def write_if_changed(path: Path, contents: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == contents:
        return False
    path.write_text(contents, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ucd-version", default=DEFAULT_UCD_VERSION)
    parser.add_argument("--input", type=Path, help="local DerivedCoreProperties.txt")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--check", action="store_true", help="fail if generated files differ")
    args = parser.parse_args()

    properties = parse_ranges(load_derived_core_properties(args.ucd_version, args.input))
    start_ranges, part_ranges = identifier_ranges(properties)
    header = render_header(args.ucd_version, len(start_ranges), len(part_ranges))
    cc = render_cc(args.ucd_version, start_ranges, part_ranges)

    header_path = args.output_dir / "unicode-data.h"
    cc_path = args.output_dir / "unicode-data.cc"
    if args.check:
        mismatches = [
            path for path, contents in [(header_path, header), (cc_path, cc)]
            if not path.exists() or path.read_text(encoding="utf-8") != contents
        ]
        if mismatches:
            for path in mismatches:
                print(f"{path} differs from generated Unicode identifier data")
            return 1
        print(f"unicode data ok: UCD {args.ucd_version}, {len(start_ranges)} start ranges, "
              f"{len(part_ranges)} part ranges")
        return 0

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_if_changed(header_path, header)
    write_if_changed(cc_path, cc)
    print(f"generated UCD {args.ucd_version}: {len(start_ranges)} start ranges, "
          f"{len(part_ranges)} part ranges")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
