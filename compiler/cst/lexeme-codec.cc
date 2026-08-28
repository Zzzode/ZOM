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

#include "compiler/cst/lexeme-codec.h"

namespace zomlang::compiler::cst {
namespace {

// ---------------------------------------------------------------------------
// Canonical framing helpers (shared discipline with the link-plan codec):
// Frame = big-endian uint64 byte length followed by the exact bytes.
// ---------------------------------------------------------------------------

void appendUint8(zc::Vector<uint8_t>& output, uint8_t value) { output.add(value); }

void appendUint32(zc::Vector<uint8_t>& output, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  output.addAll(value);
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

// The zero digest carried by non-Invalid lexemes; it is never encoded for those
// variants (the diagnostic frame is empty unless the lexeme is Invalid).
identity::Sha256Digest zeroDigest() {
  uint8_t bytes[32] = {0};
  auto digest = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

// The kind field encoded for one lexeme: the token kind for a Token, the trivia
// kind for a Trivia, and zero for an Invalid.
uint32_t encodedKind(const CstLexeme& lexeme) {
  switch (lexeme.tag()) {
    case CstLexemeTag::Token:
      return lexeme.tokenKind();
    case CstLexemeTag::Trivia:
      return static_cast<uint32_t>(lexeme.triviaKind());
    case CstLexemeTag::Invalid:
      return 0;
  }
  ZC_UNREACHABLE
}

}  // namespace

// ---------------------------------------------------------------------------
// CstLexeme
// ---------------------------------------------------------------------------

zc::Maybe<CstLexeme> CstLexeme::token(uint32_t tokenKind, ByteRange range,
                                      zc::ArrayPtr<const uint8_t> spelling) {
  if (range.end < range.start || spelling.size() != range.width()) { return zc::none; }
  return CstLexeme(CstLexemeTag::Token, tokenKind, range, zc::heapArray<uint8_t>(spelling),
                   zeroDigest());
}

zc::Maybe<CstLexeme> CstLexeme::trivia(TriviaKind triviaKind, ByteRange range,
                                       zc::ArrayPtr<const uint8_t> spelling) {
  if (range.end < range.start || spelling.size() != range.width()) { return zc::none; }
  return CstLexeme(CstLexemeTag::Trivia, static_cast<uint32_t>(triviaKind), range,
                   zc::heapArray<uint8_t>(spelling), zeroDigest());
}

zc::Maybe<CstLexeme> CstLexeme::invalid(ByteRange range, zc::ArrayPtr<const uint8_t> spelling,
                                        const identity::Sha256Digest& diagnosticDigest) {
  if (range.end < range.start || spelling.size() != range.width() || spelling.size() == 0) {
    return zc::none;
  }
  return CstLexeme(CstLexemeTag::Invalid, 0, range, zc::heapArray<uint8_t>(spelling),
                   diagnosticDigest);
}

CstLexeme CstLexeme::clone() const {
  return CstLexeme(tagValue, kindValue, rangeValue, zc::heapArray<uint8_t>(spellingValue.asPtr()),
                   diagnosticValue);
}

// ---------------------------------------------------------------------------
// LexemeStreamCodec
// ---------------------------------------------------------------------------

zc::Array<uint8_t> LexemeStreamCodec::encode(const VerifiedLexemeStream& stream) {
  zc::Vector<uint8_t> preimage;
  for (const auto byte : "zom.cst-lexemes"_zc) { preimage.add(static_cast<uint8_t>(byte)); }
  preimage.add(0);
  appendFramed(preimage, stream.contentDigest().bytes());
  appendUint64(preimage, stream.sourceByteCount());
  const auto lexemes = stream.lexemes();
  appendUint64(preimage, lexemes.size());
  for (const auto& lexeme : lexemes) {
    appendUint8(preimage, static_cast<uint8_t>(lexeme.tag()));
    appendUint32(preimage, encodedKind(lexeme));
    appendUint64(preimage, lexeme.range().start);
    appendUint64(preimage, lexeme.range().end);
    appendFramed(preimage, lexeme.spelling());
    // The diagnostic frame carries the digest bytes for an Invalid lexeme and is
    // empty otherwise, so token and trivia streams never depend on a digest value.
    if (lexeme.tag() == CstLexemeTag::Invalid) {
      appendFramed(preimage, lexeme.diagnosticDigest().bytes());
    } else {
      appendUint64(preimage, 0);
    }
  }
  return preimage.releaseAsArray();
}

LexemeStreamId LexemeStreamCodec::computeId(const VerifiedLexemeStream& stream) {
  auto bytes = encode(stream);
  return LexemeStreamId::fromDigest(requireDigest(bytes.asPtr()));
}

// ---------------------------------------------------------------------------
// LexemePartitionVerifier
// ---------------------------------------------------------------------------

LexemeStreamResult LexemePartitionVerifier::verify(LexemeStreamRequest&& request) {
  const auto lexemes = request.lexemes.asPtr();

  // Empty source: no lexemes and a zero byte count. A non-empty source must
  // contribute at least one lexeme; a zero byte count must carry none.
  if (request.sourceByteCount == 0 || lexemes.size() == 0) {
    if (request.sourceByteCount != 0 || lexemes.size() != 0) {
      return LexemeStreamResult(LexemePartitionFailure::EmptyPartition);
    }
    // Both empty: the content digest must be the SHA-256 of zero bytes.
    zc::ArrayPtr<const uint8_t> empty;
    if (requireDigest(empty) != request.contentDigest) {
      return LexemeStreamResult(LexemePartitionFailure::ContentDigestMismatch);
    }
    VerifiedLexemeStream stream(request.contentDigest, 0, zc::mv(request.lexemes),
                                LexemeStreamId());
    stream.idValue = LexemeStreamCodec::computeId(stream);
    return LexemeStreamResult(zc::mv(stream));
  }

  // Every lexeme's spelling byte count must equal its range width.
  for (const auto& lexeme : lexemes) {
    if (lexeme.spelling().size() != lexeme.range().width()) {
      return LexemeStreamResult(LexemePartitionFailure::SpellingWidthMismatch);
    }
  }

  // Ranges must form an ascending, gapless, non-overlapping partition: each
  // lexeme starts exactly where the previous ended, beginning at 0.
  if (lexemes[0].range().start != 0) {
    return LexemeStreamResult(LexemePartitionFailure::IncompleteCoverage);
  }
  for (size_t index = 0; index < lexemes.size(); ++index) {
    const auto range = lexemes[index].range();
    if (range.end < range.start) {
      return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
    }
    if (index > 0 && range.start != lexemes[index - 1].range().end) {
      return LexemeStreamResult(LexemePartitionFailure::NonAdjacentPartition);
    }
  }
  if (lexemes[lexemes.size() - 1].range().end != request.sourceByteCount) {
    return LexemeStreamResult(LexemePartitionFailure::IncompleteCoverage);
  }

  // Concatenating the lexeme spellings byte-for-byte must reconstruct the source;
  // the digest of that reconstruction must equal the declared content digest.
  zc::Vector<uint8_t> reconstructed(request.sourceByteCount);
  for (const auto& lexeme : lexemes) { reconstructed.addAll(lexeme.spelling()); }
  if (requireDigest(reconstructed.asPtr()) != request.contentDigest) {
    return LexemeStreamResult(LexemePartitionFailure::ContentDigestMismatch);
  }

  VerifiedLexemeStream stream(request.contentDigest, request.sourceByteCount,
                              zc::mv(request.lexemes), LexemeStreamId());
  stream.idValue = LexemeStreamCodec::computeId(stream);
  return LexemeStreamResult(zc::mv(stream));
}

}  // namespace zomlang::compiler::cst
