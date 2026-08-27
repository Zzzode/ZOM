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
#include "zc/core/vector.h"
#include "compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::identity {

namespace canonical_overload_header_detail {
struct CanonicalCallableResultData;
struct CanonicalGenericParameterData;
struct CanonicalBoundObligationData;
struct CanonicalCallableParameterData;
struct CanonicalOverloadHeaderData;

#define ZOM_CANONICAL_HEADER_SUM_VARIANT(sumName, variantName, tag) \
  ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_##sumName(variantName, tag)
#define ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalCallableResult(variantName, tag) \
  inline constexpr uint8_t CanonicalCallableResult_##variantName = tag;
#define ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalNameRoot(variantName, tag)
#define ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalHeaderTypeSyntax(variantName, tag)
#include "compiler/identity/canonical/canonical-header-syntax-schema.def"
#undef ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalHeaderTypeSyntax
#undef ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalNameRoot
#undef ZOM_CANONICAL_OVERLOAD_SUM_VARIANT_CanonicalCallableResult
#undef ZOM_CANONICAL_HEADER_SUM_VARIANT
}  // namespace canonical_overload_header_detail

enum class CanonicalCallableResultKind : uint8_t {
  Unit = canonical_overload_header_detail::CanonicalCallableResult_Unit,
  ConstructorSelf = canonical_overload_header_detail::CanonicalCallableResult_ConstructorSelf,
  Type = canonical_overload_header_detail::CanonicalCallableResult_Type
};

ZC_NODISCARD bool isCanonicalCallableResultKind(CanonicalCallableResultKind value) noexcept;

/// \brief Canonical unit, constructor-self, or explicit callable result.
class CanonicalCallableResult final {
public:
  ~CanonicalCallableResult() noexcept(false);
  CanonicalCallableResult(CanonicalCallableResult&&) noexcept;
  CanonicalCallableResult& operator=(CanonicalCallableResult&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalCallableResult);

  ZC_NODISCARD static CanonicalCallableResult unit();
  ZC_NODISCARD static CanonicalCallableResult constructorSelf();
  /// \brief Normalizes an explicit predefined Unit type to the canonical unit variant.
  ZC_NODISCARD static CanonicalCallableResult type(CanonicalHeaderTypeSyntax&& type);
  ZC_NODISCARD CanonicalCallableResult clone() const;
  ZC_NODISCARD CanonicalCallableResultKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> type() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalCallableResult(
      zc::Own<canonical_overload_header_detail::CanonicalCallableResultData>&& impl) noexcept;
  zc::Own<canonical_overload_header_detail::CanonicalCallableResultData> impl;
};

/// \brief One ordered canonical generic parameter record.
class CanonicalGenericParameter final {
public:
  ~CanonicalGenericParameter() noexcept(false);
  CanonicalGenericParameter(CanonicalGenericParameter&&) noexcept;
  CanonicalGenericParameter& operator=(CanonicalGenericParameter&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalGenericParameter);

  ZC_NODISCARD static CanonicalGenericParameter from(
      zc::Maybe<CanonicalHeaderTypeSyntax>&& defaultType);
  ZC_NODISCARD static zc::Maybe<CanonicalGenericParameter> decodeCanonical(
      CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalGenericParameter clone() const;
  ZC_NODISCARD zc::Maybe<const CanonicalHeaderTypeSyntax&> defaultType() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalGenericParameter(
      zc::Own<canonical_overload_header_detail::CanonicalGenericParameterData>&& impl) noexcept;
  zc::Own<canonical_overload_header_detail::CanonicalGenericParameterData> impl;
};

/// \brief Canonical subject-bound obligation sorted by complete encoded bytes.
class CanonicalBoundObligation final {
public:
  ~CanonicalBoundObligation() noexcept(false);
  CanonicalBoundObligation(CanonicalBoundObligation&&) noexcept;
  CanonicalBoundObligation& operator=(CanonicalBoundObligation&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalBoundObligation);

