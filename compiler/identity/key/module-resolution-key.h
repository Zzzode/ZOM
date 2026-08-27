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
#include "compiler/identity/key/source-key.h"

namespace zomlang::compiler::identity {

/// \brief Stable key for one crate-local canonical module-path catalog bucket.
class ModuleCatalogPathBucketKey final {
public:
  ModuleCatalogPathBucketKey(ModuleCatalogPathBucketKey&&) noexcept = default;
  ModuleCatalogPathBucketKey& operator=(ModuleCatalogPathBucketKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleCatalogPathBucketKey);

  /// \brief Validates and constructs a bucket key with a non-empty canonical path.
  ZC_NODISCARD static zc::Maybe<ModuleCatalogPathBucketKey> from(
      CrateKey&& crate, zc::Vector<ModulePathSegment>&& path);
  /// \brief Decodes one exact bounded domain-separated bucket key.
  ZC_NODISCARD static zc::Maybe<ModuleCatalogPathBucketKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleCatalogPathBucketKey clone() const;
  ZC_NODISCARD const CrateKey& crate() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModulePathSegment> path() const noexcept;
  /// \brief Encodes the exact domain-separated RFC 0018 bucket-key bytes.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ModuleCatalogPathBucketKey(CrateKey&& crate, zc::Vector<ModulePathSegment>&& path) noexcept;

  CrateKey crateValue;
  zc::Vector<ModulePathSegment> pathValue;
};

/// \brief Independently admitted requester-to-crate-root module ancestry input.
class RequesterModuleAncestry final {
public:
  RequesterModuleAncestry(RequesterModuleAncestry&&) noexcept = default;
  RequesterModuleAncestry& operator=(RequesterModuleAncestry&&) noexcept = default;
  ZC_DISALLOW_COPY(RequesterModuleAncestry);

  /// \brief Requires a non-empty exact requester-first chain of strict lexical parents.
  ZC_NODISCARD static zc::Maybe<RequesterModuleAncestry> from(ModuleKey&& requester,
                                                              zc::Vector<ModuleKey>&& ancestry);
  /// \brief Decodes one exact bounded requester-first ancestry value.
  ZC_NODISCARD static zc::Maybe<RequesterModuleAncestry> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD RequesterModuleAncestry clone() const;
  ZC_NODISCARD const ModuleKey& requester() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleKey> ancestry() const noexcept;
  /// \brief Encodes the query value as one complete module-key sequence.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  RequesterModuleAncestry(ModuleKey&& requester, zc::Vector<ModuleKey>&& ancestry) noexcept;

  ModuleKey requesterValue;
  zc::Vector<ModuleKey> ancestryValue;
};

/// \brief Independently admitted absent-or-present exact module-catalog path bucket input.
class ModuleCatalogPathBucket final {
public:
  ModuleCatalogPathBucket(ModuleCatalogPathBucket&&) noexcept = default;
  ModuleCatalogPathBucket& operator=(ModuleCatalogPathBucket&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleCatalogPathBucket);

  ZC_NODISCARD static ModuleCatalogPathBucket absent(ModuleCatalogPathBucketKey&& key);
  /// \brief Requires the present module to equal the bucket crate and complete path.
  ZC_NODISCARD static zc::Maybe<ModuleCatalogPathBucket> present(ModuleCatalogPathBucketKey&& key,
                                                                 ModuleKey&& module);
  /// \brief Decodes one exact optional module value and validates it against the supplied key.
  ZC_NODISCARD static zc::Maybe<ModuleCatalogPathBucket> decodeCanonical(
      ModuleCatalogPathBucketKey&& key, zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleCatalogPathBucket clone() const;
  ZC_NODISCARD const ModuleCatalogPathBucketKey& key() const noexcept;
  ZC_NODISCARD zc::Maybe<const ModuleKey&> module() const noexcept;
  /// \brief Encodes only the optional query value; the bucket key remains the query key.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ModuleCatalogPathBucket(ModuleCatalogPathBucketKey&& key, zc::Maybe<ModuleKey>&& module) noexcept;

  ModuleCatalogPathBucketKey keyValue;
  zc::Maybe<ModuleKey> moduleValue;
};

enum class UnicodeNormalizationPolicy : uint8_t { Nfc = 0x01 };
enum class CaseComparisonPolicy : uint8_t { CaseSensitive = 0x01 };
enum class SymlinkHandlingPolicy : uint8_t { ResolveThenConfine = 0x01 };
enum class ModuleContainmentPolicy : uint8_t { DeclaredRootsOnly = 0x01 };
enum class LocalModuleLookupPolicy : uint8_t { RequesterAncestryAndCrateRoot = 0x01 };
enum class DependencyAliasLookupPolicy : uint8_t { ExactFirstSegment = 0x01 };
enum class PreludeLookupPolicy : uint8_t { ConfiguredCratePrelude = 0x01 };
enum class ModuleCandidateSelectionPolicy : uint8_t { AllDistinctMatchesNoPrecedence = 0x01 };

/// \brief Complete RFC 0018 module-resolution policy key.
class ModuleResolutionPolicyKey final {
public:
  ModuleResolutionPolicyKey(ModuleResolutionPolicyKey&&) noexcept = default;
  ModuleResolutionPolicyKey& operator=(ModuleResolutionPolicyKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleResolutionPolicyKey);

