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

#include "zomlang/compiler/identity/module-resolution-key.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kMaximumModulePathSegments = 256;
constexpr uint64_t kMaximumModuleResolutionKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumRequesterAncestryBytes = 4 * 1024 * 1024;
constexpr uint64_t kMaximumPolicyBytes = 128;
constexpr uint64_t kMaximumModuleResolutionCandidates = 65536;
constexpr uint64_t kMaximumModuleResolutionCandidatesBytes = 64 * 1024 * 1024;

bool hasDomain(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  if (bytes.size() <= domain.size() || bytes[domain.size()] != 0x00) { return false; }
  for (size_t index = 0; index < domain.size(); ++index) {
    if (bytes[index] != static_cast<uint8_t>(domain[index])) { return false; }
  }
  return true;
}

bool isValid(UnicodeNormalizationPolicy value) { return value == UnicodeNormalizationPolicy::Nfc; }

bool isValid(CaseComparisonPolicy value) { return value == CaseComparisonPolicy::CaseSensitive; }

bool isValid(SymlinkHandlingPolicy value) {
  return value == SymlinkHandlingPolicy::ResolveThenConfine;
}

bool isValid(ModuleContainmentPolicy value) {
  return value == ModuleContainmentPolicy::DeclaredRootsOnly;
}

bool isValid(LocalModuleLookupPolicy value) {
  return value == LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot;
}

bool isValid(DependencyAliasLookupPolicy value) {
  return value == DependencyAliasLookupPolicy::ExactFirstSegment;
}

bool isValid(PreludeLookupPolicy value) {
  return value == PreludeLookupPolicy::ConfiguredCratePrelude;
}

bool isValid(ModuleCandidateSelectionPolicy value) {
  return value == ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence;
}

bool isValid(ModuleDependencyKind value) {
  return value >= ModuleDependencyKind::Import && value <= ModuleDependencyKind::Prelude;
}

