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
#include "zomlang/compiler/identity/key/source-key.h"

namespace zomlang::compiler::identity {

/// \brief Source-qualified structural range validated before context handles exist.
class UnbrandedSourceRange final {
public:
  UnbrandedSourceRange(UnbrandedSourceRange&&) noexcept = default;
  UnbrandedSourceRange& operator=(UnbrandedSourceRange&&) noexcept = default;
  ZC_DISALLOW_COPY(UnbrandedSourceRange);

  ZC_NODISCARD UnbrandedSourceRange clone() const;
  ZC_NODISCARD bool belongsTo(const SourceFileKey& source) const;
  ZC_NODISCARD const Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t byteStart() const noexcept;
  ZC_NODISCARD uint64_t byteEnd() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  UnbrandedSourceRange(SourceFileKey&& source, const Sha256Digest& contentDigest,
                       uint64_t byteStart, uint64_t byteEnd) noexcept;

  SourceFileKey sourceValue;
  Sha256Digest digestValue;
  uint64_t startValue;
  uint64_t endValue;

  friend class ImmutableSourceSnapshot;
};

/// \brief Immutable source bytes and digest selected by one canonical source key.
class ImmutableSourceSnapshot final {
public:
  ImmutableSourceSnapshot(ImmutableSourceSnapshot&&) noexcept = default;
  ImmutableSourceSnapshot& operator=(ImmutableSourceSnapshot&&) noexcept = default;
  ZC_DISALLOW_COPY(ImmutableSourceSnapshot);

  /// \brief Admits source bytes and computes their immutable SHA-256 digest.
  ZC_NODISCARD static zc::Maybe<ImmutableSourceSnapshot> from(SourceFileKey&& source,
                                                              zc::Array<uint8_t>&& bytes);
  ZC_NODISCARD ImmutableSourceSnapshot clone() const;
  ZC_NODISCARD const SourceFileKey& source() const noexcept;
  ZC_NODISCARD const Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;
  ZC_NODISCARD zc::Maybe<UnbrandedSourceRange> unbrandedRange(uint64_t byteStart,
                                                              uint64_t byteEnd) const;
  ZC_NODISCARD zc::Maybe<SourceSpan> span(uint64_t byteStart, uint64_t byteEnd) const;

private:
  ImmutableSourceSnapshot(SourceFileKey&& source, const Sha256Digest& contentDigest,
                          zc::Array<uint8_t>&& bytes) noexcept;

  SourceFileKey sourceValue;
  Sha256Digest digestValue;
  zc::Array<uint8_t> bytesValue;
};

}  // namespace zomlang::compiler::identity
