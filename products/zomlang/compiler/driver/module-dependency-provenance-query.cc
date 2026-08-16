// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/module-dependency-provenance-query.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

constexpr zc::StringPtr kWitnessDomain = "zom.query.module-dependency-provenance-witness"_zc;
constexpr uint64_t kMaximumDependencySitesOrGraphEdges = 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameSource(const identity::SourceFileKey& left, const identity::SourceFileKey& right) {
  return left.sameAs(right);
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

identity::ModuleDependencyKind dependencyKind(DetachedModuleDependencySiteKind kind) {
  switch (kind) {
    case DetachedModuleDependencySiteKind::Import:
      return identity::ModuleDependencyKind::Import;
    case DetachedModuleDependencySiteKind::ForeignReexport:
      return identity::ModuleDependencyKind::ForeignReexport;
    case DetachedModuleDependencySiteKind::ModuleAlias:
      return identity::ModuleDependencyKind::ModuleAlias;
  }
  ZC_UNREACHABLE;
}

zc::Array<uint8_t> dependencySignature(identity::ModuleDependencyKind kind,
                                       zc::ArrayPtr<const identity::ModulePathSegment> path) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  encoder.encodeSequenceSize(path.size());
  for (const auto& segment : path) { segment.encode(encoder); }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> providerWitness(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const identity::Sha256Digest& sourceDigest, const DetachedModuleDependencySiteSet& sites,
    const ModuleDependencyRequestSetRecord& requests) {
  if (!sameModule(module, sites.module()) || !sameSource(source, sites.source()) ||
      sourceDigest != sites.sourceDigest()) {
    return zc::none;
  }
  const auto moduleBytes = module.encode();
  const auto sourceBytes = source.encode();
  const auto siteBytes = sites.encodeCanonical();
  const auto requestBytes = requests.encodeCanonical();
  identity::CanonicalEncoder frames;
  frames.encodeByteString(moduleBytes.asPtr());
  frames.encodeByteString(sourceBytes.asPtr());
  frames.encodeByteString(sourceDigest.bytes());
  frames.encodeByteString(siteBytes.asPtr());
  frames.encodeByteString(requestBytes.asPtr());
  const auto framedBytes = frames.finish();
  zc::Vector<uint8_t> witnessBytes(kWitnessDomain.size() + 1 + framedBytes.size());
  witnessBytes.addAll(kWitnessDomain.asBytes());
  witnessBytes.add(0x00);
  witnessBytes.addAll(framedBytes.asPtr());
  return identity::sha256(witnessBytes.asPtr());
}

zc::Maybe<identity::Sha256Digest> verifierWitness(
    const identity::ModuleKey& module, const identity::SourceFileKey& source,
    const identity::Sha256Digest& sourceDigest, const DetachedModuleDependencySiteSet& sites,
    const ModuleDependencyRequestSetRecord& requests) {
  if (sites.module().encode().asPtr() != module.encode().asPtr() ||
      sites.source().encode().asPtr() != source.encode().asPtr() ||
      sites.sourceDigest() != sourceDigest) {
    return zc::none;
  }
  zc::Vector<zc::Array<uint8_t>> fields;
  fields.add(module.encode());
  fields.add(source.encode());
  fields.add(zc::heapArray<uint8_t>(sourceDigest.bytes()));
  fields.add(sites.encodeCanonical());
  fields.add(requests.encodeCanonical());
  identity::CanonicalEncoder frames;
  for (const auto& field : fields) { frames.encodeByteString(field.asPtr()); }
  const auto payload = frames.finish();
  identity::Sha256Hasher hasher;
  if (!hasher.update(kWitnessDomain.asBytes())) { return zc::none; }
  const uint8_t separator = 0;
  if (!hasher.update(zc::arrayPtr(&separator, 1)) || !hasher.update(payload.asPtr())) {
    return zc::none;
  }
  return hasher.finish();
}

template <typename Context>
query::TypedQueryResult<binder::CanonicalParsedModule> loadParsedModule(
    Context& context, const identity::ModuleKey& key, const identity::SourceFileKey& source) {
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(source);
  if (sourceKey == zc::none) {
    return query::TypedQueryResult<binder::CanonicalParsedModule>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeRejected()) {
    return query::TypedQueryResult<binder::CanonicalParsedModule>::runtimeFailure(
        parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<parser::ParseSourceQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    return query::TypedQueryResult<binder::CanonicalParsedModule>::semanticFailure(
        Contract::encode(parsed.diagnostics()));
  }
  if (!parsed.isPublished()) {
    return query::TypedQueryResult<binder::CanonicalParsedModule>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
  if (canonical == zc::none || !sameSource(ZC_ASSERT_NONNULL(canonical).source(), source) ||
      !source.belongsTo(key.crate())) {
    return query::TypedQueryResult<binder::CanonicalParsedModule>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<binder::CanonicalParsedModule>::value(
      zc::mv(ZC_ASSERT_NONNULL(canonical)));
}

zc::Maybe<ModuleDependencyProvenanceMap> buildProviderCandidate(
    const identity::ModuleKey& module, const binder::CanonicalParsedModule& parsed,
    const DetachedModuleDependencySiteSet& sites,
    const ModuleDependencyRequestSetRecord& requests) {
  const auto& tree = parsed.tree();
  if (!ast::verifySchema(tree) || !tree.contains(tree.root()) ||
      tree.nodeCount() > kMaximumDependencySitesOrGraphEdges ||
      sites.sites().size() > kMaximumDependencySitesOrGraphEdges ||
      requests.requests().size() > kMaximumDependencySitesOrGraphEdges ||
      !sameModule(sites.module(), module) || !sameSource(sites.source(), parsed.source()) ||
      sites.sourceDigest() != parsed.contentDigest()) {
    return zc::none;
  }
  zc::Vector<ast::NodeId> nodes(tree.nodeCount());
  bool valid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (!valid || !tree.contains(node) || nodes.size() >= tree.nodeCount()) {
      valid = false;
      return;
    }
    nodes.add(node);
  });
  if (!valid || nodes.size() != tree.nodeCount()) { return zc::none; }

  zc::HashMap<zc::Array<uint8_t>, size_t> requestIndex;
  requestIndex.reserve(requests.requests().size());
  zc::Vector<zc::Vector<ModuleDependencyProvenanceSite>> groupedSites(requests.requests().size());
  for (size_t index = 0; index < requests.requests().size(); ++index) {
    zc::Vector<ModuleDependencyProvenanceSite> group;
    groupedSites.add(zc::mv(group));
    const auto& request = requests.requests()[index];
    if (!sameModule(request.requester(), module)) { return zc::none; }
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) { continue; }
    auto path = request.normalizedPath();
    if (path == zc::none) { return zc::none; }
    auto signature = dependencySignature(request.dependencyKind(), ZC_ASSERT_NONNULL(path));
    if (requestIndex.find(signature.asPtr()) != zc::none) { return zc::none; }
    requestIndex.insert(zc::mv(signature), index);
  }
  for (const auto& detached : sites.sites()) {
    auto signature =
        dependencySignature(dependencyKind(detached.kind()), detached.normalizedPath());
    auto index = requestIndex.find(signature.asPtr());
    if (index == zc::none || detached.schemaPreorderOrdinal() >= nodes.size()) { return zc::none; }
    const auto node = nodes[detached.schemaPreorderOrdinal()];
    auto span = parsed.spanFor(tree.node(node).range);
    if (span == zc::none || !ZC_ASSERT_NONNULL(span).belongsTo(parsed.source())) {
      return zc::none;
    }
    groupedSites[ZC_ASSERT_NONNULL(index)].add(ModuleDependencyProvenanceSite(
        detached.schemaPreorderOrdinal(), node, zc::mv(ZC_ASSERT_NONNULL(span))));
  }

  zc::Vector<ModuleDependencyProvenanceEntry> entries(requests.requests().size());
  for (size_t index = 0; index < requests.requests().size(); ++index) {
    const auto& request = requests.requests()[index];
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
      entries.add(ModuleDependencyProvenanceEntry(request.clone(),
                                                  ModuleDependencyProvenanceOrigin::prelude()));
      continue;
    }
    auto origin = ModuleDependencyProvenanceOrigin::source(zc::mv(groupedSites[index]));
    if (origin == zc::none) { return zc::none; }
    entries.add(
        ModuleDependencyProvenanceEntry(request.clone(), zc::mv(ZC_ASSERT_NONNULL(origin))));
  }
  auto witness = providerWitness(module, parsed.source(), parsed.contentDigest(), sites, requests);
  if (witness == zc::none) { return zc::none; }
  return ModuleDependencyProvenanceMap::from(module.clone(), parsed.source().clone(),
                                             parsed.contentDigest(), zc::mv(entries),
                                             ZC_ASSERT_NONNULL(witness));
}

