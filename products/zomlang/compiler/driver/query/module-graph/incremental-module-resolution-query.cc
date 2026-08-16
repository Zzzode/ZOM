// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/module-graph/incremental-module-resolution-query.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::incremental_module_resolution_query {
namespace {

constexpr uint64_t kMaximumSearchRootsPerCrate = 4096;
constexpr uint64_t kMaximumSearchRootValueBytes = 16 * 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumCatalogBucketBytes = 256 * 1024;

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return sameBytes(left.encode().asPtr(), right.encode().asPtr());
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return sameBytes(left.encode().asPtr(), right.encode().asPtr());
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

zc::Array<uint8_t> encodeSearchRoot(const binder::ModuleSearchRoot& root) {
  identity::CanonicalEncoder encoder;
  root.encode(encoder);
  return encoder.finish();
}

zc::Vector<identity::ModulePathSegment> clonePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Maybe<identity::CrateKey> decodeCrate(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, crate) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(value);
  }
  return zc::none;
}

zc::Maybe<identity::ModuleKey> decodeModule(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, module) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(value);
  }
  return zc::none;
}

template <typename Spec>
zc::Maybe<typename Spec::Value> readValue(query::QueryContext& context,
                                          const typename Spec::Key& key) {
  auto result = context.get<Spec>(key);
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  return result.value().clone();
}

bool validateSearchRoots(const CanonicalModuleSearchRoots& roots, const identity::CrateKey& crate) {
  return sameCrate(roots.crate(), crate) && roots.roots().size() != 0;
}

zc::Maybe<DependencyAliasRootQueryKey> aliasInputKey(const identity::ModuleResolutionKey& key,
                                                     zc::StringPtr firstSegment) {
  auto alias = identity::DependencyAlias::fromCanonical(firstSegment);
  if (alias == zc::none) { return zc::none; }
  ZC_IF_SOME(value, alias) {
    return DependencyAliasRootQueryKey::from(key.requester().crate().clone(), zc::mv(value));
  }
  return zc::none;
}

bool aliasProjectionMatches(const identity::ModuleResolutionKey& key,
                            const DependencyAliasRootQueryKey& inputKey,
                            const ExplicitModuleTarget& value) {
  auto encodedAlias = key.dependencyAlias();
  auto target = value.target();
  if ((encodedAlias == zc::none) != (target == zc::none)) { return false; }
  ZC_IF_SOME(alias, encodedAlias) {
    if (alias != inputKey.alias().text()) { return false; }
  }
  return true;
}

bool providerInsertBucket(zc::TreeMap<zc::String, identity::ModuleCatalogPathBucketKey>& buckets,
                          const identity::CrateKey& crate,
                          zc::Vector<identity::ModulePathSegment>&& path) {
  auto key = identity::ModuleCatalogPathBucketKey::from(crate.clone(), zc::mv(path));
  if (key == zc::none) { return false; }
  ZC_IF_SOME(value, key) {
    auto encoded = zc::encodeHex(value.encode().asPtr());
    if (buckets.find(encoded) == zc::none) { buckets.insert(zc::mv(encoded), zc::mv(value)); }
    return true;
  }
  return false;
}

