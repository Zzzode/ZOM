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
#include "zc/core/vector.h"
#include "compiler/identity/canonical/canonical-scalar.h"

namespace zomlang::compiler::identity {

class CanonicalDecoder;
class CanonicalEncoder;

namespace canonical_header_schema_detail {
#define ZOM_CANONICAL_HEADER_ENUM_VALUE(enumName, valueName, tag) \
  ZOM_CANONICAL_HEADER_ENUM_VALUE_##enumName(valueName, tag)
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_CallableHeaderKind(valueName, tag) \
  inline constexpr uint8_t CallableHeaderKind_##valueName = tag;
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_ReceiverShape(valueName, tag) \
  inline constexpr uint8_t ReceiverShape_##valueName = tag;
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_ExternalAbi(valueName, tag) \
  inline constexpr uint8_t ExternalAbi_##valueName = tag;
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_PredefinedTypeKind(valueName, tag) \
  inline constexpr uint8_t PredefinedTypeKind_##valueName = tag;
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_ReferenceMutability(valueName, tag) \
  inline constexpr uint8_t ReferenceMutability_##valueName = tag;
#define ZOM_CANONICAL_HEADER_ENUM_VALUE_RawPointerMutability(valueName, tag) \
  inline constexpr uint8_t RawPointerMutability_##valueName = tag;
#define ZOM_CANONICAL_HEADER_SUM_VARIANT(sumName, variantName, tag) \
  ZOM_CANONICAL_HEADER_SUM_VARIANT_##sumName(variantName, tag)
#define ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalCallableResult(variantName, tag)
#define ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalNameRoot(variantName, tag) \
  inline constexpr uint8_t CanonicalNameRoot_##variantName = tag;
#define ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalHeaderTypeSyntax(variantName, tag)
#include "compiler/identity/canonical/canonical-header-syntax-schema.def"
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalHeaderTypeSyntax
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalNameRoot
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT_CanonicalCallableResult
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_RawPointerMutability
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_ReferenceMutability
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_PredefinedTypeKind
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_ExternalAbi
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_ReceiverShape
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_CallableHeaderKind
#undef ZOM_CANONICAL_HEADER_ENUM_VALUE
}  // namespace canonical_header_schema_detail

enum class CallableHeaderKind : uint8_t {
  Function = canonical_header_schema_detail::CallableHeaderKind_Function,
  Method = canonical_header_schema_detail::CallableHeaderKind_Method,
  Constructor = canonical_header_schema_detail::CallableHeaderKind_Constructor
};

enum class ReceiverShape : uint8_t {
  Shared = canonical_header_schema_detail::ReceiverShape_Shared,
  Mutable = canonical_header_schema_detail::ReceiverShape_Mutable,
  Move = canonical_header_schema_detail::ReceiverShape_Move
};

enum class ExternalAbi : uint8_t {
  Cdecl = canonical_header_schema_detail::ExternalAbi_Cdecl,
  Stdcall = canonical_header_schema_detail::ExternalAbi_Stdcall,
  ZomNative = canonical_header_schema_detail::ExternalAbi_ZomNative
};

enum class PredefinedTypeKind : uint8_t {
  I8 = canonical_header_schema_detail::PredefinedTypeKind_I8,
  I16 = canonical_header_schema_detail::PredefinedTypeKind_I16,
  I32 = canonical_header_schema_detail::PredefinedTypeKind_I32,
  I64 = canonical_header_schema_detail::PredefinedTypeKind_I64,
  U8 = canonical_header_schema_detail::PredefinedTypeKind_U8,
  U16 = canonical_header_schema_detail::PredefinedTypeKind_U16,
  U32 = canonical_header_schema_detail::PredefinedTypeKind_U32,
  U64 = canonical_header_schema_detail::PredefinedTypeKind_U64,
  F32 = canonical_header_schema_detail::PredefinedTypeKind_F32,
  F64 = canonical_header_schema_detail::PredefinedTypeKind_F64,
  Bool = canonical_header_schema_detail::PredefinedTypeKind_Bool,
  Str = canonical_header_schema_detail::PredefinedTypeKind_Str,
  Char = canonical_header_schema_detail::PredefinedTypeKind_Char,
  Null = canonical_header_schema_detail::PredefinedTypeKind_Null,
  Unit = canonical_header_schema_detail::PredefinedTypeKind_Unit,
  Never = canonical_header_schema_detail::PredefinedTypeKind_Never,
  Any = canonical_header_schema_detail::PredefinedTypeKind_Any
};

enum class ReferenceMutability : uint8_t {
  Shared = canonical_header_schema_detail::ReferenceMutability_Shared,
  Mutable = canonical_header_schema_detail::ReferenceMutability_Mutable
};

enum class RawPointerMutability : uint8_t {
  Const = canonical_header_schema_detail::RawPointerMutability_Const,
  Mutable = canonical_header_schema_detail::RawPointerMutability_Mutable
};

enum class CanonicalNameRootKind : uint8_t {
  Absolute = canonical_header_schema_detail::CanonicalNameRoot_Absolute,
  Relative = canonical_header_schema_detail::CanonicalNameRoot_Relative,
  Generic = canonical_header_schema_detail::CanonicalNameRoot_Generic
};

ZC_NODISCARD bool isCanonicalHeaderValue(CallableHeaderKind value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(ReceiverShape value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(ExternalAbi value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(PredefinedTypeKind value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(ReferenceMutability value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(RawPointerMutability value) noexcept;
ZC_NODISCARD bool isCanonicalHeaderValue(CanonicalNameRootKind value) noexcept;

/// \brief Closed absolute, relative, or binder-relative generic name root.
class CanonicalNameRoot final {
public:
  CanonicalNameRoot(CanonicalNameRoot&&) noexcept = default;
  CanonicalNameRoot& operator=(CanonicalNameRoot&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalNameRoot);

  ZC_NODISCARD static CanonicalNameRoot absolute() noexcept;
  ZC_NODISCARD static CanonicalNameRoot relative() noexcept;
  ZC_NODISCARD static CanonicalNameRoot generic(uint32_t binderDepth, uint32_t ordinal) noexcept;
  /// \brief Decodes one inline closed canonical name root.
  ZC_NODISCARD static zc::Maybe<CanonicalNameRoot> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalNameRoot clone() const noexcept;
  ZC_NODISCARD CanonicalNameRootKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<uint32_t> binderDepth() const noexcept;
  ZC_NODISCARD zc::Maybe<uint32_t> ordinal() const noexcept;
  void encode(CanonicalEncoder& encoder) const;

private:
  CanonicalNameRoot(CanonicalNameRootKind kind, uint32_t binderDepth, uint32_t ordinal) noexcept;

  CanonicalNameRootKind kindValue;
  uint32_t binderDepthValue;
  uint32_t ordinalValue;
};

/// \brief Immutable canonical name root and ordered NFC identifier suffix.
class CanonicalNameReference final {
public:
  CanonicalNameReference(CanonicalNameReference&&) noexcept = default;
  CanonicalNameReference& operator=(CanonicalNameReference&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalNameReference);

  ZC_NODISCARD static zc::Maybe<CanonicalNameReference> from(
      CanonicalNameRoot&& root, zc::Vector<SemanticIdentifier>&& suffix);
  /// \brief Decodes one inline root and bounded ordered identifier suffix.
  ZC_NODISCARD static zc::Maybe<CanonicalNameReference> decodeCanonical(CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalNameReference clone() const;
  ZC_NODISCARD const CanonicalNameRoot& root() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const SemanticIdentifier> suffix() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  CanonicalNameReference(CanonicalNameRoot&& root,
                         zc::Vector<SemanticIdentifier>&& suffix) noexcept;

  CanonicalNameRoot rootValue;
  zc::Vector<SemanticIdentifier> suffixValue;
};

}  // namespace zomlang::compiler::identity
