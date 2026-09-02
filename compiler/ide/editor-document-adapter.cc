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

#include "compiler/ide/editor-document-adapter.h"

#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/package-key.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/identity/source-snapshot.h"
#include "zc/core/encoding.h"
#include "zc/core/hash.h"
#include "zc/core/map.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ide {
namespace {

namespace sq = identity::source_query;

// Whether `bytes` is valid UTF-8. RFC 0023 requires UTF-8 authoritative source
// bytes; the adapter rejects malformed UTF-8 at the input boundary. zc's UTF-16
// transcoder reports any ill-formed input through hadErrors.
bool isValidUtf8(zc::ArrayPtr<const uint8_t> bytes) {
  auto chars = zc::arrayPtr(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
  return !zc::encodeUtf16(chars).hadErrors;
}

// Resolves an LSP position to a byte offset in `text`, which MUST be valid UTF-8.
// The position is a zero-based `line` plus a zero-based `character` measured in
// UTF-16 code units within that line's content (LSP's default position encoding).
//
// Line terminators are `\r\n`, a bare `\r`, and `\n`; each counts as exactly one
// terminator, and `\r\n` is atomic (a position can never fall between its `\r`
// and `\n`). A line's content excludes its terminator, so `character` equal to
// the content's UTF-16 length denotes the line end (the byte before the
// terminator, or end of text on the last line). The empty line after a trailing
// terminator is addressable.
//
// Returns none when the position is unresolvable: `line` past the last line,
// `character` past the line content's UTF-16 length, or `character` splitting an
// astral scalar's surrogate pair (an astral scalar spans two UTF-16 units, and a
// position between them names no byte). Complexity is O(offset) in the resolved
// byte offset.
zc::Maybe<size_t> positionToByteOffset(zc::ArrayPtr<const uint8_t> text, uint32_t line,
                                       uint32_t character) {
  // Advance to the byte where `line` begins by counting terminators.
  size_t offset = 0;
  for (uint32_t currentLine = 0; currentLine < line;) {
    if (offset >= text.size()) { return zc::none; }  // line past the last line
    const uint8_t byte = text[offset];
    if (byte == '\r') {
      ++offset;
      if (offset < text.size() && text[offset] == '\n') { ++offset; }  // CRLF is atomic
      ++currentLine;
    } else if (byte == '\n') {
      ++offset;
      ++currentLine;
    } else {
      ++offset;
    }
  }
  // Walk the line's content by UTF-16 code units, stopping at the terminator.
  for (uint32_t remaining = character; remaining > 0;) {
    if (offset >= text.size()) { return zc::none; }  // character past line content (end of text)
    const uint8_t lead = text[offset];
    if (lead == '\r' || lead == '\n') { return zc::none; }  // character past line content
    size_t scalarBytes;
    uint32_t utf16Units;
    if (lead < 0x80) {
      scalarBytes = 1;
      utf16Units = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      scalarBytes = 2;
      utf16Units = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      scalarBytes = 3;
      utf16Units = 1;
    } else {
      scalarBytes = 4;  // (lead & 0xF8) == 0xF0: an astral scalar
      utf16Units = 2;   // encoded as a UTF-16 surrogate pair
    }
    if (remaining < utf16Units) { return zc::none; }  // splits a surrogate pair
    remaining -= utf16Units;
    offset += scalarBytes;
  }
  return offset;
}

// Maps a `file://` document URI to path segments under the crate. LSP document
// URIs are `file://<authority>/<path>`; `CanonicalUrl` is scoped to package-source
// `https` URLs and rejects `file`, so this parses the file URI directly.
//
// Security: the mapping must be injective enough that two distinct URIs never
// alias to the same StableSourceQueryKey. So it rejects, deterministically:
//   - a non-empty authority other than `localhost` (RFC 8089 admits only empty or
//     `localhost` for a local file; dropping a remote authority would alias
//     `file://host/p` onto `file:///p`);
//   - a path that is not absolute (no leading `/` after the authority);
//   - any percent-encoding (`%`): this parser does not percent-decode, so
//     admitting `%2e`/`%2E` would alias with a future decoder and could smuggle a
//     `.`/`..` traversal segment. Percent-decoding is a later tightening;
//   - `.`, `..`, and empty non-trailing segments (rejected by
//     `CanonicalPathSegment::fromCanonical`, which also forbids `/`, `\`, and NUL).
// A URI that yields no admissible segment maps to no source key. This admits an
// already-plain absolute local file URI only.
zc::Maybe<zc::Vector<identity::CanonicalPathSegment>> filePathSegments(zc::StringPtr uri) {
  constexpr zc::StringPtr kFileScheme = "file://"_zc;
  if (uri.size() < kFileScheme.size()) { return zc::none; }
  for (size_t index = 0; index < kFileScheme.size(); ++index) {
    if (uri[index] != kFileScheme[index]) { return zc::none; }
  }
  const char* end = uri.end();
  const char* cursor = uri.begin() + kFileScheme.size();
  // Authority runs to the next '/'. Only an empty authority (`file:///path`) or
  // `localhost` (`file://localhost/path`) names the local host; any other
  // authority is a remote reference this adapter does not admit, and dropping it
  // would alias distinct hosts onto one key.
  const char* authorityEnd = cursor;
  while (authorityEnd < end && *authorityEnd != '/') { ++authorityEnd; }
  if (authorityEnd >= end) { return zc::none; }  // no path
  const size_t authorityLength = static_cast<size_t>(authorityEnd - cursor);
  if (authorityLength != 0) {
    // Compare bytes (the slice is not NUL-terminated): only `localhost` names the
    // local host.
    constexpr zc::StringPtr kLocalhost = "localhost"_zc;
    if (authorityLength != kLocalhost.size() ||
        zc::arrayPtr(cursor, authorityLength) != kLocalhost.asArray()) {
      return zc::none;
    }
  }
  const char* pathCursor = authorityEnd + 1;

  zc::Vector<identity::CanonicalPathSegment> segments;
  const char* segStart = pathCursor;
  auto flush = [&](const char* segEnd) -> bool {
    if (segEnd <= segStart) { return true; }  // skip empty (e.g. trailing slash)
    // Reject percent-encoding outright: this parser does not decode, so an
    // encoded segment would alias with a decoding parser and could hide a
    // traversal segment. A later tightening adds percent-decoding.
    for (const char* p = segStart; p < segEnd; ++p) {
      if (*p == '%') { return false; }
    }
    // fromCanonical needs a NUL-terminated StringPtr; the URI slice is not, so
    // copy the segment into an owned string first. It rejects `.`, `..`, empty,
    // and any segment containing `/`, `\`, or NUL.
    auto owned = zc::heapString(zc::arrayPtr(segStart, static_cast<size_t>(segEnd - segStart)));
    auto segment = identity::CanonicalPathSegment::fromCanonical(owned);
    ZC_IF_SOME(value, segment) {
      segments.add(zc::mv(value));
      return true;
    }
    return false;
  };
  for (const char* p = pathCursor; p < end; ++p) {
    if (*p == '/') {
      if (!flush(p)) { return zc::none; }
      segStart = p + 1;
    }
  }
  if (!flush(end)) { return zc::none; }
  if (segments.size() == 0) { return zc::none; }
  return zc::mv(segments);
}

}  // namespace

struct EditorDocumentAdapter::Impl final {
  struct OpenState {
    EditorDocumentId id;
    sq::StableSourceQueryKey sourceKey;
    DocumentVersion version;
    // The current document bytes (valid UTF-8), retained so incremental range
    // edits resolve positions against the live text.
    zc::Array<uint8_t> text;
  };