zc::Maybe<identity::ModuleResolutionCandidates> providerCandidates(
    query::QueryContext& context, const identity::ModuleResolutionKey& key) {
  auto requesterRoots = readValue<ModuleSearchRootsInput>(context, key.requester().crate());
  if (requesterRoots == zc::none ||
      !validateSearchRoots(ZC_ASSERT_NONNULL(requesterRoots), key.requester().crate())) {
    return zc::none;
  }

  zc::TreeMap<zc::String, identity::ModuleCatalogPathBucketKey> bucketMap;
  if (key.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
    auto configured = readValue<ConfiguredPreludeInput>(context, key.requester().crate());
    if (configured == zc::none) { return zc::none; }
    ZC_IF_SOME(target, ZC_ASSERT_NONNULL(configured).target()) {
      auto targetRoots = readValue<ModuleSearchRootsInput>(context, target.crate());
      if (targetRoots == zc::none ||
          !validateSearchRoots(ZC_ASSERT_NONNULL(targetRoots), target.crate()) ||
          !providerInsertBucket(bucketMap, target.crate(), clonePath(target.path()))) {
        return zc::none;
      }
    }
  } else {
    auto path = key.normalizedPath();
    if (path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) { return zc::none; }
    auto ancestry = readValue<RequesterModuleAncestryInput>(context, key.requester());
    if (ancestry == zc::none ||
        !sameModule(ZC_ASSERT_NONNULL(ancestry).requester(), key.requester())) {
      return zc::none;
    }
    for (const auto& ancestor : ZC_ASSERT_NONNULL(ancestry).ancestry()) {
      auto candidatePath = clonePath(ancestor.path());
      for (const auto& segment : ZC_ASSERT_NONNULL(path)) { candidatePath.add(segment.clone()); }
      if (!providerInsertBucket(bucketMap, key.requester().crate(), zc::mv(candidatePath))) {
        return zc::none;
      }
    }
    if (!providerInsertBucket(bucketMap, key.requester().crate(),
                              clonePath(ZC_ASSERT_NONNULL(path)))) {
      return zc::none;
    }

    auto inputKey = aliasInputKey(key, ZC_ASSERT_NONNULL(path).front().text());
    if (inputKey == zc::none) { return zc::none; }
    auto aliasRoot = readValue<DependencyAliasRootInput>(context, ZC_ASSERT_NONNULL(inputKey));
    if (aliasRoot == zc::none ||
        !aliasProjectionMatches(key, ZC_ASSERT_NONNULL(inputKey), ZC_ASSERT_NONNULL(aliasRoot))) {
      return zc::none;
    }
    ZC_IF_SOME(target, ZC_ASSERT_NONNULL(aliasRoot).target()) {
      auto targetRoots = readValue<ModuleSearchRootsInput>(context, target.crate());
      if (targetRoots == zc::none ||
          !validateSearchRoots(ZC_ASSERT_NONNULL(targetRoots), target.crate())) {
        return zc::none;
      }
      auto candidatePath = clonePath(target.path());
      for (size_t index = 1; index < ZC_ASSERT_NONNULL(path).size(); ++index) {
        candidatePath.add(ZC_ASSERT_NONNULL(path)[index].clone());
      }
      if (!providerInsertBucket(bucketMap, target.crate(), zc::mv(candidatePath))) {
        return zc::none;
      }
    }
  }

  zc::Vector<identity::ModuleCatalogPathBucketKey> bucketKeys(bucketMap.size());
  for (const auto& entry : bucketMap) { bucketKeys.add(entry.value.clone()); }
  zc::Vector<identity::ModuleKey> candidates;
  for (size_t index = 0; index < bucketKeys.size(); ++index) {
    auto result = context.get<ModuleCatalogPathBucketInput>(bucketKeys[index]);
    if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value ||
        result.value().key().encode().asPtr() != bucketKeys[index].encode().asPtr()) {
      return zc::none;
    }
    ZC_IF_SOME(module, result.value().module()) { candidates.add(module.clone()); }
  }
  return identity::ModuleResolutionCandidates::from(zc::mv(candidates));
}

bool verifierAppendBucket(zc::Vector<identity::ModuleCatalogPathBucketKey>& buckets,
                          const identity::CrateKey& crate,
                          zc::Vector<identity::ModulePathSegment>&& path) {
  auto candidate = identity::ModuleCatalogPathBucketKey::from(crate.clone(), zc::mv(path));
  if (candidate == zc::none) { return false; }
  ZC_IF_SOME(value, candidate) {
    for (const auto& prior : buckets) {
      if (prior.encode().asPtr() == value.encode().asPtr()) { return true; }
    }
    buckets.add(zc::mv(value));
    return true;
  }
  return false;
}

