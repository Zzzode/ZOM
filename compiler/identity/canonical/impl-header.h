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
#include "zc/core/vector.h"
#include "compiler/identity/canonical/header-type.h"
#include "compiler/identity/canonical/overload-header.h"

namespace zomlang::compiler::identity {

namespace canonical_impl_header_detail {
struct CanonicalTraitReferenceData;
struct CanonicalImplHeaderData;
}  // namespace canonical_impl_header_detail

enum class ImplPolarity : uint8_t { Positive = 0x01, Negative = 0x02 };

enum class ImplSafety : uint8_t { Safe = 0x01, Unsafe = 0x02 };

ZC_NODISCARD bool isCanonicalImplHeaderValue(ImplPolarity value) noexcept;
ZC_NODISCARD bool isCanonicalImplHeaderValue(ImplSafety value) noexcept;

/// \brief Canonical implemented-trait name and declaration-ordered type arguments.
class CanonicalTraitReference final {
public:
  ~CanonicalTraitReference() noexcept(false);
  CanonicalTraitReference(CanonicalTraitReference&&) noexcept;
  CanonicalTraitReference& operator=(CanonicalTraitReference&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalTraitReference);

  /// \brief Admits only absolute or relative trait name roots.
  ZC_NODISCARD static zc::Maybe<CanonicalTraitReference> from(
      CanonicalNameReference&& name, zc::Vector<CanonicalHeaderTypeSyntax>&& arguments);
  ZC_NODISCARD static zc::Maybe<CanonicalTraitReference> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalTraitReference clone() const;
  ZC_NODISCARD const CanonicalNameReference& name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalHeaderTypeSyntax> arguments() const noexcept;
  /// \brief Encodes the name followed by the ordered argument sequence without a wrapper.
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalTraitReference(
      zc::Own<canonical_impl_header_detail::CanonicalTraitReferenceData>&& impl) noexcept;

  zc::Own<canonical_impl_header_detail::CanonicalTraitReferenceData> impl;
};

/// \brief Canonical syntax-owned suffix of an RFC 0018 implementation identity record.
///
/// Its codec writes the generic parameters, polarity, safety, trait reference, self type,
/// and obligations directly in record order. The enclosing ImplIdentityRecord prepends the
/// expanded module and enclosing-owner sequence without a nested wrapper.
class ImplHeader final {
public:
  ~ImplHeader() noexcept(false);
  ImplHeader(ImplHeader&&) noexcept;
  ImplHeader& operator=(ImplHeader&&) noexcept;
  ZC_DISALLOW_COPY(ImplHeader);

  /// \brief Validates closed tags and canonicalizes obligations by complete encoded bytes.
  /// Generic parameters and trait arguments retain declaration order.
  ZC_NODISCARD static zc::Maybe<ImplHeader> from(
      zc::Vector<CanonicalGenericParameter>&& genericParameters, ImplPolarity polarity,
      ImplSafety safety, CanonicalTraitReference&& trait, CanonicalHeaderTypeSyntax&& selfType,
      zc::Vector<CanonicalBoundObligation>&& obligations);
  ZC_NODISCARD static zc::Maybe<ImplHeader> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD ImplHeader clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalGenericParameter> genericParameters() const noexcept;
  ZC_NODISCARD ImplPolarity polarity() const noexcept;
  ZC_NODISCARD ImplSafety safety() const noexcept;
  ZC_NODISCARD const CanonicalTraitReference& trait() const noexcept;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& selfType() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalBoundObligation> obligations() const noexcept;
  /// \brief Encodes fields inline without a record or byte-string wrapper.
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ImplHeader(
      zc::Own<canonical_impl_header_detail::CanonicalImplHeaderData>&& impl) noexcept;

  zc::Own<canonical_impl_header_detail::CanonicalImplHeaderData> impl;
};

}  // namespace zomlang::compiler::identity
