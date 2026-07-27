// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-resolution.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool nonzero(const identity::Sha256Digest& digest) noexcept {
  for (const auto byte : digest.bytes()) {
    if (byte != 0) { return true; }
  }
  return false;
}

template <typename Value, typename EncodeKey>
bool canonicalSort(zc::Vector<Value>& values, EncodeKey&& encodeKey) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    auto currentKey = encodeKey(current);
    size_t insertion = index;
    while (insertion > 0) {
      auto priorKey = encodeKey(values[insertion - 1]);
      if (!lessBytes(currentKey.asPtr(), priorKey.asPtr())) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (encodeKey(values[index - 1]).asPtr() == encodeKey(values[index]).asPtr()) { return false; }
  }
  return true;
}

zc::Array<uint8_t> encodeBuildOutput(const identity::BuildScriptProducerKey& value) {
  identity::CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodeSearchRoot(const ModuleSearchRoot& value) {
  identity::CanonicalEncoder encoder;
  value.encode(encoder);
  return encoder.finish();
}

zc::Array<uint8_t> encodeAliasKey(const ModuleDependencyAliasRoot& value) {
  identity::CanonicalEncoder encoder;
  value.requester.encode(encoder);
  value.alias.encode(encoder);
  return encoder.finish();
}

bool sameKey(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Maybe<size_t> uniqueCatalogIndex(zc::ArrayPtr<const StructuralModuleCatalogEntry> catalog,
                                     identity::ModuleId module) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < catalog.size(); ++index) {
    if (catalog[index].module != module) { continue; }
    if (result != zc::none) { return zc::none; }
    result = index;
  }
  return result;
}