void verifierSortBuckets(zc::Vector<identity::ModuleCatalogPathBucketKey>& buckets) {
  for (size_t index = 1; index < buckets.size(); ++index) {
    auto current = zc::mv(buckets[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareBytes(current.encode().asPtr(), buckets[insertion - 1].encode().asPtr()) < 0) {
      buckets[insertion] = zc::mv(buckets[insertion - 1]);
      --insertion;
    }
    buckets[insertion] = zc::mv(current);
  }
}

zc::Maybe<identity::ModuleResolutionCandidates> verifierCandidates(
    query::QueryContext& context, const identity::ModuleResolutionKey& key) {
  auto requesterRootResult = context.get<ModuleSearchRootsInput>(key.requester().crate());
  if (requesterRootResult.isRuntimeFailure() ||
      requesterRootResult.kind() != query::QueryValueKind::Value ||
      !validateSearchRoots(requesterRootResult.value(), key.requester().crate())) {
    return zc::none;
  }

  zc::Vector<identity::ModuleCatalogPathBucketKey> bucketKeys;
  if (key.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
    auto configuredResult = context.get<ConfiguredPreludeInput>(key.requester().crate());
    if (configuredResult.isRuntimeFailure() ||
        configuredResult.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    ZC_IF_SOME(target, configuredResult.value().target()) {
      auto roots = context.get<ModuleSearchRootsInput>(target.crate());
      if (roots.isRuntimeFailure() || roots.kind() != query::QueryValueKind::Value ||
          !validateSearchRoots(roots.value(), target.crate()) ||
          !verifierAppendBucket(bucketKeys, target.crate(), clonePath(target.path()))) {
        return zc::none;
      }
    }
  } else {
    auto path = key.normalizedPath();
    if (path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) { return zc::none; }
    auto ancestryResult = context.get<RequesterModuleAncestryInput>(key.requester());
    if (ancestryResult.isRuntimeFailure() ||
        ancestryResult.kind() != query::QueryValueKind::Value ||
        !sameModule(ancestryResult.value().requester(), key.requester())) {
      return zc::none;
    }
    for (size_t ancestor = 0; ancestor < ancestryResult.value().ancestry().size(); ++ancestor) {
      const auto& base = ancestryResult.value().ancestry()[ancestor];
      auto candidatePath = clonePath(base.path());
      for (size_t segment = 0; segment < ZC_ASSERT_NONNULL(path).size(); ++segment) {
        candidatePath.add(ZC_ASSERT_NONNULL(path)[segment].clone());
      }
      if (!verifierAppendBucket(bucketKeys, key.requester().crate(), zc::mv(candidatePath))) {
        return zc::none;
      }
    }
    if (!verifierAppendBucket(bucketKeys, key.requester().crate(),
                              clonePath(ZC_ASSERT_NONNULL(path)))) {
      return zc::none;
    }

    auto inputKey = aliasInputKey(key, ZC_ASSERT_NONNULL(path)[0].text());
    if (inputKey == zc::none) { return zc::none; }
    auto aliasResult = context.get<DependencyAliasRootInput>(ZC_ASSERT_NONNULL(inputKey));
    if (aliasResult.isRuntimeFailure() || aliasResult.kind() != query::QueryValueKind::Value ||
        !aliasProjectionMatches(key, ZC_ASSERT_NONNULL(inputKey), aliasResult.value())) {
      return zc::none;
    }
    ZC_IF_SOME(target, aliasResult.value().target()) {
      auto roots = context.get<ModuleSearchRootsInput>(target.crate());
      if (roots.isRuntimeFailure() || roots.kind() != query::QueryValueKind::Value ||
          !validateSearchRoots(roots.value(), target.crate())) {
        return zc::none;
      }
      auto candidatePath = clonePath(target.path());
      for (size_t segment = 1; segment < ZC_ASSERT_NONNULL(path).size(); ++segment) {
        candidatePath.add(ZC_ASSERT_NONNULL(path)[segment].clone());
      }
      if (!verifierAppendBucket(bucketKeys, target.crate(), zc::mv(candidatePath))) {
        return zc::none;
      }
    }
  }

  verifierSortBuckets(bucketKeys);
  zc::Vector<identity::ModuleKey> candidates;
  for (const auto& bucketKey : bucketKeys) {
    auto result = context.get<ModuleCatalogPathBucketInput>(bucketKey);
    if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value ||
        result.value().key().encode().asPtr() != bucketKey.encode().asPtr()) {
      return zc::none;
    }
    ZC_IF_SOME(module, result.value().module()) {
      bool duplicate = false;
      for (const auto& prior : candidates) { duplicate = duplicate || sameModule(prior, module); }
      if (!duplicate) { candidates.add(module.clone()); }
    }
  }
  return identity::ModuleResolutionCandidates::from(zc::mv(candidates));
}

bool stageCatalogBucket(query::InputTransaction& transaction,
                        const binder::StructuralModuleResolver& resolver,
                        const identity::CrateKey& crate,
                        zc::ArrayPtr<const identity::ModulePathSegment> path,
                        zc::TreeMap<zc::String, bool>& stagedBuckets) {
  auto bucket = resolver.catalogPathBucketInput(crate, path);
  if (bucket == zc::none) { return false; }
  ZC_IF_SOME(value, bucket) {
    auto encodedKey = zc::encodeHex(value.key().encode().asPtr());
    if (stagedBuckets.find(encodedKey) != zc::none) { return true; }
    auto queryValue = CanonicalModuleCatalogBucket::fromVerified(value);
    if (!transaction.set<ModuleCatalogPathBucketInput>(value.key(), queryValue).isApplied()) {
      return false;
    }
    stagedBuckets.insert(zc::mv(encodedKey), true);
    return true;
  }
  return false;
}

zc::Maybe<const identity::RequesterModuleAncestry&> ancestryFor(
    const binder::StructuralModuleResolver& resolver, const identity::ModuleKey& requester) {
  zc::Maybe<const identity::RequesterModuleAncestry&> result;
  for (const auto& ancestry : resolver.requesterAncestryInputs()) {
    if (!sameModule(ancestry.requester(), requester)) { continue; }
    if (result != zc::none) { return zc::none; }
    result = ancestry;
  }
  return result;
}

zc::Maybe<const binder::ModuleDependencyAliasRoot&> aliasFor(
    const binder::StructuralModuleResolver& resolver, const identity::CrateKey& requester,
    zc::StringPtr alias) {
  zc::Maybe<const binder::ModuleDependencyAliasRoot&> result;
  for (const auto& candidate : resolver.dependencyAliasRootInputs()) {
    if (!sameCrate(candidate.requester, requester) || candidate.alias.text() != alias) { continue; }
    if (result != zc::none) { return zc::none; }
    result = candidate;
  }
  return result;
}

}  // namespace

