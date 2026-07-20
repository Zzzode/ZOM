// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/crate-graph.h"

#include "zc/core/vector.h"

namespace zomlang::compiler::driver {
namespace {

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

template <typename Value>
void canonicalSort(zc::Vector<Value>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < values[insertion - 1].encode().asPtr()) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename Value>
bool hasDuplicate(const zc::Vector<Value>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    if (sameBytes(values[index - 1].encode().asPtr(), values[index].encode().asPtr())) {
      return true;
    }
  }
  return false;
}

void canonicalSortRoots(zc::Vector<package::FinalizedCompilationRoot>& roots) {
  for (size_t index = 1; index < roots.size(); ++index) {
    auto current = zc::mv(roots[index]);
    const auto currentBytes = current.crateKey().encode();
    size_t insertion = index;
    while (insertion != 0 &&
           currentBytes.asPtr() < roots[insertion - 1].crateKey().encode().asPtr()) {
      roots[insertion] = zc::mv(roots[insertion - 1]);
      --insertion;
    }
    roots[insertion] = zc::mv(current);
  }
}

bool hasDuplicateRoots(const zc::Vector<package::FinalizedCompilationRoot>& roots) {
  for (size_t index = 1; index < roots.size(); ++index) {
    if (sameBytes(roots[index - 1].crateKey().encode().asPtr(),
                  roots[index].crateKey().encode().asPtr())) {
      return true;
    }
  }
  return false;
}

zc::Maybe<size_t> findPackage(const package::ResolutionOutput& resolution,
                              const identity::PackageKey& key) {
  const auto expected = key.encode();
  size_t lower = 0;
  size_t upper = resolution.packages().size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    const auto middleStorage = resolution.packages()[middle].key().encode();
    const zc::ArrayPtr<const uint8_t> middleBytes = middleStorage;
    if (middleBytes < expected.asPtr()) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < resolution.packages().size() &&
      sameBytes(resolution.packages()[lower].key().encode().asPtr(), expected.asPtr())) {
    return lower;
  }
  return zc::none;
}

zc::Maybe<identity::BuildScriptProducerKey> buildProducerFor(
    const package::ResolvedPackageRecord& record, const package::VerifiedBuildScriptPlan& buildPlan,
    CrateGraphIssue& issue) {
  if (!record.manifest().hasBuildScript()) { return zc::none; }
  size_t matches = 0;
  zc::Maybe<identity::BuildScriptProducerKey> producer;
  for (const auto& node : buildPlan.nodes()) {
    if (sameBytes(node.key().preparatory().package().encode().asPtr(),
                  record.key().encode().asPtr())) {
      producer = node.key().preparatory().producerKey();
      ++matches;
    }
  }
  if (matches != 1) {
    issue = CrateGraphIssue::InvalidCrateIdentity;
    return zc::none;
  }
  return producer;
}

