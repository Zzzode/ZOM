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

namespace zomlang::compiler::ide {

/// \brief A resolved half-open source byte range for an IDE diagnostic.
struct SnapshotRange final {
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;
  bool isTokenRange = false;

  bool operator==(const SnapshotRange& other) const noexcept = default;
};

/// \brief The closed resource kinds that can own an IDE diagnostic location.
enum class SnapshotDiagnosticResourceKind : uint8_t {
  Source = 0x01,
  Document = 0x02,
};

/// \brief One IDE-safe resource-qualified diagnostic location.
class SnapshotDiagnosticLocation final {
public:
  SnapshotDiagnosticLocation(SnapshotDiagnosticLocation&&) noexcept = default;
  SnapshotDiagnosticLocation& operator=(SnapshotDiagnosticLocation&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotDiagnosticLocation);
  ~SnapshotDiagnosticLocation() noexcept = default;

  ZC_NODISCARD static SnapshotDiagnosticLocation source(zc::Array<uint8_t>&& identityBytes,
                                                        SnapshotRange range);
  ZC_NODISCARD static SnapshotDiagnosticLocation document(zc::Array<uint8_t>&& identityBytes,
                                                          SnapshotRange range);

  ZC_NODISCARD SnapshotDiagnosticLocation clone() const;
  ZC_NODISCARD SnapshotDiagnosticResourceKind resourceKind() const noexcept {
    return resourceKindValue;
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> resourceIdentityBytes() const ZC_LIFETIMEBOUND {
    return resourceIdentityBytesValue.asPtr();
  }
  ZC_NODISCARD SnapshotRange range() const noexcept { return rangeValue; }

  bool operator==(const SnapshotDiagnosticLocation& other) const noexcept;
  bool operator!=(const SnapshotDiagnosticLocation& other) const noexcept {
    return !(*this == other);
  }

private:
  SnapshotDiagnosticLocation(SnapshotDiagnosticResourceKind resourceKind,
                             zc::Array<uint8_t>&& resourceIdentityBytes,
                             SnapshotRange range) noexcept
      : resourceKindValue(resourceKind),
        resourceIdentityBytesValue(zc::mv(resourceIdentityBytes)),
        rangeValue(range) {}

  SnapshotDiagnosticResourceKind resourceKindValue;
  zc::Array<uint8_t> resourceIdentityBytesValue;
  SnapshotRange rangeValue;
};

/// \brief The closed presentation role of one related diagnostic location.
enum class SnapshotDiagnosticRelatedRole : uint8_t {
  Highlight = 0x01,
  Note = 0x02,
  PreviousDeclaration = 0x03,
};

/// \brief One IDE-safe related diagnostic record.
class SnapshotDiagnosticRelated final {
public:
  SnapshotDiagnosticRelated(SnapshotDiagnosticRelated&&) noexcept = default;
  SnapshotDiagnosticRelated& operator=(SnapshotDiagnosticRelated&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotDiagnosticRelated);
  ~SnapshotDiagnosticRelated() noexcept = default;

  ZC_NODISCARD static SnapshotDiagnosticRelated highlight(SnapshotDiagnosticLocation&& location);
  ZC_NODISCARD static SnapshotDiagnosticRelated note(diagnostics::DiagID code,
                                                     SnapshotDiagnosticLocation&& location,
                                                     zc::ArrayPtr<const zc::String> arguments);
  ZC_NODISCARD static SnapshotDiagnosticRelated previousDeclaration(
      SnapshotDiagnosticLocation&& location);

  ZC_NODISCARD SnapshotDiagnosticRelated clone() const;
  ZC_NODISCARD SnapshotDiagnosticRelatedRole role() const noexcept { return roleValue; }
  ZC_NODISCARD zc::Maybe<diagnostics::DiagID> code() const noexcept { return codeValue; }
  ZC_NODISCARD const SnapshotDiagnosticLocation& location() const noexcept { return locationValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND {
    return argumentValues.asPtr();
  }

  bool operator==(const SnapshotDiagnosticRelated& other) const noexcept;
  bool operator!=(const SnapshotDiagnosticRelated& other) const noexcept {
    return !(*this == other);
  }

private:
  SnapshotDiagnosticRelated(SnapshotDiagnosticRelatedRole role, zc::Maybe<diagnostics::DiagID> code,
                            SnapshotDiagnosticLocation&& location,
                            zc::Array<zc::String>&& arguments) noexcept
      : roleValue(role),
        codeValue(code),
        locationValue(zc::mv(location)),
        argumentValues(zc::mv(arguments)) {}

  SnapshotDiagnosticRelatedRole roleValue;
  zc::Maybe<diagnostics::DiagID> codeValue;
  SnapshotDiagnosticLocation locationValue;
  zc::Array<zc::String> argumentValues;
};

/// \brief One IDE-safe diagnostic projected out of the verified compiler facts.
///
/// The projection carries the diagnostic code, catalog-derived severity, one
/// resource-qualified primary location, owned message arguments, and complete
/// related information. Compiler provenance keys and source-manager handles do
/// not cross this boundary.
class SnapshotDiagnostic final {
public:
  SnapshotDiagnostic(SnapshotDiagnostic&&) noexcept = default;
  SnapshotDiagnostic& operator=(SnapshotDiagnostic&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotDiagnostic);
  ~SnapshotDiagnostic() noexcept = default;

  /// \brief Projects one diagnostic with complete resolved locations.
  ///
  /// \param code The diagnostic code.
  /// \param primary The resolved primary location, consumed.
  /// \param arguments The message-substitution arguments, copied.
  /// \param related The complete related-information records, consumed.
  /// \return The projected diagnostic carrying the complete resolved structure.
  ZC_NODISCARD static SnapshotDiagnostic project(diagnostics::DiagID code,
                                                 SnapshotDiagnosticLocation&& primary,
                                                 zc::ArrayPtr<const zc::String> arguments,
                                                 zc::Array<SnapshotDiagnosticRelated>&& related);

  ZC_NODISCARD SnapshotDiagnostic clone() const;

  ZC_NODISCARD diagnostics::DiagID code() const noexcept { return codeValue; }
  ZC_NODISCARD diagnostics::DiagSeverity severity() const noexcept { return severityValue; }
  ZC_NODISCARD const SnapshotDiagnosticLocation& primary() const noexcept { return primaryValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND {
    return argumentValues.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const SnapshotDiagnosticRelated> related() const ZC_LIFETIMEBOUND {
    return relatedValues.asPtr();
  }

  bool operator==(const SnapshotDiagnostic& other) const noexcept;
  bool operator!=(const SnapshotDiagnostic& other) const noexcept { return !(*this == other); }

private:
  SnapshotDiagnostic(diagnostics::DiagID code, diagnostics::DiagSeverity severity,
                     SnapshotDiagnosticLocation&& primary, zc::Array<zc::String>&& arguments,
                     zc::Array<SnapshotDiagnosticRelated>&& related) noexcept
      : codeValue(code),
        severityValue(severity),
        primaryValue(zc::mv(primary)),
        argumentValues(zc::mv(arguments)),
        relatedValues(zc::mv(related)) {}

  diagnostics::DiagID codeValue;
  diagnostics::DiagSeverity severityValue;
  SnapshotDiagnosticLocation primaryValue;
  zc::Array<zc::String> argumentValues;
  zc::Array<SnapshotDiagnosticRelated> relatedValues;
};

}  // namespace zomlang::compiler::ide
