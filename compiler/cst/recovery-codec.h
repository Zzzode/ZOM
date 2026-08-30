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

#include "compiler/cst/lexeme-codec.h"
#include "compiler/identity/crypto/sha256.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::cst {

/// \brief The closed recovery-element variant tag.
///
/// RFC 0023 "Recoverable Parsing": a recovery element records a missing token, a
/// missing subtree, or a contiguous run of skipped significant lexemes. Recovery
/// elements consume no additional source spelling; they annotate the lexeme
/// stream produced by the recoverable parser.
enum class RecoveryElementTag : uint8_t {
  MissingToken = 0x01,
  MissingSubtree = 0x02,
  SkippedTokens = 0x03,
};

/// \brief The closed parser syntax category used by `MissingSubtree` recovery.
enum class RecoverySyntaxCategory : uint32_t {
  SourceFile = 0x01,
  Declaration = 0x02,
  Statement = 0x03,
  Expression = 0x04,
  Type = 0x05,
  Pattern = 0x06,
};

/// \brief One recovery annotation over a verified lexeme stream.
///
/// RFC 0023 "Recoverable Parsing":
///   - `MissingToken{expected, anchor}` records one or more expected token kinds
///     at a zero-width anchor offset. `expected` is a non-empty, strictly
///     ascending sequence of opaque token-kind codes.
///   - `MissingSubtree{expected, anchor}` records an expected syntax category at
///     a zero-width anchor offset. The category is an opaque code.
///   - `SkippedTokens{firstLexeme, lexemeCount, range}` references a contiguous
///     non-empty run of already-retained lexemes; the run must contain at least
///     one `Token` or `Invalid`, and `range` must equal the run's covering range.
///
/// Token kinds and syntax categories are opaque `uint32_t` codes so this module
/// does not depend on the AST kind enumeration or invent a syntax-category
/// enumeration. The record has no public aggregate initializer; the validating
/// factories are the only way to build one.
class RecoveryElement final {
public:
  RecoveryElement(RecoveryElement&&) noexcept = default;
  RecoveryElement& operator=(RecoveryElement&&) noexcept = default;
  ZC_DISALLOW_COPY(RecoveryElement);
  ~RecoveryElement() noexcept = default;

  /// \brief Builds a missing-token element.
  /// \return none when `expected` is empty or not strictly ascending.
  ZC_NODISCARD static zc::Maybe<RecoveryElement> missingToken(zc::ArrayPtr<const uint32_t> expected,
                                                              uint64_t anchor);

  /// \brief Builds a missing-subtree element.
  ZC_NODISCARD static RecoveryElement missingSubtree(uint32_t expectedCategory, uint64_t anchor);
  ZC_NODISCARD static RecoveryElement missingSubtree(RecoverySyntaxCategory expectedCategory,
                                                     uint64_t anchor) {
    return missingSubtree(static_cast<uint32_t>(expectedCategory), anchor);
  }

  /// \brief Builds a skipped-tokens element.
  /// \return none when `lexemeCount` is zero (an empty run is not a skip) or
  ///         when `range.end < range.start`.
  ZC_NODISCARD static zc::Maybe<RecoveryElement> skippedTokens(uint32_t firstLexeme,
                                                               uint32_t lexemeCount,
                                                               ByteRange range);

  ZC_NODISCARD RecoveryElementTag tag() const noexcept { return tagValue; }
  /// \brief The zero-width anchor; valid for `MissingToken` and `MissingSubtree`.
  ZC_NODISCARD uint64_t anchor() const noexcept { return anchorValue; }
  /// \brief The expected token-kind codes; valid for `MissingToken`.
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> expectedTokens() const noexcept {
    return expectedTokenValues.asPtr();
  }
  /// \brief The expected syntax-category code; valid for `MissingSubtree`.
  ZC_NODISCARD uint32_t expectedCategory() const noexcept { return expectedCategoryValue; }
  /// \brief The first referenced lexeme index; valid for `SkippedTokens`.
  ZC_NODISCARD uint32_t firstLexeme() const noexcept { return firstLexemeValue; }
  /// \brief The referenced lexeme count; valid for `SkippedTokens`.
  ZC_NODISCARD uint32_t lexemeCount() const noexcept { return lexemeCountValue; }
  /// \brief The covering range; valid for `SkippedTokens`.
  ZC_NODISCARD ByteRange range() const noexcept { return rangeValue; }

  /// \brief The byte offset the canonical recovery order sorts on first: the
  /// anchor for a missing element, and the covering range start for a skip.
  ZC_NODISCARD uint64_t sortAnchor() const noexcept {
    return tagValue == RecoveryElementTag::SkippedTokens ? rangeValue.start : anchorValue;
  }

  /// \brief Orders elements by the RFC 0023 canonical recovery order: anchor byte
  /// offset, variant tag, expected token or category tag, then skipped-token
  /// range. Returns <0, 0, or >0.
  ZC_NODISCARD int compareCanonical(const RecoveryElement& other) const noexcept;

  ZC_NODISCARD RecoveryElement clone() const;

private:
  RecoveryElement(RecoveryElementTag tag, uint64_t anchor, zc::Array<uint32_t>&& expectedTokens,
                  uint32_t expectedCategory, uint32_t firstLexeme, uint32_t lexemeCount,
                  ByteRange range) noexcept
      : tagValue(tag),
        anchorValue(anchor),
        expectedTokenValues(zc::mv(expectedTokens)),
        expectedCategoryValue(expectedCategory),
        firstLexemeValue(firstLexeme),
        lexemeCountValue(lexemeCount),
        rangeValue(range) {}