zc::OneOf<identity::CrateKey, CrateGraphIssue> makeProviderLibrary(
    const package::VerifiedPackageCompilationRequest& request,
    const package::ResolvedPackageRecord& provider,
    const package::VerifiedBuildScriptPlan& buildPlan) {
  auto library = provider.libraryTarget();
  auto packageManifest = provider.manifest().package();
  if (library == zc::none) { return CrateGraphIssue::MissingProviderLibrary; }
  if (packageManifest == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
  CrateGraphIssue outputIssue = CrateGraphIssue::InvalidCrateIdentity;
  auto buildProducer = buildProducerFor(provider, buildPlan, outputIssue);
  if (provider.manifest().hasBuildScript() && buildProducer == zc::none) { return outputIssue; }
  ZC_IF_SOME(manifest, packageManifest) {
    auto compilation = identity::CompilationConfigKey::from(
        identity::CompilationDomain::Target, request.target().semanticProjection().clone(),
        identity::SemanticCompilerOptionsKey::from(manifest.editionYear(),
                                                   request.languageOptions().useUnicode,
                                                   request.languageOptions().allowDollarIdentifiers,
                                                   request.languageOptions().supportRegexLiterals),
        zc::mv(buildProducer));
    if (compilation == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
    ZC_IF_SOME(target, library) {
      ZC_IF_SOME(config, compilation) {
        auto crate =
            identity::CrateKey::from(provider.key().clone(), identity::CrateTargetKind::Library,
                                     target.clone(), zc::mv(config));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  return CrateGraphIssue::InvalidCrateIdentity;
}

zc::Maybe<package::FinalizedCompilationRoot> makeProviderCompilationRoot(
    const package::ResolvedPackageRecord& provider, const identity::CrateKey& crate) {
  if (crate.targetKind() != identity::CrateTargetKind::Library ||
      !sameBytes(provider.key().encode().asPtr(), crate.package().encode().asPtr())) {
    return zc::none;
  }
  auto libraryTarget = provider.libraryTarget();
  auto libraryManifest = provider.manifest().library();
  if (libraryTarget == zc::none || libraryManifest == zc::none) { return zc::none; }
  ZC_IF_SOME(target, libraryTarget) {
    ZC_IF_SOME(manifest, libraryManifest) {
      if (manifest.kind() != identity::CrateTargetKind::Library ||
          manifest.name() != target.text() || manifest.name() != crate.targetName()) {
        return zc::none;
      }
      return package::FinalizedCompilationRoot::from(provider.key().clone(), crate.clone(),
                                                     manifest.path().clone());
    }
  }
  return zc::none;
}

zc::Maybe<identity::BuildScriptProducerKey> completedBuildProducerFor(
    const package::ResolvedPackageRecord& record, const package::VerifiedBuildScriptPlan& plan,
    zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults,
    CrateGraphIssue& issue) {
  if (!record.manifest().hasBuildScript()) { return zc::none; }
  size_t planMatches = 0;
  zc::Maybe<identity::BuildScriptProducerKey> expectedProducer;
  for (const auto& node : plan.nodes()) {
    if (!sameBytes(node.key().preparatory().package().encode().asPtr(),
                   record.key().encode().asPtr())) {
      continue;
    }
    expectedProducer = node.key().preparatory().producerKey();
    ++planMatches;
  }
  if (planMatches != 1) {
    issue = CrateGraphIssue::InvalidBuildResults;
    return zc::none;
  }
  size_t resultMatches = 0;
  for (const auto& result : completedResults) {
    ZC_IF_SOME(producer, expectedProducer) {
      if (result.output().producerKey().digest() == producer.digest()) { ++resultMatches; }
    }
  }
  if (resultMatches != 1) {
    issue = completedResults.size() == 0 ? CrateGraphIssue::BuildResultsRequired
                                         : CrateGraphIssue::InvalidBuildResults;
    return zc::none;
  }
  return expectedProducer;
}

identity::SemanticCompilerOptionsKey semanticOptions(
    const package::VerifiedPackageCompilationRequest& request, uint32_t editionYear) {
  return identity::SemanticCompilerOptionsKey::from(
      editionYear, request.languageOptions().useUnicode,
      request.languageOptions().allowDollarIdentifiers,
      request.languageOptions().supportRegexLiterals);
}

zc::OneOf<identity::CrateKey, CrateGraphIssue> makeHostLibrary(
    const package::VerifiedPackageCompilationRequest& request,
    const package::ResolvedPackageRecord& provider, const package::VerifiedBuildScriptPlan& plan,
    zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults) {
  auto library = provider.libraryTarget();
  auto packageManifest = provider.manifest().package();
  if (library == zc::none) { return CrateGraphIssue::MissingProviderLibrary; }
  if (packageManifest == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
  CrateGraphIssue outputIssue = CrateGraphIssue::InvalidBuildResults;
  auto buildProducer = completedBuildProducerFor(provider, plan, completedResults, outputIssue);
  if (provider.manifest().hasBuildScript() && buildProducer == zc::none) { return outputIssue; }
  ZC_IF_SOME(manifest, packageManifest) {
    auto compilation = identity::CompilationConfigKey::from(
        identity::CompilationDomain::Host, request.hostTarget().semanticProjection().clone(),
        semanticOptions(request, manifest.editionYear()), zc::mv(buildProducer));
    if (compilation == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
    ZC_IF_SOME(target, library) {
      ZC_IF_SOME(config, compilation) {
        auto crate =
            identity::CrateKey::from(provider.key().clone(), identity::CrateTargetKind::Library,
                                     target.clone(), zc::mv(config));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  return CrateGraphIssue::InvalidCrateIdentity;
}

bool appliesTo(identity::DependencyDomain domain, identity::CrateTargetKind kind) {
  if (domain == identity::DependencyDomain::Target) {
    return kind == identity::CrateTargetKind::Library ||
           kind == identity::CrateTargetKind::Binary || kind == identity::CrateTargetKind::Test ||
           kind == identity::CrateTargetKind::Benchmark ||
           kind == identity::CrateTargetKind::Example;
  }
  if (domain == identity::DependencyDomain::Development) {
    return kind == identity::CrateTargetKind::Test ||
           kind == identity::CrateTargetKind::Benchmark ||
           kind == identity::CrateTargetKind::Example;
  }
  return false;
}

struct EdgeRange final {
  size_t begin = 0;
  size_t end = 0;
};

zc::Vector<EdgeRange> packageEdgeRanges(const package::ResolutionOutput& resolution) {
  zc::Vector<EdgeRange> ranges;
  size_t cursor = 0;
  for (const auto& packageRecord : resolution.packages()) {
    const auto key = packageRecord.key().encode();
    while (cursor < resolution.edges().size()) {
      const auto consumerStorage = resolution.edges()[cursor].consumer().encode();
      const zc::ArrayPtr<const uint8_t> consumerBytes = consumerStorage;
      if (!(consumerBytes < key.asPtr())) { break; }
      ++cursor;
    }
    const size_t begin = cursor;
    while (cursor < resolution.edges().size() &&
           sameBytes(resolution.edges()[cursor].consumer().encode().asPtr(), key.asPtr())) {
      ++cursor;
    }
    ranges.add(EdgeRange{begin, cursor});
  }
  return ranges;
}

struct SelectedPackageTarget final {
  size_t packageIndex;
  identity::CrateTargetKind kind;
};

void addSelectedPackageTarget(zc::Vector<SelectedPackageTarget>& selected, size_t packageIndex,
                              identity::CrateTargetKind kind) {
  for (const auto& existing : selected) {
    if (existing.packageIndex == packageIndex && existing.kind == kind) { return; }
  }
  selected.add(SelectedPackageTarget{packageIndex, kind});
}

zc::Maybe<CrateGraphIssue> collectHostProviderPackages(const package::ResolutionOutput& resolution,
                                                       zc::ArrayPtr<const EdgeRange> ranges,
                                                       size_t rootPackageIndex,
                                                       zc::Vector<size_t>& providers) {
  zc::Vector<uint8_t> included(resolution.packages().size());
  for (size_t index = 0; index < resolution.packages().size(); ++index) { included.add(0); }
  const auto addProvider = [&](const identity::PackageDependencyEdgeKey& edge) {
    auto providerIndex = findPackage(resolution, edge.provider());
    if (providerIndex == zc::none) {
      return zc::Maybe<CrateGraphIssue>(CrateGraphIssue::MissingProviderLibrary);
    }
    ZC_IF_SOME(index, providerIndex) {
      if (resolution.packages()[index].libraryTarget() == zc::none) {
        return zc::Maybe<CrateGraphIssue>(CrateGraphIssue::MissingProviderLibrary);
      }
      if (included[index] == 0) {
        included[index] = 1;
        providers.add(index);
      }
    }
    return zc::Maybe<CrateGraphIssue>();
  };

  const auto rootRange = ranges[rootPackageIndex];
  for (size_t edgeIndex = rootRange.begin; edgeIndex < rootRange.end; ++edgeIndex) {
    const auto& edge = resolution.edges()[edgeIndex];
    if (edge.domain() != identity::DependencyDomain::Build) { continue; }
    ZC_IF_SOME(issue, addProvider(edge)) { return issue; }
  }
  for (size_t cursor = 0; cursor < providers.size(); ++cursor) {
    const auto range = ranges[providers[cursor]];
    for (size_t edgeIndex = range.begin; edgeIndex < range.end; ++edgeIndex) {
      const auto& edge = resolution.edges()[edgeIndex];
      if (edge.domain() != identity::DependencyDomain::Target) { continue; }
      ZC_IF_SOME(issue, addProvider(edge)) { return issue; }
    }
  }
  return zc::none;
}

zc::Maybe<size_t> findCrate(zc::ArrayPtr<const zc::Array<uint8_t>> crateBytes,
                            const identity::CrateKey& crate) {
  const auto expected = crate.encode();
  size_t lower = 0;
  size_t upper = crateBytes.size();
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    if (crateBytes[middle].asPtr() < expected.asPtr()) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < crateBytes.size() && sameBytes(crateBytes[lower].asPtr(), expected.asPtr())) {
    return lower;
  }
  return zc::none;
}

bool hasCycle(zc::ArrayPtr<const identity::CrateKey> crates,
              zc::ArrayPtr<const identity::CrateDependencyEdgeKey> edges) {
  zc::Vector<zc::Array<uint8_t>> crateBytes;
  for (const auto& crate : crates) { crateBytes.add(crate.encode()); }
  zc::Vector<uint64_t> indegree;
  zc::Vector<zc::Vector<size_t>> outgoing;
  for (size_t index = 0; index < crates.size(); ++index) {
    indegree.add(0);
    outgoing.add(zc::Vector<size_t>());
  }
  for (const auto& edge : edges) {
    auto consumer = findCrate(crateBytes.asPtr(), edge.consumer());
    auto provider = findCrate(crateBytes.asPtr(), edge.provider());
    if (consumer == zc::none || provider == zc::none) { return true; }
    ZC_IF_SOME(consumerIndex, consumer) {
      ZC_IF_SOME(providerIndex, provider) {
        outgoing[consumerIndex].add(providerIndex);
        ++indegree[providerIndex];
      }
    }
  }
  zc::Vector<size_t> queue;
  for (size_t index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0) { queue.add(index); }
  }
  size_t visited = 0;
  for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
    ++visited;
    for (const auto target : outgoing[queue[cursor]]) {
      --indegree[target];
      if (indegree[target] == 0) { queue.add(target); }
    }
  }
  return visited != crates.size();
}

}  // namespace

struct VerifiedCrateGraph::Impl final {
  Impl(zc::Vector<package::FinalizedCompilationRoot>&& roots,
       zc::Vector<identity::PackageDependencyEdgeKey>&& packageEdges,
       zc::Vector<identity::CrateKey>&& crates,
       zc::Vector<identity::CrateDependencyEdgeKey>&& edges) noexcept
      : roots(zc::mv(roots)),
        packageEdges(zc::mv(packageEdges)),
        crates(zc::mv(crates)),
        edges(zc::mv(edges)) {}

  zc::Vector<package::FinalizedCompilationRoot> roots;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> edges;
};

VerifiedCrateGraph::VerifiedCrateGraph(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedCrateGraph::~VerifiedCrateGraph() noexcept(false) = default;
VerifiedCrateGraph::VerifiedCrateGraph(VerifiedCrateGraph&&) noexcept = default;
VerifiedCrateGraph& VerifiedCrateGraph::operator=(VerifiedCrateGraph&&) noexcept = default;

CrateGraphBuildResult VerifiedCrateGraph::buildFinal(
    const package::VerifiedPackageCompilationRequest& request,
    const package::ResolutionOutput& resolution,
    const package::VerifiedBuildScriptPlan& buildPlan) {
  auto finalized = request.finalizeRoots(buildPlan);
  if (finalized == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }

  zc::Vector<package::FinalizedCompilationRoot> roots;
  ZC_IF_SOME(values, finalized) { roots = zc::mv(values); }
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<size_t> cratePackages;
  zc::Vector<bool> includedLibraries;
  for (size_t index = 0; index < resolution.packages().size(); ++index) {
    includedLibraries.add(false);
  }
  for (const auto& root : roots) {
    auto packageIndex = findPackage(resolution, root.packageKey());
    if (packageIndex == zc::none) { return CrateGraphIssue::RootOutsideResolution; }
    ZC_IF_SOME(index, packageIndex) {
      size_t matchingRequests = 0;
      bool requestRequiresBuildScript = false;
      for (const auto& requested : request.roots()) {
        if (sameBytes(requested.packageKey().encode().asPtr(),
                      root.packageKey().encode().asPtr()) &&
            requested.targetKind() == root.crateKey().targetKind() &&
            requested.targetName() == root.crateKey().targetName()) {
          requestRequiresBuildScript = requested.requiresBuildScript();
          ++matchingRequests;
        }
      }
      if (matchingRequests != 1 ||
          requestRequiresBuildScript != resolution.packages()[index].manifest().hasBuildScript()) {
        return CrateGraphIssue::InvalidCrateIdentity;
      }
      crates.add(root.crateKey().clone());
      cratePackages.add(index);
      if (root.crateKey().targetKind() == identity::CrateTargetKind::Library) {
        includedLibraries[index] = true;
      }
    }
  }

  const auto ranges = packageEdgeRanges(resolution);
  zc::Vector<identity::CrateDependencyEdgeKey> edges;
  for (size_t cursor = 0; cursor < crates.size(); ++cursor) {
    const auto packageIndex = cratePackages[cursor];
    const auto range = ranges[packageIndex];
    for (size_t edgeIndex = range.begin; edgeIndex < range.end; ++edgeIndex) {
      const auto& packageEdge = resolution.edges()[edgeIndex];
      if (!appliesTo(packageEdge.domain(), crates[cursor].targetKind())) { continue; }
      auto providerIndex = findPackage(resolution, packageEdge.provider());
      if (providerIndex == zc::none) { return CrateGraphIssue::MissingProviderLibrary; }
      ZC_IF_SOME(index, providerIndex) {
        if (!includedLibraries[index]) {
          auto provider = makeProviderLibrary(request, resolution.packages()[index], buildPlan);
          if (provider.is<CrateGraphIssue>()) { return provider.get<CrateGraphIssue>(); }
          auto providerCrate = zc::mv(provider.get<identity::CrateKey>());
          auto providerRoot =
              makeProviderCompilationRoot(resolution.packages()[index], providerCrate);
          if (providerRoot == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
          ZC_IF_SOME(root, providerRoot) { roots.add(zc::mv(root)); }
          crates.add(zc::mv(providerCrate));
          cratePackages.add(index);
          includedLibraries[index] = true;
        }
        size_t providerCrateIndex = 0;
        bool foundProvider = false;
        for (size_t candidate = 0; candidate < crates.size(); ++candidate) {
          if (cratePackages[candidate] == index &&
              crates[candidate].targetKind() == identity::CrateTargetKind::Library) {
            providerCrateIndex = candidate;
            foundProvider = true;
            break;
          }
        }
        if (!foundProvider) { return CrateGraphIssue::InvalidCrateIdentity; }
        auto edge = identity::CrateDependencyEdgeKey::from(
            packageEdge.clone(), crates[cursor].clone(), crates[providerCrateIndex].clone());
        if (edge == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
        ZC_IF_SOME(value, edge) { edges.add(zc::mv(value)); }
      }
    }
  }

  canonicalSort(crates);
  canonicalSort(edges);
  canonicalSortRoots(roots);
  if (hasDuplicate(crates)) { return CrateGraphIssue::DuplicateCrate; }
  if (hasDuplicate(edges)) { return CrateGraphIssue::DuplicateEdge; }
  if (hasDuplicateRoots(roots) || roots.size() != crates.size()) {
    return CrateGraphIssue::InvalidCrateIdentity;
  }
  for (size_t index = 0; index < roots.size(); ++index) {
    if (!sameBytes(roots[index].crateKey().encode().asPtr(), crates[index].encode().asPtr())) {
      return CrateGraphIssue::InvalidCrateIdentity;
    }
  }
  if (hasCycle(crates.asPtr(), edges.asPtr())) { return CrateGraphIssue::DependencyCycle; }
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  for (const auto& edge : edges) { packageEdges.add(edge.packageEdge().clone()); }
  canonicalSort(packageEdges);
  zc::Vector<identity::PackageDependencyEdgeKey> uniquePackageEdges;
  for (auto& edge : packageEdges) {
    if (uniquePackageEdges.empty() ||
        !sameBytes(uniquePackageEdges.back().encode().asPtr(), edge.encode().asPtr())) {
      uniquePackageEdges.add(zc::mv(edge));
    }
  }
  return VerifiedCrateGraph(
      zc::heap<Impl>(zc::mv(roots), zc::mv(uniquePackageEdges), zc::mv(crates), zc::mv(edges)));
}

zc::ArrayPtr<const package::FinalizedCompilationRoot> VerifiedCrateGraph::roots() const noexcept {
  return impl->roots;
}

zc::ArrayPtr<const identity::PackageDependencyEdgeKey> VerifiedCrateGraph::packageEdges()
    const noexcept {
  return impl->packageEdges;
}

zc::ArrayPtr<const identity::CrateKey> VerifiedCrateGraph::crates() const noexcept {
  return impl->crates;
}

zc::ArrayPtr<const identity::CrateDependencyEdgeKey> VerifiedCrateGraph::edges() const noexcept {
  return impl->edges;
}

struct VerifiedPreparatoryCrateGraph::Impl final {
  Impl(identity::CrateKey&& root, zc::Vector<identity::PackageKey>&& packages,
       zc::Vector<identity::PackageDependencyEdgeKey>&& packageEdges,
       zc::Vector<identity::CrateKey>&& crates,
       zc::Vector<identity::CrateDependencyEdgeKey>&& edges,
       identity::SemanticContextFingerprint&& fingerprint) noexcept
      : root(zc::mv(root)),
        packages(zc::mv(packages)),
        packageEdges(zc::mv(packageEdges)),
        crates(zc::mv(crates)),
        edges(zc::mv(edges)),
        fingerprint(zc::mv(fingerprint)) {}

  identity::CrateKey root;
  zc::Vector<identity::PackageKey> packages;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> edges;
  identity::SemanticContextFingerprint fingerprint;
};

VerifiedPreparatoryCrateGraph::VerifiedPreparatoryCrateGraph(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedPreparatoryCrateGraph::~VerifiedPreparatoryCrateGraph() noexcept(false) = default;
VerifiedPreparatoryCrateGraph::VerifiedPreparatoryCrateGraph(
    VerifiedPreparatoryCrateGraph&&) noexcept = default;
VerifiedPreparatoryCrateGraph& VerifiedPreparatoryCrateGraph::operator=(
    VerifiedPreparatoryCrateGraph&&) noexcept = default;

BuildScriptPlanBuildResult VerifiedPreparatoryCrateGraph::buildPlan(
    const package::VerifiedPackageCompilationRequest& request,
    const package::ResolutionOutput& resolution) {
  const auto ranges = packageEdgeRanges(resolution);
  zc::Vector<SelectedPackageTarget> selected;
  for (const auto& root : request.roots()) {
    auto packageIndex = findPackage(resolution, root.packageKey());
    if (packageIndex == zc::none) { return CrateGraphIssue::RootOutsideResolution; }
    ZC_IF_SOME(index, packageIndex) {
      addSelectedPackageTarget(selected, index, root.targetKind());
    }
  }
  for (size_t cursor = 0; cursor < selected.size(); ++cursor) {
    const auto current = selected[cursor];
    const auto range = ranges[current.packageIndex];
    for (size_t edgeIndex = range.begin; edgeIndex < range.end; ++edgeIndex) {
      const auto& edge = resolution.edges()[edgeIndex];
      if (!appliesTo(edge.domain(), current.kind)) { continue; }
      auto providerIndex = findPackage(resolution, edge.provider());
      if (providerIndex == zc::none) { return CrateGraphIssue::MissingProviderLibrary; }
      ZC_IF_SOME(index, providerIndex) {
        if (resolution.packages()[index].libraryTarget() == zc::none) {
          return CrateGraphIssue::MissingProviderLibrary;
        }
        addSelectedPackageTarget(selected, index, identity::CrateTargetKind::Library);
      }
    }
  }

  zc::Vector<uint8_t> required(resolution.packages().size());
  zc::Vector<uint8_t> processed(resolution.packages().size());
  for (size_t index = 0; index < resolution.packages().size(); ++index) {
    required.add(0);
    processed.add(0);
  }
  for (const auto& value : selected) {
    if (resolution.packages()[value.packageIndex].manifest().hasBuildScript()) {
      required[value.packageIndex] = 1;
    }
  }
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (size_t index = 0; index < resolution.packages().size(); ++index) {
      if (required[index] == 0 || processed[index] != 0) { continue; }
      processed[index] = 1;
      progressed = true;
      zc::Vector<size_t> providers;
      ZC_IF_SOME(issue, collectHostProviderPackages(resolution, ranges.asPtr(), index, providers)) {
        return issue;
      }
      for (const auto provider : providers) {
        if (resolution.packages()[provider].manifest().hasBuildScript()) { required[provider] = 1; }
      }
    }
  }

  zc::Vector<size_t> requiredPackages;
  zc::Vector<identity::PreparatoryBuildScriptKey> keys;
  for (size_t index = 0; index < resolution.packages().size(); ++index) {
    if (required[index] == 0) { continue; }
    const auto& record = resolution.packages()[index];
    auto packageManifest = record.manifest().package();
    auto buildScript = record.manifest().buildScript();
    if (packageManifest == zc::none || buildScript == zc::none) {
      return CrateGraphIssue::InvalidCrateIdentity;
    }
    zc::Vector<identity::PackageKey> directDependencies;
    const auto range = ranges[index];
    for (size_t edgeIndex = range.begin; edgeIndex < range.end; ++edgeIndex) {
      const auto& edge = resolution.edges()[edgeIndex];
      if (edge.domain() == identity::DependencyDomain::Build) {
        directDependencies.add(edge.provider().clone());
      }
    }
    canonicalSort(directDependencies);
    zc::Vector<identity::PackageKey> uniqueDirectDependencies;
    for (auto& dependency : directDependencies) {
      if (uniqueDirectDependencies.empty() ||
          !sameBytes(uniqueDirectDependencies.back().encode().asPtr(),
                     dependency.encode().asPtr())) {
        uniqueDirectDependencies.add(zc::mv(dependency));
      }
    }
    ZC_IF_SOME(manifest, packageManifest) {
      ZC_IF_SOME(contract, buildScript) {
        auto targetName = identity::TargetName::fromCanonical(contract.target().name());
        if (targetName == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
        ZC_IF_SOME(name, targetName) {
          auto key = identity::PreparatoryBuildScriptKey::from(
              record.key().clone(), zc::mv(name), request.hostTarget().semanticProjection().clone(),
              semanticOptions(request, manifest.editionYear()), zc::mv(uniqueDirectDependencies));
          if (key == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
          ZC_IF_SOME(value, key) { keys.add(zc::mv(value)); }
        }
      }
    }
    requiredPackages.add(index);
  }

  zc::Vector<package::BuildScriptPlanNode> nodes;
  for (size_t requiredIndex = 0; requiredIndex < requiredPackages.size(); ++requiredIndex) {
    const auto packageIndex = requiredPackages[requiredIndex];
    zc::Vector<size_t> providers;
    ZC_IF_SOME(issue,
               collectHostProviderPackages(resolution, ranges.asPtr(), packageIndex, providers)) {
      return issue;
    }
    zc::Vector<package::BuildScriptPlanNodeKey> predecessors;
    for (const auto provider : providers) {
      if (required[provider] == 0) { continue; }
      if (provider == packageIndex) { return CrateGraphIssue::DependencyCycle; }
      bool found = false;
      for (size_t candidate = 0; candidate < requiredPackages.size(); ++candidate) {
        if (requiredPackages[candidate] == provider) {
          predecessors.add(package::BuildScriptPlanNodeKey::from(keys[candidate].clone()));
          found = true;
          break;
        }
      }
      if (!found) { return CrateGraphIssue::InvalidCrateIdentity; }
    }
    auto contract = resolution.packages()[packageIndex].manifest().buildScript();
    if (contract == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
    ZC_IF_SOME(value, contract) {
      auto node = package::BuildScriptPlanNode::from(
          package::BuildScriptPlanNodeKey::from(keys[requiredIndex].clone()), value.clone(),
          zc::mv(predecessors));
      if (node == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
      ZC_IF_SOME(result, node) { nodes.add(zc::mv(result)); }
    }
  }
  auto plan = package::VerifiedBuildScriptPlan::from(zc::mv(nodes));
  if (plan == zc::none) { return CrateGraphIssue::DependencyCycle; }
  ZC_IF_SOME(value, plan) { return zc::mv(value); }
  return CrateGraphIssue::InvalidCrateIdentity;
}

PreparatoryCrateGraphBuildResult VerifiedPreparatoryCrateGraph::build(
    const package::VerifiedPackageCompilationRequest& request,
    const package::BuildScriptPlanNode& node, const package::ResolutionOutput& resolution,
    const package::VerifiedBuildScriptPlan& plan,
    zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults) {
  auto rootPackageIndex = findPackage(resolution, node.key().preparatory().package());
  if (rootPackageIndex == zc::none) { return CrateGraphIssue::RootOutsideResolution; }

  const auto ranges = packageEdgeRanges(resolution);
  zc::Vector<identity::PackageKey> directDependencies;
  ZC_IF_SOME(index, rootPackageIndex) {
    const auto& rootRecord = resolution.packages()[index];
    auto packageManifest = rootRecord.manifest().package();
    auto buildScript = rootRecord.manifest().buildScript();
    if (packageManifest == zc::none || buildScript == zc::none) {
      return CrateGraphIssue::InvalidCrateIdentity;
    }
    ZC_IF_SOME(contract, buildScript) {
      if (!sameBytes(contract.encode().asPtr(), node.contract().encode().asPtr())) {
        return CrateGraphIssue::InvalidCrateIdentity;
      }
    }
    for (size_t edgeIndex = ranges[index].begin; edgeIndex < ranges[index].end; ++edgeIndex) {
      const auto& edge = resolution.edges()[edgeIndex];
      if (edge.domain() == identity::DependencyDomain::Build) {
        directDependencies.add(edge.provider().clone());
      }
    }
    canonicalSort(directDependencies);
    zc::Vector<identity::PackageKey> uniqueDirectDependencies;
    for (auto& dependency : directDependencies) {
      if (uniqueDirectDependencies.empty() ||
          !sameBytes(uniqueDirectDependencies.back().encode().asPtr(),
                     dependency.encode().asPtr())) {
        uniqueDirectDependencies.add(zc::mv(dependency));
      }
    }
    ZC_IF_SOME(manifest, packageManifest) {
      auto targetName = identity::TargetName::fromCanonical(node.contract().target().name());
      if (targetName == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
      ZC_IF_SOME(name, targetName) {
        auto expected = identity::PreparatoryBuildScriptKey::from(
            rootRecord.key().clone(), name.clone(),
            request.hostTarget().semanticProjection().clone(),
            semanticOptions(request, manifest.editionYear()), zc::mv(uniqueDirectDependencies));
        if (expected == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
        ZC_IF_SOME(expectedKey, expected) {
          if (!sameBytes(expectedKey.encode().asPtr(), node.key().preparatory().encode().asPtr())) {
            return CrateGraphIssue::InvalidCrateIdentity;
          }
        }
        zc::Maybe<identity::BuildScriptProducerKey> noBuildOutput;
        auto compilation = identity::CompilationConfigKey::from(
            identity::CompilationDomain::Host, request.hostTarget().semanticProjection().clone(),
            semanticOptions(request, manifest.editionYear()), zc::mv(noBuildOutput));
        if (compilation == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
        ZC_IF_SOME(config, compilation) {
          auto root = identity::CrateKey::from(rootRecord.key().clone(),
                                               identity::CrateTargetKind::BuildScript, zc::mv(name),
                                               zc::mv(config));
          if (root == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
          ZC_IF_SOME(rootCrate, root) {
            zc::Vector<identity::CrateKey> crates;
            zc::Vector<size_t> cratePackages;
            zc::Vector<bool> includedLibraries;
            for (size_t packageIndex = 0; packageIndex < resolution.packages().size();
                 ++packageIndex) {
              includedLibraries.add(false);
            }
            crates.add(rootCrate.clone());
            cratePackages.add(index);
            zc::Vector<identity::CrateDependencyEdgeKey> edges;
            for (size_t cursor = 0; cursor < crates.size(); ++cursor) {
              const auto consumerPackage = cratePackages[cursor];
              const auto range = ranges[consumerPackage];
              for (size_t edgeIndex = range.begin; edgeIndex < range.end; ++edgeIndex) {
                const auto& packageEdge = resolution.edges()[edgeIndex];
                const auto requiredDomain = cursor == 0 ? identity::DependencyDomain::Build
                                                        : identity::DependencyDomain::Target;
                if (packageEdge.domain() != requiredDomain) { continue; }
                auto providerIndex = findPackage(resolution, packageEdge.provider());
                if (providerIndex == zc::none) { return CrateGraphIssue::MissingProviderLibrary; }
                ZC_IF_SOME(providerPackage, providerIndex) {
                  if (!includedLibraries[providerPackage]) {
                    auto provider = makeHostLibrary(request, resolution.packages()[providerPackage],
                                                    plan, completedResults);
                    if (provider.is<CrateGraphIssue>()) { return provider.get<CrateGraphIssue>(); }
                    crates.add(zc::mv(provider.get<identity::CrateKey>()));
                    cratePackages.add(providerPackage);
                    includedLibraries[providerPackage] = true;
                  }
                  size_t providerCrateIndex = 0;
                  bool foundProvider = false;
                  for (size_t candidate = 1; candidate < crates.size(); ++candidate) {
                    if (cratePackages[candidate] == providerPackage &&
                        crates[candidate].targetKind() == identity::CrateTargetKind::Library) {
                      providerCrateIndex = candidate;
                      foundProvider = true;
                      break;
                    }
                  }
                  if (!foundProvider) { return CrateGraphIssue::InvalidCrateIdentity; }
                  auto edge = identity::CrateDependencyEdgeKey::from(
                      packageEdge.clone(), crates[cursor].clone(),
                      crates[providerCrateIndex].clone());
                  if (edge == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
                  ZC_IF_SOME(value, edge) { edges.add(zc::mv(value)); }
                }
              }
            }

            canonicalSort(crates);
            canonicalSort(edges);
            if (hasDuplicate(crates)) { return CrateGraphIssue::DuplicateCrate; }
            if (hasDuplicate(edges)) { return CrateGraphIssue::DuplicateEdge; }
            if (hasCycle(crates.asPtr(), edges.asPtr())) {
              return CrateGraphIssue::DependencyCycle;
            }

            zc::Vector<identity::PackageKey> packages;
            for (const auto& crate : crates) { packages.add(crate.package().clone()); }
            canonicalSort(packages);
            zc::Vector<identity::PackageKey> uniquePackages;
            for (auto& package : packages) {
              if (uniquePackages.empty() ||
                  !sameBytes(uniquePackages.back().encode().asPtr(), package.encode().asPtr())) {
                uniquePackages.add(zc::mv(package));
              }
            }
            zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
            for (const auto& edge : edges) { packageEdges.add(edge.packageEdge().clone()); }
            canonicalSort(packageEdges);
            zc::Vector<identity::PackageDependencyEdgeKey> uniquePackageEdges;
            for (auto& edge : packageEdges) {
              if (uniquePackageEdges.empty() ||
                  !sameBytes(uniquePackageEdges.back().encode().asPtr(), edge.encode().asPtr())) {
                uniquePackageEdges.add(zc::mv(edge));
              }
            }
            zc::Vector<identity::SourceContentIdentity> sourceContents;
            zc::Vector<identity::ModuleKey> modules;
            auto fingerprint = identity::SemanticContextFingerprint::compute(
                uniquePackages.asPtr(), uniquePackageEdges.asPtr(), crates.asPtr(), edges.asPtr(),
                sourceContents.asPtr(), modules.asPtr());
            if (fingerprint == zc::none) { return CrateGraphIssue::InvalidCrateIdentity; }
            ZC_IF_SOME(value, fingerprint) {
              return VerifiedPreparatoryCrateGraph(zc::heap<Impl>(
                  zc::mv(rootCrate), zc::mv(uniquePackages), zc::mv(uniquePackageEdges),
                  zc::mv(crates), zc::mv(edges), zc::mv(value)));
            }
          }
        }
      }
    }
  }
  return CrateGraphIssue::InvalidCrateIdentity;
}

const identity::CrateKey& VerifiedPreparatoryCrateGraph::root() const noexcept {
  return impl->root;
}

zc::ArrayPtr<const identity::PackageKey> VerifiedPreparatoryCrateGraph::packages() const noexcept {
  return impl->packages;
}

zc::ArrayPtr<const identity::PackageDependencyEdgeKey> VerifiedPreparatoryCrateGraph::packageEdges()
    const noexcept {
  return impl->packageEdges;
}

zc::ArrayPtr<const identity::CrateKey> VerifiedPreparatoryCrateGraph::crates() const noexcept {
  return impl->crates;
}

zc::ArrayPtr<const identity::CrateDependencyEdgeKey> VerifiedPreparatoryCrateGraph::edges()
    const noexcept {
  return impl->edges;
}

const identity::SemanticContextFingerprint& VerifiedPreparatoryCrateGraph::fingerprint()
    const noexcept {
  return impl->fingerprint;
}

}  // namespace zomlang::compiler::driver