  Impl(query::QueryDatabase& database, identity::CrateKey&& crate,
       sq::CanonicalCompilationOptions&& options)
      : database(database),
        crate(zc::mv(crate)),
        options(zc::mv(options)),
        openBySourceBytes(),
        openById() {}

  query::QueryDatabase& database;
  identity::CrateKey crate;
  sq::CanonicalCompilationOptions options;
  bool optionsCommitted = false;
  uint64_t nextId = 1;
  // Canonical source-key bytes (hex) -> open state, enforcing one open lifecycle
  // per source. Id -> the same source-key bytes for reverse lookup.
  zc::TreeMap<zc::String, OpenState> openBySourceBytes;
  zc::TreeMap<uint64_t, zc::String> openById;

  ZC_NODISCARD zc::Maybe<sq::StableSourceQueryKey> sourceKeyForUri(zc::StringPtr uri) const {
    auto segments = filePathSegments(uri);
    if (segments == zc::none) { return zc::none; }
    auto path =
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(ZC_ASSERT_NONNULL(segments)));
    auto sourceFile = identity::SourceFileKey::from(
        crate.clone(), identity::SourceOriginKey::localFile(zc::mv(path)));
    return sq::StableSourceQueryKey::fromVerified(sourceFile);
  }

  ZC_NODISCARD zc::Maybe<sq::CanonicalSourceSnapshot> overlaySnapshot(
      const sq::StableSourceQueryKey& sourceKey, zc::ArrayPtr<const uint8_t> bytes) {
    // Rebuild the SourceFileKey the snapshot must bind to from the source key's
    // canonical bytes.
    identity::CanonicalDecoder decoder(sourceKey.canonicalSourceBytes());
    auto sourceFile = identity::SourceFileKey::decodeCanonical(decoder);
    if (sourceFile == zc::none || !decoder.finished()) { return zc::none; }
    auto immutable = identity::ImmutableSourceSnapshot::from(ZC_ASSERT_NONNULL(sourceFile).clone(),
                                                             zc::heapArray<uint8_t>(bytes));
    if (immutable == zc::none) { return zc::none; }
    return sq::CanonicalSourceSnapshot::fromVerified(ZC_ASSERT_NONNULL(immutable));
  }