  ZC_NODISCARD static CanonicalBoundObligation from(CanonicalHeaderTypeSyntax&& subject,
                                                    CanonicalHeaderTypeSyntax&& bound);
  ZC_NODISCARD static zc::Maybe<CanonicalBoundObligation> decodeCanonical(
      CanonicalDecoder& decoder);
  ZC_NODISCARD CanonicalBoundObligation clone() const;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& subject() const noexcept;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& bound() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalBoundObligation(
      zc::Own<canonical_overload_header_detail::CanonicalBoundObligationData>&& impl) noexcept;
  zc::Own<canonical_overload_header_detail::CanonicalBoundObligationData> impl;
};

/// \brief One ordered canonical callable parameter record.
class CanonicalCallableParameter final {
public:
  ~CanonicalCallableParameter() noexcept(false);
  CanonicalCallableParameter(CanonicalCallableParameter&&) noexcept;
  CanonicalCallableParameter& operator=(CanonicalCallableParameter&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalCallableParameter);

  ZC_NODISCARD static CanonicalCallableParameter from(SemanticIdentifier&& label,
                                                      CanonicalHeaderTypeSyntax&& type,
                                                      bool hasDefault);
  ZC_NODISCARD CanonicalCallableParameter clone() const;
  ZC_NODISCARD zc::StringPtr label() const noexcept;
  ZC_NODISCARD const CanonicalHeaderTypeSyntax& type() const noexcept;
  ZC_NODISCARD bool hasDefault() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalCallableParameter(
      zc::Own<canonical_overload_header_detail::CanonicalCallableParameterData>&& impl) noexcept;
  zc::Own<canonical_overload_header_detail::CanonicalCallableParameterData> impl;
};

/// \brief Immutable admitted RFC 0018 canonical overload header.
class OverloadHeader final {
public:
  ~OverloadHeader() noexcept(false);
  OverloadHeader(OverloadHeader&&) noexcept;
  OverloadHeader& operator=(OverloadHeader&&) noexcept;
  ZC_DISALLOW_COPY(OverloadHeader);

  /// \brief Validates closed enums, rejects present-empty raises, and canonicalizes sets.
  /// Obligations and recursively union-flattened raises are sorted-unique by canonical bytes;
  /// generic and parameter sequences retain source order. Functions reject receivers and
  /// ConstructorSelf; methods reject ConstructorSelf and external ABIs; constructors require
  /// ConstructorSelf and reject receivers and external ABIs.
  ZC_NODISCARD static zc::Maybe<OverloadHeader> from(
      CallableHeaderKind callableKind, DeclaredDefinitionName&& name,
      zc::Maybe<ReceiverShape>&& receiver,
      zc::Vector<CanonicalGenericParameter>&& genericParameters,
      zc::Vector<CanonicalBoundObligation>&& obligations,
      zc::Vector<CanonicalCallableParameter>&& parameters, CanonicalCallableResult&& result,
      zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>&& raises,
      zc::Maybe<ExternalAbi>&& externalAbi);
  ZC_NODISCARD OverloadHeader clone() const;
  ZC_NODISCARD CallableHeaderKind callableKind() const noexcept;
  ZC_NODISCARD zc::StringPtr name() const noexcept;
  ZC_NODISCARD zc::Maybe<ReceiverShape> receiver() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalGenericParameter> genericParameters() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalBoundObligation> obligations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalCallableParameter> parameters() const noexcept;
  ZC_NODISCARD const CanonicalCallableResult& result() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> raises() const noexcept;
  ZC_NODISCARD zc::Maybe<ExternalAbi> externalAbi() const noexcept;
  void encode(CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit OverloadHeader(
      zc::Own<canonical_overload_header_detail::CanonicalOverloadHeaderData>&& impl) noexcept;
  zc::Own<canonical_overload_header_detail::CanonicalOverloadHeaderData> impl;
};

}  // namespace zomlang::compiler::identity