bool sameKey(const ModuleKey& left, const ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameCrate(const CrateKey& left, const CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool samePath(zc::ArrayPtr<const ModulePathSegment> left,
              zc::ArrayPtr<const ModulePathSegment> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

zc::Array<uint8_t> domainSeparated(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> record) {
  zc::Vector<uint8_t> encoded(domain.size() + 1 + record.size());
  encoded.addAll(domain.asBytes());
  encoded.add(0x00);
  encoded.addAll(record);
  return encoded.releaseAsArray();
}

}  // namespace

ModuleCatalogPathBucketKey::ModuleCatalogPathBucketKey(
    CrateKey&& crate, zc::Vector<ModulePathSegment>&& path) noexcept
    : crateValue(zc::mv(crate)), pathValue(zc::mv(path)) {}

zc::Maybe<ModuleCatalogPathBucketKey> ModuleCatalogPathBucketKey::from(
    CrateKey&& crate, zc::Vector<ModulePathSegment>&& path) {
  if (path.size() == 0) { return zc::none; }
  if (path.size() > kMaximumModulePathSegments) { return zc::none; }
  ModuleCatalogPathBucketKey candidate(zc::mv(crate), zc::mv(path));
  if (candidate.encode().size() > kMaximumModuleResolutionKeyBytes) { return zc::none; }
  return zc::mv(candidate);
}

zc::Maybe<ModuleCatalogPathBucketKey> ModuleCatalogPathBucketKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.module-catalog-path-bucket"_zc;
  if (bytes.size() > kMaximumModuleResolutionKeyBytes || !hasDomain(bytes, domain)) {
    return zc::none;
  }
  CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto crate = CrateKey::decodeCanonical(decoder);
  auto count = decoder.decodeSequenceSize(kMaximumModulePathSegments);
  if (crate == zc::none || count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<ModulePathSegment> path(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto segment = ModulePathSegment::decodeCanonical(decoder);
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { path.add(zc::mv(value)); }
  }
  if (!decoder.finished()) { return zc::none; }
  ZC_IF_SOME(crateValue, crate) {
    auto result = from(zc::mv(crateValue), zc::mv(path));
    ZC_IF_SOME(value, result) {
      if (value.encode().asPtr() != bytes) { return zc::none; }
      return zc::mv(value);
    }
  }
  return zc::none;
}

ModuleCatalogPathBucketKey ModuleCatalogPathBucketKey::clone() const {
  zc::Vector<ModulePathSegment> path(pathValue.size());
  for (const auto& segment : pathValue) { path.add(segment.clone()); }
  return ModuleCatalogPathBucketKey(crateValue.clone(), zc::mv(path));
}

const CrateKey& ModuleCatalogPathBucketKey::crate() const noexcept { return crateValue; }

zc::ArrayPtr<const ModulePathSegment> ModuleCatalogPathBucketKey::path() const noexcept {
  return pathValue.asPtr();
}

zc::Array<uint8_t> ModuleCatalogPathBucketKey::encode() const {
  CanonicalEncoder record;
  crateValue.encode(record);
  record.encodeSequenceSize(pathValue.size());
  for (const auto& segment : pathValue) { segment.encode(record); }

  constexpr auto domain = "zom.module-catalog-path-bucket"_zc;
  const auto recordBytes = record.finish();
  return domainSeparated(domain, recordBytes.asPtr());
}

RequesterModuleAncestry::RequesterModuleAncestry(ModuleKey&& requester,
                                                 zc::Vector<ModuleKey>&& ancestry) noexcept
    : requesterValue(zc::mv(requester)), ancestryValue(zc::mv(ancestry)) {}

zc::Maybe<RequesterModuleAncestry> RequesterModuleAncestry::from(ModuleKey&& requester,
                                                                 zc::Vector<ModuleKey>&& ancestry) {
  if (ancestry.size() == 0 || !sameKey(requester, ancestry[0])) { return zc::none; }
  if (ancestry.size() > kMaximumModulePathSegments) { return zc::none; }
  for (size_t index = 1; index < ancestry.size(); ++index) {
    const auto& child = ancestry[index - 1];
    const auto& parent = ancestry[index];
    if (!sameCrate(requester.crate(), parent.crate()) ||
        child.path().size() != parent.path().size() + 1) {
      return zc::none;
    }
    for (size_t segment = 0; segment < parent.path().size(); ++segment) {
      if (child.path()[segment] != parent.path()[segment]) { return zc::none; }
    }
  }
  RequesterModuleAncestry candidate(zc::mv(requester), zc::mv(ancestry));
  if (candidate.encode().size() > kMaximumRequesterAncestryBytes) { return zc::none; }
  return zc::mv(candidate);
}

zc::Maybe<RequesterModuleAncestry> RequesterModuleAncestry::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumRequesterAncestryBytes) { return zc::none; }
  CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumModulePathSegments);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<ModuleKey> ancestry(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto module = ModuleKey::decodeCanonical(decoder);
    if (module == zc::none) { return zc::none; }
    ZC_IF_SOME(value, module) { ancestry.add(zc::mv(value)); }
  }
  if (!decoder.finished()) { return zc::none; }
  auto requester = ancestry[0].clone();
  auto result = from(zc::mv(requester), zc::mv(ancestry));
  ZC_IF_SOME(value, result) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(value);
  }
  return zc::none;
}

RequesterModuleAncestry RequesterModuleAncestry::clone() const {
  zc::Vector<ModuleKey> ancestry(ancestryValue.size());
  for (const auto& module : ancestryValue) { ancestry.add(module.clone()); }
  return RequesterModuleAncestry(requesterValue.clone(), zc::mv(ancestry));
}

const ModuleKey& RequesterModuleAncestry::requester() const noexcept { return requesterValue; }

zc::ArrayPtr<const ModuleKey> RequesterModuleAncestry::ancestry() const noexcept {
  return ancestryValue.asPtr();
}

zc::Array<uint8_t> RequesterModuleAncestry::encode() const {
  CanonicalEncoder encoder;
  encoder.encodeSequenceSize(ancestryValue.size());
  for (const auto& module : ancestryValue) { module.encode(encoder); }
  return encoder.finish();
}

ModuleCatalogPathBucket::ModuleCatalogPathBucket(ModuleCatalogPathBucketKey&& key,
                                                 zc::Maybe<ModuleKey>&& module) noexcept
    : keyValue(zc::mv(key)), moduleValue(zc::mv(module)) {}

ModuleCatalogPathBucket ModuleCatalogPathBucket::absent(ModuleCatalogPathBucketKey&& key) {
  zc::Maybe<ModuleKey> module;
  return ModuleCatalogPathBucket(zc::mv(key), zc::mv(module));
}

