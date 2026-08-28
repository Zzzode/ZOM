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

#include "compiler/identity/crypto/sha256.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::cst {

/// \brief The closed lexeme variant tag.
///
/// RFC 0023 "Recoverable Parsing": the lexer emits every source byte through one
/// closed lexeme stream of `Token`, `Trivia`, and `Invalid` lexemes.
enum class CstLexemeTag : uint8_t {
  Token = 0x01,
  Trivia = 0x02,
  Invalid = 0x03,
};

/// \brief The closed trivia kind.
///
/// RFC 0023 declaration order: whitespace, line comment, block comment. Trivia is
/// retained in the lexeme stream and ignored by the parser cursor for grammar
/// decisions, but preserved for lossless reconstruction and formatting.
enum class TriviaKind : uint8_t {
  Whitespace = 0x01,
  LineComment = 0x02,
  BlockComment = 0x03,
};

/// \brief A half-open source byte range `[start, end)`.
struct ByteRange final {
  uint64_t start = 0;
  uint64_t end = 0;

  ZC_NODISCARD uint64_t width() const noexcept { return end - start; }
};

/// \brief One retained source lexeme in the recoverable lexeme stream.
///
/// RFC 0023 "Recoverable Parsing": a `Token` carries a token kind, a `Trivia`
/// carries a trivia kind, and an `Invalid` carries a diagnostic (modeled here as
/// an opaque diagnostic digest; the full `ParserDiagnosticFact` binding is a
/// later slice). Every lexeme carries its byte range and its exact spelling
/// bytes. The record has no public aggregate initializer; the validating
/// factories are the only way to build one, and each enforces that the spelling
/// byte count equals the range width (and, for `Invalid`, a non-empty spelling).
class CstLexeme final {
public:
  CstLexeme(CstLexeme&&) noexcept = default;
  CstLexeme& operator=(CstLexeme&&) noexcept = default;
  ZC_DISALLOW_COPY(CstLexeme);
  ~CstLexeme() noexcept = default;

  /// \brief Builds a significant token lexeme.
  /// \param tokenKind The lexer token kind (an opaque `uint32_t` here so this
  ///        module does not depend on the AST kind enumeration).
  /// \return none when `spelling.size() != range.width()`.
  ZC_NODISCARD static zc::Maybe<CstLexeme> token(uint32_t tokenKind, ByteRange range,
                                                 zc::ArrayPtr<const uint8_t> spelling);

  /// \brief Builds a trivia lexeme.
  /// \return none when `spelling.size() != range.width()`.
  ZC_NODISCARD static zc::Maybe<CstLexeme> trivia(TriviaKind triviaKind, ByteRange range,
                                                  zc::ArrayPtr<const uint8_t> spelling);

  /// \brief Builds an invalid lexeme carrying its diagnostic digest.
  /// \return none when `spelling.size() != range.width()` or the spelling is empty.
  ZC_NODISCARD static zc::Maybe<CstLexeme> invalid(ByteRange range,
                                                   zc::ArrayPtr<const uint8_t> spelling,
                                                   const identity::Sha256Digest& diagnosticDigest);

  ZC_NODISCARD CstLexemeTag tag() const noexcept { return tagValue; }
  ZC_NODISCARD ByteRange range() const noexcept { return rangeValue; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> spelling() const noexcept {
    return spellingValue.asPtr();
  }

  /// \brief The token kind; valid only for a `Token` lexeme.
  ZC_NODISCARD uint32_t tokenKind() const noexcept { return kindValue; }
  /// \brief The trivia kind; valid only for a `Trivia` lexeme.
  ZC_NODISCARD TriviaKind triviaKind() const noexcept { return static_cast<TriviaKind>(kindValue); }
  /// \brief The diagnostic digest; valid only for an `Invalid` lexeme.
  ZC_NODISCARD const identity::Sha256Digest& diagnosticDigest() const noexcept {
    return diagnosticValue;
  }

  ZC_NODISCARD CstLexeme clone() const;

private:
  CstLexeme(CstLexemeTag tag, uint32_t kind, ByteRange range, zc::Array<uint8_t>&& spelling,
            const identity::Sha256Digest& diagnostic) noexcept
      : tagValue(tag),
        kindValue(kind),
        rangeValue(range),
        spellingValue(zc::mv(spelling)),
        diagnosticValue(diagnostic) {}

  CstLexemeTag tagValue;
  uint32_t kindValue;
  ByteRange rangeValue;
  zc::Array<uint8_t> spellingValue;
  identity::Sha256Digest diagnosticValue;
};

/// \brief The domain-separated immutable identity of one verified lexeme stream.
///
/// Computed by `LexemeStreamCodec` as SHA-256 over the stream's canonical
/// preimage and compared by digest.
class LexemeStreamId final {
public:
  constexpr LexemeStreamId() noexcept = default;

