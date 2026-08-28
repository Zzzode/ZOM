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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 recoverable-parsing recovery slice: prove the RecoveryElement model
// and its independent verifier are deterministic and fail closed on each of the
// RFC 0023 recovery invariants (L541-551): a SkippedTokens run is a contiguous
// non-empty sequence of retained lexemes containing at least one Token or
// Invalid whose range equals the covering range; missing-element anchors are
// zero-width and in range; the sequence is strictly ascending in the canonical
// recovery order; and equal recovery elements are forbidden. This slice models
// and verifies the recovery sequence as pure data over a verified lexeme stream;
// it runs no live parser and binds no diagnostics.

#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/recovery-codec.h"
#include "compiler/identity/crypto/sha256.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::cst {
namespace {

identity::Sha256Digest digestOf(zc::StringPtr text) {
  auto digest = identity::sha256(text.asBytes());
  ZC_REQUIRE(digest != zc::none);
  return ZC_REQUIRE_NONNULL(digest);
}

zc::ArrayPtr<const uint8_t> bytesOf(zc::StringPtr text) { return text.asBytes(); }

CstLexeme token(uint32_t kind, uint64_t start, uint64_t end, zc::StringPtr spelling) {
  auto lexeme = CstLexeme::token(kind, ByteRange{start, end}, bytesOf(spelling));
  ZC_REQUIRE(lexeme != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(lexeme));
}

CstLexeme trivia(TriviaKind kind, uint64_t start, uint64_t end, zc::StringPtr spelling) {
  auto lexeme = CstLexeme::trivia(kind, ByteRange{start, end}, bytesOf(spelling));
  ZC_REQUIRE(lexeme != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(lexeme));
}

// The canonical minimal lexeme stream: the 5-byte source `i32 x`.
VerifiedLexemeStream verifiedMinimalStream() {
  zc::Vector<CstLexeme> lexemes(3);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  lexemes.add(trivia(TriviaKind::Whitespace, 3, 4, " "_zc));
  lexemes.add(token(2, 4, 5, "x"_zc));
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32 x"_zc), 5, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

RecoveryElement missingToken1(uint32_t expected, uint64_t anchor) {
  zc::Vector<uint32_t> kinds(1);
  kinds.add(expected);
  auto element = RecoveryElement::missingToken(kinds.asPtr(), anchor);
  ZC_REQUIRE(element != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(element));
}

RecoveryElement missingToken2(uint32_t first, uint32_t second, uint64_t anchor) {
  zc::Vector<uint32_t> kinds(2);
  kinds.add(first);
  kinds.add(second);
  auto element = RecoveryElement::missingToken(kinds.asPtr(), anchor);
  ZC_REQUIRE(element != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(element));
}

RecoveryElement skipped(uint32_t first, uint32_t count, uint64_t start, uint64_t end) {
  auto element = RecoveryElement::skippedTokens(first, count, ByteRange{start, end});
  ZC_REQUIRE(element != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(element));
}

// A canonical recovery sequence over the minimal stream, in canonical recovery
// order (anchor byte offset, then variant tag): a missing token at offset 0, a
// skip of the whole stream (covering-range start 0, tag after MissingToken), and
// a missing subtree at offset 3.
zc::Array<RecoveryElement> minimalElements() {
  zc::Vector<RecoveryElement> elements(3);
  elements.add(missingToken2(7, 9, 0));
  elements.add(skipped(0, 3, 0, 5));
  elements.add(RecoveryElement::missingSubtree(4, 3));
  return elements.releaseAsArray();
}

VerifiedRecoverySequence verifiedMinimal() {
  auto stream = verifiedMinimalStream();
  auto result = RecoverySequenceVerifier::verify(stream, minimalElements());
  ZC_REQUIRE(result.is<VerifiedRecoverySequence>());
  return zc::mv(result.get<VerifiedRecoverySequence>());
}

// The verified sequence encodes to a fixed preimage: assert its exact byte
// length, full lowercase hex, and RecoverySequenceId. The bytes are produced by
// the live encoder; the asserted values are the frozen oracle.
ZC_TEST("CST recovery sequence encodes to the frozen oracle") {
  auto sequence = verifiedMinimal();
  auto bytes = RecoverySequenceCodec::encode(sequence);
  ZC_EXPECT(bytes.size() == 128);
  ZC_EXPECT(
      zc::encodeHex(bytes.asPtr()) ==
      "7a6f6d2e6373742d7265636f766572790000000000000000208e8671bcf8ae9252d57d98d329aadc79b0e2587"
      "115816a2e7181a52a9b514cfd0000000000000003010000000000000000000000000000000200000007000000"
      "090300000000000000030000000000000000000000000000000502000000000000000300000004"_zc);
  ZC_EXPECT(zc::encodeHex(sequence.id().digest().bytes()) ==
            "318ee16ab015ad83a07d42a9c249c26551e614ad36ddd37e2310e07a8c6e3789"_zc);
}

// The domain tag prefixes the preimage and re-encoding is byte-identical.
ZC_TEST("CST recovery sequence encoding is deterministic and domain-separated") {
  auto first = RecoverySequenceCodec::encode(verifiedMinimal());
  auto second = RecoverySequenceCodec::encode(verifiedMinimal());
  ZC_EXPECT(first.asPtr() == second.asPtr());
  const char domain[] = "zom.cst-recovery";
  ZC_REQUIRE(first.size() >= sizeof(domain));
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

// An empty recovery sequence over a verified stream verifies.
ZC_TEST("CST empty recovery sequence verifies") {
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), zc::Array<RecoveryElement>());
  ZC_REQUIRE(result.is<VerifiedRecoverySequence>());
  ZC_EXPECT(result.get<VerifiedRecoverySequence>().elements().size() == 0);
}

// A SkippedTokens run referencing a lexeme index past the stream is rejected.
ZC_TEST("CST recovery rejects an out-of-range skipped run") {
  zc::Vector<RecoveryElement> elements(1);
  elements.add(skipped(2, 5, 4, 5));  // firstLexeme 2 + count 5 > 3 lexemes
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::SkippedRunOutOfRange);
}

