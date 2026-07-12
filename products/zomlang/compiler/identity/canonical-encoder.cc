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

#include "zc/core/vector.h"

namespace zomlang::compiler::identity {

struct CanonicalEncoder::Impl final {
  Impl() = default;
  explicit Impl(zc::MemoryResource& resource) : bytes(resource) {}

  zc::Vector<uint8_t> bytes;
};

CanonicalEncoder::CanonicalEncoder() noexcept : impl(zc::heap<Impl>()) {}
CanonicalEncoder::CanonicalEncoder(zc::MemoryResource& resource)
    : impl(zc::resourceHeap<Impl>(resource, resource)) {}
CanonicalEncoder::~CanonicalEncoder() noexcept(false) = default;
CanonicalEncoder::CanonicalEncoder(CanonicalEncoder&&) noexcept = default;
CanonicalEncoder& CanonicalEncoder::operator=(CanonicalEncoder&&) noexcept = default;

void CanonicalEncoder::encodeUint8(uint8_t value) { impl->bytes.add(value); }

void CanonicalEncoder::encodeUint32(uint32_t value) {
  for (uint32_t shift = 24;; shift -= 8) {
    encodeUint8(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

void CanonicalEncoder::encodeUint64(uint64_t value) {
  for (uint32_t shift = 56;; shift -= 8) {
    encodeUint8(static_cast<uint8_t>(value >> shift));
    if (shift == 0) { break; }
  }
}

void CanonicalEncoder::encodeBool(bool value) { encodeUint8(value ? 0x01 : 0x00); }

void CanonicalEncoder::encodeDigest(const Sha256Digest& digest) {
  impl->bytes.addAll(digest.bytes());
}

void CanonicalEncoder::encodeByteString(zc::ArrayPtr<const uint8_t> bytes) {
  encodeUint64(bytes.size());
  impl->bytes.addAll(bytes);
}

void CanonicalEncoder::encodeSequenceSize(uint64_t size) { encodeUint64(size); }

void CanonicalEncoder::encodeNone() { encodeUint8(0x00); }

void CanonicalEncoder::encodeSome() { encodeUint8(0x01); }

zc::Array<uint8_t> CanonicalEncoder::finish() { return impl->bytes.releaseAsArray(); }

}  // namespace zomlang::compiler::identity