  /// \brief Validates and constructs the closed eight-field policy record.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionPolicyKey> from(
      UnicodeNormalizationPolicy unicodeNormalization, CaseComparisonPolicy caseComparison,
      SymlinkHandlingPolicy symlinkHandling, ModuleContainmentPolicy containment,
      LocalModuleLookupPolicy localLookup, DependencyAliasLookupPolicy dependencyAliasLookup,
      PreludeLookupPolicy preludeLookup,
      ModuleCandidateSelectionPolicy candidateSelection) noexcept;
  /// \brief Decodes one exact domain-separated closed policy record.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionPolicyKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleResolutionPolicyKey clone() const noexcept;
  ZC_NODISCARD UnicodeNormalizationPolicy unicodeNormalization() const noexcept;
  ZC_NODISCARD CaseComparisonPolicy caseComparison() const noexcept;
  ZC_NODISCARD SymlinkHandlingPolicy symlinkHandling() const noexcept;
  ZC_NODISCARD ModuleContainmentPolicy containment() const noexcept;
  ZC_NODISCARD LocalModuleLookupPolicy localLookup() const noexcept;
  ZC_NODISCARD DependencyAliasLookupPolicy dependencyAliasLookup() const noexcept;
  ZC_NODISCARD PreludeLookupPolicy preludeLookup() const noexcept;
  ZC_NODISCARD ModuleCandidateSelectionPolicy candidateSelection() const noexcept;
  /// \brief Encodes the exact domain-separated RFC 0018 policy bytes.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ModuleResolutionPolicyKey(UnicodeNormalizationPolicy unicodeNormalization,
                            CaseComparisonPolicy caseComparison,
                            SymlinkHandlingPolicy symlinkHandling,
                            ModuleContainmentPolicy containment,
                            LocalModuleLookupPolicy localLookup,
                            DependencyAliasLookupPolicy dependencyAliasLookup,
                            PreludeLookupPolicy preludeLookup,
                            ModuleCandidateSelectionPolicy candidateSelection) noexcept;

  UnicodeNormalizationPolicy unicodeNormalizationValue;
  CaseComparisonPolicy caseComparisonValue;
  SymlinkHandlingPolicy symlinkHandlingValue;
  ModuleContainmentPolicy containmentValue;
  LocalModuleLookupPolicy localLookupValue;
  DependencyAliasLookupPolicy dependencyAliasLookupValue;
  PreludeLookupPolicy preludeLookupValue;
  ModuleCandidateSelectionPolicy candidateSelectionValue;
};

enum class ModuleDependencyKind : uint8_t {
  Import = 0x01,
  ForeignReexport = 0x02,
  ModuleAlias = 0x03,
  Prelude = 0x04
};

/// \brief Stable semantic key for one module-resolution request.
class ModuleResolutionKey final {
public:
  ModuleResolutionKey(ModuleResolutionKey&&) noexcept = default;
  ModuleResolutionKey& operator=(ModuleResolutionKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleResolutionKey);

  /// \brief Validates and constructs the closed module-resolution request record.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionKey> from(
      ModuleKey&& requester, ModuleDependencyKind kind,
      zc::Maybe<zc::Vector<ModulePathSegment>>&& normalizedPath,
      zc::Maybe<DependencyAlias>&& dependencyAlias, ModuleResolutionPolicyKey&& policy);
  /// \brief Decodes one exact bounded domain-separated semantic request key.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleResolutionKey clone() const;
  ZC_NODISCARD const ModuleKey& requester() const noexcept;
  ZC_NODISCARD ModuleDependencyKind dependencyKind() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const ModulePathSegment>> normalizedPath() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::StringPtr> dependencyAlias() const noexcept;
  ZC_NODISCARD const ModuleResolutionPolicyKey& policy() const noexcept;
  /// \brief Encodes the exact domain-separated RFC 0018 request bytes.
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  ModuleResolutionKey(ModuleKey&& requester, ModuleDependencyKind kind,
                      zc::Maybe<zc::Vector<ModulePathSegment>>&& normalizedPath,
                      zc::Maybe<DependencyAlias>&& dependencyAlias,
                      ModuleResolutionPolicyKey&& policy) noexcept;

  ModuleKey requesterValue;
  ModuleDependencyKind kindValue;
  zc::Maybe<zc::Vector<ModulePathSegment>> normalizedPathValue;
  zc::Maybe<DependencyAlias> dependencyAliasValue;
  ModuleResolutionPolicyKey policyValue;
};

/// \brief Canonical sorted distinct stable candidates for one semantic module request.
class ModuleResolutionCandidates final {
public:
  ModuleResolutionCandidates(ModuleResolutionCandidates&&) noexcept = default;
  ModuleResolutionCandidates& operator=(ModuleResolutionCandidates&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleResolutionCandidates);

  /// \brief Sorts and deduplicates one bounded stable candidate set.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionCandidates> from(
      zc::Vector<ModuleKey>&& candidates);
  /// \brief Decodes one exact bounded sorted candidate sequence.
  ZC_NODISCARD static zc::Maybe<ModuleResolutionCandidates> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleResolutionCandidates clone() const;
  ZC_NODISCARD zc::ArrayPtr<const ModuleKey> candidates() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ModuleResolutionCandidates(zc::Vector<ModuleKey>&& candidates) noexcept;

  zc::Vector<ModuleKey> candidateValues;
};

}  // namespace zomlang::compiler::identity
