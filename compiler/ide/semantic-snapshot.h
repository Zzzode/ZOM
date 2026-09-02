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

#include "compiler/ide/document-version.h"
#include "compiler/ide/snapshot-diagnostic.h"
#include "compiler/ide/snapshot-outline.h"
#include "compiler/ide/snapshot-token.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::ide {

/// \brief Why the semantic snapshot is unavailable rather than a user diagnostic.
///
/// RFC 0023 "IDE Semantic Snapshots" Authority Rails: an IDE-rail failure is a
/// local feature degradation, never a compiler-rail publication. The facade maps
/// a query runtime rejection (cancellation, a missing input, an invariant
/// violation, and the rest) onto this closed reason so features can degrade
/// without being handed a diagnostic they must render.
enum class SnapshotUnavailableReason : uint8_t {
  /// The request was cancelled before a result was demanded.
  Cancelled = 0x01,
  /// A required registered input was absent at demand time.
  MissingInput = 0x02,
  /// The query evaluator rejected the demand for any other runtime reason.
  EvaluationRejected = 0x03,
};

/// \brief The IDE-safe read projection of one parse at one document version.
///
/// RFC 0023 "IDE Semantic Snapshots": the editor semantic facade returns one of
/// three closed arms and never a compiler capability handle. `Published` carries
/// the sanitized projection of a verified parse (source identity as opaque
/// canonical bytes, the source byte length, the projected lexical tokens, and
/// warning-severity diagnostics).
/// `SourceRejected` carries the projected error diagnostics of a rejected parse.
/// `Unavailable` carries a closed reason for a runtime rejection. A parse query
/// has no key-rejection arm, so none is modelled.
///
/// The value exposes no `CompilerSession`, `QueryDatabase`, capability lease,
/// source manager, or buffer; only files (as opaque key bytes), ranges, tokens,
/// and closed states cross the boundary.
class SemanticSnapshot final {
public:
  enum class Kind : uint8_t {
    Published = 0x01,
    SourceRejected = 0x02,
    Unavailable = 0x03,
  };

  SemanticSnapshot(SemanticSnapshot&&) noexcept = default;
  SemanticSnapshot& operator=(SemanticSnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticSnapshot);
  ~SemanticSnapshot() noexcept = default;

  /// \brief Builds the published projection of a verified parse.
  ///
  /// \param sourceKeyBytes The opaque canonical source-key bytes, copied.
  /// \param sourceByteLength The parsed source length in bytes.
  /// \param version The document version this projection is labelled with.
  /// \param tokens The projected lexical tokens in source order, consumed.
  /// \param outline The projected top-level declaration outline in source order,
  ///                consumed.
  /// \param diagnostics The projected warning-severity diagnostics, consumed.
  ZC_NODISCARD static SemanticSnapshot published(zc::ArrayPtr<const uint8_t> sourceKeyBytes,
                                                 uint64_t sourceByteLength, DocumentVersion version,
                                                 zc::Array<SnapshotToken>&& tokens,
                                                 zc::Array<SnapshotOutlineEntry>&& outline,
                                                 zc::Array<SnapshotDiagnostic>&& diagnostics);

  /// \brief Builds the projection of a rejected parse.
  ///
  /// \param version The document version this projection is labelled with.
  /// \param diagnostics The projected error-severity diagnostics, consumed.
  ZC_NODISCARD static SemanticSnapshot sourceRejected(DocumentVersion version,
                                                      zc::Array<SnapshotDiagnostic>&& diagnostics);

  /// \brief Builds the unavailable arm for a runtime rejection.
  ZC_NODISCARD static SemanticSnapshot unavailable(SnapshotUnavailableReason reason);

  ZC_NODISCARD Kind kind() const noexcept { return kindValue; }
  ZC_NODISCARD bool isPublished() const noexcept { return kindValue == Kind::Published; }
  ZC_NODISCARD bool isSourceRejected() const noexcept { return kindValue == Kind::SourceRejected; }
  ZC_NODISCARD bool isUnavailable() const noexcept { return kindValue == Kind::Unavailable; }

  /// \brief The document version; valid on the Published and SourceRejected arms.
  ZC_NODISCARD DocumentVersion documentVersion() const noexcept { return versionValue; }
  /// \brief The projected diagnostics; empty on the Unavailable arm.
  ZC_NODISCARD zc::ArrayPtr<const SnapshotDiagnostic> diagnostics() const ZC_LIFETIMEBOUND {
    return diagnosticValues.asPtr();
  }
  /// \brief The projected lexical tokens in source order; only the Published arm
  /// carries tokens, so this is empty on the SourceRejected and Unavailable arms.
  ZC_NODISCARD zc::ArrayPtr<const SnapshotToken> tokens() const ZC_LIFETIMEBOUND {
    return tokenValues.asPtr();
  }
  /// \brief The projected top-level declaration outline in source order; only the
  /// Published arm carries an outline, so this is empty on the SourceRejected and
  /// Unavailable arms.
  ZC_NODISCARD zc::ArrayPtr<const SnapshotOutlineEntry> outline() const ZC_LIFETIMEBOUND {
    return outlineValues.asPtr();
  }
  /// \brief The opaque canonical source-key bytes; valid only on the Published arm.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> sourceKeyBytes() const ZC_LIFETIMEBOUND {
    return sourceKeyBytesValue.asPtr();
  }
  /// \brief The source byte length; valid only on the Published arm.
  ZC_NODISCARD uint64_t sourceByteLength() const noexcept { return sourceByteLengthValue; }
  /// \brief The unavailable reason; valid only on the Unavailable arm.
  ZC_NODISCARD SnapshotUnavailableReason unavailableReason() const noexcept {
    return unavailableReasonValue;
  }

private:
  SemanticSnapshot(Kind kind, zc::Array<uint8_t>&& sourceKeyBytes, uint64_t sourceByteLength,
                   DocumentVersion version, zc::Array<SnapshotToken>&& tokens,
                   zc::Array<SnapshotOutlineEntry>&& outline,
                   zc::Array<SnapshotDiagnostic>&& diagnostics,
                   SnapshotUnavailableReason reason) noexcept
      : kindValue(kind),
        sourceKeyBytesValue(zc::mv(sourceKeyBytes)),
        sourceByteLengthValue(sourceByteLength),
        versionValue(version),
        tokenValues(zc::mv(tokens)),
        outlineValues(zc::mv(outline)),
        diagnosticValues(zc::mv(diagnostics)),
        unavailableReasonValue(reason) {}

  Kind kindValue;
  zc::Array<uint8_t> sourceKeyBytesValue;
  uint64_t sourceByteLengthValue;
  DocumentVersion versionValue;
  zc::Array<SnapshotToken> tokenValues;
  zc::Array<SnapshotOutlineEntry> outlineValues;
  zc::Array<SnapshotDiagnostic> diagnosticValues;
  SnapshotUnavailableReason unavailableReasonValue;
};

}  // namespace zomlang::compiler::ide
