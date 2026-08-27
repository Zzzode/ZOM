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
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::identity {

/// \brief Bounded reader for RFC 0011 canonical big-endian encodings.
class CanonicalDecoder final {
public:
  explicit CanonicalDecoder(zc::ArrayPtr<const uint8_t> input);
  ~CanonicalDecoder() noexcept(false);
  CanonicalDecoder(CanonicalDecoder&&) noexcept;
  CanonicalDecoder& operator=(CanonicalDecoder&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalDecoder);

  ZC_NODISCARD zc::Maybe<uint8_t> decodeUint8();
  ZC_NODISCARD zc::Maybe<uint32_t> decodeUint32();
  ZC_NODISCARD zc::Maybe<uint64_t> decodeUint64();
  ZC_NODISCARD zc::Maybe<bool> decodeBool();
  ZC_NODISCARD zc::Maybe<Sha256Digest> decodeDigest();
  ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> decodeBytes(uint64_t byteCount);
  ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> decodeByteString(uint64_t maximumBytes);
  ZC_NODISCARD zc::Maybe<uint64_t> decodeSequenceSize(uint64_t maximumElements);
  ZC_NODISCARD bool finished() const noexcept;
  ZC_NODISCARD uint64_t remaining() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
