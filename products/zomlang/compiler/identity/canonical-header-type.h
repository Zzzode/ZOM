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
#include "zomlang/compiler/identity/canonical-header-name.h"

namespace zomlang::compiler::identity {

namespace canonical_header_type_detail {
struct CanonicalNamedHeaderTypeData;
struct CanonicalObjectTypeMemberData;
struct CanonicalAssociatedBindingData;
struct CanonicalHeaderTypeSyntaxData;

#define ZOM_CANONICAL_HEADER_SUM_VARIANT(sumName, variantName, tag) \
  ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_##sumName(variantName, tag)
#define ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalCallableResult(variantName, tag)
#define ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalNameRoot(variantName, tag)
#define ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalHeaderTypeSyntax(variantName, tag) \
  inline constexpr uint8_t CanonicalHeaderTypeSyntax_##variantName = tag;
#include "zomlang/compiler/identity/canonical-header-syntax-schema.def"
#undef ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalHeaderTypeSyntax
#undef ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalNameRoot
#undef ZOM_CANONICAL_HEADER_TYPE_SUM_VARIANT_CanonicalCallableResult
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT
}  // namespace canonical_header_type_detail

enum class CanonicalHeaderTypeSyntaxKind : uint8_t {
  Named = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Named,
  Predefined = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Predefined,
  Function = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Function,
  Union = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Union,
  Intersection = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Intersection,
  FixedArray = canonical_header_type_detail::CanonicalHeaderTypeSyntax_FixedArray,
  DynamicArray = canonical_header_type_detail::CanonicalHeaderTypeSyntax_DynamicArray,
  Slice = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Slice,
  Optional = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Optional,
  Reference = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Reference,
  RawPointer = canonical_header_type_detail::CanonicalHeaderTypeSyntax_RawPointer,
  TypeQuery = canonical_header_type_detail::CanonicalHeaderTypeSyntax_TypeQuery,
  Object = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Object,
  Tuple = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Tuple,
  AssociatedProjection =
      canonical_header_type_detail::CanonicalHeaderTypeSyntax_AssociatedProjection,
  Dynamic = canonical_header_type_detail::CanonicalHeaderTypeSyntax_Dynamic
};

ZC_NODISCARD bool isCanonicalHeaderTypeSyntaxKind(CanonicalHeaderTypeSyntaxKind value) noexcept;

class CanonicalHeaderTypeSyntax;

/// \brief Canonical named type principal with ordered type arguments.
class CanonicalNamedHeaderType final {
public:
  ~CanonicalNamedHeaderType() noexcept(false);
  CanonicalNamedHeaderType(CanonicalNamedHeaderType&&) noexcept;
  CanonicalNamedHeaderType& operator=(CanonicalNamedHeaderType&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalNamedHeaderType);

  ZC_NODISCARD static CanonicalNamedHeaderType from(
      CanonicalNameReference&& name, zc::Vector<CanonicalHeaderTypeSyntax>&& arguments);
  ZC_NODISCARD CanonicalNamedHeaderType clone() const;
  ZC_NODISCARD const CanonicalNameReference& name() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalHeaderTypeSyntax> arguments() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalNamedHeaderType(
      zc::Own<canonical_header_type_detail::CanonicalNamedHeaderTypeData>&& impl) noexcept;
  zc::Own<canonical_header_type_detail::CanonicalNamedHeaderTypeData> impl;
};

/// \brief Canonical object member sorted by its complete encoded bytes.
class CanonicalObjectTypeMember final {
public:
  ~CanonicalObjectTypeMember() noexcept(false);
  CanonicalObjectTypeMember(CanonicalObjectTypeMember&&) noexcept;
  CanonicalObjectTypeMember& operator=(CanonicalObjectTypeMember&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalObjectTypeMember);

  ZC_NODISCARD static CanonicalObjectTypeMember from(SemanticIdentifier&& name,
                                                     CanonicalHeaderTypeSyntax&& type,
                                                     bool isMutable, bool isOptional);
  ZC_NODISCARD CanonicalObjectTypeMember clone() const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& type() const noexcept;
  ZC_NODISCARD bool isMutable() const noexcept;
  ZC_NODISCARD bool isOptional() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalObjectTypeMember(
      zc::Own<canonical_header_type_detail::CanonicalObjectTypeMemberData>&& impl) noexcept;
  zc::Own<canonical_header_type_detail::CanonicalObjectTypeMemberData> impl;
};

/// \brief Canonical dynamic-type associated binding.
class CanonicalAssociatedBinding final {
public:
  ~CanonicalAssociatedBinding() noexcept(false);
  CanonicalAssociatedBinding(CanonicalAssociatedBinding&&) noexcept;
  CanonicalAssociatedBinding& operator=(CanonicalAssociatedBinding&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalAssociatedBinding);