zc::Maybe<ModuleDependencyProvenanceMap> buildVerifierCandidate(
    const identity::ModuleKey& module, const binder::CanonicalParsedModule& parsed,
    const DetachedModuleDependencySiteSet& sites,
    const ModuleDependencyRequestSetRecord& requests) {
  const auto& tree = parsed.tree();
  if (!tree.contains(tree.root()) || !ast::verifySchema(tree) ||
      sites.sites().size() > kMaximumDependencySitesOrGraphEdges ||
      requests.requests().size() > kMaximumDependencySitesOrGraphEdges ||
      sites.module().encode().asPtr() != module.encode().asPtr() ||
      sites.source().encode().asPtr() != parsed.source().encode().asPtr() ||
      sites.sourceDigest() != parsed.contentDigest()) {
    return zc::none;
  }

  zc::Vector<ast::NodeId> ordinalNodes(tree.nodeCount());
  uint64_t visited = 0;
  bool validTraversal = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (!validTraversal || !tree.contains(node) || visited >= tree.nodeCount()) {
      validTraversal = false;
      return;
    }
    ordinalNodes.add(node);
    ++visited;
  });
  if (!validTraversal || visited != tree.nodeCount() || ordinalNodes.size() != tree.nodeCount()) {
    return zc::none;
  }

  zc::HashMap<zc::Array<uint8_t>, size_t> requestPositions;
  requestPositions.reserve(requests.requests().size());
  zc::Vector<zc::Vector<ModuleDependencyProvenanceSite>> groupedSites(requests.requests().size());
  for (size_t position = 0; position < requests.requests().size(); ++position) {
    zc::Vector<ModuleDependencyProvenanceSite> group;
    groupedSites.add(zc::mv(group));
    const auto& request = requests.requests()[position];
    if (request.requester().encode().asPtr() != module.encode().asPtr()) { return zc::none; }
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) { continue; }
    auto path = request.normalizedPath();
    if (path == zc::none) { return zc::none; }
    auto signature = dependencySignature(request.dependencyKind(), ZC_ASSERT_NONNULL(path));
    if (requestPositions.find(signature.asPtr()) != zc::none) { return zc::none; }
    requestPositions.insert(zc::mv(signature), position);
  }
  for (const auto& detached : sites.sites()) {
    auto signature =
        dependencySignature(dependencyKind(detached.kind()), detached.normalizedPath());
    auto index = requestPositions.find(signature.asPtr());
    if (index == zc::none || detached.schemaPreorderOrdinal() >= ordinalNodes.size()) {
      return zc::none;
    }
    const auto node = ordinalNodes[detached.schemaPreorderOrdinal()];
    auto span = parsed.spanFor(tree.node(node).range);
    if (span == zc::none || !ZC_ASSERT_NONNULL(span).source().sameAs(parsed.source())) {
      return zc::none;
    }
    groupedSites[ZC_ASSERT_NONNULL(index)].add(ModuleDependencyProvenanceSite(
        detached.schemaPreorderOrdinal(), node, zc::mv(ZC_ASSERT_NONNULL(span))));
  }

  zc::Vector<ModuleDependencyProvenanceEntry> entries(requests.requests().size());
  for (size_t index = 0; index < requests.requests().size(); ++index) {
    const auto& request = requests.requests()[index];
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
      entries.add(ModuleDependencyProvenanceEntry(request.clone(),
                                                  ModuleDependencyProvenanceOrigin::prelude()));
      continue;
    }
    auto origin = ModuleDependencyProvenanceOrigin::source(zc::mv(groupedSites[index]));
    if (origin == zc::none) { return zc::none; }
    entries.add(
        ModuleDependencyProvenanceEntry(request.clone(), zc::mv(ZC_ASSERT_NONNULL(origin))));
  }
  auto witness = verifierWitness(module, parsed.source(), parsed.contentDigest(), sites, requests);
  if (witness == zc::none) { return zc::none; }
  return ModuleDependencyProvenanceMap::from(module.clone(), parsed.source().clone(),
                                             parsed.contentDigest(), zc::mv(entries),
                                             ZC_ASSERT_NONNULL(witness));
}

