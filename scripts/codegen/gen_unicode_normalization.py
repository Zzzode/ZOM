#!/usr/bin/env python3
"""Generate Unicode NFC normalization and full case-fold tables from a pinned UCD release."""

from __future__ import annotations

import argparse
from functools import cache
from pathlib import Path
import subprocess
from urllib.request import urlopen


DEFAULT_UCD_VERSION = "15.1.0"
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = ROOT / "compiler" / "identity"


def ucd_url(version: str, filename: str) -> str:
    return f"https://www.unicode.org/Public/{version}/ucd/{filename}"


def load_text(version: str, filename: str, input_dir: Path | None) -> str:
    if input_dir is not None:
        return (input_dir / filename).read_text(encoding="utf-8")
    with urlopen(ucd_url(version, filename), timeout=60) as response:
        return response.read().decode("utf-8")


def parse_unicode_data(text: str) -> tuple[dict[int, int], dict[int, tuple[int, ...]]]:
    combining_classes: dict[int, int] = {}
    decompositions: dict[int, tuple[int, ...]] = {}
    for line in text.splitlines():
        if not line:
            continue
        fields = line.split(";")
        code_point = int(fields[0], 16)
        combining_class = int(fields[3])
        if combining_class:
            combining_classes[code_point] = combining_class

        decomposition = fields[5]
        if decomposition and not decomposition.startswith("<"):
            decompositions[code_point] = tuple(int(value, 16) for value in decomposition.split())
    return combining_classes, decompositions


def parse_full_composition_exclusions(text: str) -> set[int]:
    exclusions: set[int] = set()
    for line in text.splitlines():
        content = line.split("#", 1)[0].strip()
        if not content or ";" not in content:
            continue
        code_range, property_name = [part.strip() for part in content.split(";", 1)]
        if property_name.split()[0] != "Full_Composition_Exclusion":
            continue
        if ".." in code_range:
            start_text, end_text = code_range.split("..", 1)
        else:
            start_text = code_range
            end_text = code_range
        exclusions.update(range(int(start_text, 16), int(end_text, 16) + 1))
    return exclusions


def parse_full_case_folding(text: str) -> dict[int, tuple[int, ...]]:
    mappings: dict[int, tuple[int, ...]] = {}
    for line in text.splitlines():
        content = line.split("#", 1)[0].strip()
        if not content:
            continue
        code_point_text, status, mapping_text, _ = [
            part.strip() for part in content.split(";", 3)
        ]
        if status not in {"C", "F"}:
            continue
        mappings[int(code_point_text, 16)] = tuple(
            int(value, 16) for value in mapping_text.split()
        )
    return dict(sorted(mappings.items()))


def expand_decompositions(
    raw: dict[int, tuple[int, ...]],
) -> dict[int, tuple[int, ...]]:
    @cache
    def expand(code_point: int) -> tuple[int, ...]:
        mapping = raw.get(code_point)
        if mapping is None:
            return (code_point,)
        return tuple(child for part in mapping for child in expand(part))

    return {code_point: expand(code_point) for code_point in sorted(raw)}


def composition_pairs(
    raw: dict[int, tuple[int, ...]], exclusions: set[int]
) -> list[tuple[int, int, int]]:
    pairs = [
        (mapping[0], mapping[1], code_point)
        for code_point, mapping in raw.items()
        if len(mapping) == 2 and code_point not in exclusions
    ]
    return sorted(pairs)


def render_header(
    version: str, combining_count: int, decomposition_count: int,
    scalar_count: int, composition_count: int, case_fold_count: int,
    case_fold_scalar_count: int,
) -> str:
    unicode_data_source = ucd_url(version, "UnicodeData.txt")
    normalization_source = ucd_url(version, "DerivedNormalizationProps.txt")
    case_folding_source = ucd_url(version, "CaseFolding.txt")
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
// Contains tables derived from Unicode Character Database {version}.
// See third_party/unicode/LICENSE.txt and third_party/unicode/README.md.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"