  ZC_NODISCARD static CanonicalAssociatedBinding from(SemanticIdentifier&& name,
                                                      CanonicalHeaderTypeSyntax&& type);
  ZC_NODISCARD CanonicalAssociatedBinding clone() const;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& type() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalAssociatedBinding(
      zc::Own<canonical_header_type_detail::CanonicalAssociatedBindingData>&& impl) noexcept;
  zc::Own<canonical_header_type_detail::CanonicalAssociatedBindingData> impl;
};

/// \brief Immutable normalized RFC 0018 canonical header type syntax.
class CanonicalHeaderTypeSyntax final {
public:
  ~CanonicalHeaderTypeSyntax() noexcept(false);
  CanonicalHeaderTypeSyntax(CanonicalHeaderTypeSyntax&&) noexcept;
  CanonicalHeaderTypeSyntax& operator=(CanonicalHeaderTypeSyntax&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalHeaderTypeSyntax);

  ZC_NODISCARD static CanonicalHeaderTypeSyntax named(CanonicalNamedHeaderType&& type);
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> predefined(PredefinedTypeKind kind);
  /// \brief Rejects a present empty raises set; sorts and deduplicates present raises.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> function(
      zc::Vector<CanonicalHeaderTypeSyntax>&& parameters, CanonicalHeaderTypeSyntax&& result,
      zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>&& raises);
  /// \brief Rejects empty input, then flattens, sorts, deduplicates, and collapses singletons.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> unionOf(
      zc::Vector<CanonicalHeaderTypeSyntax>&& members);
  /// \brief Rejects empty input, then flattens, sorts, deduplicates, and collapses singletons.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> intersectionOf(
      zc::Vector<CanonicalHeaderTypeSyntax>&& members);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax fixedArray(CanonicalHeaderTypeSyntax&& element,
                                                           uint64_t evaluatedLength);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax dynamicArray(CanonicalHeaderTypeSyntax&& element);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax slice(CanonicalHeaderTypeSyntax&& element);
  /// \brief Admits only depth 0x01 or 0x02.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> optional(
      CanonicalHeaderTypeSyntax&& element, uint8_t depth);
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> reference(
      ReferenceMutability mutability, CanonicalHeaderTypeSyntax&& element);
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> rawPointer(
      RawPointerMutability mutability, CanonicalHeaderTypeSyntax&& element);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax typeQuery(CanonicalNameReference&& name);
  /// \brief Sorts and deduplicates members by complete canonical bytes.
  ZC_NODISCARD static CanonicalHeaderTypeSyntax object(
      zc::Vector<CanonicalObjectTypeMember>&& members);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax tuple(
      zc::Vector<CanonicalHeaderTypeSyntax>&& elements);
  ZC_NODISCARD static CanonicalHeaderTypeSyntax associatedProjection(
      CanonicalHeaderTypeSyntax&& base, zc::Maybe<CanonicalHeaderTypeSyntax>&& interfaceType,
      SemanticIdentifier&& member);
  /// \brief Sorts and deduplicates markers and bindings by complete canonical bytes.
  ZC_NODISCARD static CanonicalHeaderTypeSyntax dynamic(
      CanonicalNamedHeaderType&& principal, zc::Vector<CanonicalNameReference>&& markers,
      zc::Vector<CanonicalAssociatedBinding>&& associatedBindings);

  /// \brief Decodes one inline canonical type with a maximum nesting depth of 100.
  ZC_NODISCARD static zc::Maybe<CanonicalHeaderTypeSyntax> decodeCanonical(
      CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalHeaderTypeSyntax clone() const;
  ZC_NODISCARD CanonicalHeaderTypeSyntaxKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalNamedHeaderType&> namedType() const noexcept;
  ZC_NODISCARD zc::Maybe<PredefinedTypeKind> predefinedKind() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> functionParameters()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> functionResult() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> functionRaises()
      const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> members() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> element() const noexcept;
  ZC_NODISCARD zc::Maybe<uint64_t> fixedArrayLength() const noexcept;
  ZC_NODISCARD zc::Maybe<uint8_t> optionalDepth() const noexcept;
  ZC_NODISCARD zc::Maybe<ReferenceMutability> referenceMutability() const noexcept;
  ZC_NODISCARD zc::Maybe<RawPointerMutability> rawPointerMutability() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalNameReference&> typeQueryName() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalObjectTypeMember>> objectMembers()
      const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> tupleElements()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> associatedBase() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> associatedInterface() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::StringPtr> associatedMember() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalNamedHeaderType&> dynamicPrincipal() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalNameReference>> dynamicMarkers()
      const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalAssociatedBinding>> dynamicBindings()
      const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalHeaderTypeSyntax(
      zc::Own<canonical_header_type_detail::CanonicalHeaderTypeSyntaxData>&& impl) noexcept;
  zc::Own<canonical_header_type_detail::CanonicalHeaderTypeSyntaxData> impl;
};

}  // namespace zomlang::compiler::identity