  // Commits the overlay bytes plus an OpenOverlay selection (and the crate options
  // once) in one input transaction. Returns false without committing anything on
  // any staging or commit failure.
  ZC_NODISCARD bool commitOverlay(const sq::StableSourceQueryKey& sourceKey,
                                  const sq::CanonicalSourceSnapshot& snapshot) {
    auto opened = database.beginInputTransaction(database.snapshot().revision());
    if (!opened.isOpened()) { return false; }
    auto write = zc::mv(opened).takeTransaction();
    if (!optionsCommitted) {
      if (!write.set<sq::CompilationOptionsInput>(crate, options).isApplied()) { return false; }
    }
    if (!write.set<sq::EditorDocumentInput>(sourceKey, snapshot).isApplied()) { return false; }
    auto selection = sq::IdeSourceSelection::openOverlay(snapshot.contentDigest());
    if (!write.set<sq::IdeSourceSelectionInput>(sourceKey, selection).isApplied()) { return false; }
    if (!write.commit().isCommitted()) { return false; }
    optionsCommitted = true;
    return true;
  }

  // Erases the overlay and selection for one source in one transaction so the
  // effective source falls back to the workspace input.
  ZC_NODISCARD bool eraseOverlay(const sq::StableSourceQueryKey& sourceKey) {
    auto opened = database.beginInputTransaction(database.snapshot().revision());
    if (!opened.isOpened()) { return false; }
    auto write = zc::mv(opened).takeTransaction();
    if (!write.erase<sq::EditorDocumentInput>(sourceKey).isApplied()) { return false; }
    if (!write.erase<sq::IdeSourceSelectionInput>(sourceKey).isApplied()) { return false; }
    return write.commit().isCommitted();
  }