DependencyAliasRootQueryKey::DependencyAliasRootQueryKey(identity::CrateKey&& crate,
                                                         identity::DependencyAlias&& alias) noexcept
    : crateValue(zc::mv(crate)), aliasValue(zc::mv(alias)) {}

zc::Maybe<DependencyAliasRootQueryKey> DependencyAliasRootQueryKey::from(
    identity::CrateKey&& crate, identity::DependencyAlias&& alias) {
  DependencyAliasRootQueryKey result(zc::mv(crate), zc::mv(alias));
  if (result.encode().size() > kMaximumModuleKeyBytes) { return zc::none; }
  return zc::mv(result);
}

DependencyAliasRootQueryKey DependencyAliasRootQueryKey::clone() const {
  return DependencyAliasRootQueryKey(crateValue.clone(), aliasValue.clone());
}

const identity::CrateKey& DependencyAliasRootQueryKey::crate() const noexcept { return crateValue; }

const identity::DependencyAlias& DependencyAliasRootQueryKey::alias() const noexcept {
  return aliasValue;
}

zc::Array<uint8_t> DependencyAliasRootQueryKey::encode() const {
  identity::CanonicalEncoder encoder;
  crateValue.encode(encoder);
  aliasValue.encode(encoder);
  return encoder.finish();
}

zc::Maybe<DependencyAliasRootQueryKey> DependencyAliasRootQueryKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  auto alias = identity::DependencyAlias::decodeCanonical(decoder);
  if (crate == zc::none || alias == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(crateValue, crate) {
    ZC_IF_SOME(aliasValue, alias) {
      auto result = from(zc::mv(crateValue), zc::mv(aliasValue));
      ZC_IF_SOME(value, result) {
        if (value.encode().asPtr() != bytes) { return zc::none; }
        return zc::mv(value);
      }
    }
  }
  return zc::none;
}

bool DependencyAliasRootQueryKey::operator==(const DependencyAliasRootQueryKey& other) const {
  return encode().asPtr() == other.encode().asPtr();
}

