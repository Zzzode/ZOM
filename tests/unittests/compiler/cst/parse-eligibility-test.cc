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

// RFC 0023 ParseSyntaxVerifier acceptance predicate (L561-563): prove the
// parse-eligibility gate accepts only a recovery-free, Invalid-free, error-free
// verified lexeme stream, and reports the first violated condition otherwise.
// This composes the verified lexeme stream and the verified recovery sequence as
// pure data; it constructs no AST and runs no parser.

#include "compiler/cst/parse-eligibility.h"

#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/recovery-codec.h"
#include "compiler/identity/crypto/sha256.h"
#include "zc/core/vector.h"
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

CstLexeme invalid(uint64_t start, uint64_t end, zc::StringPtr spelling) {
  auto lexeme = CstLexeme::invalid(ByteRange{start, end}, bytesOf(spelling), digestOf("diag"_zc));
  ZC_REQUIRE(lexeme != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(lexeme));
}

// A clean single-token stream over `i32`.
VerifiedLexemeStream cleanStream() {
  zc::Vector<CstLexeme> lexemes(1);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32"_zc), 3, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

// A stream containing an Invalid lexeme over the source `i32@`.
VerifiedLexemeStream invalidStream() {
  zc::Vector<CstLexeme> lexemes(2);
  lexemes.add(token(1, 0, 3, "i32"_zc));
  lexemes.add(invalid(3, 4, "@"_zc));
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{digestOf("i32@"_zc), 4, lexemes.releaseAsArray()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

// An empty recovery sequence over `stream`.
VerifiedRecoverySequence emptyRecovery(const VerifiedLexemeStream& stream) {
  auto result = RecoverySequenceVerifier::verify(stream, zc::Array<RecoveryElement>());
  ZC_REQUIRE(result.is<VerifiedRecoverySequence>());
  return zc::mv(result.get<VerifiedRecoverySequence>());
}

// A one-element recovery sequence (a missing subtree) over `stream`.
VerifiedRecoverySequence nonEmptyRecovery(const VerifiedLexemeStream& stream) {
  zc::Vector<RecoveryElement> elements(1);
  elements.add(RecoveryElement::missingSubtree(1, 0));
  auto result = RecoverySequenceVerifier::verify(stream, elements.releaseAsArray());
  ZC_REQUIRE(result.is<VerifiedRecoverySequence>());
  return zc::mv(result.get<VerifiedRecoverySequence>());
}

// A clean stream with empty recovery and no parser errors is eligible.
ZC_TEST("Parse eligibility accepts a clean stream") {
  auto stream = cleanStream();
  auto recovery = emptyRecovery(stream);
  auto result = parseEligibility(stream, recovery, 0);
  ZC_EXPECT(result.isEligible());
}

// A non-empty recovery sequence is RecoveryPresent.
ZC_TEST("Parse eligibility rejects a recovered parse") {
  auto stream = cleanStream();
  auto recovery = nonEmptyRecovery(stream);
  auto result = parseEligibility(stream, recovery, 0);
  ZC_REQUIRE(!result.isEligible());
  ZC_EXPECT(result.reason() == ParseIneligibilityReason::RecoveryPresent);
}

// An Invalid lexeme in the stream is InvalidLexemePresent.
ZC_TEST("Parse eligibility rejects an invalid lexeme") {
  auto stream = invalidStream();
  auto recovery = emptyRecovery(stream);
  auto result = parseEligibility(stream, recovery, 0);
  ZC_REQUIRE(!result.isEligible());
  ZC_EXPECT(result.reason() == ParseIneligibilityReason::InvalidLexemePresent);
}

// A positive error-severity parser diagnostic count is ParserErrorPresent.
ZC_TEST("Parse eligibility rejects a parser error") {
  auto stream = cleanStream();
  auto recovery = emptyRecovery(stream);
  auto result = parseEligibility(stream, recovery, 1);
  ZC_REQUIRE(!result.isEligible());
  ZC_EXPECT(result.reason() == ParseIneligibilityReason::ParserErrorPresent);
}

// A recovery sequence bound to a different lexeme stream is a mismatch.
ZC_TEST("Parse eligibility rejects a recovery-stream mismatch") {
  auto stream = cleanStream();
  auto otherStream = invalidStream();
  auto foreignRecovery = emptyRecovery(otherStream);
  auto result = parseEligibility(stream, foreignRecovery, 0);
  ZC_REQUIRE(!result.isEligible());
  ZC_EXPECT(result.reason() == ParseIneligibilityReason::RecoveryStreamMismatch);
}

}  // namespace
}  // namespace zomlang::compiler::cst