  RecoveryElementTag tagValue;
  uint64_t anchorValue;
  zc::Array<uint32_t> expectedTokenValues;
  uint32_t expectedCategoryValue;
  uint32_t firstLexemeValue;
  uint32_t lexemeCountValue;
  ByteRange rangeValue;
};

/// \brief The domain-separated immutable identity of one verified recovery sequence.
class RecoverySequenceId final {
public:
  constexpr RecoverySequenceId() noexcept = default;

  ZC_NODISCARD static RecoverySequenceId fromDigest(const identity::Sha256Digest& digest) noexcept {
    return RecoverySequenceId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const RecoverySequenceId& other) const noexcept { return value == other.value; }
  bool operator!=(const RecoverySequenceId& other) const noexcept { return !(*this == other); }

private:
  explicit RecoverySequenceId(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief The closed recovery-sequence verification failure algebra.
///
/// RFC 0023 "Recoverable Parsing" (L541-551). Each rejection names the exact
/// violated invariant; the verifier publishes no partial sequence.
enum class RecoveryFailure : uint8_t {
  /// A `SkippedTokens` run references a lexeme index outside the stream.
  SkippedRunOutOfRange = 0x01,
  /// A `SkippedTokens` run contains no `Token` or `Invalid` lexeme.
  SkippedRunNoSignificant = 0x02,
  /// A `SkippedTokens` range does not equal its run's covering range.
  SkippedRangeMismatch = 0x03,
  /// A missing-element anchor lies past the source byte count.
  AnchorOutOfRange = 0x04,
  /// Elements were not in strictly ascending canonical recovery order.
  UnsortedSequence = 0x05,
  /// Two elements compared canonically equal (equal recovery elements forbidden).
  DuplicateElement = 0x06,
};

/// \brief A recovery sequence whose invariants an independent verifier proved
/// against a verified lexeme stream.
///
/// RFC 0023 "Recoverable Parsing": the verified sequence binds to the lexeme
/// stream by its `LexemeStreamId`, stores the ordered recovery elements, and
/// carries a `RecoverySequenceId`. It has no public aggregate initializer; only
/// `RecoverySequenceVerifier::verify` constructs one.
///
/// The production parser binds this verified sequence into
/// `RecoverableSyntaxTree`. Explicit recovery-element production for malformed
/// syntax remains fail-closed; clean parses bind the verified empty sequence.
class VerifiedRecoverySequence final {
public:
  VerifiedRecoverySequence(VerifiedRecoverySequence&&) noexcept = default;
  VerifiedRecoverySequence& operator=(VerifiedRecoverySequence&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedRecoverySequence);
  ~VerifiedRecoverySequence() noexcept = default;

  ZC_NODISCARD const LexemeStreamId& lexemeStreamId() const noexcept { return streamIdValue; }
  ZC_NODISCARD zc::ArrayPtr<const RecoveryElement> elements() const noexcept {
    return elementValues.asPtr();
  }
  ZC_NODISCARD const RecoverySequenceId& id() const noexcept { return idValue; }

private:
  friend class RecoverySequenceVerifier;

  VerifiedRecoverySequence(const LexemeStreamId& streamId, zc::Array<RecoveryElement>&& elements,
                           const RecoverySequenceId& id) noexcept
      : streamIdValue(streamId), elementValues(zc::mv(elements)), idValue(id) {}

  LexemeStreamId streamIdValue;
  zc::Array<RecoveryElement> elementValues;
  RecoverySequenceId idValue;
};

/// \brief The result of verifying a recovery sequence request.
using RecoverySequenceResult = zc::OneOf<VerifiedRecoverySequence, RecoveryFailure>;

/// \brief Canonical codec for the verified recovery sequence.
///
/// The preimage is a domain-separated, length-framed encoding:
///   ASCII("zom.cst-recovery") 0x00
///   Frame(lexemeStreamId digest)
///   uint64(elementCount)
///   for each element: uint8(tag) and the variant fields in declaration order
///     (MissingToken: uint64(anchor) uint64(expectedCount) [uint32(kind)...];
///      MissingSubtree: uint64(anchor) uint32(category);
///      SkippedTokens: uint32(firstLexeme) uint32(lexemeCount)
///        uint64(rangeStart) uint64(rangeEnd)).
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes.
class RecoverySequenceCodec final {
public:
  /// \brief Encodes a verified recovery sequence to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const VerifiedRecoverySequence& sequence);
  /// \brief Computes the sequence's `RecoverySequenceId` (SHA-256 of the preimage).
  ZC_NODISCARD static RecoverySequenceId computeId(const VerifiedRecoverySequence& sequence);
};

/// \brief The independent verifier that constructs a `VerifiedRecoverySequence`.
///
/// RFC 0023 "Recoverable Parsing" (L541-551): every `SkippedTokens` run is a
/// contiguous non-empty sequence of already-retained lexemes containing at least
/// one `Token` or `Invalid`, with a covering range; missing-element anchors are
/// zero-width and in range; the sequence is strictly ascending in the canonical
/// recovery order; and equal recovery elements are forbidden. Rejection consumes
/// the request and publishes no partial sequence.
class RecoverySequenceVerifier final {
public:
  /// \brief Verifies `elements` against `stream` and, on success, constructs the
  /// sequence. The elements are consumed on every branch.
  ZC_NODISCARD static RecoverySequenceResult verify(const VerifiedLexemeStream& stream,
                                                    zc::Array<RecoveryElement>&& elements);
};

}  // namespace zomlang::compiler::cst