ExplicitModuleTarget::ExplicitModuleTarget(zc::Maybe<identity::ModuleKey>&& target) noexcept
    : targetValue(zc::mv(target)) {}

ExplicitModuleTarget ExplicitModuleTarget::absent() {
  zc::Maybe<identity::ModuleKey> target;
  return ExplicitModuleTarget(zc::mv(target));
}

ExplicitModuleTarget ExplicitModuleTarget::present(identity::ModuleKey&& target) {
  zc::Maybe<identity::ModuleKey> value(zc::mv(target));
  return ExplicitModuleTarget(zc::mv(value));
}

ExplicitModuleTarget ExplicitModuleTarget::clone() const {
  zc::Maybe<identity::ModuleKey> target;
  ZC_IF_SOME(value, targetValue) { target = value.clone(); }
  return ExplicitModuleTarget(zc::mv(target));
}

zc::Maybe<const identity::ModuleKey&> ExplicitModuleTarget::target() const noexcept {
  ZC_IF_SOME(value, targetValue) { return value; }
  return zc::none;
}

zc::Array<uint8_t> ExplicitModuleTarget::encode() const {
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, targetValue) {
    encoder.encodeSome();
    value.encode(encoder);
  } else {
    encoder.encodeNone();
  }
  return encoder.finish();
}

zc::Maybe<ExplicitModuleTarget> ExplicitModuleTarget::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto presentTag = decoder.decodeBool();
  if (presentTag == zc::none) { return zc::none; }
  ZC_IF_SOME(value, presentTag) {
    if (!value) {
      if (!decoder.finished()) { return zc::none; }
      return absent();
    }
  }
  auto target = identity::ModuleKey::decodeCanonical(decoder);
  if (target == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, target) {
    auto result = present(zc::mv(value));
    if (result.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(result);
  }
  return zc::none;
}

CanonicalModuleSearchRoots::CanonicalModuleSearchRoots(
    zc::Vector<binder::ModuleSearchRoot>&& roots) noexcept
    : rootValues(zc::mv(roots)) {}