zc::Maybe<ModuleCatalogPathBucket> ModuleCatalogPathBucket::present(
    ModuleCatalogPathBucketKey&& key, ModuleKey&& module) {
  if (!sameCrate(key.crate(), module.crate()) || !samePath(key.path(), module.path())) {
    return zc::none;
  }
  zc::Maybe<ModuleKey> presentModule(zc::mv(module));
  return ModuleCatalogPathBucket(zc::mv(key), zc::mv(presentModule));
}

zc::Maybe<ModuleCatalogPathBucket> ModuleCatalogPathBucket::decodeCanonical(
    ModuleCatalogPathBucketKey&& key, zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleResolutionKeyBytes) { return zc::none; }
  CanonicalDecoder decoder(bytes);
  auto presentValue = decoder.decodeBool();
  if (presentValue == zc::none) { return zc::none; }
  ZC_IF_SOME(isPresent, presentValue) {
    if (!isPresent) {
      if (!decoder.finished()) { return zc::none; }
      auto result = absent(zc::mv(key));
      if (result.encode().asPtr() != bytes) { return zc::none; }
      return zc::mv(result);
    }
  }
  auto module = ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, module) {
    auto result = present(zc::mv(key), zc::mv(value));
    ZC_IF_SOME(bucket, result) {
      if (bucket.encode().asPtr() != bytes) { return zc::none; }
      return zc::mv(bucket);
    }
  }
  return zc::none;
}

ModuleCatalogPathBucket ModuleCatalogPathBucket::clone() const {
  zc::Maybe<ModuleKey> module;
  ZC_IF_SOME(value, moduleValue) { module = value.clone(); }
  return ModuleCatalogPathBucket(keyValue.clone(), zc::mv(module));
}

const ModuleCatalogPathBucketKey& ModuleCatalogPathBucket::key() const noexcept { return keyValue; }

zc::Maybe<const ModuleKey&> ModuleCatalogPathBucket::module() const noexcept {
  ZC_IF_SOME(value, moduleValue) { return value; }
  return zc::none;
}

zc::Array<uint8_t> ModuleCatalogPathBucket::encode() const {
  CanonicalEncoder encoder;
  ZC_IF_SOME(module, moduleValue) {
    encoder.encodeSome();
    module.encode(encoder);
  } else {
    encoder.encodeNone();
  }
  return encoder.finish();
}

ModuleResolutionPolicyKey::ModuleResolutionPolicyKey(
    UnicodeNormalizationPolicy unicodeNormalization, CaseComparisonPolicy caseComparison,
    SymlinkHandlingPolicy symlinkHandling, ModuleContainmentPolicy containment,
    LocalModuleLookupPolicy localLookup, DependencyAliasLookupPolicy dependencyAliasLookup,
    PreludeLookupPolicy preludeLookup, ModuleCandidateSelectionPolicy candidateSelection) noexcept
    : unicodeNormalizationValue(unicodeNormalization),
      caseComparisonValue(caseComparison),
      symlinkHandlingValue(symlinkHandling),
      containmentValue(containment),
      localLookupValue(localLookup),
      dependencyAliasLookupValue(dependencyAliasLookup),
      preludeLookupValue(preludeLookup),
      candidateSelectionValue(candidateSelection) {}

zc::Maybe<ModuleResolutionPolicyKey> ModuleResolutionPolicyKey::from(
    UnicodeNormalizationPolicy unicodeNormalization, CaseComparisonPolicy caseComparison,
    SymlinkHandlingPolicy symlinkHandling, ModuleContainmentPolicy containment,
    LocalModuleLookupPolicy localLookup, DependencyAliasLookupPolicy dependencyAliasLookup,
    PreludeLookupPolicy preludeLookup, ModuleCandidateSelectionPolicy candidateSelection) noexcept {
  if (!isValid(unicodeNormalization) || !isValid(caseComparison) || !isValid(symlinkHandling) ||
      !isValid(containment) || !isValid(localLookup) || !isValid(dependencyAliasLookup) ||
      !isValid(preludeLookup) || !isValid(candidateSelection)) {
    return zc::none;
  }
  return ModuleResolutionPolicyKey(unicodeNormalization, caseComparison, symlinkHandling,
                                   containment, localLookup, dependencyAliasLookup, preludeLookup,
                                   candidateSelection);
}

