// Copyright (c) 2026 Zode.Z. All rights reserved
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

#pragma once

#include <cstdint>

#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ide {

/// \brief A resolved half-open source byte range for an IDE diagnostic.
///
/// RFC 0023 "IDE Semantic Snapshots": the editor semantic facade exposes ranges,
/// not the compiler-internal provenance keys the range was resolved from.
struct SnapshotRange final {
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;
  bool isTokenRange = false;

  bool operator==(const SnapshotRange& other) const noexcept = default;
};

/// \brief One IDE-safe diagnostic projected out of the verified compiler facts.
///
/// RFC 0023 "IDE Semantic Snapshots" Authority Rails: the editor semantic facade
/// exposes only files, ranges, symbols, display types, edits, and closed
/// availability states. This is the diagnostic projection: the diagnostic code,
/// its severity (derived from the code, not stored on the fact), an optional
/// resolved source byte range, and the message-substitution arguments as owned
/// copies.
///
/// The range is optional and never sentinel-encoded. It is present when the
/// facade resolved the diagnostic's provenance against a published parse (which
/// carries the provenance map), and absent when the range could not be resolved
/// -- currently every diagnostic on the source-rejected path, whose failure
/// channel carries no provenance map. A consumer must branch on `range()` rather
/// than read a zero range.
///
/// It deliberately carries none of the compiler-internal identity that the
/// underlying `diagnostics::DiagnosticFact` exposes: no `DiagnosticProvenanceKey`,
/// no `DiagnosticOccurrenceKey`, no `SourceFileKey`/`ModuleKey`, and no query,
/// session, source-manager, or buffer handle. The provenance keys are resolved
/// to a byte range by the facade before projection and then dropped.
class SnapshotDiagnostic final {
public:
  SnapshotDiagnostic(SnapshotDiagnostic&&) noexcept = default;
  SnapshotDiagnostic& operator=(SnapshotDiagnostic&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotDiagnostic);
  ~SnapshotDiagnostic() noexcept = default;

  /// \brief Projects one diagnostic with a resolved range.
  ///
  /// The severity is derived from `code` through `diagnostics::getDiagnosticInfo`
  /// so it stays consistent with the single authoritative diagnostic table. The
  /// arguments are copied into owned strings so the projection outlives the
  /// borrowed fact.
  ///
  /// \param code The diagnostic code.
  /// \param range The resolved half-open source range.
  /// \param arguments The message-substitution arguments, copied.
  /// \return The projected diagnostic carrying `range`.
  ZC_NODISCARD static SnapshotDiagnostic projectRanged(diagnostics::DiagID code,
                                                       SnapshotRange range,
                                                       zc::ArrayPtr<const zc::String> arguments);

  /// \brief Projects one diagnostic whose source range could not be resolved.
  ///
  /// \param code The diagnostic code.
  /// \param arguments The message-substitution arguments, copied.
  /// \return The projected diagnostic with no range.
  ZC_NODISCARD static SnapshotDiagnostic projectRangeless(diagnostics::DiagID code,
                                                          zc::ArrayPtr<const zc::String> arguments);

  ZC_NODISCARD SnapshotDiagnostic clone() const;

  ZC_NODISCARD diagnostics::DiagID code() const noexcept { return codeValue; }
  ZC_NODISCARD diagnostics::DiagSeverity severity() const noexcept { return severityValue; }
  /// \brief The resolved range, or none when it could not be resolved.
  ZC_NODISCARD zc::Maybe<SnapshotRange> range() const noexcept { return rangeValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND {
    return argumentValues.asPtr();
  }

  bool operator==(const SnapshotDiagnostic& other) const noexcept;
  bool operator!=(const SnapshotDiagnostic& other) const noexcept { return !(*this == other); }

private:
  SnapshotDiagnostic(diagnostics::DiagID code, diagnostics::DiagSeverity severity,
                     zc::Maybe<SnapshotRange> range, zc::Array<zc::String>&& arguments) noexcept
      : codeValue(code),
        severityValue(severity),
        rangeValue(range),
        argumentValues(zc::mv(arguments)) {}

  diagnostics::DiagID codeValue;
  diagnostics::DiagSeverity severityValue;
  zc::Maybe<SnapshotRange> rangeValue;
  zc::Array<zc::String> argumentValues;
};

}  // namespace zomlang::compiler::ide
