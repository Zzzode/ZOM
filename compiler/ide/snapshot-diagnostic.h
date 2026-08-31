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

/// \brief One IDE-safe diagnostic projected out of the verified compiler facts.
///
/// RFC 0023 "IDE Semantic Snapshots" Authority Rails: the editor semantic facade
/// exposes only files, ranges, symbols, display types, edits, and closed
/// availability states. This is the diagnostic projection: the diagnostic code,
/// its severity (derived from the code, not stored on the fact), the resolved
/// half-open source byte range, and the message-substitution arguments as owned
/// copies.
///
/// It deliberately carries none of the compiler-internal identity the underlying
/// `diagnostics::DiagnosticFact` exposes: no `DiagnosticProvenanceKey`, no
/// `DiagnosticOccurrenceKey`, no `SourceFileKey`/`ModuleKey`, and no query,
/// session, source-manager, or buffer handle. The provenance keys are resolved
/// to a byte range by the facade before projection and then dropped.
class SnapshotDiagnostic final {
public:
  SnapshotDiagnostic(SnapshotDiagnostic&&) noexcept = default;
  SnapshotDiagnostic& operator=(SnapshotDiagnostic&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotDiagnostic);
  ~SnapshotDiagnostic() noexcept = default;

  /// \brief Projects one resolved diagnostic.
  ///
  /// The severity is derived from `code` through `diagnostics::getDiagnosticInfo`
  /// so it stays consistent with the single authoritative diagnostic table. The
  /// arguments are copied into owned strings so the projection outlives the
  /// borrowed fact.
  ///
  /// \param code The diagnostic code.
  /// \param byteStart The resolved half-open range start.
  /// \param byteEnd The resolved half-open range end.
  /// \param isTokenRange Whether the range spans a whole token.
  /// \param arguments The message-substitution arguments, copied.
  /// \return The projected diagnostic.
  ZC_NODISCARD static SnapshotDiagnostic project(diagnostics::DiagID code, uint64_t byteStart,
                                                 uint64_t byteEnd, bool isTokenRange,
                                                 zc::ArrayPtr<const zc::String> arguments);

  ZC_NODISCARD SnapshotDiagnostic clone() const;

  ZC_NODISCARD diagnostics::DiagID code() const noexcept { return codeValue; }
  ZC_NODISCARD diagnostics::DiagSeverity severity() const noexcept { return severityValue; }
  ZC_NODISCARD uint64_t byteStart() const noexcept { return byteStartValue; }
  ZC_NODISCARD uint64_t byteEnd() const noexcept { return byteEndValue; }
  ZC_NODISCARD bool isTokenRange() const noexcept { return isTokenRangeValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND {
    return argumentValues.asPtr();
  }

  bool operator==(const SnapshotDiagnostic& other) const noexcept;
  bool operator!=(const SnapshotDiagnostic& other) const noexcept { return !(*this == other); }

private:
  SnapshotDiagnostic(diagnostics::DiagID code, diagnostics::DiagSeverity severity,
                     uint64_t byteStart, uint64_t byteEnd, bool isTokenRange,
                     zc::Array<zc::String>&& arguments) noexcept
      : codeValue(code),
        severityValue(severity),
        byteStartValue(byteStart),
        byteEndValue(byteEnd),
        isTokenRangeValue(isTokenRange),
        argumentValues(zc::mv(arguments)) {}

  diagnostics::DiagID codeValue;
  diagnostics::DiagSeverity severityValue;
  uint64_t byteStartValue;
  uint64_t byteEndValue;
  bool isTokenRangeValue;
  zc::Array<zc::String> argumentValues;
};

}  // namespace zomlang::compiler::ide