namespace zomlang::compiler::identity {{

struct UnicodeCombiningClassEntry final {{
  uint32_t codePoint;
  uint8_t combiningClass;
}};

struct UnicodeDecompositionEntry final {{
  uint32_t codePoint;
  uint32_t offset;
  uint16_t length;
}};

struct UnicodeCompositionEntry final {{
  uint32_t starter;
  uint32_t combining;
  uint32_t composite;
}};

struct UnicodeCaseFoldEntry final {{
  uint32_t codePoint;
  uint32_t offset;
  uint8_t length;
}};

inline constexpr char kUnicodeNormalizationDataVersion[] = "{version}";
inline constexpr char kUnicodeNormalizationUnicodeDataSource[] =
    "{unicode_data_source}";
inline constexpr char kUnicodeNormalizationPropertiesSource[] =
    "{normalization_source}";
inline constexpr char kUnicodeCaseFoldingSource[] = "{case_folding_source}";

inline constexpr size_t kUnicodeCombiningClassCount = {combining_count};
inline constexpr size_t kUnicodeDecompositionCount = {decomposition_count};
inline constexpr size_t kUnicodeDecompositionScalarCount = {scalar_count};
inline constexpr size_t kUnicodeCompositionCount = {composition_count};
inline constexpr size_t kUnicodeCaseFoldCount = {case_fold_count};
inline constexpr size_t kUnicodeCaseFoldScalarCount = {case_fold_scalar_count};

extern const zc::ArrayPtr<const UnicodeCombiningClassEntry> UNICODE_COMBINING_CLASSES;
extern const zc::ArrayPtr<const UnicodeDecompositionEntry> UNICODE_DECOMPOSITIONS;
extern const zc::ArrayPtr<const uint32_t> UNICODE_DECOMPOSITION_SCALARS;
extern const zc::ArrayPtr<const UnicodeCompositionEntry> UNICODE_COMPOSITIONS;
extern const zc::ArrayPtr<const UnicodeCaseFoldEntry> UNICODE_CASE_FOLDS;
extern const zc::ArrayPtr<const uint32_t> UNICODE_CASE_FOLD_SCALARS;

}}  // namespace zomlang::compiler::identity
'''


def format_entries(type_name: str, name: str, rows: list[str]) -> str:
    lines = [f"static constexpr {type_name} {name}[] = {{"]
    lines.extend(f"    {row}," for row in rows)
    lines.append("};")
    return "\n".join(lines)


def render_cc(
    version: str, combining_classes: dict[int, int],
    decompositions: dict[int, tuple[int, ...]],
    compositions: list[tuple[int, int, int]],
    case_folds: dict[int, tuple[int, ...]],
) -> str:
    unicode_data_source = ucd_url(version, "UnicodeData.txt")
    normalization_source = ucd_url(version, "DerivedNormalizationProps.txt")
    case_folding_source = ucd_url(version, "CaseFolding.txt")
    scalar_values: list[int] = []
    decomposition_rows: list[str] = []
    for code_point, mapping in decompositions.items():
        decomposition_rows.append(
            f"{{0x{code_point:06X}, {len(scalar_values)}, {len(mapping)}}}"
        )
        scalar_values.extend(mapping)

    combining_array = format_entries(
        "UnicodeCombiningClassEntry", "UNICODE_COMBINING_CLASSES_DATA",
        [f"{{0x{code_point:06X}, {value}}}" for code_point, value in sorted(combining_classes.items())],
    )
    decomposition_array = format_entries(
        "UnicodeDecompositionEntry", "UNICODE_DECOMPOSITIONS_DATA", decomposition_rows
    )
    scalar_array = format_entries(
        "uint32_t", "UNICODE_DECOMPOSITION_SCALARS_DATA",
        [f"0x{value:06X}" for value in scalar_values],
    )
    composition_array = format_entries(
        "UnicodeCompositionEntry", "UNICODE_COMPOSITIONS_DATA",
        [f"{{0x{first:06X}, 0x{second:06X}, 0x{composite:06X}}}"
         for first, second, composite in compositions],
    )
    case_fold_scalars: list[int] = []
    case_fold_rows: list[str] = []
    for code_point, mapping in case_folds.items():
        case_fold_rows.append(
            f"{{0x{code_point:06X}, {len(case_fold_scalars)}, {len(mapping)}}}"
        )
        case_fold_scalars.extend(mapping)
    case_fold_array = format_entries(
        "UnicodeCaseFoldEntry", "UNICODE_CASE_FOLDS_DATA", case_fold_rows
    )
    case_fold_scalar_array = format_entries(
        "uint32_t", "UNICODE_CASE_FOLD_SCALARS_DATA",
        [f"0x{value:06X}" for value in case_fold_scalars],
    )
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
// Contains tables derived from Unicode Character Database {version}.
// See third_party/unicode/LICENSE.txt and third_party/unicode/README.md.

#include "compiler/identity/text/unicode-normalization-data.h"

namespace zomlang::compiler::identity {{

// Generated by scripts/codegen/gen_unicode_normalization.py.
// UCD version: {version}
// Source: {unicode_data_source}
// Source: {normalization_source}
// Source: {case_folding_source}

{combining_array}

{decomposition_array}

{scalar_array}

{composition_array}

{case_fold_array}

{case_fold_scalar_array}

static_assert(zc::size(UNICODE_COMBINING_CLASSES_DATA) == kUnicodeCombiningClassCount);
static_assert(zc::size(UNICODE_DECOMPOSITIONS_DATA) == kUnicodeDecompositionCount);
static_assert(zc::size(UNICODE_DECOMPOSITION_SCALARS_DATA) ==
              kUnicodeDecompositionScalarCount);
static_assert(zc::size(UNICODE_COMPOSITIONS_DATA) == kUnicodeCompositionCount);
static_assert(zc::size(UNICODE_CASE_FOLDS_DATA) == kUnicodeCaseFoldCount);
static_assert(zc::size(UNICODE_CASE_FOLD_SCALARS_DATA) == kUnicodeCaseFoldScalarCount);

constexpr zc::ArrayPtr<const UnicodeCombiningClassEntry> UNICODE_COMBINING_CLASSES =
    zc::arrayPtr(UNICODE_COMBINING_CLASSES_DATA);
constexpr zc::ArrayPtr<const UnicodeDecompositionEntry> UNICODE_DECOMPOSITIONS =
    zc::arrayPtr(UNICODE_DECOMPOSITIONS_DATA);
constexpr zc::ArrayPtr<const uint32_t> UNICODE_DECOMPOSITION_SCALARS =
    zc::arrayPtr(UNICODE_DECOMPOSITION_SCALARS_DATA);
constexpr zc::ArrayPtr<const UnicodeCompositionEntry> UNICODE_COMPOSITIONS =
    zc::arrayPtr(UNICODE_COMPOSITIONS_DATA);
constexpr zc::ArrayPtr<const UnicodeCaseFoldEntry> UNICODE_CASE_FOLDS =
    zc::arrayPtr(UNICODE_CASE_FOLDS_DATA);
constexpr zc::ArrayPtr<const uint32_t> UNICODE_CASE_FOLD_SCALARS =
    zc::arrayPtr(UNICODE_CASE_FOLD_SCALARS_DATA);

}}  // namespace zomlang::compiler::identity
'''