zc::Maybe<ModuleResolutionPolicyKey> ModuleResolutionPolicyKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.module-resolution-policy"_zc;
  if (bytes.size() > kMaximumPolicyBytes || !hasDomain(bytes, domain)) { return zc::none; }
  CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto unicode = decoder.decodeUint8();
  auto comparison = decoder.decodeUint8();
  auto symlink = decoder.decodeUint8();
  auto containment = decoder.decodeUint8();
  auto local = decoder.decodeUint8();
  auto alias = decoder.decodeUint8();
  auto prelude = decoder.decodeUint8();
  auto selection = decoder.decodeUint8();
  if (unicode == zc::none || comparison == zc::none || symlink == zc::none ||
      containment == zc::none || local == zc::none || alias == zc::none || prelude == zc::none ||
      selection == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto result = from(static_cast<UnicodeNormalizationPolicy>(ZC_ASSERT_NONNULL(unicode)),
                     static_cast<CaseComparisonPolicy>(ZC_ASSERT_NONNULL(comparison)),
                     static_cast<SymlinkHandlingPolicy>(ZC_ASSERT_NONNULL(symlink)),
                     static_cast<ModuleContainmentPolicy>(ZC_ASSERT_NONNULL(containment)),
                     static_cast<LocalModuleLookupPolicy>(ZC_ASSERT_NONNULL(local)),
                     static_cast<DependencyAliasLookupPolicy>(ZC_ASSERT_NONNULL(alias)),
                     static_cast<PreludeLookupPolicy>(ZC_ASSERT_NONNULL(prelude)),
                     static_cast<ModuleCandidateSelectionPolicy>(ZC_ASSERT_NONNULL(selection)));
  ZC_IF_SOME(value, result) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(value);
  }
  return zc::none;
}

ModuleResolutionPolicyKey ModuleResolutionPolicyKey::clone() const noexcept {
  return ModuleResolutionPolicyKey(
      unicodeNormalizationValue, caseComparisonValue, symlinkHandlingValue, containmentValue,
      localLookupValue, dependencyAliasLookupValue, preludeLookupValue, candidateSelectionValue);
}

UnicodeNormalizationPolicy ModuleResolutionPolicyKey::unicodeNormalization() const noexcept {
  return unicodeNormalizationValue;
}

CaseComparisonPolicy ModuleResolutionPolicyKey::caseComparison() const noexcept {
  return caseComparisonValue;
}

SymlinkHandlingPolicy ModuleResolutionPolicyKey::symlinkHandling() const noexcept {
  return symlinkHandlingValue;
}

ModuleContainmentPolicy ModuleResolutionPolicyKey::containment() const noexcept {
  return containmentValue;
}

LocalModuleLookupPolicy ModuleResolutionPolicyKey::localLookup() const noexcept {
  return localLookupValue;
}

DependencyAliasLookupPolicy ModuleResolutionPolicyKey::dependencyAliasLookup() const noexcept {
  return dependencyAliasLookupValue;
}

PreludeLookupPolicy ModuleResolutionPolicyKey::preludeLookup() const noexcept {
  return preludeLookupValue;
}

ModuleCandidateSelectionPolicy ModuleResolutionPolicyKey::candidateSelection() const noexcept {
  return candidateSelectionValue;
}

zc::Array<uint8_t> ModuleResolutionPolicyKey::encode() const {
  constexpr auto domain = "zom.module-resolution-policy"_zc;
  zc::Vector<uint8_t> record(8);
  record.add(static_cast<uint8_t>(unicodeNormalizationValue));
  record.add(static_cast<uint8_t>(caseComparisonValue));
  record.add(static_cast<uint8_t>(symlinkHandlingValue));
  record.add(static_cast<uint8_t>(containmentValue));
  record.add(static_cast<uint8_t>(localLookupValue));
  record.add(static_cast<uint8_t>(dependencyAliasLookupValue));
  record.add(static_cast<uint8_t>(preludeLookupValue));
  record.add(static_cast<uint8_t>(candidateSelectionValue));
  return domainSeparated(domain, record.asPtr());
}

ModuleResolutionKey::ModuleResolutionKey(ModuleKey&& requester, ModuleDependencyKind kind,
                                         zc::Maybe<zc::Vector<ModulePathSegment>>&& normalizedPath,
                                         zc::Maybe<DependencyAlias>&& dependencyAlias,
                                         ModuleResolutionPolicyKey&& policy) noexcept
    : requesterValue(zc::mv(requester)),
      kindValue(kind),
      normalizedPathValue(zc::mv(normalizedPath)),
      dependencyAliasValue(zc::mv(dependencyAlias)),
      policyValue(zc::mv(policy)) {}

