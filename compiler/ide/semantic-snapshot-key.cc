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

#include "compiler/ide/semantic-snapshot-key.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::ide {
namespace {

// ---------------------------------------------------------------------------
// Canonical framing helpers, mirroring the CST lexeme codec discipline:
// Frame = big-endian uint64 byte length followed by the exact bytes.
// ---------------------------------------------------------------------------

constexpr zc::StringPtr kDomainTag = "zom.ide.snapshot"_zc;

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  output.addAll(value);
}

// The document version is encoded as its 32-bit two's-complement bit pattern in
// big-endian order, so negative versions round-trip and the width is fixed.
void appendVersion(zc::Vector<uint8_t>& output, DocumentVersion version) {
  const auto bits = static_cast<uint32_t>(version.raw());
  for (int shift = 24; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(bits >> static_cast<uint32_t>(shift)));
  }
}

// A little cursor over the candidate bytes; every read is bounds-checked and
// advances only on success.
class ByteReader final {
public:
  explicit ByteReader(zc::ArrayPtr<const uint8_t> bytes) noexcept : bytesValue(bytes) {}

  ZC_NODISCARD bool takeExact(zc::ArrayPtr<const uint8_t> expected) noexcept {
    if (remaining() < expected.size()) { return false; }
    if (bytesValue.slice(offsetValue, offsetValue + expected.size()) != expected) { return false; }
    offsetValue += expected.size();
    return true;
  }

  ZC_NODISCARD bool takeUint64(uint64_t& out) noexcept {
    if (remaining() < 8) { return false; }
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
      value = (value << 8) | bytesValue[offsetValue + index];
    }
    offsetValue += 8;
    out = value;
    return true;
  }

  ZC_NODISCARD bool takeFrame(zc::ArrayPtr<const uint8_t>& out) noexcept {
    uint64_t length = 0;
    if (!takeUint64(length)) { return false; }
    if (remaining() < length) { return false; }
    out = bytesValue.slice(offsetValue, offsetValue + length);
    offsetValue += length;
    return true;
  }

  ZC_NODISCARD bool takeInt32(int32_t& out) noexcept {
    if (remaining() < 4) { return false; }
    uint32_t bits = 0;
    for (size_t index = 0; index < 4; ++index) {
      bits = (bits << 8) | bytesValue[offsetValue + index];
    }
    offsetValue += 4;
    out = static_cast<int32_t>(bits);
    return true;
  }

  ZC_NODISCARD bool atEnd() const noexcept { return offsetValue == bytesValue.size(); }

private:
  ZC_NODISCARD size_t remaining() const noexcept { return bytesValue.size() - offsetValue; }

  zc::ArrayPtr<const uint8_t> bytesValue;
  size_t offsetValue = 0;
};

}  // namespace

SemanticSnapshotKey SemanticSnapshotKey::bind(
    identity::source_query::StableSourceQueryKey&& sourceKey, DocumentVersion version) {
  return SemanticSnapshotKey(zc::mv(sourceKey), version);
}

zc::Array<uint8_t> SemanticSnapshotKey::encodeCanonical() const {
  zc::Vector<uint8_t> preimage;
  preimage.addAll(kDomainTag.asBytes());
  preimage.add(0);
  appendFramed(preimage, sourceKeyValue.canonicalSourceBytes());
  appendVersion(preimage, versionValue);
  return preimage.releaseAsArray();
}

zc::Maybe<SemanticSnapshotKey> SemanticSnapshotKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  ByteReader reader(bytes);
  if (!reader.takeExact(kDomainTag.asBytes())) { return zc::none; }
  const uint8_t separator[] = {0};
  if (!reader.takeExact(zc::arrayPtr(separator))) { return zc::none; }

  zc::ArrayPtr<const uint8_t> sourceBytes;
  if (!reader.takeFrame(sourceBytes)) { return zc::none; }

  int32_t versionBits = 0;
  if (!reader.takeInt32(versionBits)) { return zc::none; }

  // Trailing bytes are not part of one canonical key.
  if (!reader.atEnd()) { return zc::none; }

  // The inner frame must itself be a bounded canonical source key.
  auto sourceKey = identity::source_query::StableSourceQueryKey::decodeBounded(sourceBytes);
  ZC_IF_SOME(key, sourceKey) {
    return SemanticSnapshotKey(zc::mv(key), DocumentVersion::initial(versionBits));
  }
  return zc::none;
}

}  // namespace zomlang::compiler::ide