zc::Maybe<size_t> uniqueAncestryCandidateIndex(
    zc::ArrayPtr<const RequesterModuleAncestryCandidate> ancestry,
    const identity::ModuleKey& requester) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < ancestry.size(); ++index) {
    if (!sameKey(ancestry[index].requester, requester)) { continue; }
    if (result != zc::none) { return zc::none; }
    result = index;
  }
  return result;
}

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool samePackage(const identity::PackageKey& left, const identity::PackageKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool startsWith(zc::ArrayPtr<const identity::CanonicalPathSegment> path,
                zc::ArrayPtr<const identity::CanonicalPathSegment> prefix) {
  if (path.size() < prefix.size()) { return false; }
  for (size_t index = 0; index < prefix.size(); ++index) {
    if (path[index] != prefix[index]) { return false; }
  }
  return true;
}

zc::Vector<identity::ModulePathSegment> clonePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Vector<identity::ModuleKey> cloneModules(zc::ArrayPtr<const identity::ModuleKey> modules) {
  zc::Vector<identity::ModuleKey> result(modules.size());
  for (const auto& module : modules) { result.add(module.clone()); }
  return result;
}

identity::ModuleResolutionPolicyKey fixedResolutionPolicy() {
  auto policy = identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  ZC_IREQUIRE(policy != zc::none, "fixed module-resolution policy must be valid");
  ZC_IF_SOME(value, policy) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

zc::Array<uint8_t> encodeSyntaxSite(const ModuleSyntaxDependencySite& site) {
  identity::CanonicalEncoder encoder;
  site.span.source().encode(encoder);
  encoder.encodeUint64(site.span.byteStart());
  encoder.encodeUint64(site.span.byteEnd());
  encoder.encodeUint32(site.schemaPreorderOrdinal);
  encoder.encodeUint32(site.node.value);
  return encoder.finish();
}

bool canonicalSortSyntaxSites(zc::Vector<ModuleSyntaxDependencySite>& sites) {
  return canonicalSort(sites, [](const auto& site) { return encodeSyntaxSite(site); });
}

zc::Vector<ModuleSyntaxDependencySite> cloneSyntaxSites(
    zc::ArrayPtr<const ModuleSyntaxDependencySite> sites) {
  zc::Vector<ModuleSyntaxDependencySite> result(sites.size());
  for (const auto& site : sites) {
    result.add(
        ModuleSyntaxDependencySite(site.node, site.span.clone(), site.schemaPreorderOrdinal));
  }
  return result;
}

ModuleResolutionInvariantFact failure(ModuleResolutionInvariantKind kind) {
  return ModuleResolutionInvariantFact{kind, 1};
}

}  // namespace

ModuleSearchRoot::ModuleSearchRoot(WorkspaceModuleSearchRoot&& root) noexcept
    : value(zc::mv(root)) {}
ModuleSearchRoot::ModuleSearchRoot(PackageModuleSearchRoot&& root) noexcept : value(zc::mv(root)) {}
ModuleSearchRoot::ModuleSearchRoot(GeneratedModuleSearchRoot&& root) noexcept
    : value(zc::mv(root)) {}
ModuleSearchRoot::ModuleSearchRoot(ToolchainCoreModuleSearchRoot&& root) noexcept
    : value(zc::mv(root)) {}

ModuleSearchRoot ModuleSearchRoot::workspace(identity::CrateKey&& crate,
                                             identity::CanonicalWorkspaceRelativePath&& root) {
  return ModuleSearchRoot(WorkspaceModuleSearchRoot{zc::mv(crate), zc::mv(root)});
}
ModuleSearchRoot ModuleSearchRoot::package(identity::CrateKey&& crate,
                                           identity::PackageKey&& package,
                                           identity::CanonicalRelativePath&& root) {
  return ModuleSearchRoot(PackageModuleSearchRoot{zc::mv(crate), zc::mv(package), zc::mv(root)});
}
ModuleSearchRoot ModuleSearchRoot::generated(identity::CrateKey&& crate,
                                             identity::BuildScriptProducerKey producer,
                                             identity::CanonicalRelativePath&& root) {
  return ModuleSearchRoot(GeneratedModuleSearchRoot{zc::mv(crate), producer, zc::mv(root)});
}
zc::Maybe<ModuleSearchRoot> ModuleSearchRoot::toolchainCore(
    identity::CrateKey&& crate, const identity::Sha256Digest& distributionDigest) {
  if (crate.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      crate.unit().toolchain().component() != identity::ToolchainComponent::Core ||
      crate.targetKind() != identity::CrateTargetKind::Library || crate.targetName() != "core"_zc ||
      crate.semanticOptions().editionYear() != 2026 ||
      crate.compilation().hasBuildScriptProducer() || !nonzero(distributionDigest)) {
    return zc::none;
  }
  return ModuleSearchRoot(ToolchainCoreModuleSearchRoot{zc::mv(crate), distributionDigest});
}
ModuleSearchRoot ModuleSearchRoot::clone() const {
  if (value.is<WorkspaceModuleSearchRoot>()) {
    const auto& root = value.get<WorkspaceModuleSearchRoot>();
    return workspace(root.crate.clone(), root.root.clone());
  }
  if (value.is<PackageModuleSearchRoot>()) {
    const auto& root = value.get<PackageModuleSearchRoot>();
    return package(root.crate.clone(), root.package.clone(), root.root.clone());
  }
  if (value.is<GeneratedModuleSearchRoot>()) {
    const auto& root = value.get<GeneratedModuleSearchRoot>();
    return generated(root.crate.clone(),
                     identity::BuildScriptProducerKey::from(root.producer.digest()),
                     root.root.clone());
  }
  const auto& root = value.get<ToolchainCoreModuleSearchRoot>();
  auto cloned = toolchainCore(root.crate.clone(), root.distributionDigest);
  ZC_IF_SOME(result, cloned) { return zc::mv(result); }
  ZC_UNREACHABLE
}
zc::Maybe<ModuleSearchRoot> ModuleSearchRoot::decodeCanonical(identity::CanonicalDecoder& decoder) {
  auto kind = decoder.decodeUint8();
  if (kind == zc::none) { return zc::none; }
  ZC_IF_SOME(value, kind) {
    switch (value) {
      case 0x01: {
        auto crate = identity::CrateKey::decodeCanonical(decoder);
        auto root = identity::CanonicalWorkspaceRelativePath::decodeCanonical(decoder);
        if (crate == zc::none || root == zc::none) { return zc::none; }
        ZC_IF_SOME(crateValue, crate) {
          ZC_IF_SOME(rootValue, root) { return workspace(zc::mv(crateValue), zc::mv(rootValue)); }
        }
        break;
      }
      case 0x02: {
        auto crate = identity::CrateKey::decodeCanonical(decoder);
        auto package = identity::PackageKey::decodeCanonical(decoder);
        auto root = identity::CanonicalRelativePath::decodeCanonical(decoder);
        if (crate == zc::none || package == zc::none || root == zc::none) { return zc::none; }
        ZC_IF_SOME(crateValue, crate) {
          ZC_IF_SOME(packageValue, package) {
            ZC_IF_SOME(rootValue, root) {
              return ModuleSearchRoot::package(zc::mv(crateValue), zc::mv(packageValue),
                                               zc::mv(rootValue));
            }
          }
        }
        break;
      }
      case 0x03: {
        auto crate = identity::CrateKey::decodeCanonical(decoder);
        auto producer = identity::BuildScriptProducerKey::decodeCanonical(decoder);
        auto root = identity::CanonicalRelativePath::decodeCanonical(decoder);
        if (crate == zc::none || producer == zc::none || root == zc::none) { return zc::none; }
        ZC_IF_SOME(crateValue, crate) {
          ZC_IF_SOME(producerValue, producer) {
            ZC_IF_SOME(rootValue, root) {
              return generated(zc::mv(crateValue), producerValue, zc::mv(rootValue));
            }
          }
        }
        break;
      }
      case 0x04: {
        auto crate = identity::CrateKey::decodeCanonical(decoder);
        auto digest = decoder.decodeDigest();
        if (crate == zc::none || digest == zc::none) { return zc::none; }
        ZC_IF_SOME(crateValue, crate) {
          ZC_IF_SOME(digestValue, digest) { return toolchainCore(zc::mv(crateValue), digestValue); }
        }
        break;
      }
      default:
        return zc::none;
    }
  }
  return zc::none;
}
ModuleSearchRootKind ModuleSearchRoot::kind() const noexcept {
  if (value.is<WorkspaceModuleSearchRoot>()) { return ModuleSearchRootKind::Workspace; }
  if (value.is<PackageModuleSearchRoot>()) { return ModuleSearchRootKind::Package; }
  if (value.is<GeneratedModuleSearchRoot>()) { return ModuleSearchRootKind::Generated; }
  return ModuleSearchRootKind::ToolchainCore;
}
const identity::CrateKey& ModuleSearchRoot::crate() const noexcept {
  if (value.is<WorkspaceModuleSearchRoot>()) {
    return value.get<WorkspaceModuleSearchRoot>().crate;
  }
  if (value.is<PackageModuleSearchRoot>()) { return value.get<PackageModuleSearchRoot>().crate; }
  if (value.is<GeneratedModuleSearchRoot>()) {
    return value.get<GeneratedModuleSearchRoot>().crate;
  }
  return value.get<ToolchainCoreModuleSearchRoot>().crate;
}
const identity::Sha256Digest& ModuleSearchRoot::toolchainCoreDistributionDigest() const noexcept {
  return value.get<ToolchainCoreModuleSearchRoot>().distributionDigest;
}
void ModuleSearchRoot::encode(identity::CanonicalEncoder& encoder) const {
  if (value.is<WorkspaceModuleSearchRoot>()) {
    const auto& root = value.get<WorkspaceModuleSearchRoot>();
    encoder.encodeUint8(0x01);
    root.crate.encode(encoder);
    root.root.encode(encoder);
    return;
  }
  if (value.is<PackageModuleSearchRoot>()) {
    const auto& root = value.get<PackageModuleSearchRoot>();
    encoder.encodeUint8(0x02);
    root.crate.encode(encoder);
    root.package.encode(encoder);
    root.root.encode(encoder);
    return;
  }
  if (value.is<GeneratedModuleSearchRoot>()) {
    const auto& root = value.get<GeneratedModuleSearchRoot>();
    encoder.encodeUint8(0x03);
    root.crate.encode(encoder);
    root.producer.encode(encoder);
    root.root.encode(encoder);
    return;
  }
  const auto& root = value.get<ToolchainCoreModuleSearchRoot>();
  encoder.encodeUint8(0x04);
  root.crate.encode(encoder);
  encoder.encodeDigest(root.distributionDigest);
}

ModuleSourceSnapshotRevision::ModuleSourceSnapshotRevision(
    identity::SourceFileKey&& source, const identity::Sha256Digest& contentDigest) noexcept
    : source(zc::mv(source)), contentDigest(contentDigest) {}
GeneratedModuleSourceRevision::GeneratedModuleSourceRevision(
    identity::BuildScriptProducerKey producer, const identity::Sha256Digest& revision) noexcept
    : producer(producer), revision(revision) {}
ModuleDependencyAliasRoot::ModuleDependencyAliasRoot(identity::CrateKey&& requester,
                                                     identity::DependencyAlias&& alias,
                                                     identity::ModuleKey&& target) noexcept
    : requester(zc::mv(requester)), alias(zc::mv(alias)), target(zc::mv(target)) {}
RequesterModuleAncestryCandidate::RequesterModuleAncestryCandidate(
    identity::ModuleKey&& requester, zc::Vector<identity::ModuleKey>&& ancestry) noexcept
    : requester(zc::mv(requester)), ancestry(zc::mv(ancestry)) {}
ModuleResolutionEnvironmentRecord::ModuleResolutionEnvironmentRecord(
    zc::Vector<ModuleSearchRoot>&& searchRoots,
    zc::Vector<ModuleSourceSnapshotRevision>&& sourceSnapshots,
    zc::Vector<GeneratedModuleSourceRevision>&& generatedSourceRevisions,
    zc::Vector<ModuleDependencyAliasRoot>&& dependencyAliasRoots,
    zc::Vector<RequesterModuleAncestryCandidate>&& requesterAncestry) noexcept
    : searchRoots(zc::mv(searchRoots)),
      sourceSnapshots(zc::mv(sourceSnapshots)),
      generatedSourceRevisions(zc::mv(generatedSourceRevisions)),
      dependencyAliasRoots(zc::mv(dependencyAliasRoots)),
      requesterAncestry(zc::mv(requesterAncestry)) {}
StructuralModuleCatalogEntry::StructuralModuleCatalogEntry(
    identity::ModuleKey&& key, identity::ModuleId module, identity::SourceFileKey&& source) noexcept
    : key(zc::mv(key)), module(module), source(zc::mv(source)) {}
ModuleSyntaxDependencySite::ModuleSyntaxDependencySite(ast::NodeId node,
                                                       identity::SourceSpan&& span,
                                                       uint32_t schemaPreorderOrdinal) noexcept
    : node(node), span(zc::mv(span)), schemaPreorderOrdinal(schemaPreorderOrdinal) {}
ModulePreludeDependencySite::ModulePreludeDependencySite(
    identity::ModuleKey&& selectedTarget) noexcept
    : selectedTarget(zc::mv(selectedTarget)) {}

ModuleDependencyRequest::ModuleDependencyRequest(
    identity::ModuleId requester, identity::ModuleResolutionKey&& key,
    zc::Vector<ModuleSyntaxDependencySite>&& syntaxSites,
    zc::Maybe<ModulePreludeDependencySite>&& preludeSite) noexcept
    : requesterValue(requester),
      keyValue(zc::mv(key)),
      syntaxSiteValues(zc::mv(syntaxSites)),
      preludeSiteValue(zc::mv(preludeSite)) {}

zc::Maybe<ModuleDependencyRequest> ModuleDependencyRequest::source(
    identity::ModuleId requester, identity::ModuleResolutionKey&& key,
    zc::Vector<ModuleSyntaxDependencySite>&& syntaxSites) {
  if (!requester.isValid() || key.dependencyKind() == identity::ModuleDependencyKind::Prelude ||
      key.normalizedPath() == zc::none || syntaxSites.size() == 0 ||
      !canonicalSortSyntaxSites(syntaxSites)) {
    return zc::none;
  }
  for (const auto& site : syntaxSites) {
    if (!site.node || site.span.byteStart() > site.span.byteEnd()) { return zc::none; }
  }
  zc::Maybe<ModulePreludeDependencySite> noPrelude;
  return ModuleDependencyRequest(requester, zc::mv(key), zc::mv(syntaxSites), zc::mv(noPrelude));
}

zc::Maybe<ModuleDependencyRequest> ModuleDependencyRequest::prelude(
    identity::ModuleId requester, identity::ModuleResolutionKey&& key,
    identity::ModuleKey&& selectedTarget) {
  if (!requester.isValid() || key.dependencyKind() != identity::ModuleDependencyKind::Prelude ||
      key.normalizedPath() != zc::none || key.dependencyAlias() != zc::none) {
    return zc::none;
  }
  zc::Vector<ModuleSyntaxDependencySite> noSyntaxSites;
  zc::Maybe<ModulePreludeDependencySite> preludeSite(
      ModulePreludeDependencySite(zc::mv(selectedTarget)));
  return ModuleDependencyRequest(requester, zc::mv(key), zc::mv(noSyntaxSites),
                                 zc::mv(preludeSite));
}

ModuleDependencyRequest ModuleDependencyRequest::clone() const {
  zc::Maybe<ModulePreludeDependencySite> preludeSite;
  ZC_IF_SOME(value, preludeSiteValue) {
    preludeSite = ModulePreludeDependencySite(value.selectedTarget.clone());
  }
  return ModuleDependencyRequest(requesterValue, keyValue.clone(),
                                 cloneSyntaxSites(syntaxSiteValues.asPtr()), zc::mv(preludeSite));
}
const identity::ModuleResolutionKey& ModuleDependencyRequest::key() const noexcept {
  return keyValue;
}
identity::ModuleId ModuleDependencyRequest::requester() const noexcept { return requesterValue; }
identity::ModuleDependencyKind ModuleDependencyRequest::kind() const noexcept {
  return keyValue.dependencyKind();
}
zc::ArrayPtr<const identity::ModulePathSegment> ModuleDependencyRequest::normalizedPath()
    const noexcept {
  ZC_IF_SOME(path, keyValue.normalizedPath()) { return path; }
  return nullptr;
}
bool ModuleDependencyRequest::isPrelude() const noexcept {
  return keyValue.dependencyKind() == identity::ModuleDependencyKind::Prelude;
}
zc::ArrayPtr<const ModuleSyntaxDependencySite> ModuleDependencyRequest::syntaxSites()
    const noexcept {
  return syntaxSiteValues.asPtr();
}
const ModuleSyntaxDependencySite& ModuleDependencyRequest::syntaxSite() const {
  ZC_IREQUIRE(syntaxSiteValues.size() != 0, "prelude request has no syntax provenance");
  return syntaxSiteValues.front();
}
const identity::ModuleKey& ModuleDependencyRequest::requestedTarget() const {
  ZC_IF_SOME(value, preludeSiteValue) { return value.selectedTarget; }
  ZC_UNREACHABLE;
}

struct StructuralModuleResolver::Impl final {
  Impl(identity::SemanticContextBrand context, ModuleResolutionEnvironmentRecord&& environment,
       identity::ModuleResolutionPolicyKey&& policy,
       zc::Vector<StructuralModuleCatalogEntry>&& catalog,
       zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestry,
       zc::Vector<identity::ModuleCatalogPathBucket>&& catalogBuckets,
       zc::TreeMap<zc::String, size_t>&& bucketSlots) noexcept
      : context(context),
        environment(zc::mv(environment)),
        policy(zc::mv(policy)),
        catalog(zc::mv(catalog)),
        requesterAncestry(zc::mv(requesterAncestry)),
        catalogBuckets(zc::mv(catalogBuckets)),
        bucketSlots(zc::mv(bucketSlots)) {}

  identity::SemanticContextBrand context;
  ModuleResolutionEnvironmentRecord environment;
  identity::ModuleResolutionPolicyKey policy;
  zc::Vector<StructuralModuleCatalogEntry> catalog;
  zc::Vector<identity::RequesterModuleAncestry> requesterAncestry;
  zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets;
  zc::TreeMap<zc::String, size_t> bucketSlots;
};

StructuralModuleResolver::StructuralModuleResolver(zc::Own<Impl>&& resolverImpl) noexcept
    : impl(zc::mv(resolverImpl)) {}
StructuralModuleResolver::~StructuralModuleResolver() noexcept(false) = default;
StructuralModuleResolver::StructuralModuleResolver(StructuralModuleResolver&&) noexcept = default;
StructuralModuleResolver& StructuralModuleResolver::operator=(StructuralModuleResolver&&) noexcept =
    default;

StructuralModuleResolver::FreezeResult StructuralModuleResolver::freeze(
    identity::SemanticContextBrand context, const identity::SemanticIdentityRegistrySet& registries,
    ModuleResolutionEnvironmentRecord&& environment,
    zc::Vector<StructuralModuleCatalogEntry>&& catalog) {
  if (!context.isValid() || !registries.compilationUnits().isFrozen() ||
      !registries.crates().isFrozen() || !registries.sourceFiles().isFrozen() ||
      !registries.modules().isFrozen() || environment.searchRoots.size() == 0 ||
      catalog.size() == 0 || catalog.size() != registries.modules().size() ||
      environment.sourceSnapshots.size() != registries.sourceSnapshots().size() ||
      environment.requesterAncestry.size() != catalog.size()) {
    return failure(ModuleResolutionInvariantKind::InputMismatch);
  }
  if (!canonicalSort(environment.searchRoots,
                     [](const auto& value) { return encodeSearchRoot(value); }) ||
      !canonicalSort(environment.sourceSnapshots,
                     [](const auto& value) { return value.source.encode(); }) ||
      !canonicalSort(environment.generatedSourceRevisions,
                     [](const auto& value) { return encodeBuildOutput(value.producer); }) ||
      !canonicalSort(environment.dependencyAliasRoots,
                     [](const auto& value) { return encodeAliasKey(value); }) ||
      !canonicalSort(environment.requesterAncestry,
                     [](const auto& value) { return value.requester.encode(); }) ||
      !canonicalSort(catalog, [](const auto& value) { return value.key.encode(); })) {
    return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
  }

  zc::TreeMap<zc::String, size_t> moduleSlots;
  zc::TreeMap<zc::String, size_t> bucketSlots;
  zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets(catalog.size());
  for (size_t index = 0; index < catalog.size(); ++index) {
    auto moduleSlotKey = zc::encodeHex(catalog[index].key.encode().asPtr());
    if (moduleSlots.find(moduleSlotKey) != zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    moduleSlots.insert(zc::mv(moduleSlotKey), index);

    auto bucketKey = identity::ModuleCatalogPathBucketKey::from(
        catalog[index].key.crate().clone(), clonePath(catalog[index].key.path()));
    if (bucketKey == zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    ZC_IF_SOME(key, bucketKey) {
      auto bucket =
          identity::ModuleCatalogPathBucket::present(key.clone(), catalog[index].key.clone());
      if (bucket == zc::none) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
      auto bucketSlotKey = zc::encodeHex(key.encode().asPtr());
      if (bucketSlots.find(bucketSlotKey) != zc::none) {
        return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
      }
      bucketSlots.insert(zc::mv(bucketSlotKey), catalogBuckets.size());
      ZC_IF_SOME(value, bucket) { catalogBuckets.add(zc::mv(value)); }
    }
  }

  zc::Vector<identity::RequesterModuleAncestry> requesterAncestry(catalog.size());

  for (const auto& root : environment.searchRoots) {
    if (registries.crates().find(root.crate()) == zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    switch (root.kind()) {
      case ModuleSearchRootKind::Workspace: {
        const auto& value = root.value.get<WorkspaceModuleSearchRoot>();
        if (value.crate.unit().kind() != identity::CompilationUnitKind::UserPackage) {
          return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
        }
        const auto& package = value.crate.unit().userPackage();
        if (package.source().kind() != identity::PackageSourceKind::LocalPath ||
            value.root.leadingParents() != package.source().localPath().leadingParents() ||
            !startsWith(value.root.segments(), package.source().localPath().segments())) {
          return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
        }
        break;
      }
      case ModuleSearchRootKind::Package: {
        const auto& value = root.value.get<PackageModuleSearchRoot>();
        if (value.crate.unit().kind() != identity::CompilationUnitKind::UserPackage) {
          return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
        }
        const auto& package = value.crate.unit().userPackage();
        const auto sourceKind = package.source().kind();
        if (!samePackage(package, value.package) ||
            (sourceKind != identity::PackageSourceKind::Registry &&
             sourceKind != identity::PackageSourceKind::Vcs)) {
          return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
        }
        break;
      }
      case ModuleSearchRootKind::Generated: {
        const auto& value = root.value.get<GeneratedModuleSearchRoot>();
        bool foundRevision = false;
        for (const auto& revision : environment.generatedSourceRevisions) {
          if (revision.producer.digest() != value.producer.digest()) { continue; }
          if (foundRevision || !nonzero(revision.revision) || !nonzero(value.producer.digest())) {
            return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
          }
          foundRevision = true;
        }
        if (!foundRevision) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
        break;
      }
      case ModuleSearchRootKind::ToolchainCore: {
        const auto& value = root.value.get<ToolchainCoreModuleSearchRoot>();
        if (value.crate.unit().kind() != identity::CompilationUnitKind::Toolchain ||
            value.crate.unit().toolchain().component() != identity::ToolchainComponent::Core ||
            value.crate.targetKind() != identity::CrateTargetKind::Library ||
            value.crate.targetName() != "core"_zc ||
            value.crate.semanticOptions().editionYear() != 2026 ||
            value.crate.compilation().hasBuildScriptProducer() ||
            !nonzero(value.distributionDigest)) {
          return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
        }
        bool foundCoreModule = false;
        for (const auto& entry : catalog) {
          if (!sameCrate(value.crate, entry.key.crate())) { continue; }
          foundCoreModule = true;
          if (entry.source.origin().kind() != identity::SourceOriginKind::CoreFile) {
            return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
          }
        }
        if (!foundCoreModule) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
        break;
      }
    }
  }

  for (const auto& revision : environment.generatedSourceRevisions) {
    if (!nonzero(revision.producer.digest()) || !nonzero(revision.revision)) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    bool used = false;
    for (const auto& root : environment.searchRoots) {
      if (root.kind() != ModuleSearchRootKind::Generated) { continue; }
      const auto& generated = root.value.get<GeneratedModuleSearchRoot>();
      used = used || generated.producer.digest() == revision.producer.digest();
    }
    if (!used) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
  }

  for (const auto& root : environment.searchRoots) {
    bool ownsModule = false;
    for (const auto& entry : catalog) {
      ownsModule = ownsModule || sameCrate(root.crate(), entry.key.crate());
    }
    if (!ownsModule) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
  }

  for (const auto& entry : catalog) {
    size_t baseRootCount = 0;
    for (const auto& root : environment.searchRoots) {
      if (root.kind() != ModuleSearchRootKind::Generated &&
          sameCrate(root.crate(), entry.key.crate())) {
        ++baseRootCount;
      }
    }
    if (baseRootCount != 1) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
  }

  for (const auto& source : environment.sourceSnapshots) {
    if (!nonzero(source.contentDigest)) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    bool found = false;
    for (const auto& snapshot : registries.sourceSnapshots()) {
      if (!source.source.sameAs(snapshot.source())) { continue; }
      if (found || source.contentDigest != snapshot.contentDigest()) {
        return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
      }
      found = true;
    }
    if (!found) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
  }

  for (const auto& entry : catalog) {
    if (!entry.module.belongsTo(context) ||
        registries.modules().validate(entry.module) != identity::FrozenRegistryFailure::None ||
        registries.sourceFiles().find(entry.source) == zc::none ||
        !entry.source.belongsTo(entry.key.crate())) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    auto registered = registries.modules().lookup(entry.module);
    if (registered == zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    ZC_IF_SOME(value, registered) {
      if (!sameKey(value, entry.key)) {
        return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
      }
    }
    auto ancestryResult =
        uniqueAncestryCandidateIndex(environment.requesterAncestry.asPtr(), entry.key);
    if (ancestryResult == zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    size_t ancestryIndex = 0;
    ZC_IF_SOME(value, ancestryResult) { ancestryIndex = value; }
    const auto& ancestryCandidate = environment.requesterAncestry[ancestryIndex];
    auto ancestry = identity::RequesterModuleAncestry::from(
        ancestryCandidate.requester.clone(), cloneModules(ancestryCandidate.ancestry.asPtr()));
    if (ancestry == zc::none) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
    ZC_IF_SOME(value, ancestry) {
      const auto chain = value.ancestry();
      if (chain.back().path().size() != 1) {
        return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
      }
      bool activeRoot = false;
      for (const auto& catalogEntry : catalog) {
        activeRoot = activeRoot || sameKey(catalogEntry.key, chain.back());
      }
      if (!activeRoot) { return failure(ModuleResolutionInvariantKind::InvalidEnvironment); }
      requesterAncestry.add(zc::mv(value));
    }
  }

  for (const auto& alias : environment.dependencyAliasRoots) {
    if (registries.crates().find(alias.requester) == zc::none) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
    bool requesterPresent = false;
    bool targetPresent = false;
    for (const auto& entry : catalog) {
      requesterPresent = requesterPresent || sameCrate(entry.key.crate(), alias.requester);
      targetPresent = targetPresent || sameKey(entry.key, alias.target);
    }
    if (!requesterPresent || !targetPresent) {
      return failure(ModuleResolutionInvariantKind::InvalidEnvironment);
    }
  }

  auto policy = fixedResolutionPolicy();
  return StructuralModuleResolver(zc::heap<Impl>(context, zc::mv(environment), zc::mv(policy),
                                                 zc::mv(catalog), zc::mv(requesterAncestry),
                                                 zc::mv(catalogBuckets), zc::mv(bucketSlots)));
}
const identity::ModuleResolutionPolicyKey& StructuralModuleResolver::policy() const noexcept {
  return impl->policy;
}
zc::ArrayPtr<const StructuralModuleCatalogEntry> StructuralModuleResolver::catalog()
    const noexcept {
  return impl->catalog.asPtr();
}
zc::ArrayPtr<const ModuleSearchRoot> StructuralModuleResolver::searchRootInputs() const noexcept {
  return impl->environment.searchRoots.asPtr();
}
zc::ArrayPtr<const ModuleDependencyAliasRoot> StructuralModuleResolver::dependencyAliasRootInputs()
    const noexcept {
  return impl->environment.dependencyAliasRoots.asPtr();
}
zc::ArrayPtr<const identity::RequesterModuleAncestry>
StructuralModuleResolver::requesterAncestryInputs() const noexcept {
  return impl->requesterAncestry.asPtr();
}
zc::ArrayPtr<const identity::ModuleCatalogPathBucket>
StructuralModuleResolver::catalogPathBucketInputs() const noexcept {
  return impl->catalogBuckets.asPtr();
}

zc::Maybe<identity::DependencyAlias> StructuralModuleResolver::dependencyAlias(
    const identity::CrateKey& requester,
    const identity::ModulePathSegment& firstPathSegment) const {
  zc::Maybe<identity::DependencyAlias> selected;
  for (const auto& alias : impl->environment.dependencyAliasRoots) {
    if (!sameCrate(alias.requester, requester) || alias.alias.text() != firstPathSegment.text()) {
      continue;
    }
    if (selected != zc::none) { return zc::none; }
    selected = alias.alias.clone();
  }
  return selected;
}

zc::Maybe<identity::ModuleResolutionKey> StructuralModuleResolver::resolutionKey(
    identity::ModuleId requester, identity::ModuleDependencyKind kind,
    zc::Vector<identity::ModulePathSegment>&& normalizedPath) const {
  auto requesterResult = uniqueCatalogIndex(impl->catalog.asPtr(), requester);
  if (requesterResult == zc::none) { return zc::none; }
  size_t requesterIndex = 0;
  ZC_IF_SOME(value, requesterResult) { requesterIndex = value; }
  const auto& requesterKey = impl->catalog[requesterIndex].key;
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> path;
  zc::Maybe<identity::DependencyAlias> alias;
  if (kind == identity::ModuleDependencyKind::Prelude) {
    if (normalizedPath.size() != 0) { return zc::none; }
  } else {
    if (normalizedPath.size() == 0) { return zc::none; }
    alias = dependencyAlias(requesterKey.crate(), normalizedPath.front());
    path = zc::mv(normalizedPath);
  }
  return identity::ModuleResolutionKey::from(requesterKey.clone(), kind, zc::mv(path),
                                             zc::mv(alias), impl->policy.clone());
}

zc::Maybe<identity::ModuleCatalogPathBucket> StructuralModuleResolver::readCatalogPathBucket(
    const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) const {
  auto key = identity::ModuleCatalogPathBucketKey::from(crate.clone(), clonePath(path));
  if (key == zc::none) { return zc::none; }
  ZC_IF_SOME(value, key) {
    ZC_IF_SOME(slot, impl->bucketSlots.find(zc::encodeHex(value.encode().asPtr()))) {
      if (slot >= impl->catalogBuckets.size()) { return zc::none; }
      return impl->catalogBuckets[slot].clone();
    }
    return identity::ModuleCatalogPathBucket::absent(zc::mv(value));
  }
  return zc::none;
}

zc::Maybe<identity::ModuleCatalogPathBucket> StructuralModuleResolver::catalogPathBucketInput(
    const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) const {
  return readCatalogPathBucket(crate, path);
}

}  // namespace zomlang::compiler::binder
