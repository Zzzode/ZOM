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

#include "compiler/cst/recovery-codec.h"

namespace zomlang::compiler::cst {
namespace {

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

}  // namespace

// ---------------------------------------------------------------------------
// RecoveryElement
// ---------------------------------------------------------------------------

zc::Maybe<RecoveryElement> RecoveryElement::missingToken(zc::ArrayPtr<const uint32_t> expected,
                                                         uint64_t anchor) {
  if (expected.size() == 0) { return zc::none; }
  for (size_t index = 1; index < expected.size(); ++index) {
    // The expected set must be strictly ascending (sorted, no duplicates).
    if (expected[index - 1] >= expected[index]) { return zc::none; }
  }
  return RecoveryElement(RecoveryElementTag::MissingToken, anchor,
                         zc::heapArray<uint32_t>(expected), 0, 0, 0, ByteRange{anchor, anchor});
}

RecoveryElement RecoveryElement::missingSubtree(uint32_t expectedCategory, uint64_t anchor) {
  return RecoveryElement(RecoveryElementTag::MissingSubtree, anchor, zc::Array<uint32_t>(),
                         expectedCategory, 0, 0, ByteRange{anchor, anchor});
}

zc::Maybe<RecoveryElement> RecoveryElement::skippedTokens(uint32_t firstLexeme,
                                                          uint32_t lexemeCount, ByteRange range) {
  if (lexemeCount == 0 || range.end < range.start) { return zc::none; }
  return RecoveryElement(RecoveryElementTag::SkippedTokens, 0, zc::Array<uint32_t>(), 0,
                         firstLexeme, lexemeCount, range);
}

int RecoveryElement::compareCanonical(const RecoveryElement& other) const noexcept {
  // (1) anchor byte offset (the covering-range start for a skip).
  const auto leftAnchor = sortAnchor();
  const auto rightAnchor = other.sortAnchor();
  if (leftAnchor != rightAnchor) { return leftAnchor < rightAnchor ? -1 : 1; }
  // (2) variant tag.
  if (tagValue != other.tagValue) {
    return static_cast<uint8_t>(tagValue) < static_cast<uint8_t>(other.tagValue) ? -1 : 1;
  }
  // (3) expected token or category tag, then (4) skipped-token range.
  switch (tagValue) {
    case RecoveryElementTag::MissingToken: {
      const auto left = expectedTokenValues.asPtr();
      const auto right = other.expectedTokenValues.asPtr();
      const size_t shared = left.size() < right.size() ? left.size() : right.size();
      for (size_t index = 0; index < shared; ++index) {
        if (left[index] != right[index]) { return left[index] < right[index] ? -1 : 1; }
      }
      if (left.size() != right.size()) { return left.size() < right.size() ? -1 : 1; }
      return 0;
    }
    case RecoveryElementTag::MissingSubtree:
      if (expectedCategoryValue != other.expectedCategoryValue) {
        return expectedCategoryValue < other.expectedCategoryValue ? -1 : 1;
      }
      return 0;
    case RecoveryElementTag::SkippedTokens:
      if (rangeValue.end != other.rangeValue.end) {
        return rangeValue.end < other.rangeValue.end ? -1 : 1;
      }
      return 0;
  }
  ZC_UNREACHABLE
}

RecoveryElement RecoveryElement::clone() const {
  return RecoveryElement(tagValue, anchorValue,
                         zc::heapArray<uint32_t>(expectedTokenValues.asPtr()),
                         expectedCategoryValue, firstLexemeValue, lexemeCountValue, rangeValue);
}

// ---------------------------------------------------------------------------
// RecoverySequenceCodec
// ---------------------------------------------------------------------------

zc::Array<uint8_t> RecoverySequenceCodec::encode(const VerifiedRecoverySequence& sequence) {
  zc::Vector<uint8_t> preimage;
  for (const auto byte : "zom.cst-recovery"_zc) { preimage.add(static_cast<uint8_t>(byte)); }
  preimage.add(0);
  appendFramed(preimage, sequence.lexemeStreamId().digest().bytes());
  const auto elements = sequence.elements();
  appendUint64(preimage, elements.size());
  for (const auto& element : elements) {
    appendUint8(preimage, static_cast<uint8_t>(element.tag()));
    switch (element.tag()) {
      case RecoveryElementTag::MissingToken: {
        appendUint64(preimage, element.anchor());
        const auto expected = element.expectedTokens();
        appendUint64(preimage, expected.size());
        for (const auto kind : expected) { appendUint32(preimage, kind); }
        break;
      }
      case RecoveryElementTag::MissingSubtree:
        appendUint64(preimage, element.anchor());
        appendUint32(preimage, element.expectedCategory());
        break;
      case RecoveryElementTag::SkippedTokens:
        appendUint32(preimage, element.firstLexeme());
        appendUint32(preimage, element.lexemeCount());
        appendUint64(preimage, element.range().start);
        appendUint64(preimage, element.range().end);
        break;
    }
  }
  return preimage.releaseAsArray();
}

RecoverySequenceId RecoverySequenceCodec::computeId(const VerifiedRecoverySequence& sequence) {
  auto bytes = encode(sequence);
  return RecoverySequenceId::fromDigest(requireDigest(bytes.asPtr()));
}

// ---------------------------------------------------------------------------
// RecoverySequenceVerifier
// ---------------------------------------------------------------------------

RecoverySequenceResult RecoverySequenceVerifier::verify(const VerifiedLexemeStream& stream,
                                                        zc::Array<RecoveryElement>&& elements) {
  const auto lexemes = stream.lexemes();
  const uint64_t sourceByteCount =
      lexemes.size() == 0 ? 0 : lexemes[lexemes.size() - 1].range().end;

  for (const auto& element : elements) {
    switch (element.tag()) {
      case RecoveryElementTag::MissingToken:
      case RecoveryElementTag::MissingSubtree:
        // A missing-element anchor is zero-width and must lie within the source.
        if (element.anchor() > sourceByteCount) {
          return RecoverySequenceResult(RecoveryFailure::AnchorOutOfRange);
        }
        break;
      case RecoveryElementTag::SkippedTokens: {
        const uint64_t first = element.firstLexeme();
        const uint64_t count = element.lexemeCount();
        // The run must reference existing lexemes.
        if (count == 0 || first + count > lexemes.size()) {
          return RecoverySequenceResult(RecoveryFailure::SkippedRunOutOfRange);
        }
        // The run must contain at least one Token or Invalid lexeme.
        bool significant = false;
        for (uint64_t index = first; index < first + count; ++index) {
          const auto tag = lexemes[index].tag();
          if (tag == CstLexemeTag::Token || tag == CstLexemeTag::Invalid) {
            significant = true;
            break;
          }
        }
        if (!significant) {
          return RecoverySequenceResult(RecoveryFailure::SkippedRunNoSignificant);
        }
        // The recorded range must equal the run's covering range.
        const auto covering =
            ByteRange{lexemes[first].range().start, lexemes[first + count - 1].range().end};
        if (element.range().start != covering.start || element.range().end != covering.end) {
          return RecoverySequenceResult(RecoveryFailure::SkippedRangeMismatch);
        }
        break;
      }
    }
  }

  // The sequence must be strictly ascending in the canonical recovery order;
  // an equal pair is a forbidden duplicate.
  for (size_t index = 1; index < elements.size(); ++index) {
    const int order = elements[index - 1].compareCanonical(elements[index]);
    if (order == 0) { return RecoverySequenceResult(RecoveryFailure::DuplicateElement); }
    if (order > 0) { return RecoverySequenceResult(RecoveryFailure::UnsortedSequence); }
  }

  VerifiedRecoverySequence sequence(stream.id(), zc::mv(elements), RecoverySequenceId());
  sequence.idValue = RecoverySequenceCodec::computeId(sequence);
  return RecoverySequenceResult(zc::mv(sequence));
}

}  // namespace zomlang::compiler::cst
