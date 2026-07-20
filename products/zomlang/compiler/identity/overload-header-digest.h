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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/identity/canonical-overload-header.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::identity {

namespace overload_header_digest_detail {
struct OverloadHeaderAuthorityData;
}

/// \brief Strong raw 32-byte digest of one complete canonical overload header.
class OverloadHeaderDigest final {
public:
  OverloadHeaderDigest(OverloadHeaderDigest&&) noexcept = default;
  OverloadHeaderDigest& operator=(OverloadHeaderDigest&&) noexcept = default;
  ZC_DISALLOW_COPY(OverloadHeaderDigest);

  /// \brief Computes SHA-256("zom.overload-header.v0" || 0x00 || Encode(header)).
  ZC_NODISCARD static OverloadHeaderDigest compute(const CanonicalOverloadHeader& header);
  /// \brief Admits exactly 32 already-verified digest bytes.
  ZC_NODISCARD static zc::Maybe<OverloadHeaderDigest> fromBytes(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD OverloadHeaderDigest clone() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const ZC_LIFETIMEBOUND;
  /// \brief Encodes exactly 32 raw bytes without a length or hash wrapper.
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

  bool operator==(const OverloadHeaderDigest& other) const noexcept;
  bool operator!=(const OverloadHeaderDigest& other) const noexcept { return !(*this == other); }

private:
  explicit OverloadHeaderDigest(const Sha256Digest& digest) noexcept;

  Sha256Digest digestValue;
};

/// \brief Complete overload-header equality authority retained beside its digest index.
class OverloadHeaderAuthority final {
public:
  ~OverloadHeaderAuthority() noexcept(false);
  OverloadHeaderAuthority(OverloadHeaderAuthority&&) noexcept;
  OverloadHeaderAuthority& operator=(OverloadHeaderAuthority&&) noexcept;
  ZC_DISALLOW_COPY(OverloadHeaderAuthority);

  /// \brief Computes the digest and retains the complete canonical header.
  ZC_NODISCARD static OverloadHeaderAuthority from(CanonicalOverloadHeader&& header);
  ZC_NODISCARD OverloadHeaderAuthority clone() const;
  ZC_NODISCARD const OverloadHeaderDigest& digest() const noexcept;
  ZC_NODISCARD const CanonicalOverloadHeader& header() const noexcept;
  /// \brief Independently recomputes the digest from the retained complete header.
  ZC_NODISCARD bool verify() const;
  /// \brief Compares complete canonical header bytes rather than digest bytes.
  ZC_NODISCARD bool sameRecordAs(const OverloadHeaderAuthority& other) const;

private:
  explicit OverloadHeaderAuthority(
      zc::Own<overload_header_digest_detail::OverloadHeaderAuthorityData>&& impl) noexcept;

  zc::Own<overload_header_digest_detail::OverloadHeaderAuthorityData> impl;
};

}  // namespace zomlang::compiler::identity
