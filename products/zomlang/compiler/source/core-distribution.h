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
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/package-key.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/type/semantic-type-data.h"

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

/// \brief Canonical role identity template independent from one compilation projection.
class CoreRoleIdentityTemplate final {
public:
  ZC_NODISCARD static zc::Maybe<CoreRoleIdentityTemplate> from(
      CoreSemanticRole role, zc::Vector<identity::ModulePathSegment>&& module,
      zc::Vector<identity::EnclosingStableOwnerKey>&& owners, identity::DefinitionKind kind,
      identity::DefinitionNamespace nameSpace, identity::DeclaredDefinitionName&& declaredName,
      zc::Maybe<identity::OverloadHeaderDigest>&& overloadHeader);

  ~CoreRoleIdentityTemplate() noexcept(false);
  CoreRoleIdentityTemplate(CoreRoleIdentityTemplate&&) noexcept;
  CoreRoleIdentityTemplate& operator=(CoreRoleIdentityTemplate&&) noexcept;
  ZC_DISALLOW_COPY(CoreRoleIdentityTemplate);

  ZC_NODISCARD CoreRoleIdentityTemplate clone() const;
  ZC_NODISCARD CoreSemanticRole role() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> module() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::EnclosingStableOwnerKey> owners() const noexcept;
  ZC_NODISCARD identity::DefinitionKind kind() const noexcept;
  ZC_NODISCARD identity::DefinitionNamespace nameSpace() const noexcept;
  ZC_NODISCARD zc::StringPtr declaredName() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::OverloadHeaderDigest&> overloadHeader() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  struct Impl;
  explicit CoreRoleIdentityTemplate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief One canonical core source path and exact content digest.
class CoreSourceFile final {
public:
  ZC_NODISCARD static CoreSourceFile from(identity::CanonicalRelativePath&& path,
                                          const identity::Sha256Digest& digest);

  CoreSourceFile(CoreSourceFile&&) noexcept = default;
  CoreSourceFile& operator=(CoreSourceFile&&) noexcept = default;
  ZC_DISALLOW_COPY(CoreSourceFile);

  ZC_NODISCARD CoreSourceFile clone() const;
  ZC_NODISCARD const identity::CanonicalRelativePath& path() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;

private:
  CoreSourceFile(identity::CanonicalRelativePath&& path,
                 const identity::Sha256Digest& digest) noexcept;

  identity::CanonicalRelativePath pathValue;
  identity::Sha256Digest digestValue;
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

/// \brief Canonical source, role, and edition authority for one compiler core distribution.
class CoreDistributionRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreDistributionRecord> from(
      uint32_t editionYear, identity::CanonicalRelativePath&& rootModule,
      identity::CanonicalRelativePath&& preludeModule, zc::Vector<CoreSourceFile>&& files,
      zc::Vector<CoreRoleIdentityTemplate>&& roles);
  ZC_NODISCARD static zc::Maybe<CoreDistributionRecord> decodeCanonical(
      identity::CanonicalDecoder& decoder);
  ZC_NODISCARD static zc::Maybe<CoreDistributionRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreDistributionRecord() noexcept(false);
  CoreDistributionRecord(CoreDistributionRecord&&) noexcept;
  CoreDistributionRecord& operator=(CoreDistributionRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreDistributionRecord);

  ZC_NODISCARD CoreDistributionRecord clone() const;
  ZC_NODISCARD uint32_t editionYear() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& rootModule() const noexcept;
  ZC_NODISCARD const identity::CanonicalRelativePath& preludeModule() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreSourceFile> files() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleIdentityTemplate> roles() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  struct Impl;
  explicit CoreDistributionRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable typed input value for distribution and policy query publication.
class CoreDistributionInputRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreDistributionInputRecord> from(
      CoreDistributionRecord&& record, const identity::Sha256Digest& digest,
      CoreStandardMarkerPolicyTemplate&& policyTemplate);
  ZC_NODISCARD static zc::Maybe<CoreDistributionInputRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreDistributionInputRecord() noexcept(false);
  CoreDistributionInputRecord(CoreDistributionInputRecord&&) noexcept;
  CoreDistributionInputRecord& operator=(CoreDistributionInputRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreDistributionInputRecord);

  ZC_NODISCARD CoreDistributionInputRecord clone() const;
  ZC_NODISCARD const CoreDistributionRecord& record() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD const CoreStandardMarkerPolicyTemplate& policyTemplate() const noexcept;
  void encode(identity::CanonicalEncoder& encoder) const;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  struct Impl;
  explicit CoreDistributionInputRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Computes SHA-256("zom.core-distribution" || 0x00 || Encode(record)).
ZC_NODISCARD zc::Maybe<identity::Sha256Digest> computeCoreDistributionDigest(
    const CoreDistributionRecord& record);

/// \brief Constructs the fixed RFC 0025 initial distribution record.
ZC_NODISCARD zc::Maybe<CoreDistributionRecord> initialCoreDistributionRecord();

/// \brief Constructs the fixed RFC 0025 initial marker policy template.
ZC_NODISCARD zc::Maybe<CoreStandardMarkerPolicyTemplate> initialCoreMarkerPolicyTemplate();

/// \brief Constructs the complete fixed RFC 0025 initial distribution input.
ZC_NODISCARD zc::Maybe<CoreDistributionInputRecord> initialCoreDistributionInput();

}  // namespace zomlang::compiler::source::core