zc::Maybe<CanonicalModuleSearchRoots> CanonicalModuleSearchRoots::fromVerified(
    const identity::CrateKey& crate,
    zc::ArrayPtr<const binder::ModuleSearchRoot> environmentRoots) {
  zc::Vector<binder::ModuleSearchRoot> roots;
  for (const auto& root : environmentRoots) {
    if (sameCrate(root.crate(), crate)) { roots.add(root.clone()); }
  }
  if (roots.size() == 0 || roots.size() > kMaximumSearchRootsPerCrate) { return zc::none; }
  for (size_t index = 1; index < roots.size(); ++index) {
    auto current = zc::mv(roots[index]);
    size_t insertion = index;
    while (insertion != 0 && compareBytes(encodeSearchRoot(current).asPtr(),
                                          encodeSearchRoot(roots[insertion - 1]).asPtr()) < 0) {
      roots[insertion] = zc::mv(roots[insertion - 1]);
      --insertion;
    }
    roots[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < roots.size(); ++index) {
    if (encodeSearchRoot(roots[index - 1]).asPtr() == encodeSearchRoot(roots[index]).asPtr()) {
      return zc::none;
    }
  }
  CanonicalModuleSearchRoots result(zc::mv(roots));
  if (result.encode().size() > kMaximumSearchRootValueBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalModuleSearchRoots CanonicalModuleSearchRoots::clone() const {
  zc::Vector<binder::ModuleSearchRoot> roots(rootValues.size());
  for (const auto& root : rootValues) { roots.add(root.clone()); }
  return CanonicalModuleSearchRoots(zc::mv(roots));
}

const identity::CrateKey& CanonicalModuleSearchRoots::crate() const noexcept {
  return rootValues.front().crate();
}

zc::ArrayPtr<const binder::ModuleSearchRoot> CanonicalModuleSearchRoots::roots() const noexcept {
  return rootValues.asPtr();
}

zc::Array<uint8_t> CanonicalModuleSearchRoots::encode() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(rootValues.size());
  for (const auto& root : rootValues) { root.encode(encoder); }
  return encoder.finish();
}

zc::Maybe<CanonicalModuleSearchRoots> CanonicalModuleSearchRoots::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumSearchRootValueBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumSearchRootsPerCrate);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<binder::ModuleSearchRoot> roots(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto root = binder::ModuleSearchRoot::decodeCanonical(decoder);
    if (root == zc::none) { return zc::none; }
    ZC_IF_SOME(value, root) {
      if (roots.size() != 0 && compareBytes(encodeSearchRoot(roots.back()).asPtr(),
                                            encodeSearchRoot(value).asPtr()) >= 0) {
        return zc::none;
      }
      if (roots.size() != 0 && !sameCrate(roots.front().crate(), value.crate())) {
        return zc::none;
      }
      roots.add(zc::mv(value));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  CanonicalModuleSearchRoots result(zc::mv(roots));
  if (result.encode().asPtr() != bytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalModuleCatalogBucket::CanonicalModuleCatalogBucket(
    identity::ModuleCatalogPathBucket&& bucket) noexcept
    : bucketValue(zc::mv(bucket)) {}

CanonicalModuleCatalogBucket CanonicalModuleCatalogBucket::fromVerified(
    const identity::ModuleCatalogPathBucket& bucket) {
  return CanonicalModuleCatalogBucket(bucket.clone());
}

CanonicalModuleCatalogBucket CanonicalModuleCatalogBucket::clone() const {
  return CanonicalModuleCatalogBucket(bucketValue.clone());
}

const identity::ModuleCatalogPathBucketKey& CanonicalModuleCatalogBucket::key() const noexcept {
  return bucketValue.key();
}

zc::Maybe<const identity::ModuleKey&> CanonicalModuleCatalogBucket::module() const noexcept {
  return bucketValue.module();
}

zc::Array<uint8_t> CanonicalModuleCatalogBucket::encode() const {
  identity::CanonicalEncoder encoder;
  const auto keyBytes = bucketValue.key().encode();
  const auto valueBytes = bucketValue.encode();
  encoder.encodeByteString(keyBytes.asPtr());
  encoder.encodeByteString(valueBytes.asPtr());
  return encoder.finish();
}

zc::Maybe<CanonicalModuleCatalogBucket> CanonicalModuleCatalogBucket::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCatalogBucketBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto keyBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  auto valueBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (keyBytes == zc::none || valueBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto key =
      identity::ModuleCatalogPathBucketKey::decodeCanonical(ZC_ASSERT_NONNULL(keyBytes).asPtr());
  if (key == zc::none) { return zc::none; }
  auto bucket = identity::ModuleCatalogPathBucket::decodeCanonical(
      zc::mv(ZC_ASSERT_NONNULL(key)), ZC_ASSERT_NONNULL(valueBytes).asPtr());
  if (bucket == zc::none) { return zc::none; }
  ZC_IF_SOME(value, bucket) {
    CanonicalModuleCatalogBucket result(zc::mv(value));
    if (result.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(result);
  }
  return zc::none;
}

zc::Array<uint8_t> RequesterModuleAncestryInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<RequesterModuleAncestryInput::Key> RequesterModuleAncestryInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}
zc::Array<uint8_t> RequesterModuleAncestryInput::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<RequesterModuleAncestryInput::Value> RequesterModuleAncestryInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::RequesterModuleAncestry::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleCatalogPathBucketInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<ModuleCatalogPathBucketInput::Key> ModuleCatalogPathBucketInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::ModuleCatalogPathBucketKey::decodeCanonical(bytes);
}
zc::Array<uint8_t> ModuleCatalogPathBucketInput::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<ModuleCatalogPathBucketInput::Value> ModuleCatalogPathBucketInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalModuleCatalogBucket::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleSearchRootsInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<ModuleSearchRootsInput::Key> ModuleSearchRootsInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeCrate(bytes);
}
zc::Array<uint8_t> ModuleSearchRootsInput::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<ModuleSearchRootsInput::Value> ModuleSearchRootsInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalModuleSearchRoots::decodeCanonical(bytes);
}

zc::Array<uint8_t> DependencyAliasRootInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<DependencyAliasRootInput::Key> DependencyAliasRootInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return DependencyAliasRootQueryKey::decodeCanonical(bytes);
}
zc::Array<uint8_t> DependencyAliasRootInput::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<DependencyAliasRootInput::Value> DependencyAliasRootInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ExplicitModuleTarget::decodeCanonical(bytes);
}