  // Applies `edits` in notification order to `current`, each edit's range
  // resolved against the text produced by the preceding edits. Returns the new
  // text on success, or the closed reason a step failed on (InvalidRange for an
  // unresolvable or inverted range, MalformedUtf8 for malformed replacement text
  // or a malformed final result). Commits nothing; the caller owns the commit.
  ZC_NODISCARD zc::OneOf<zc::Array<uint8_t>, ChangeDocumentResult> applyEdits(
      zc::ArrayPtr<const uint8_t> current, zc::ArrayPtr<const LspRangeEdit> edits) {
    zc::Array<uint8_t> candidate = zc::heapArray<uint8_t>(current);
    for (const auto& edit : edits) {
      auto start = positionToByteOffset(candidate, edit.startLine, edit.startCharacter);
      auto end = positionToByteOffset(candidate, edit.endLine, edit.endCharacter);
      if (start == zc::none || end == zc::none) { return ChangeDocumentResult::InvalidRange; }
      const size_t startOffset = ZC_ASSERT_NONNULL(start);
      const size_t endOffset = ZC_ASSERT_NONNULL(end);
      if (endOffset < startOffset) { return ChangeDocumentResult::InvalidRange; }
      if (!isValidUtf8(edit.newTextUtf8)) { return ChangeDocumentResult::MalformedUtf8; }
      zc::Vector<uint8_t> next(candidate.size() - (endOffset - startOffset) +
                               edit.newTextUtf8.size());
      next.addAll(candidate.slice(0, startOffset));
      next.addAll(edit.newTextUtf8);
      next.addAll(candidate.slice(endOffset, candidate.size()));
      candidate = next.releaseAsArray();
    }
    if (!isValidUtf8(candidate)) { return ChangeDocumentResult::MalformedUtf8; }
    return zc::mv(candidate);
  }
};

EditorDocumentAdapter::EditorDocumentAdapter(query::QueryDatabase& database,
                                             identity::CrateKey&& crate,
                                             sq::CanonicalCompilationOptions&& options)
    : impl(zc::heap<Impl>(database, zc::mv(crate), zc::mv(options))) {}
EditorDocumentAdapter::~EditorDocumentAdapter() noexcept = default;
EditorDocumentAdapter::EditorDocumentAdapter(EditorDocumentAdapter&&) noexcept = default;
EditorDocumentAdapter& EditorDocumentAdapter::operator=(EditorDocumentAdapter&&) noexcept = default;

zc::Maybe<sq::StableSourceQueryKey> EditorDocumentAdapter::sourceKeyForUri(
    zc::StringPtr uri) const {
  return impl->sourceKeyForUri(uri);
}

OpenDocumentResult EditorDocumentAdapter::openDocument(zc::StringPtr uri, DocumentVersion version,
                                                       zc::ArrayPtr<const uint8_t> utf8Bytes) {
  auto sourceKey = impl->sourceKeyForUri(uri);
  if (sourceKey == zc::none) {
    return OpenDocumentResult::rejected(OpenDocumentResult::Kind::InvalidUri);
  }
  auto keyHex = zc::encodeHex(ZC_ASSERT_NONNULL(sourceKey).canonicalSourceBytes());
  if (impl->openBySourceBytes.find(keyHex) != zc::none) {
    return OpenDocumentResult::rejected(OpenDocumentResult::Kind::DuplicateOpen);
  }
  if (!isValidUtf8(utf8Bytes)) {
    return OpenDocumentResult::rejected(OpenDocumentResult::Kind::MalformedUtf8);
  }
  auto snapshot = impl->overlaySnapshot(ZC_ASSERT_NONNULL(sourceKey), utf8Bytes);
  if (snapshot == zc::none) {
    return OpenDocumentResult::rejected(OpenDocumentResult::Kind::CommitRejected);
  }
  if (!impl->commitOverlay(ZC_ASSERT_NONNULL(sourceKey), ZC_ASSERT_NONNULL(snapshot))) {
    return OpenDocumentResult::rejected(OpenDocumentResult::Kind::CommitRejected);
  }
  const auto id = EditorDocumentId::fromRaw(impl->nextId++);
  impl->openBySourceBytes.insert(zc::heapString(keyHex),
                                 Impl::OpenState{id, ZC_ASSERT_NONNULL(sourceKey).clone(), version,
                                                 zc::heapArray<uint8_t>(utf8Bytes)});
  impl->openById.insert(id.raw(), zc::heapString(keyHex));
  return OpenDocumentResult::opened(id);
}