// A SkippedTokens run over only trivia (no Token or Invalid) is rejected.
ZC_TEST("CST recovery rejects a skipped run with no significant lexeme") {
  zc::Vector<RecoveryElement> elements(1);
  elements.add(skipped(1, 1, 3, 4));  // lexeme 1 is the whitespace trivia
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::SkippedRunNoSignificant);
}

// A SkippedTokens range that does not equal the run's covering range is rejected.
ZC_TEST("CST recovery rejects a skipped range mismatch") {
  zc::Vector<RecoveryElement> elements(1);
  elements.add(skipped(0, 1, 0, 5));  // lexeme 0 covers [0, 3), not [0, 5)
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::SkippedRangeMismatch);
}

// A missing-element anchor past the source byte count is rejected.
ZC_TEST("CST recovery rejects an out-of-range anchor") {
  zc::Vector<RecoveryElement> elements(1);
  elements.add(RecoveryElement::missingSubtree(4, 6));  // anchor 6 > source 5
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::AnchorOutOfRange);
}

// An out-of-order sequence is rejected.
ZC_TEST("CST recovery rejects an unsorted sequence") {
  zc::Vector<RecoveryElement> elements(2);
  elements.add(RecoveryElement::missingSubtree(4, 3));  // anchor 3
  elements.add(missingToken1(7, 0));                    // anchor 0, out of order
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::UnsortedSequence);
}

// Two canonically-equal elements are a forbidden duplicate.
ZC_TEST("CST recovery rejects a duplicate element") {
  zc::Vector<RecoveryElement> elements(2);
  elements.add(RecoveryElement::missingSubtree(4, 3));
  elements.add(RecoveryElement::missingSubtree(4, 3));
  auto result =
      RecoverySequenceVerifier::verify(verifiedMinimalStream(), elements.releaseAsArray());
  ZC_REQUIRE(result.is<RecoveryFailure>());
  ZC_EXPECT(result.get<RecoveryFailure>() == RecoveryFailure::DuplicateElement);
}

// The factory guards reject an empty or non-ascending expected token set and a
// zero-count skip before an element can enter a request.
ZC_TEST("CST recovery factories reject malformed elements") {
  zc::Vector<uint32_t> empty;
  ZC_EXPECT(RecoveryElement::missingToken(empty.asPtr(), 0) == zc::none);
  zc::Vector<uint32_t> descending(2);
  descending.add(9);
  descending.add(7);  // not strictly ascending
  ZC_EXPECT(RecoveryElement::missingToken(descending.asPtr(), 0) == zc::none);
  ZC_EXPECT(RecoveryElement::skippedTokens(0, 0, ByteRange{0, 0}) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::cst