zc::Maybe<ModuleResolutionKey> ModuleResolutionKey::from(
    ModuleKey&& requester, ModuleDependencyKind kind,
    zc::Maybe<zc::Vector<ModulePathSegment>>&& normalizedPath,
    zc::Maybe<DependencyAlias>&& dependencyAlias, ModuleResolutionPolicyKey&& policy) {
  if (!isValid(kind)) { return zc::none; }

  bool hasPath = false;
  bool hasAlias = false;
  ZC_IF_SOME(path, normalizedPath) {
    hasPath = true;
    if (path.size() == 0) { return zc::none; }
    ZC_IF_SOME(alias, dependencyAlias) {
      hasAlias = true;
      if (alias.text() != path[0].text()) { return zc::none; }
    }
  }
  if (!hasPath) { hasAlias = dependencyAlias != zc::none; }

  if (kind == ModuleDependencyKind::Prelude) {
    if (hasPath || hasAlias) { return zc::none; }
  } else if (!hasPath) {
    return zc::none;
  }

  ModuleResolutionKey candidate(zc::mv(requester), kind, zc::mv(normalizedPath),
                                zc::mv(dependencyAlias), zc::mv(policy));
  if (candidate.encode().size() > kMaximumModuleResolutionKeyBytes) { return zc::none; }
  return zc::mv(candidate);
}

zc::Maybe<ModuleResolutionKey> ModuleResolutionKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.module-resolution"_zc;
  if (bytes.size() > kMaximumModuleResolutionKeyBytes || !hasDomain(bytes, domain)) {
    return zc::none;
  }
  CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto requester = ModuleKey::decodeCanonical(decoder);
  auto kind = decoder.decodeUint8();
  auto hasPathValue = decoder.decodeBool();
  if (requester == zc::none || kind == zc::none || hasPathValue == zc::none) { return zc::none; }

  zc::Maybe<zc::Vector<ModulePathSegment>> path;
  ZC_IF_SOME(hasPath, hasPathValue) {
    if (hasPath) {
      auto count = decoder.decodeSequenceSize(kMaximumModulePathSegments);
      if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
      zc::Vector<ModulePathSegment> segments(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
      for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
        auto segment = ModulePathSegment::decodeCanonical(decoder);
        if (segment == zc::none) { return zc::none; }
        ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
      }
      path = zc::mv(segments);
    }
  }

  auto hasAliasValue = decoder.decodeBool();
  if (hasAliasValue == zc::none) { return zc::none; }
  zc::Maybe<DependencyAlias> dependencyAlias;
  ZC_IF_SOME(hasAlias, hasAliasValue) {
    if (hasAlias) {
      auto decodedAlias = DependencyAlias::decodeCanonical(decoder);
      if (decodedAlias == zc::none) { return zc::none; }
      ZC_IF_SOME(value, decodedAlias) { dependencyAlias = zc::mv(value); }
    }
  }

  auto policyBytes = decoder.decodeByteString(kMaximumPolicyBytes);
  if (policyBytes == zc::none || !decoder.finished()) { return zc::none; }
  zc::Maybe<ModuleResolutionPolicyKey> policy;
  ZC_IF_SOME(value, policyBytes) { policy = ModuleResolutionPolicyKey::decodeCanonical(value); }
  if (policy == zc::none) { return zc::none; }

  ZC_IF_SOME(requesterValue, requester) {
    ZC_IF_SOME(kindValue, kind) {
      ZC_IF_SOME(policyValue, policy) {
        auto result = from(zc::mv(requesterValue), static_cast<ModuleDependencyKind>(kindValue),
                           zc::mv(path), zc::mv(dependencyAlias), zc::mv(policyValue));
        ZC_IF_SOME(value, result) {
          if (value.encode().asPtr() != bytes) { return zc::none; }
          return zc::mv(value);
        }
      }
    }
  }
  return zc::none;
}

ModuleResolutionKey ModuleResolutionKey::clone() const {
  zc::Maybe<zc::Vector<ModulePathSegment>> path;
  ZC_IF_SOME(value, normalizedPathValue) {
    zc::Vector<ModulePathSegment> cloned(value.size());
    for (const auto& segment : value) { cloned.add(segment.clone()); }
    path = zc::mv(cloned);
  }
  zc::Maybe<DependencyAlias> alias;
  ZC_IF_SOME(value, dependencyAliasValue) { alias = value.clone(); }
  return ModuleResolutionKey(requesterValue.clone(), kindValue, zc::mv(path), zc::mv(alias),
                             policyValue.clone());
}

