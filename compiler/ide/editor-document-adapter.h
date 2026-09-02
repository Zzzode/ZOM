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
#include "compiler/identity/key/crate-key.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/query/query-database.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ide {

/// \brief A process-local handle for one open editor document lifecycle.
///
/// RFC 0023 "IDE Semantic Snapshots": the `EditorDocumentId` is issued by the
/// adapter at `didOpen` and is deliberately NOT content-addressed, so it never
/// enters a query key. It identifies one open lifecycle; reopening the same URI
/// after a close issues a new id. It is an opaque monotonic value; only equality
/// matters.
class EditorDocumentId final {
public:
  constexpr EditorDocumentId() noexcept = default;

  ZC_NODISCARD static constexpr EditorDocumentId fromRaw(uint64_t value) noexcept {
    return EditorDocumentId(value);
  }
  ZC_NODISCARD constexpr uint64_t raw() const noexcept { return _value; }
  bool operator==(const EditorDocumentId& other) const noexcept { return _value == other._value; }
  bool operator!=(const EditorDocumentId& other) const noexcept { return !(*this == other); }

private:
  explicit constexpr EditorDocumentId(uint64_t value) noexcept : _value(value) {}
  uint64_t _value = 0;
};

/// \brief The closed result of opening one document.
class OpenDocumentResult final {
public:
  enum class Kind : uint8_t {
    Opened = 0x01,
    /// The URI is not a `file://` document URI whose path maps to a source in the
    /// adapter's crate.
    InvalidUri = 0x02,
    /// A document is already open for this URI's canonical source.
    DuplicateOpen = 0x03,
    /// The document bytes are not valid UTF-8.
    MalformedUtf8 = 0x04,
    /// The overlay inputs could not be committed to the database.
    CommitRejected = 0x05,
  };

  OpenDocumentResult(OpenDocumentResult&&) noexcept = default;
  OpenDocumentResult& operator=(OpenDocumentResult&&) noexcept = default;
  ZC_DISALLOW_COPY(OpenDocumentResult);

  ZC_NODISCARD static OpenDocumentResult opened(EditorDocumentId id) {
    return OpenDocumentResult(Kind::Opened, id);
  }
  ZC_NODISCARD static OpenDocumentResult rejected(Kind kind) {
    return OpenDocumentResult(kind, EditorDocumentId());
  }

  ZC_NODISCARD Kind kind() const noexcept { return kindField; }
  ZC_NODISCARD bool isOpened() const noexcept { return kindField == Kind::Opened; }
  /// \brief The issued document id; valid only on the Opened arm.
  ZC_NODISCARD EditorDocumentId document() const noexcept { return documentField; }

private:
  OpenDocumentResult(Kind kind, EditorDocumentId document) noexcept
      : kindField(kind), documentField(document) {}
  Kind kindField;
  EditorDocumentId documentField;
};

/// \brief The closed result of applying one document change.
enum class ChangeDocumentResult : uint8_t {
  Applied = 0x01,
  /// No open lifecycle matches the id.
  UnknownDocument = 0x02,
  /// The new version does not strictly succeed the current version.
  NonIncreasingVersion = 0x03,
  /// The new bytes are not valid UTF-8.
  MalformedUtf8 = 0x04,
  /// The overlay inputs could not be committed.
  CommitRejected = 0x05,
  /// An incremental edit range is not resolvable to a byte range: a position is
  /// past the addressed line's content, past the last line, splits a UTF-8 scalar
  /// or a UTF-16 surrogate pair, or the end precedes the start.
  InvalidRange = 0x06,
};

/// \brief One LSP incremental content change: a range over the current document
/// text replaced with new UTF-8 text.
///
/// RFC 0023 "IDE Semantic Snapshots": an LSP `didChange` may carry an ordered
/// list of these. Each range is interpreted over the document text produced by
/// applying every preceding edit in the same notification. A position is a
/// zero-based `line` plus a zero-based `character` measured in UTF-16 code units
/// within that line's content (LSP's default position encoding). Line content
/// excludes its terminator; `\r\n`, a bare `\r`, and `\n` each count as one
/// terminator, so a position can never fall between the `\r` and `\n` of a CRLF.
struct LspRangeEdit final {
  /// Zero-based start line.
  uint32_t startLine;
  /// Zero-based start UTF-16 code unit within the start line's content.
  uint32_t startCharacter;
  /// Zero-based end line.
  uint32_t endLine;
  /// Zero-based end UTF-16 code unit within the end line's content.
  uint32_t endCharacter;
  /// The replacement text; must be valid UTF-8.
  zc::ArrayPtr<const uint8_t> newTextUtf8;
};

/// \brief The closed result of closing one document.
enum class CloseDocumentResult : uint8_t {
  Closed = 0x01,
  /// No open lifecycle matches the id.
  UnknownDocument = 0x02,
  /// The overlay inputs could not be erased.
  CommitRejected = 0x03,
};

