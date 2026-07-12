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

#include "zomlang/compiler/identity/canonical-decoder.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::identity {

struct CanonicalDecoder::Impl final {
  zc::Array<uint8_t> bytes;
  size_t cursor = 0;
};

CanonicalDecoder::CanonicalDecoder(zc::ArrayPtr<const uint8_t> input)
    : impl(zc::heap<Impl>(Impl{zc::heapArray(input), 0})) {}
CanonicalDecoder::~CanonicalDecoder() noexcept(false) = default;
CanonicalDecoder::CanonicalDecoder(CanonicalDecoder&&) noexcept = default;
CanonicalDecoder& CanonicalDecoder::operator=(CanonicalDecoder&&) noexcept = default;

zc::Maybe<uint8_t> CanonicalDecoder::decodeUint8() {
  if (impl->cursor == impl->bytes.size()) { return zc::none; }
  return impl->bytes[impl->cursor++];
}

zc::Maybe<uint32_t> CanonicalDecoder::decodeUint32() {
  if (impl->bytes.size() - impl->cursor < 4) { return zc::none; }
  uint32_t result = 0;
  for (size_t index = 0; index < 4; ++index) {
    result = static_cast<uint32_t>((result << 8U) | impl->bytes[impl->cursor++]);
  }
  return result;
}

zc::Maybe<uint64_t> CanonicalDecoder::decodeUint64() {
  if (impl->bytes.size() - impl->cursor < 8) { return zc::none; }
  uint64_t result = 0;
  for (size_t index = 0; index < 8; ++index) {
    result = (result << 8U) | impl->bytes[impl->cursor++];
  }
  return result;
}

zc::Maybe<bool> CanonicalDecoder::decodeBool() {
  auto value = decodeUint8();
  ZC_IF_SOME(byte, value) {
    if (byte == 0x00) { return false; }
    if (byte == 0x01) { return true; }
  }
  return zc::none;
}

zc::Maybe<Sha256Digest> CanonicalDecoder::decodeDigest() {
  if (impl->bytes.size() - impl->cursor < 32) { return zc::none; }
  auto result = Sha256Digest::fromBytes(impl->bytes.slice(impl->cursor, impl->cursor + 32));
  impl->cursor += 32;
  return result;
}

zc::Maybe<zc::Array<uint8_t>> CanonicalDecoder::decodeBytes(uint64_t byteCount) {
  if (byteCount > impl->bytes.size() - impl->cursor) { return zc::none; }
  auto result = zc::heapArray(impl->bytes.slice(impl->cursor, impl->cursor + byteCount));
  impl->cursor += static_cast<size_t>(byteCount);
  return result;
}

zc::Maybe<zc::Array<uint8_t>> CanonicalDecoder::decodeByteString(uint64_t maximumBytes) {
  auto length = decodeUint64();
  ZC_IF_SOME(value, length) {
    if (value > maximumBytes || value > impl->bytes.size() - impl->cursor) { return zc::none; }
    auto result = zc::heapArray(impl->bytes.slice(impl->cursor, impl->cursor + value));
    impl->cursor += static_cast<size_t>(value);
    return result;
  }
  return zc::none;
}

zc::Maybe<uint64_t> CanonicalDecoder::decodeSequenceSize(uint64_t maximumElements) {
  auto count = decodeUint64();
  ZC_IF_SOME(value, count) {
    if (value <= maximumElements) { return value; }
  }
  return zc::none;
}

bool CanonicalDecoder::finished() const noexcept { return impl->cursor == impl->bytes.size(); }
uint64_t CanonicalDecoder::remaining() const noexcept { return impl->bytes.size() - impl->cursor; }

}  // namespace zomlang::compiler::identity