const ModuleKey& ModuleResolutionKey::requester() const noexcept { return requesterValue; }

ModuleDependencyKind ModuleResolutionKey::dependencyKind() const noexcept { return kindValue; }

zc::Maybe<zc::ArrayPtr<const ModulePathSegment>> ModuleResolutionKey::normalizedPath()
    const noexcept {
  ZC_IF_SOME(value, normalizedPathValue) { return value.asPtr(); }
  return zc::none;
}

zc::Maybe<zc::StringPtr> ModuleResolutionKey::dependencyAlias() const noexcept {
  ZC_IF_SOME(value, dependencyAliasValue) { return value.text(); }
  return zc::none;
}

const ModuleResolutionPolicyKey& ModuleResolutionKey::policy() const noexcept {
  return policyValue;
}

zc::Array<uint8_t> ModuleResolutionKey::encode() const {
  CanonicalEncoder record;
  requesterValue.encode(record);
  record.encodeUint8(static_cast<uint8_t>(kindValue));
  ZC_IF_SOME(path, normalizedPathValue) {
    record.encodeSome();
    record.encodeSequenceSize(path.size());
    for (const auto& segment : path) { segment.encode(record); }
  } else {
    record.encodeNone();
  }
  ZC_IF_SOME(alias, dependencyAliasValue) {
    record.encodeSome();
    alias.encode(record);
  } else {
    record.encodeNone();
  }
  const auto policyBytes = policyValue.encode();
  record.encodeByteString(policyBytes.asPtr());

  constexpr auto domain = "zom.module-resolution"_zc;
  const auto recordBytes = record.finish();
  return domainSeparated(domain, recordBytes.asPtr());
}

ModuleResolutionCandidates::ModuleResolutionCandidates(zc::Vector<ModuleKey>&& candidates) noexcept
    : candidateValues(zc::mv(candidates)) {}

zc::Maybe<ModuleResolutionCandidates> ModuleResolutionCandidates::from(
    zc::Vector<ModuleKey>&& candidates) {
  if (candidates.size() > kMaximumModuleResolutionCandidates) { return zc::none; }
  for (size_t index = 1; index < candidates.size(); ++index) {
    auto current = zc::mv(candidates[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(current.encode().asPtr(), candidates[insertion - 1].encode().asPtr()) < 0) {
      candidates[insertion] = zc::mv(candidates[insertion - 1]);
      --insertion;
    }
    candidates[insertion] = zc::mv(current);
  }
  zc::Vector<ModuleKey> unique(candidates.size());
  for (auto& candidate : candidates) {
    if (unique.size() == 0 || !sameKey(unique.back(), candidate)) { unique.add(zc::mv(candidate)); }
  }
  ModuleResolutionCandidates result(zc::mv(unique));
  if (result.encode().size() > kMaximumModuleResolutionCandidatesBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleResolutionCandidates> ModuleResolutionCandidates::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleResolutionCandidatesBytes) {
    return zc::none;
  }
  CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumModuleResolutionCandidates);
  if (count == zc::none) { return zc::none; }
  zc::Vector<ModuleKey> candidates(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto candidate = ModuleKey::decodeCanonical(decoder);
    if (candidate == zc::none) { return zc::none; }
    ZC_IF_SOME(value, candidate) {
      if (candidates.size() != 0 &&
          compareBytes(candidates.back().encode().asPtr(), value.encode().asPtr()) >= 0) {
        return zc::none;
      }
      candidates.add(zc::mv(value));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  ModuleResolutionCandidates result(zc::mv(candidates));
  if (result.encode().asPtr() != bytes) { return zc::none; }
  return zc::mv(result);
}

ModuleResolutionCandidates ModuleResolutionCandidates::clone() const {
  zc::Vector<ModuleKey> candidates(candidateValues.size());
  for (const auto& candidate : candidateValues) { candidates.add(candidate.clone()); }
  return ModuleResolutionCandidates(zc::mv(candidates));
}

zc::ArrayPtr<const ModuleKey> ModuleResolutionCandidates::candidates() const noexcept {
  return candidateValues.asPtr();
}

zc::Array<uint8_t> ModuleResolutionCandidates::encode() const {
  CanonicalEncoder encoder;
  encoder.encodeSequenceSize(candidateValues.size());
  for (const auto& candidate : candidateValues) { candidate.encode(encoder); }
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