zc::Array<uint8_t> ConfiguredPreludeInput::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<ConfiguredPreludeInput::Key> ConfiguredPreludeInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeCrate(bytes);
}
zc::Array<uint8_t> ConfiguredPreludeInput::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<ConfiguredPreludeInput::Value> ConfiguredPreludeInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ExplicitModuleTarget::decodeCanonical(bytes);
}

zc::Array<uint8_t> ResolveModuleRequest::encodeKey(const Key& key) { return key.encode(); }
zc::Maybe<ResolveModuleRequest::Key> ResolveModuleRequest::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::ModuleResolutionKey::decodeCanonical(bytes);
}
zc::Array<uint8_t> ResolveModuleRequest::encodeValue(const Value& value) {
  return value.encode();
}
zc::Maybe<ResolveModuleRequest::Value> ResolveModuleRequest::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::ModuleResolutionCandidates::decodeCanonical(bytes);
}
query::TypedQueryResult<ResolveModuleRequest::Value> ResolveModuleRequest::provide(
    query::QueryContext& context, const Key& key) {
  auto candidates = providerCandidates(context, key);
  if (candidates == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(candidates)));
}
bool ResolveModuleRequest::verify(query::QueryContext& context, const Key& key,
                                       const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto expected = verifierCandidates(context, key);
  return expected != zc::none &&
         ZC_ASSERT_NONNULL(expected).encode().asPtr() == result.value().encode().asPtr();
}

