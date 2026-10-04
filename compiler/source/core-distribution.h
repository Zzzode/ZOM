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

#include "compiler/identity/crypto/sha256.h"
#include "compiler/type/semantic-type-data.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::identity {
class CanonicalDecoder;
class CanonicalEncoder;
}  // namespace zomlang::compiler::identity

namespace zomlang::compiler::source::core {

/// \brief Closed semantic roles authenticated by the compiler core distribution.
enum class CoreSemanticRole : uint8_t { Copy = 0x01, Linear = 0x02 };

/// \brief Closed structural-subject tags carried by the core marker policy template.
enum class CoreMarkerStructuralSubject : uint8_t {
  Tuple = 0x01,
  Object = 0x02,
  FixedArray = 0x03,
  NominalStruct = 0x04,
  NominalEnum = 0x05
};

/// \brief Closed rule kind for a reference in the core marker policy template.
enum class CoreMarkerReferenceTemplateRuleKind : uint8_t { Unconditional = 0x01, Requires = 0x02 };

/// \brief One role-based reference rule in the distribution policy template.
class CoreMarkerReferenceTemplateRule final {
public:
  ZC_NODISCARD static CoreMarkerReferenceTemplateRule unconditional();
  ZC_NODISCARD static CoreMarkerReferenceTemplateRule required(CoreSemanticRole role);

  CoreMarkerReferenceTemplateRule(CoreMarkerReferenceTemplateRule&&) noexcept = default;
  CoreMarkerReferenceTemplateRule& operator=(CoreMarkerReferenceTemplateRule&&) noexcept = default;
  ZC_DISALLOW_COPY(CoreMarkerReferenceTemplateRule);

  ZC_NODISCARD CoreMarkerReferenceTemplateRule clone() const;
  ZC_NODISCARD CoreMarkerReferenceTemplateRuleKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<CoreSemanticRole> requiredRole() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  CoreMarkerReferenceTemplateRule(CoreMarkerReferenceTemplateRuleKind kind,
                                  zc::Maybe<CoreSemanticRole> role) noexcept;

  CoreMarkerReferenceTemplateRuleKind kindValue;
  zc::Maybe<CoreSemanticRole> roleValue;
};

/// \brief Canonical reference-rule map entry sorted by mutability tag.
struct CoreMarkerReferenceTemplateEntry final {
  type::semantic::Mutability mutability;
  CoreMarkerReferenceTemplateRule rule;

  ZC_NODISCARD CoreMarkerReferenceTemplateEntry clone() const;
  void encode(identity::CanonicalEncoder& encoder) const;
};

/// \brief One role policy before semantic definitions are resolved.
class CoreMarkerPolicyTemplate final {
public:
  ZC_NODISCARD static zc::Maybe<CoreMarkerPolicyTemplate> from(
      zc::Vector<CoreMarkerStructuralSubject>&& structuralSubjects,
      zc::Vector<type::semantic::PrimitiveKind>&& builtinPrimitives,
      zc::Vector<CoreMarkerReferenceTemplateEntry>&& referenceRules,
      zc::Vector<type::semantic::Mutability>&& rawPointerMutabilities);

  ~CoreMarkerPolicyTemplate() noexcept(false);
  CoreMarkerPolicyTemplate(CoreMarkerPolicyTemplate&&) noexcept;
  CoreMarkerPolicyTemplate& operator=(CoreMarkerPolicyTemplate&&) noexcept;
  ZC_DISALLOW_COPY(CoreMarkerPolicyTemplate);

  ZC_NODISCARD CoreMarkerPolicyTemplate clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CoreMarkerStructuralSubject> structuralSubjects() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const type::semantic::PrimitiveKind> builtinPrimitives() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreMarkerReferenceTemplateEntry> referenceRules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const type::semantic::Mutability> rawPointerMutabilities()
      const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  struct Impl;
  explicit CoreMarkerPolicyTemplate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Canonical role-to-policy entry sorted by role tag.
struct CoreMarkerPolicyTemplateEntry final {
  CoreSemanticRole role;
  CoreMarkerPolicyTemplate policy;

  ZC_NODISCARD CoreMarkerPolicyTemplateEntry clone() const;
  void encode(identity::CanonicalEncoder& encoder) const;
};

/// \brief Immutable accepted role-keyed policy template and its domain revision.
class CoreStandardMarkerPolicyTemplate final {
public:
  ZC_NODISCARD static zc::Maybe<CoreStandardMarkerPolicyTemplate> from(
      zc::Vector<CoreMarkerPolicyTemplateEntry>&& entries);
  ZC_NODISCARD static zc::Maybe<CoreStandardMarkerPolicyTemplate> decodeCanonical(
      identity::CanonicalDecoder& decoder);
  ZC_NODISCARD static zc::Maybe<CoreStandardMarkerPolicyTemplate> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreStandardMarkerPolicyTemplate() noexcept(false);
  CoreStandardMarkerPolicyTemplate(CoreStandardMarkerPolicyTemplate&&) noexcept;
  CoreStandardMarkerPolicyTemplate& operator=(CoreStandardMarkerPolicyTemplate&&) noexcept;
  ZC_DISALLOW_COPY(CoreStandardMarkerPolicyTemplate);

  ZC_NODISCARD CoreStandardMarkerPolicyTemplate clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CoreMarkerPolicyTemplateEntry> entries() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& revision() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  struct Impl;
  explicit CoreStandardMarkerPolicyTemplate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Constructs the fixed initial marker policy template.
ZC_NODISCARD zc::Maybe<CoreStandardMarkerPolicyTemplate> initialCoreMarkerPolicyTemplate();

}  // namespace zomlang::compiler::source::core
