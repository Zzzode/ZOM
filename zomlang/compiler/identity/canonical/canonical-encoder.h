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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::identity {

/// \brief Canonical architecture-independent byte encoder for semantic identity values.
class CanonicalEncoder final {
public:
  CanonicalEncoder() noexcept;

  /// \brief Constructs an encoder whose object and byte storage use `resource`.
  /// \param resource Resource that must outlive this encoder and its finished byte array.
  explicit CanonicalEncoder(zc::MemoryResource& resource);

  /// \brief Constructs a heap-backed encoder with one exact byte capacity.
  /// \param encodedByteCount Required final byte count.
  /// \return An encoder, or none when the byte count is not representable as `size_t`.
  ZC_NODISCARD static zc::Maybe<CanonicalEncoder> forExactSize(uint64_t encodedByteCount);

  /// \brief Constructs a resource-backed encoder with one exact byte capacity.
  /// \param resource Resource that must outlive this encoder and its finished byte array.
  /// \param encodedByteCount Required final byte count.
  /// \return An encoder, or none when the byte count is not representable as `size_t`.
  ZC_NODISCARD static zc::Maybe<CanonicalEncoder> forExactSize(zc::MemoryResource& resource,
                                                               uint64_t encodedByteCount);

  ~CanonicalEncoder() noexcept(false);
  CanonicalEncoder(CanonicalEncoder&&) noexcept;
  CanonicalEncoder& operator=(CanonicalEncoder&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalEncoder);

  void encodeUint8(uint8_t value);
  void encodeUint32(uint32_t value);
  void encodeUint64(uint64_t value);
  void encodeBool(bool value);
  void encodeDigest(const Sha256Digest& digest);
  void encodeByteString(zc::ArrayPtr<const uint8_t> bytes);
  void encodeSequenceSize(uint64_t size);
  void encodeNone();
  void encodeSome();

  /// \brief Finishes the stream and transfers ownership of its encoded bytes.
  ZC_NODISCARD zc::Array<uint8_t> finish();

private:
  struct ExactSizeTag final {};
  CanonicalEncoder(ExactSizeTag, size_t encodedByteCount);
  CanonicalEncoder(ExactSizeTag, zc::MemoryResource& resource, size_t encodedByteCount);
  void requireCapacity(size_t byteCount) const;

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
