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

// RFC 0023 recoverable-parsing first slice: prove the CST lexeme partition codec
// and its independent verifier are deterministic and fail closed on each of the
// RFC 0023 partition invariants (adjacent coverage of [0, sourceByteCount),
// spelling width, non-empty partition, and content-digest reconstruction). This
// slice models and verifies the lexeme stream as pure data and computes its
// deterministic LexemeStreamId; it runs no live lexer or parser and reads no
// filesystem.

#include "compiler/cst/lexeme-codec.h"
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

// The canonical minimal fixture: the 5-byte source `i32 x` partitioned into a
// token, a whitespace trivia, and a token. Token kinds are opaque here (1 and 2
// stand for two distinct lexer token kinds).
LexemeStreamRequest minimalRequest() {
  zc::Vector<CstLexeme> lexemes(3);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  lexemes.add(trivia(TriviaKind::Whitespace, 3, 4, " "_zc));
  lexemes.add(token(2, 4, 5, "x"_zc));
  return LexemeStreamRequest{digestOf("i32 x"_zc), 5, lexemes.releaseAsArray()};
}

VerifiedLexemeStream verifiedMinimal() {
  auto result = LexemePartitionVerifier::verify(minimalRequest());
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

// The verified stream encodes to a fixed preimage: assert its exact byte length,
// full lowercase hex, and LexemeStreamId digest. The bytes are produced by the
// live encoder; the asserted values are the frozen oracle.
ZC_TEST("CST lexeme stream encodes to the frozen oracle") {
  auto stream = verifiedMinimal();
  auto bytes = LexemeStreamCodec::encode(stream);
  ZC_EXPECT(bytes.size() == 188);
  ZC_EXPECT(
      zc::encodeHex(bytes.asPtr()) ==
      "7a6f6d2e6373742d6c6578656d657300000000000000002015ec484e18ff0095d6140bba8b007821aeab2deb5"
      "9506ed1ff4d1456ab4841b4000000000000000500000000000000030100000001000000000000000000000000"
      "000000030000000000000003693332000000000000000002000000010000000000000003000000000000000400"
      "000000000000012000000000000000000100000002000000000000000400000000000000050000000000000001"
      "780000000000000000"_zc);
  ZC_EXPECT(zc::encodeHex(stream.id().digest().bytes()) ==
            "8e8671bcf8ae9252d57d98d329aadc79b0e2587115816a2e7181a52a9b514cfd"_zc);
}

// The domain tag prefixes the preimage and re-encoding is byte-identical.
ZC_TEST("CST lexeme stream encoding is deterministic and domain-separated") {
  auto first = LexemeStreamCodec::encode(verifiedMinimal());
  auto second = LexemeStreamCodec::encode(verifiedMinimal());
  ZC_EXPECT(first.asPtr() == second.asPtr());
  const char domain[] = "zom.cst-lexemes";
  ZC_REQUIRE(first.size() >= sizeof(domain));
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

// An empty source with the SHA-256 of zero bytes and no lexemes verifies.
ZC_TEST("CST empty source verifies to an empty partition") {
  zc::ArrayPtr<const uint8_t> empty;
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{ZC_REQUIRE_NONNULL(identity::sha256(empty)), 0, zc::Array<CstLexeme>()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  ZC_EXPECT(result.get<VerifiedLexemeStream>().lexemes().size() == 0);
}

// A gap between adjacent ranges is a non-adjacent partition.
ZC_TEST("CST partition rejects a range gap") {
  zc::Vector<CstLexeme> lexemes(2);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  lexemes.add(token(2, 4, 5, "x"_zc));  // gap at [3, 4)
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32x"_zc), 5, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::NonAdjacentPartition);
}

// Overlapping ranges are a non-adjacent partition.
ZC_TEST("CST partition rejects a range overlap") {
  zc::Vector<CstLexeme> lexemes(2);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  lexemes.add(token(2, 2, 5, "2 x"_zc));  // overlaps [2, 3)
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i322 x"_zc), 5, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::NonAdjacentPartition);
}

// A partition that does not start at byte 0 fails coverage.
ZC_TEST("CST partition rejects a non-zero first offset") {
  zc::Vector<CstLexeme> lexemes(1);
  lexemes.add(token(1, 1, 4, "i32"_zc));  // starts at 1, not 0
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32"_zc), 4, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::IncompleteCoverage);
}

// A partition that does not reach sourceByteCount fails coverage.
ZC_TEST("CST partition rejects a short final offset") {
  zc::Vector<CstLexeme> lexemes(1);
  lexemes.add(token(1, 0, 3, "i32"_zc));  // ends at 3, source is 5
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32"_zc), 5, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::IncompleteCoverage);
}

// The content digest must equal the SHA-256 of the concatenated spellings.
ZC_TEST("CST partition rejects a content-digest mismatch") {
  auto request = minimalRequest();
  request.contentDigest = digestOf("different"_zc);
  auto result = LexemePartitionVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::ContentDigestMismatch);
}

// A non-empty source with zero lexemes is an empty partition.
ZC_TEST("CST partition rejects a missing lexeme for a non-empty source") {
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("x"_zc), 1, zc::Array<CstLexeme>()});
  ZC_REQUIRE(result.is<LexemePartitionFailure>());
  ZC_EXPECT(result.get<LexemePartitionFailure>() == LexemePartitionFailure::EmptyPartition);
}

// The spelling-width factory guard rejects a lexeme whose spelling byte count
// does not equal its range width before it can enter a request.
ZC_TEST("CST lexeme factory rejects a spelling-width mismatch") {
  ZC_EXPECT(CstLexeme::token(1, ByteRange{0, 3}, bytesOf("ab"_zc)) == zc::none);
  ZC_EXPECT(CstLexeme::invalid(ByteRange{0, 0}, bytesOf(""_zc), digestOf("d"_zc)) == zc::none);
}

}  // namespace
}  // namespace zomlang::compiler::cst
