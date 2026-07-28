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

#include "zomlang/compiler/identity/canonical-encoder.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::identity {

struct CanonicalEncoder::Impl final {
  Impl() = default;
  explicit Impl(zc::MemoryResource& resource) : bytes(resource) {}
  Impl(ExactSizeTag, size_t encodedByteCount)
      : bytes(encodedByteCount), exactByteCount(encodedByteCount) {}
  Impl(ExactSizeTag, zc::MemoryResource& resource, size_t encodedByteCount)
      : bytes(resource, encodedByteCount), exactByteCount(encodedByteCount) {}

  zc::Vector<uint8_t> bytes;
  zc::Maybe<size_t> exactByteCount;
};

CanonicalEncoder::CanonicalEncoder() noexcept : impl(zc::heap<Impl>()) {}
CanonicalEncoder::CanonicalEncoder(zc::MemoryResource& resource)
    : impl(zc::resourceHeap<Impl>(resource, resource)) {}
CanonicalEncoder::CanonicalEncoder(ExactSizeTag tag, size_t encodedByteCount)
    : impl(zc::heap<Impl>(tag, encodedByteCount)) {}
CanonicalEncoder::CanonicalEncoder(ExactSizeTag tag, zc::MemoryResource& resource,
                                   size_t encodedByteCount)
    : impl(zc::resourceHeap<Impl>(resource, tag, resource, encodedByteCount)) {}
CanonicalEncoder::~CanonicalEncoder() noexcept(false) = default;
CanonicalEncoder::CanonicalEncoder(CanonicalEncoder&&) noexcept = default;
CanonicalEncoder& CanonicalEncoder::operator=(CanonicalEncoder&&) noexcept = default;

zc::Maybe<CanonicalEncoder> CanonicalEncoder::forExactSize(uint64_t encodedByteCount) {
  if (encodedByteCount > static_cast<uint64_t>(static_cast<size_t>(zc::maxValue))) {
    return zc::none;
  }
  return CanonicalEncoder(ExactSizeTag{}, static_cast<size_t>(encodedByteCount));
}

zc::Maybe<CanonicalEncoder> CanonicalEncoder::forExactSize(zc::MemoryResource& resource,
                                                           uint64_t encodedByteCount) {
  if (encodedByteCount > static_cast<uint64_t>(static_cast<size_t>(zc::maxValue))) {
    return zc::none;
  }
  return CanonicalEncoder(ExactSizeTag{}, resource, static_cast<size_t>(encodedByteCount));
}

namespace {

void appendUint32(zc::Vector<uint8_t>& bytes, uint32_t value) {
  for (uint32_t shift = 24;; shift -= 8) {
    bytes.add(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t shift = 56;; shift -= 8) {
    bytes.add(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

}  // namespace

void CanonicalEncoder::requireCapacity(size_t byteCount) const {
  ZC_IF_SOME(exactByteCount, impl->exactByteCount) {
    ZC_REQUIRE(impl->bytes.size() <= exactByteCount,
               "CanonicalEncoder exact capacity invariant violated");
    ZC_REQUIRE(byteCount <= exactByteCount - impl->bytes.size(),
               "CanonicalEncoder exact capacity exceeded");
  }
}

void CanonicalEncoder::encodeUint8(uint8_t value) {
  requireCapacity(1);
  impl->bytes.add(value);
}

void CanonicalEncoder::encodeUint32(uint32_t value) {
  requireCapacity(sizeof(value));
  appendUint32(impl->bytes, value);
}

void CanonicalEncoder::encodeUint64(uint64_t value) {
  requireCapacity(sizeof(value));
  appendUint64(impl->bytes, value);
}

void CanonicalEncoder::encodeBool(bool value) { encodeUint8(value ? 0x01 : 0x00); }

void CanonicalEncoder::encodeDigest(const Sha256Digest& digest) {
  requireCapacity(digest.bytes().size());
  impl->bytes.addAll(digest.bytes());
}

void CanonicalEncoder::encodeByteString(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_REQUIRE(bytes.size() <= static_cast<size_t>(zc::maxValue) - sizeof(uint64_t),
             "CanonicalEncoder byte string size overflow");
  requireCapacity(sizeof(uint64_t) + bytes.size());
  appendUint64(impl->bytes, bytes.size());
  impl->bytes.addAll(bytes);
}

void CanonicalEncoder::encodeSequenceSize(uint64_t size) { encodeUint64(size); }

void CanonicalEncoder::encodeNone() { encodeUint8(0x00); }

void CanonicalEncoder::encodeSome() { encodeUint8(0x01); }

zc::Array<uint8_t> CanonicalEncoder::finish() {
  ZC_IF_SOME(exactByteCount, impl->exactByteCount) {
    ZC_REQUIRE(impl->bytes.size() == exactByteCount, "CanonicalEncoder exact capacity underfilled");
  }
  return impl->bytes.releaseAsArray();
}

}  // namespace zomlang::compiler::identity