/// \brief An in-process editor-document lifecycle adapter over one crate.
///
/// RFC 0023 "IDE Semantic Snapshots" (SEAM B, adapter core): owns the
/// process-local document identity and the open/change/close lifecycle, and
/// commits the content-addressed overlay and selection inputs the effective-source
/// query reads. This is the adapter core only: it has no protocol transport (no
/// JSON-RPC, no sockets) and resolves a URI within one injected `CrateKey` rather
/// than a discovered workspace. It applies both whole-document text sync and
/// incremental UTF-16 range edits over its own retained document text, without
/// reaching into the compiler's byte-based `SourceManager`. Transport and
/// workspace discovery are later slices.
///
/// The crate and compilation options are injected, not synthesized: a
/// `StableSourceQueryKey` is crate-scoped, so the adapter maps a URI to a source
/// key deterministically WITHIN the injected crate and never fabricates a crate
/// from a URI. The caller supplies a stable crate (a test fixture, or a resolved
/// workspace crate later).
class EditorDocumentAdapter final {
public:
  /// \brief Builds an adapter that commits documents under one crate.
  ///
  /// \param database The query database overlay and selection inputs commit to.
  /// \param crate The stable crate every opened document is scoped to.
  /// \param options The compilation options committed once for the crate so the
  ///        parse can run; consumed.
  EditorDocumentAdapter(query::QueryDatabase& database, identity::CrateKey&& crate,
                        identity::source_query::CanonicalCompilationOptions&& options);
  ~EditorDocumentAdapter() noexcept;
  EditorDocumentAdapter(EditorDocumentAdapter&&) noexcept;
  EditorDocumentAdapter& operator=(EditorDocumentAdapter&&) noexcept;
  ZC_DISALLOW_COPY(EditorDocumentAdapter);

  /// \brief Opens a document at `uri` with `version` and `utf8Bytes`.
  ///
  /// Maps the `file://` URI to a source key within the adapter's crate, validates
  /// UTF-8, and commits the overlay bytes plus an OpenOverlay selection in one
  /// input transaction. Rejects a URI already open, a non-`file://` or
  /// unmappable URI, and malformed UTF-8 without committing anything.
  ZC_NODISCARD OpenDocumentResult openDocument(zc::StringPtr uri, DocumentVersion version,
                                               zc::ArrayPtr<const uint8_t> utf8Bytes);

  /// \brief Replaces the whole text of an open document.
  ///
  /// Requires `version` to strictly succeed the document's current version and
  /// `newUtf8Bytes` to be valid UTF-8; on success commits the new overlay bytes
  /// and selection in one transaction. Any violation commits nothing.
  ZC_NODISCARD ChangeDocumentResult changeDocument(EditorDocumentId document,
                                                   DocumentVersion version,
                                                   zc::ArrayPtr<const uint8_t> newUtf8Bytes);

  /// \brief Applies an ordered list of incremental range edits to an open
  /// document.
  ///
  /// Requires `version` to strictly succeed the document's current version. Each
  /// edit's range is resolved against the text produced by applying every
  /// preceding edit in `edits` (LSP notification order). A position is resolved by
  /// counting `\r\n`/`\r`/`\n` line terminators to reach the line, then walking
  /// that line's content by UTF-16 code units (one for a BMP scalar, two for an
  /// astral scalar) to reach the character; line content excludes the terminator,
  /// so `character` at the content length denotes the line end. The whole
  /// notification is atomic: if any position is unresolvable (`InvalidRange`), any
  /// `newTextUtf8` is malformed, the final text is malformed UTF-8, or the version
  /// does not increase, nothing is committed and the document is unchanged. On
  /// success commits the resulting overlay bytes and selection in one transaction.
  ZC_NODISCARD ChangeDocumentResult applyIncrementalChange(EditorDocumentId document,
                                                           DocumentVersion version,
                                                           zc::ArrayPtr<const LspRangeEdit> edits);

  /// \brief Closes an open document.
  ///
  /// Erases the overlay and selection inputs in one transaction, so the effective
  /// source falls back to the workspace source (or unavailable when none is
  /// committed), and drops the lifecycle. A later reopen issues a new id.
  ZC_NODISCARD CloseDocumentResult closeDocument(EditorDocumentId document);

  /// \brief Derives the source key one URI maps to within the adapter's crate, or
  /// none when the URI does not normalize to a source in this crate.
  ///
  /// Consumers resolve a semantic snapshot over this key. It does not require the
  /// document to be open, so a consumer can key a workspace read the same way.
  ZC_NODISCARD zc::Maybe<identity::source_query::StableSourceQueryKey> sourceKeyForUri(
      zc::StringPtr uri) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::ide