def write_if_changed(path: Path, contents: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == contents:
        return False
    path.write_text(contents, encoding="utf-8")
    return True


def format_cpp(contents: str, path: Path) -> str:
    completed = subprocess.run(
        ["clang-format", "-style=file", f"--assume-filename={path}"],
        input=contents,
        text=True,
        capture_output=True,
        check=True,
    )
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ucd-version", default=DEFAULT_UCD_VERSION)
    parser.add_argument("--input-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    unicode_data = load_text(args.ucd_version, "UnicodeData.txt", args.input_dir)
    derived = load_text(args.ucd_version, "DerivedNormalizationProps.txt", args.input_dir)
    case_folding = load_text(args.ucd_version, "CaseFolding.txt", args.input_dir)
    combining_classes, raw_decompositions = parse_unicode_data(unicode_data)
    decompositions = expand_decompositions(raw_decompositions)
    exclusions = parse_full_composition_exclusions(derived)
    compositions = composition_pairs(raw_decompositions, exclusions)
    case_folds = parse_full_case_folding(case_folding)
    scalar_count = sum(len(mapping) for mapping in decompositions.values())
    case_fold_scalar_count = sum(len(mapping) for mapping in case_folds.values())

    header_path = args.output_dir / "unicode-normalization-data.h"
    cc_path = args.output_dir / "unicode-normalization-data.cc"
    header = format_cpp(render_header(
        args.ucd_version, len(combining_classes), len(decompositions),
        scalar_count, len(compositions), len(case_folds), case_fold_scalar_count
    ), header_path)
    implementation = format_cpp(render_cc(
        args.ucd_version, combining_classes, decompositions, compositions, case_folds
    ), cc_path)

    if args.check:
        mismatches = [
            path for path, contents in [(header_path, header), (cc_path, implementation)]
            if not path.exists() or path.read_text(encoding="utf-8") != contents
        ]
        if mismatches:
            for path in mismatches:
                print(f"{path} differs from generated Unicode normalization data")
            return 1
        print(
            f"unicode normalization data ok: UCD {args.ucd_version}, "
            f"{len(combining_classes)} combining classes, "
            f"{len(decompositions)} decompositions, {len(compositions)} compositions, "
            f"{len(case_folds)} case folds"
        )
        return 0

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_if_changed(header_path, header)
    write_if_changed(cc_path, implementation)
    print(
        f"generated Unicode normalization data: UCD {args.ucd_version}, "
        f"{len(combining_classes)} combining classes, "
        f"{len(decompositions)} decompositions, {len(compositions)} compositions, "
        f"{len(case_folds)} case folds"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
