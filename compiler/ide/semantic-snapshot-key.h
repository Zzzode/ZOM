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
#include "compiler/identity/source-query-input.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::ide {

/// \brief The canonical identity of one source content at one document version.
///
/// RFC 0023 "IDE Semantic Snapshots": an IDE semantic snapshot is a verified
/// result computed for one immutable source content observed at one document
/// version. This key binds the two: the stable content identity of the source
/// (`identity::source_query::StableSourceQueryKey`, the same content-addressed
/// key the parse query is keyed by) and the `DocumentVersion` the editor
/// supplied. Two requests over the same content at different versions produce
/// distinct keys, so a version never relabels an older result (RFC 0023 L213,
/// L988).
///
/// The process-local `EditorDocumentId` route handle is deliberately absent: it
/// is not content-addressed and cannot enter a stable canonical key. Workspace
/// source identity is already carried by the embedded `StableSourceQueryKey`.
///
/// The key is move-only because `StableSourceQueryKey` owns its canonical bytes.
/// Its canonical encoding is a domain-separated, length-framed preimage so the
/// bytes are stable across builds and independently decodable.
class SemanticSnapshotKey final {
public:
  SemanticSnapshotKey(SemanticSnapshotKey&&) noexcept = default;
  SemanticSnapshotKey& operator=(SemanticSnapshotKey&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticSnapshotKey);
  ~SemanticSnapshotKey() noexcept = default;

  /// \brief Binds a verified source content key to a document version.
  ///
  /// Binding never fails: `StableSourceQueryKey` is already a validated content
  /// identity and every signed 32-bit `DocumentVersion` is valid.
  ///
  /// \param sourceKey The verified stable source content key, consumed.
  /// \param version The editor-supplied document version.
  /// \return The bound snapshot key.
  ZC_NODISCARD static SemanticSnapshotKey bind(
      identity::source_query::StableSourceQueryKey&& sourceKey, DocumentVersion version);

  /// \brief Decodes one canonical snapshot key, failing closed on any deviation.
  ///
  /// The bytes must be exactly one canonical preimage: the domain tag, one
  /// length-framed inner source key that itself decodes as a bounded
  /// `StableSourceQueryKey`, and one fixed 4-byte big-endian version, with no
  /// missing or trailing bytes.
  ///
  /// \param bytes The candidate canonical bytes.
  /// \return The decoded key, or none when the bytes are not a canonical preimage.
  ZC_NODISCARD static zc::Maybe<SemanticSnapshotKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  /// \brief Encodes this key to its canonical preimage bytes.
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  /// \brief The embedded stable source content key.
  ZC_NODISCARD const identity::source_query::StableSourceQueryKey& sourceKey() const
      ZC_LIFETIMEBOUND {
    return sourceKeyValue;
  }
  /// \brief The document version this snapshot is bound to.
  ZC_NODISCARD DocumentVersion documentVersion() const noexcept { return versionValue; }

  bool operator==(const SemanticSnapshotKey& other) const noexcept {
    return versionValue == other.versionValue && sourceKeyValue == other.sourceKeyValue;
  }
  bool operator!=(const SemanticSnapshotKey& other) const noexcept { return !(*this == other); }

private:
  SemanticSnapshotKey(identity::source_query::StableSourceQueryKey&& sourceKey,
                      DocumentVersion version) noexcept
      : sourceKeyValue(zc::mv(sourceKey)), versionValue(version) {}

  identity::source_query::StableSourceQueryKey sourceKeyValue;
  DocumentVersion versionValue;
};

}  // namespace zomlang::compiler::ide