zc::Maybe<binder::BinderKeyFailure> missingSourceFailure(const identity::ModuleKey& module) {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  return binder::BinderKeyFailure::from(binder::BinderKeyFailureKind::MissingSelectedModuleSource,
                                        binder::BinderQueryOwner::module(module.clone()),
                                        zc::mv(noPath));
}

template <typename SourceDescriptor>
query::CapabilityProviderResult<ModuleDependencyProvenanceQuery> forwardSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<ModuleDependencyProvenanceQuery,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto decoded = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (decoded == zc::none) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::sourceRejected<
      diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(decoded)));
}

query::CapabilityRejectionCheck verifySourceRejection(
    query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context,
    const identity::ModuleKey& key, zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics) {
  auto selected = context.get<SelectedModuleSourceQuery>(key);
  if (selected.isRuntimeFailure() || selected.kind() != query::QueryValueKind::Value) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  auto parsed = context.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (!parsed.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
  auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
  auto expected = binder::encodeStableBindingDiagnosticFacts(parsed.diagnostics().values());
  return actual != zc::none && expected != zc::none &&
                 ZC_ASSERT_NONNULL(actual).asPtr() == ZC_ASSERT_NONNULL(expected).asPtr()
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

query::CapabilityRejectionCheck verifyKeyRejection(
    query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context,
    const identity::ModuleKey& key, const binder::BinderKeyFailure& failure) {
  auto selected = context.get<SelectedModuleSourceQuery>(key);
  if (selected.isRuntimeFailure() || selected.kind() != query::QueryValueKind::Absence) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto expected = missingSourceFailure(key);
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

}  // namespace

ModuleDependencyProvenanceSite::ModuleDependencyProvenanceSite(uint32_t schemaPreorderOrdinal,
                                                               ast::NodeId node,
                                                               identity::SourceSpan&& span) noexcept
    : schemaPreorderOrdinalValue(schemaPreorderOrdinal), nodeValue(node), spanValue(zc::mv(span)) {}

ModuleDependencyProvenanceSite ModuleDependencyProvenanceSite::clone() const {
  return ModuleDependencyProvenanceSite(schemaPreorderOrdinalValue, nodeValue, spanValue.clone());
}

uint32_t ModuleDependencyProvenanceSite::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}

ast::NodeId ModuleDependencyProvenanceSite::node() const noexcept { return nodeValue; }

const identity::SourceSpan& ModuleDependencyProvenanceSite::span() const noexcept {
  return spanValue;
}

ModuleDependencyProvenanceOrigin::ModuleDependencyProvenanceOrigin(
    ModuleDependencyProvenanceOriginKind kind,
    zc::Vector<ModuleDependencyProvenanceSite>&& sites) noexcept
    : kindValue(kind), siteValues(zc::mv(sites)) {}

zc::Maybe<ModuleDependencyProvenanceOrigin> ModuleDependencyProvenanceOrigin::source(
    zc::Vector<ModuleDependencyProvenanceSite>&& sites) {
  if (sites.size() == 0 || sites.size() > kMaximumDependencySitesOrGraphEdges) { return zc::none; }
  for (size_t index = 1; index < sites.size(); ++index) {
    if (sites[index - 1].schemaPreorderOrdinal() >= sites[index].schemaPreorderOrdinal()) {
      return zc::none;
    }
  }
  return ModuleDependencyProvenanceOrigin(ModuleDependencyProvenanceOriginKind::Source,
                                          zc::mv(sites));
}

ModuleDependencyProvenanceOrigin ModuleDependencyProvenanceOrigin::prelude() {
  zc::Vector<ModuleDependencyProvenanceSite> sites;
  return ModuleDependencyProvenanceOrigin(ModuleDependencyProvenanceOriginKind::Prelude,
                                          zc::mv(sites));
}

ModuleDependencyProvenanceOrigin ModuleDependencyProvenanceOrigin::clone() const {
  zc::Vector<ModuleDependencyProvenanceSite> sites(siteValues.size());
  for (const auto& site : siteValues) { sites.add(site.clone()); }
  return ModuleDependencyProvenanceOrigin(kindValue, zc::mv(sites));
}

ModuleDependencyProvenanceOriginKind ModuleDependencyProvenanceOrigin::kind() const noexcept {
  return kindValue;
}

zc::ArrayPtr<const ModuleDependencyProvenanceSite> ModuleDependencyProvenanceOrigin::sites()
    const noexcept {
  return siteValues.asPtr();
}

ModuleDependencyProvenanceEntry::ModuleDependencyProvenanceEntry(
    identity::ModuleResolutionKey&& request, ModuleDependencyProvenanceOrigin&& origin) noexcept
    : requestValue(zc::mv(request)), originValue(zc::mv(origin)) {}

ModuleDependencyProvenanceEntry ModuleDependencyProvenanceEntry::clone() const {
  return ModuleDependencyProvenanceEntry(requestValue.clone(), originValue.clone());
}

const identity::ModuleResolutionKey& ModuleDependencyProvenanceEntry::request() const noexcept {
  return requestValue;
}

const ModuleDependencyProvenanceOrigin& ModuleDependencyProvenanceEntry::origin() const noexcept {
  return originValue;
}

ModuleDependencyProvenanceMap::ModuleDependencyProvenanceMap(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    const identity::Sha256Digest& sourceDigest,
    zc::Vector<ModuleDependencyProvenanceEntry>&& entries,
    const identity::Sha256Digest& stableWitness) noexcept
    : moduleValue(zc::mv(module)),
      sourceValue(zc::mv(source)),
      sourceDigestValue(sourceDigest),
      entryValues(zc::mv(entries)),
      stableWitnessValue(stableWitness) {}

zc::Maybe<ModuleDependencyProvenanceMap> ModuleDependencyProvenanceMap::from(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    const identity::Sha256Digest& sourceDigest,
    zc::Vector<ModuleDependencyProvenanceEntry>&& entries,
    const identity::Sha256Digest& stableWitness) {
  if (!source.belongsTo(module.crate()) || entries.size() > kMaximumDependencySitesOrGraphEdges) {
    return zc::none;
  }
  size_t sourceSiteCount = 0;
  size_t preludeCount = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (!sameModule(entry.request().requester(), module) ||
        (index != 0 && compareBytes(entries[index - 1].request().encode().asPtr(),
                                    entry.request().encode().asPtr()) >= 0)) {
      return zc::none;
    }
    if (entry.request().dependencyKind() == identity::ModuleDependencyKind::Prelude) {
      if (entry.origin().kind() != ModuleDependencyProvenanceOriginKind::Prelude ||
          entry.origin().sites().size() != 0 || ++preludeCount > 1) {
        return zc::none;
      }
      continue;
    }
    if (entry.origin().kind() != ModuleDependencyProvenanceOriginKind::Source ||
        entry.origin().sites().size() == 0) {
      return zc::none;
    }
    for (const auto& site : entry.origin().sites()) {
      if (!site.node() || !site.span().belongsTo(source) ||
          site.span().byteStart() > site.span().byteEnd() ||
          ++sourceSiteCount > kMaximumDependencySitesOrGraphEdges) {
        return zc::none;
      }
    }
  }
  return ModuleDependencyProvenanceMap(zc::mv(module), zc::mv(source), sourceDigest,
                                       zc::mv(entries), stableWitness);
}