ChangeDocumentResult EditorDocumentAdapter::changeDocument(
    EditorDocumentId document, DocumentVersion version, zc::ArrayPtr<const uint8_t> newUtf8Bytes) {
  auto keyHexEntry = impl->openById.find(document.raw());
  if (keyHexEntry == zc::none) { return ChangeDocumentResult::UnknownDocument; }
  const zc::String& keyHex = ZC_ASSERT_NONNULL(keyHexEntry);
  auto& state = ZC_ASSERT_NONNULL(impl->openBySourceBytes.find(keyHex));
  if (!version.succeeds(state.version)) { return ChangeDocumentResult::NonIncreasingVersion; }
  if (!isValidUtf8(newUtf8Bytes)) { return ChangeDocumentResult::MalformedUtf8; }
  auto snapshot = impl->overlaySnapshot(state.sourceKey, newUtf8Bytes);
  if (snapshot == zc::none) { return ChangeDocumentResult::CommitRejected; }
  if (!impl->commitOverlay(state.sourceKey, ZC_ASSERT_NONNULL(snapshot))) {
    return ChangeDocumentResult::CommitRejected;
  }
  state.version = version;
  state.text = zc::heapArray<uint8_t>(newUtf8Bytes);
  return ChangeDocumentResult::Applied;
}

ChangeDocumentResult EditorDocumentAdapter::applyIncrementalChange(
    EditorDocumentId document, DocumentVersion version, zc::ArrayPtr<const LspRangeEdit> edits) {
  auto keyHexEntry = impl->openById.find(document.raw());
  if (keyHexEntry == zc::none) { return ChangeDocumentResult::UnknownDocument; }
  const zc::String& keyHex = ZC_ASSERT_NONNULL(keyHexEntry);
  auto& state = ZC_ASSERT_NONNULL(impl->openBySourceBytes.find(keyHex));
  // Enforce the version gate before touching text, so a stale notification never
  // pays for edit resolution and never mutates state.
  if (!version.succeeds(state.version)) { return ChangeDocumentResult::NonIncreasingVersion; }
  auto applied = impl->applyEdits(state.text, edits);
  if (applied.is<ChangeDocumentResult>()) { return applied.get<ChangeDocumentResult>(); }
  zc::Array<uint8_t> candidate = zc::mv(applied.get<zc::Array<uint8_t>>());
  auto snapshot = impl->overlaySnapshot(state.sourceKey, candidate);
  if (snapshot == zc::none) { return ChangeDocumentResult::CommitRejected; }
  if (!impl->commitOverlay(state.sourceKey, ZC_ASSERT_NONNULL(snapshot))) {
    return ChangeDocumentResult::CommitRejected;
  }
  state.version = version;
  state.text = zc::mv(candidate);
  return ChangeDocumentResult::Applied;
}

CloseDocumentResult EditorDocumentAdapter::closeDocument(EditorDocumentId document) {
  auto keyHexEntry = impl->openById.find(document.raw());
  if (keyHexEntry == zc::none) { return CloseDocumentResult::UnknownDocument; }
  const zc::String keyHex = zc::heapString(ZC_ASSERT_NONNULL(keyHexEntry));
  auto& state = ZC_ASSERT_NONNULL(impl->openBySourceBytes.find(keyHex));
  if (!impl->eraseOverlay(state.sourceKey)) { return CloseDocumentResult::CommitRejected; }
  impl->openById.erase(document.raw());
  impl->openBySourceBytes.erase(keyHex);
  return CloseDocumentResult::Closed;
}

}  // namespace zomlang::compiler::ide