bool stageModuleResolutionQueryInputs(
    query::InputTransaction& transaction, const binder::StructuralModuleResolver& resolver,
    zc::ArrayPtr<const binder::ModuleDependencyRequest> requests) {
  zc::TreeMap<zc::String, identity::CrateKey> crates;
  zc::TreeMap<zc::String, bool> stagedCatalogBuckets;
  zc::TreeMap<zc::String, bool> stagedAliasRoots;
  zc::TreeMap<zc::String, bool> stagedPreludes;
  for (const auto& root : resolver.searchRootInputs()) {
    auto encoded = zc::encodeHex(root.crate().encode().asPtr());
    if (crates.find(encoded) == zc::none) { crates.insert(zc::mv(encoded), root.crate().clone()); }
  }
  for (const auto& entry : crates) {
    auto roots = CanonicalModuleSearchRoots::fromVerified(entry.value, resolver.searchRootInputs());
    if (roots == zc::none) { return false; }
    ZC_IF_SOME(value, roots) {
      if (!transaction.set<ModuleSearchRootsInput>(entry.value, value).isApplied()) {
        return false;
      }
    }
  }

  for (const auto& ancestry : resolver.requesterAncestryInputs()) {
    if (!transaction.set<RequesterModuleAncestryInput>(ancestry.requester(), ancestry)
             .isApplied()) {
      return false;
    }
  }
  for (const auto& bucket : resolver.catalogPathBucketInputs()) {
    auto encodedKey = zc::encodeHex(bucket.key().encode().asPtr());
    if (stagedCatalogBuckets.find(encodedKey) != zc::none) { return false; }
    auto value = CanonicalModuleCatalogBucket::fromVerified(bucket);
    if (!transaction.set<ModuleCatalogPathBucketInput>(bucket.key(), value).isApplied()) {
      return false;
    }
    stagedCatalogBuckets.insert(zc::mv(encodedKey), true);
  }
  for (const auto& alias : resolver.dependencyAliasRootInputs()) {
    auto key = DependencyAliasRootQueryKey::from(alias.requester.clone(), alias.alias.clone());
    if (key == zc::none) { return false; }
    auto value = ExplicitModuleTarget::present(alias.target.clone());
    ZC_IF_SOME(keyValue, key) {
      auto encodedKey = zc::encodeHex(keyValue.encode().asPtr());
      if (stagedAliasRoots.find(encodedKey) != zc::none) { return false; }
      if (!transaction.set<DependencyAliasRootInput>(keyValue, value).isApplied()) { return false; }
      stagedAliasRoots.insert(zc::mv(encodedKey), true);
    }
  }

  for (size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex) {
    const auto& request = requests[requestIndex];
    const auto& requester = request.key().requester();
    if (request.isPrelude()) {
      for (size_t priorIndex = 0; priorIndex < requestIndex; ++priorIndex) {
        const auto& prior = requests[priorIndex];
        if (prior.isPrelude() && sameCrate(prior.key().requester().crate(), requester.crate()) &&
            !sameModule(prior.requestedTarget(), request.requestedTarget())) {
          return false;
        }
      }
      auto configured = ExplicitModuleTarget::present(request.requestedTarget().clone());
      auto encodedPrelude = zc::encodeHex(requester.crate().encode().asPtr());
      if (stagedPreludes.find(encodedPrelude) == zc::none) {
        if (!transaction.set<ConfiguredPreludeInput>(requester.crate(), configured).isApplied()) {
          return false;
        }
        stagedPreludes.insert(zc::mv(encodedPrelude), true);
      }
      if (!stageCatalogBucket(transaction, resolver, request.requestedTarget().crate(),
                              request.requestedTarget().path(), stagedCatalogBuckets)) {
        return false;
      }
      continue;
    }

    auto ancestry = ancestryFor(resolver, requester);
    if (ancestry == zc::none || request.normalizedPath().size() == 0) { return false; }
    ZC_IF_SOME(value, ancestry) {
      for (const auto& ancestor : value.ancestry()) {
        auto path = clonePath(ancestor.path());
        for (const auto& segment : request.normalizedPath()) { path.add(segment.clone()); }
        if (!stageCatalogBucket(transaction, resolver, requester.crate(), path.asPtr(),
                                stagedCatalogBuckets)) {
          return false;
        }
      }
    }
    if (!stageCatalogBucket(transaction, resolver, requester.crate(), request.normalizedPath(),
                            stagedCatalogBuckets)) {
      return false;
    }

    auto alias = identity::DependencyAlias::fromCanonical(request.normalizedPath().front().text());
    if (alias == zc::none) { return false; }
    ZC_IF_SOME(aliasValue, alias) {
      auto inputKey =
          DependencyAliasRootQueryKey::from(requester.crate().clone(), aliasValue.clone());
      if (inputKey == zc::none) { return false; }
      auto target = aliasFor(resolver, requester.crate(), aliasValue.text());
      const auto encodedAlias = request.key().dependencyAlias();
      if ((target == zc::none) != (encodedAlias == zc::none)) { return false; }
      ZC_IF_SOME(expected, encodedAlias) {
        if (expected != aliasValue.text()) { return false; }
      }
      auto inputValue = ExplicitModuleTarget::absent();
      ZC_IF_SOME(targetValue, target) {
        inputValue = ExplicitModuleTarget::present(targetValue.target.clone());
        auto path = clonePath(targetValue.target.path());
        for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
          path.add(request.normalizedPath()[index].clone());
        }
        if (!stageCatalogBucket(transaction, resolver, targetValue.target.crate(), path.asPtr(),
                                stagedCatalogBuckets)) {
          return false;
        }
      }
      ZC_IF_SOME(keyValue, inputKey) {
        auto encodedKey = zc::encodeHex(keyValue.encode().asPtr());
        if (stagedAliasRoots.find(encodedKey) == zc::none) {
          if (!transaction.set<DependencyAliasRootInput>(keyValue, inputValue).isApplied()) {
            return false;
          }
          stagedAliasRoots.insert(zc::mv(encodedKey), true);
        }
      }
    }
  }
  return true;
}

bool registerIncrementalModuleResolutionQueries(query::QueryDatabase& database) {
  if (!database.registerDescriptor<RequesterModuleAncestryInput>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleCatalogPathBucketInput>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleSearchRootsInput>().isRegistered()) { return false; }
  if (!database.registerDescriptor<DependencyAliasRootInput>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ConfiguredPreludeInput>().isRegistered()) { return false; }
  return database.registerDescriptor<ResolveModuleRequest>().isRegistered();
}

}  // namespace zomlang::compiler::driver::incremental_module_resolution_query