ModuleDependencyProvenanceMap ModuleDependencyProvenanceMap::clone() const {
  zc::Vector<ModuleDependencyProvenanceEntry> entries(entryValues.size());
  for (const auto& entry : entryValues) { entries.add(entry.clone()); }
  return ModuleDependencyProvenanceMap(moduleValue.clone(), sourceValue.clone(), sourceDigestValue,
                                       zc::mv(entries), stableWitnessValue);
}

const identity::ModuleKey& ModuleDependencyProvenanceMap::module() const noexcept {
  return moduleValue;
}

const identity::SourceFileKey& ModuleDependencyProvenanceMap::source() const noexcept {
  return sourceValue;
}

const identity::Sha256Digest& ModuleDependencyProvenanceMap::sourceDigest() const noexcept {
  return sourceDigestValue;
}

zc::ArrayPtr<const ModuleDependencyProvenanceEntry> ModuleDependencyProvenanceMap::entries()
    const noexcept {
  return entryValues.asPtr();
}

const identity::Sha256Digest& ModuleDependencyProvenanceMap::stableWitness() const noexcept {
  return stableWitnessValue;
}

bool ModuleDependencyProvenanceMap::sameAs(const ModuleDependencyProvenanceMap& other) const {
  if (!sameModule(moduleValue, other.moduleValue) || !sameSource(sourceValue, other.sourceValue) ||
      sourceDigestValue != other.sourceDigestValue ||
      stableWitnessValue != other.stableWitnessValue ||
      entryValues.size() != other.entryValues.size()) {
    return false;
  }
  for (size_t index = 0; index < entryValues.size(); ++index) {
    const auto& left = entryValues[index];
    const auto& right = other.entryValues[index];
    if (left.request().encode().asPtr() != right.request().encode().asPtr() ||
        left.origin().kind() != right.origin().kind() ||
        left.origin().sites().size() != right.origin().sites().size()) {
      return false;
    }
    for (size_t siteIndex = 0; siteIndex < left.origin().sites().size(); ++siteIndex) {
      const auto& leftSite = left.origin().sites()[siteIndex];
      const auto& rightSite = right.origin().sites()[siteIndex];
      if (leftSite.schemaPreorderOrdinal() != rightSite.schemaPreorderOrdinal() ||
          leftSite.node() != rightSite.node() || !sameSpan(leftSite.span(), rightSite.span())) {
        return false;
      }
    }
  }
  return true;
}