  ZC_NODISCARD static LexemeStreamId fromDigest(const identity::Sha256Digest& digest) noexcept {
    return LexemeStreamId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const LexemeStreamId& other) const noexcept { return value == other.value; }
  bool operator!=(const LexemeStreamId& other) const noexcept { return !(*this == other); }

private:
  explicit LexemeStreamId(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief The closed lexeme-partition verification failure algebra.
///
/// RFC 0023 "Recoverable Parsing" invariants (L529-532). Each rejection names the
/// exact violated invariant; the verifier publishes no partial stream.
enum class LexemePartitionFailure : uint8_t {
  /// A non-empty source contributed zero lexemes, or an empty source declared a
  /// non-zero byte count or carried lexemes.
  EmptyPartition = 0x01,
  /// A lexeme's spelling byte count did not equal its range width.
  SpellingWidthMismatch = 0x02,
  /// Lexemes were not in ascending, gapless, non-overlapping range order.
  NonAdjacentPartition = 0x03,
  /// The partition did not cover exactly `[0, sourceByteCount)`.
  IncompleteCoverage = 0x04,
  /// The SHA-256 of the concatenated spellings did not equal `contentDigest`.
  ContentDigestMismatch = 0x05,
};

/// \brief A lexeme stream whose partition invariants an independent verifier proved.
///
/// RFC 0023 "Recoverable Parsing": the verified stream stores the source content
/// digest, the source byte count, the ordered lexemes, and a `LexemeStreamId`. It
/// has no public aggregate initializer; only `LexemePartitionVerifier::verify`
/// constructs one, so a stream cannot be assembled from unverified lexemes.
///
/// This foundation slice models and verifies the lexeme partition as pure data
/// and computes its deterministic identity. It does not run the live lexer or
/// parser, build a `RecoverableSyntaxTree`, or replace `ast::Tree` construction;
/// those are later RFC 0023 slices.
class VerifiedLexemeStream final {
public:
  VerifiedLexemeStream(VerifiedLexemeStream&&) noexcept = default;
  VerifiedLexemeStream& operator=(VerifiedLexemeStream&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedLexemeStream);
  ~VerifiedLexemeStream() noexcept = default;

  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept {
    return contentDigestValue;
  }
  ZC_NODISCARD uint64_t sourceByteCount() const noexcept { return sourceByteCountValue; }
  ZC_NODISCARD zc::ArrayPtr<const CstLexeme> lexemes() const noexcept {
    return lexemeValues.asPtr();
  }
  ZC_NODISCARD const LexemeStreamId& id() const noexcept { return idValue; }

private:
  friend class LexemePartitionVerifier;

  VerifiedLexemeStream(const identity::Sha256Digest& contentDigest, uint64_t sourceByteCount,
                       zc::Array<CstLexeme>&& lexemes, const LexemeStreamId& id) noexcept
      : contentDigestValue(contentDigest),
        sourceByteCountValue(sourceByteCount),
        lexemeValues(zc::mv(lexemes)),
        idValue(id) {}

  identity::Sha256Digest contentDigestValue;
  uint64_t sourceByteCountValue;
  zc::Array<CstLexeme> lexemeValues;
  LexemeStreamId idValue;
};

/// \brief The result of verifying a lexeme stream request.
using LexemeStreamResult = zc::OneOf<VerifiedLexemeStream, LexemePartitionFailure>;

/// \brief The unverified request an independent verifier turns into a stream.
struct LexemeStreamRequest final {
  identity::Sha256Digest contentDigest;
  uint64_t sourceByteCount = 0;
  zc::Array<CstLexeme> lexemes;
};

/// \brief Canonical codec for the verified lexeme stream.
///
/// The preimage is a domain-separated, length-framed encoding:
///   ASCII("zom.cst-lexemes") 0x00
///   Frame(contentDigest)
///   uint64(sourceByteCount)
///   uint64(lexemeCount)
///   for each lexeme: uint8(tag) uint32(kindOrZero) uint64(rangeStart)
///     uint64(rangeEnd) Frame(spelling) Frame(diagnosticDigestOrEmpty)
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes. The
/// `kindOrZero` field is the token kind for a `Token`, the trivia kind for a
/// `Trivia`, and zero for an `Invalid`; the diagnostic frame is the digest bytes
/// for an `Invalid` and empty otherwise.
class LexemeStreamCodec final {
public:
  /// \brief Encodes a verified stream to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const VerifiedLexemeStream& stream);
  /// \brief Computes the stream's `LexemeStreamId` (SHA-256 of the preimage).
  ZC_NODISCARD static LexemeStreamId computeId(const VerifiedLexemeStream& stream);
};

/// \brief The independent verifier that constructs a `VerifiedLexemeStream`.
///
/// RFC 0023 "Recoverable Parsing" (L529-532): lexeme ranges form an exact
/// adjacent partition of `[0, sourceByteCount)` with no overlap or gap, every
/// non-empty source contributes at least one lexeme, concatenating spellings
/// byte-for-byte reconstructs the source, and the spelling digest equals
/// `contentDigest`. Rejection consumes the request and publishes no partial
/// stream.
class LexemePartitionVerifier final {
public:
  /// \brief Verifies the request and, on success, constructs the stream.
  /// \param request The lexeme stream request, consumed on every branch.
  /// \return A verified stream, or the violated `LexemePartitionFailure`.
  ZC_NODISCARD static LexemeStreamResult verify(LexemeStreamRequest&& request);
};

}  // namespace zomlang::compiler::cst