zc::Array<uint8_t> ModuleDependencyProvenanceQuery::encodeKey(const Key& key) {
  return key.encode();
}

zc::Maybe<ModuleDependencyProvenanceQuery::Key> ModuleDependencyProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto key = identity::ModuleKey::decodeCanonical(decoder);
  if (key == zc::none || !decoder.finished() || ZC_ASSERT_NONNULL(key).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(key));
}

query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>
ModuleDependencyProvenanceQuery::provide(
    query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context, const Key& key) {
  auto selected = context.get<SelectedModuleSourceQuery>(key);
  if (selected.isRuntimeFailure()) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        selected.runtimeFailure());
  }
  if (selected.kind() == query::QueryValueKind::Absence) {
    auto failure = missingSourceFailure(key);
    if (failure == zc::none) {
      return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::keyRejected<
        binder::BinderKeyFailure>(zc::mv(ZC_ASSERT_NONNULL(failure)));
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sites = context.get<ModuleDependencySitesQuery>(key);
  auto requests = context.get<ModuleDependencyRequestsQuery>(key);
  if (sites.isRuntimeFailure() || requests.isRuntimeFailure()) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        sites.isRuntimeFailure() ? sites.runtimeFailure() : requests.runtimeFailure());
  }
  if (sites.kind() != query::QueryValueKind::Value ||
      requests.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parse = context.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parse.isRuntimeRejected()) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        parse.runtimeFailure());
  }
  if (parse.isSourceRejected()) { return forwardSourceRejection(parse); }
  if (!parse.isPublished()) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed = binder::CanonicalParsedModule::fromQueryResult(parse.lease().capability().clone());
  if (parsed == zc::none) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate =
      buildProviderCandidate(key, ZC_ASSERT_NONNULL(parsed), sites.value(), requests.value());
  if (candidate == zc::none) {
    return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto witness =
      query::CapabilityCandidateContract<ModuleDependencyProvenanceQuery>::encode(*owned);
  return query::CapabilityProviderResult<ModuleDependencyProvenanceQuery>::candidate(
      zc::mv(owned), zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> ModuleDependencyProvenanceQuery::verify(
    query::CapabilityQueryContext<ModuleDependencyProvenanceQuery>& context, const Key& key,
    const Capability& candidate) {
  auto selected = context.get<SelectedModuleSourceQuery>(key);
  auto sites = context.get<ModuleDependencySitesQuery>(key);
  auto requests = context.get<ModuleDependencyRequestsQuery>(key);
  if (selected.isRuntimeFailure() || sites.isRuntimeFailure() || requests.isRuntimeFailure() ||
      selected.kind() != query::QueryValueKind::Value ||
      sites.kind() != query::QueryValueKind::Value ||
      requests.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto parsed = loadParsedModule(context, key, selected.value());
  if (parsed.isRuntimeFailure() || parsed.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto expected = buildVerifierCandidate(key, parsed.value(), sites.value(), requests.value());
  if (expected == zc::none || !ZC_ASSERT_NONNULL(expected).sameAs(candidate)) { return zc::none; }
  return zc::heapArray<uint8_t>(ZC_ASSERT_NONNULL(expected).stableWitness().bytes());
}

}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::query {

using ModuleDependencyProvenanceDescriptor =
    driver::module_graph_query::ModuleDependencyProvenanceQuery;

StableWitnessBytes CapabilityCandidateContract<ModuleDependencyProvenanceDescriptor>::encode(
    const ModuleDependencyProvenanceDescriptor::Capability& candidate) {
  return StableWitnessBytes(zc::heapArray<uint8_t>(candidate.stableWitness().bytes()));
}

zc::Maybe<zc::Own<ModuleDependencyProvenanceDescriptor::Capability>> CapabilityCandidateContract<
    ModuleDependencyProvenanceDescriptor>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

zc::Array<uint8_t> CapabilityFailureContract<
    ModuleDependencyProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<ModuleDependencyProvenanceDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    ModuleDependencyProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<ModuleDependencyProvenanceDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<ModuleDependencyProvenanceDescriptor>& context,
           const ModuleDependencyProvenanceDescriptor::Key& key, const Sequence& diagnostics) {
  return driver::module_graph_query::verifySourceRejection(context, key, diagnostics.values());
}

zc::Array<uint8_t> CapabilityFailureContract<
    ModuleDependencyProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    ModuleDependencyProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck CapabilityFailureContract<ModuleDependencyProvenanceDescriptor,
                                                   KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<ModuleDependencyProvenanceDescriptor>& context,
           const ModuleDependencyProvenanceDescriptor::Key& key,
           const binder::BinderKeyFailure& failure) {
  return driver::module_graph_query::verifyKeyRejection(context, key, failure);
}

}  // namespace zomlang::compiler::query

namespace {

#define ZOM_R28_16A_SELECT_R28_16A(name, capabilityType)                                         \
  static_assert(                                                                                 \
      zc::isSameType<zomlang::compiler::driver::module_graph_query::name##Query::Capability,     \
                     zomlang::compiler::driver::module_graph_query::capabilityType>());          \
  static_assert(zc::isSameType<                                                                  \
                zomlang::compiler::driver::module_graph_query::name##Query::FailureAlternatives, \
                zomlang::compiler::query::CapabilityFailureList<                                 \
                    zomlang::compiler::query::SourceRejection<                                   \
                        zomlang::compiler::diagnostics::DiagnosticFact>,                         \
                    zomlang::compiler::query::KeyRejection<                                      \
                        zomlang::compiler::binder::BinderKeyFailure>>>())
#define ZOM_R28_16A_SELECT_M1(name, capabilityType)
#define ZOM_R28_16A_SELECT_M2(name, capabilityType)
#define ZOM_R28_16A_SELECT_M3(name, capabilityType)
#define ZOM_R28_16A_SELECT_M5(name, capabilityType)
#define ZOM_R28_16A_SELECT(task, name, capabilityType) \
  ZOM_R28_16A_SELECT_EXPAND(task, name, capabilityType)
#define ZOM_R28_16A_SELECT_EXPAND(task, name, capabilityType) \
  ZOM_R28_16A_SELECT_##task(name, capabilityType)
#define ZOM_STABLE_BINDING_CAPABILITY_QUERY(                                                    \
    name, domain, keyType, resultType, capabilityType, producer, verifier, failureAlternatives, \
    descriptorTask, providerTask, verifierTask, testTask, mutations, test)                      \
  ZOM_R28_16A_SELECT(descriptorTask, name, capabilityType);
#include "zomlang/compiler/binder/stable-binding-schema.def"
#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY
#undef ZOM_R28_16A_SELECT_EXPAND
#undef ZOM_R28_16A_SELECT
#undef ZOM_R28_16A_SELECT_M5
#undef ZOM_R28_16A_SELECT_M3
#undef ZOM_R28_16A_SELECT_M2
#undef ZOM_R28_16A_SELECT_M1
#undef ZOM_R28_16A_SELECT_R28_16A

}  // namespace
