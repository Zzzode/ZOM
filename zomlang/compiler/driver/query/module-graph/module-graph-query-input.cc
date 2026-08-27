// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/binder/graph/module-resolution.h"
#include "zomlang/compiler/driver/query/binding/active-definition-authority-query.h"
#include "zomlang/compiler/driver/core/query.h"
#include "zomlang/compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/query/module-graph/module-dependency-provenance-query.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

constexpr zc::StringPtr kSelectedCatalogValueDomain = "zom.query.selected-module-catalog-value"_zc;
constexpr zc::StringPtr kDependencySiteValueDomain =
    "zom.query.module-dependency-site-input-value"_zc;
constexpr zc::StringPtr kActiveModulesValueDomain = "zom.query.active-modules-value"_zc;
constexpr zc::StringPtr kDependencyRequestsValueDomain =
    "zom.query.module-dependency-requests-value"_zc;
constexpr zc::StringPtr kDependenciesValueDomain = "zom.query.module-dependencies-value"_zc;
constexpr zc::StringPtr kDependencyFailureDomain = "zom.query.module-dependency-failure"_zc;
constexpr zc::StringPtr kCompleteContextAuthorityDomain =
    "zom.input.complete-compilation-context-authority"_zc;
constexpr zc::StringPtr kCompleteContextWitnessDomain =
    "zom.input.complete-compilation-context-witness"_zc;
constexpr zc::StringPtr kInputTransactionDigestDomain = "zom.query.input-transaction-digest"_zc;
constexpr zc::StringPtr kModuleStructureTransactionDomain =
    "zom.query.input-transaction.module-structure"_zc;
constexpr zc::StringPtr kFinalSnapshotWitnessDomain = "zom.query.final-snapshot-witness"_zc;
constexpr zc::StringPtr kFinalFailureGraphWitnessDomain =
    "zom.query.final-failure-closure.graph"_zc;
constexpr zc::StringPtr kFinalFailureSccWitnessDomain = "zom.query.final-failure-closure.scc"_zc;
constexpr zc::StringPtr kFinalFailureAuthorityWitnessDomain =
    "zom.query.final-failure-closure.authority"_zc;
constexpr zc::StringPtr kFinalFailureReadinessWitnessDomain =
    "zom.query.final-failure-closure.readiness"_zc;
constexpr zc::StringPtr kFinalFailureGraphAbsenceWitnessDomain =
    "zom.query.final-failure-closure.graph-absence"_zc;
constexpr zc::StringPtr kFinalFailureSccAbsenceWitnessDomain =
    "zom.query.final-failure-closure.scc-absence"_zc;
constexpr zc::StringPtr kFinalFailureAuthorityAbsenceWitnessDomain =
    "zom.query.final-failure-closure.authority-absence"_zc;
constexpr zc::StringPtr kFinalFailureReadinessAbsenceWitnessDomain =
    "zom.query.final-failure-closure.readiness-absence"_zc;
constexpr zc::StringPtr kFinalFailureGraphRuntimeWitnessDomain =
    "zom.query.final-failure-closure.graph-runtime"_zc;
constexpr zc::StringPtr kFinalFailureSccRuntimeWitnessDomain =
    "zom.query.final-failure-closure.scc-runtime"_zc;
constexpr zc::StringPtr kFinalFailureAuthorityRuntimeWitnessDomain =
    "zom.query.final-failure-closure.authority-runtime"_zc;
constexpr zc::StringPtr kFinalFailureReadinessRuntimeWitnessDomain =
    "zom.query.final-failure-closure.readiness-runtime"_zc;
constexpr uint64_t kMaximumCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumPathSegmentBytes = 4096;
constexpr uint64_t kMaximumModules = 4096;
constexpr uint64_t kMaximumDependencySites = 1024 * 1024;
constexpr uint64_t kMaximumValueBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumLedgerEntries = 8 * 1024 * 1024;
constexpr uint64_t kMaximumCanonicalInputSequenceRecords = UINT32_MAX;
constexpr uint64_t kMaximumCanonicalInputValueBytes = SIZE_MAX;
constexpr uint64_t kCanonicalByteStringPrefixBytes = sizeof(uint64_t);

zc::Maybe<zc::Vector<uint32_t>> verifierSchemaOrdinals(const ast::Tree& tree) {
  if (!ast::verifySchema(tree) || !tree.contains(tree.root()) || tree.nodeCount() > UINT32_MAX) {
    return zc::none;
  }
  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& ordinal : ordinals) { ordinal = UINT32_MAX; }
  size_t next = 0;
  bool valid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (!tree.contains(node) || node.value >= ordinals.size() ||
        ordinals[node.value] != UINT32_MAX || next >= tree.nodeCount() || next > UINT32_MAX) {
      valid = false;
      return;
    }
    ordinals[node.value] = static_cast<uint32_t>(next++);
  });
  if (!valid || next != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> verifierModulePath(const ast::Tree& tree,
                                                                      ast::NodeId path) {
  if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
    return zc::none;
  }
  const auto& syntax = tree.node(path);
  const ast::IdentList segments{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (segments.empty() || !tree.contains(segments)) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> result(segments.size);
  for (const auto segment : tree.identList(segments)) {
    auto canonical = identity::ModulePathSegment::fromSource(tree.ident(segment));
    if (canonical == zc::none) { return zc::none; }
    result.add(zc::mv(ZC_ASSERT_NONNULL(canonical)));
  }
  return zc::mv(result);
}

zc::Vector<identity::ModulePathSegment> cloneVerifierPath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

bool verifierPathEquals(zc::ArrayPtr<const identity::ModulePathSegment> left,
                        zc::ArrayPtr<const identity::ModulePathSegment> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].text() != right[index].text()) { return false; }
  }
  return true;
}

zc::Maybe<DetachedModuleDependencySiteSet> reconstructVerifierSites(
    const identity::ModuleKey& module, const binder::VerifiedParsedModule& parsed) {
  const auto& tree = parsed.tree();
  auto ordinals = verifierSchemaOrdinals(tree);
  if (ordinals == zc::none) { return zc::none; }
  bool valid = true;
  zc::Vector<DetachedModuleDependencySite> sites;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (!valid) { return; }
    DetachedModuleDependencySiteKind kind = DetachedModuleDependencySiteKind::Import;
    ast::NodeId path;
    if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
      path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
    } else if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
      path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
      if (!tree.contains(path)) { return; }
      kind = DetachedModuleDependencySiteKind::ForeignReexport;
    } else if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
               static_cast<ast::ModuleDeclarationForm>(
                   syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
                   ast::ModuleDeclarationForm::Alias) {
      path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
      kind = DetachedModuleDependencySiteKind::ModuleAlias;
    } else {
      return;
    }
    if (!tree.contains(node) || node.value >= ZC_ASSERT_NONNULL(ordinals).size() ||
        ZC_ASSERT_NONNULL(ordinals)[node.value] == UINT32_MAX) {
      valid = false;
      return;
    }
    auto normalized = verifierModulePath(tree, path);
    if (normalized == zc::none) {
      valid = false;
      return;
    }
    auto site = DetachedModuleDependencySite::from(kind, zc::mv(ZC_ASSERT_NONNULL(normalized)),
                                                   ZC_ASSERT_NONNULL(ordinals)[node.value]);
    if (site == zc::none) {
      valid = false;
      return;
    }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  });
  if (!valid) { return zc::none; }
  return DetachedModuleDependencySiteSet::from(module.clone(), parsed.source().clone(),
                                               parsed.contentDigest(), zc::mv(sites));
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

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameSource(const identity::SourceFileKey& left, const identity::SourceFileKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframe(zc::StringPtr domain,
                                               zc::ArrayPtr<const uint8_t> bytes) {
  const size_t prefixSize = domain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.size() > kMaximumValueBytes ||
      bytes.slice(0, domain.size()) != domain.asBytes() || bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframeCanonicalInput(zc::StringPtr domain,
                                                             zc::ArrayPtr<const uint8_t> bytes) {
  const size_t prefixSize = domain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.slice(0, domain.size()) != domain.asBytes() ||
      bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
}

zc::Maybe<identity::CrateKey> decodeCrate(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::CrateKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(crate, value) {
    if (crate.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(crate);
  }
  return zc::none;
}

zc::Maybe<identity::ModuleKey> decodeModule(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::ModuleKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(module, value) {
    if (module.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(module);
  }
  return zc::none;
}

zc::Maybe<identity::SourceFileKey> decodeSource(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumSourceKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::SourceFileKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(source, value) {
    if (source.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(source);
  }
  return zc::none;
}

zc::Array<uint8_t> encodePath(zc::ArrayPtr<const identity::ModulePathSegment> path) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(path.size());
  for (const auto& segment : path) {
    identity::CanonicalEncoder element;
    segment.encode(element);
    const auto bytes = element.finish();
    encoder.encodeByteString(bytes.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> decodePath(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(256);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> result(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPathSegmentBytes);
    if (encoded == zc::none) { return zc::none; }
    identity::CanonicalDecoder elementDecoder(ZC_ASSERT_NONNULL(encoded).asPtr());
    auto segment = identity::ModulePathSegment::decodeCanonical(elementDecoder);
    if (segment == zc::none || !elementDecoder.finished()) { return zc::none; }
    ZC_IF_SOME(value, segment) {
      identity::CanonicalEncoder elementEncoder;
      value.encode(elementEncoder);
      if (elementEncoder.finish().asPtr() != ZC_ASSERT_NONNULL(encoded).asPtr()) {
        return zc::none;
      }
      result.add(zc::mv(value));
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<DetachedModuleDependencySite> decodeSite(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto kindValue = decoder.decodeUint8();
  auto pathBytes = decoder.decodeByteString(256 * (8 + kMaximumPathSegmentBytes));
  auto ordinal = decoder.decodeUint32();
  if (kindValue == zc::none || pathBytes == zc::none || ordinal == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  const auto rawKind = ZC_ASSERT_NONNULL(kindValue);
  if (rawKind < static_cast<uint8_t>(DetachedModuleDependencySiteKind::Import) ||
      rawKind > static_cast<uint8_t>(DetachedModuleDependencySiteKind::ModuleAlias)) {
    return zc::none;
  }
  auto path = decodePath(ZC_ASSERT_NONNULL(pathBytes).asPtr());
  if (path == zc::none) { return zc::none; }
  auto result = DetachedModuleDependencySite::from(
      static_cast<DetachedModuleDependencySiteKind>(rawKind), zc::mv(ZC_ASSERT_NONNULL(path)),
      ZC_ASSERT_NONNULL(ordinal));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

template <typename T, typename Bytes>
void sortByBytes(zc::Vector<T>& values, Bytes bytes) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && compareBytes(bytes(current), bytes(values[insertion - 1])) < 0) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename T, typename Decode>
zc::Maybe<zc::Vector<T>> decodeCanonicalSequence(identity::CanonicalDecoder& decoder,
                                                 Decode decodeValue) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (count == zc::none) { return zc::none; }
  zc::Vector<T> values;
  values.reserve(ZC_ASSERT_NONNULL(count));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto valueBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (valueBytes == zc::none) { return zc::none; }
    auto value = decodeValue(ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  return zc::mv(values);
}

bool isSelected(zc::ArrayPtr<const SelectedModuleCatalog> catalogs,
                const identity::ModuleKey& module) {
  const auto moduleBytes = module.encode();
  for (const auto& catalog : catalogs) {
    for (const auto& entry : catalog.entries()) {
      if (entry.module().encode().asPtr() == moduleBytes.asPtr()) { return true; }
    }
  }
  return false;
}

bool isSelected(zc::ArrayPtr<const SelectedModuleCatalog> catalogs,
                const identity::ModuleKey& module, const identity::SourceFileKey& source) {
  const auto moduleBytes = module.encode();
  for (const auto& catalog : catalogs) {
    for (const auto& entry : catalog.entries()) {
      if (entry.module().encode().asPtr() == moduleBytes.asPtr() &&
          sameSource(entry.source(), source)) {
        return true;
      }
    }
  }
  return false;
}

bool isCoreCrate(const identity::CrateKey& crate) {
  return crate.unit().kind() == identity::CompilationUnitKind::Toolchain &&
         crate.unit().toolchain().component() == identity::ToolchainComponent::Core;
}

identity::ModuleResolutionPolicyKey moduleResolutionPolicy() {
  auto result = identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
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

zc::Vector<identity::ModulePathSegment> clonePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Maybe<identity::ModuleResolutionKey> sourceRequest(query::QueryContext& context,
                                                       const identity::ModuleKey& requester,
                                                       const DetachedModuleDependencySite& site) {
  zc::Maybe<identity::DependencyAlias> dependencyAlias;
  auto alias = identity::DependencyAlias::fromCanonical(site.normalizedPath().front().text());
  ZC_IF_SOME(aliasValue, alias) {
    auto inputKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
        requester.crate().clone(), aliasValue.clone());
    if (inputKey == zc::none) { return zc::none; }
    auto configured = context.get<incremental_module_resolution_query::DependencyAliasRootInput>(
        ZC_ASSERT_NONNULL(inputKey));
    if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    if (configured.value().target() != zc::none) { dependencyAlias = zc::mv(aliasValue); }
  }
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> path(clonePath(site.normalizedPath()));
  return identity::ModuleResolutionKey::from(requester.clone(), dependencyKind(site.kind()),
                                             zc::mv(path), zc::mv(dependencyAlias),
                                             moduleResolutionPolicy());
}

zc::Maybe<ModuleDependencyRequestSetRecord> buildRequests(
    query::QueryContext& context, const identity::ModuleKey& requester,
    const DetachedModuleDependencySiteSet& sites) {
  zc::Vector<identity::ModuleResolutionKey> requests(sites.sites().size() + 1);
  for (const auto& site : sites.sites()) {
    auto request = sourceRequest(context, requester, site);
    if (request == zc::none) { return zc::none; }
    requests.add(zc::mv(ZC_ASSERT_NONNULL(request)));
  }
  auto configured =
      context.get<incremental_module_resolution_query::ConfiguredPreludeInput>(requester.crate());
  if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  ZC_IF_SOME(target, configured.value().target()) {
    zc::Maybe<zc::Vector<identity::ModulePathSegment>> noPath;
    zc::Maybe<identity::DependencyAlias> noAlias;
    auto request = identity::ModuleResolutionKey::from(
        requester.clone(), identity::ModuleDependencyKind::Prelude, zc::mv(noPath), zc::mv(noAlias),
        moduleResolutionPolicy());
    if (request == zc::none || !isCoreCrate(target.crate())) { return zc::none; }
    requests.add(zc::mv(ZC_ASSERT_NONNULL(request)));
  }
  return ModuleDependencyRequestSetRecord::from(zc::mv(requests));
}

zc::Maybe<ModuleDependencyRequestSetRecord> rebuildVerifierRequests(
    query::QueryContext& context, const identity::ModuleKey& requester,
    const DetachedModuleDependencySiteSet& sites) {
  zc::Vector<identity::ModuleResolutionKey> requests(sites.sites().size() + 1);
  for (size_t index = sites.sites().size(); index != 0; --index) {
    const auto& site = sites.sites()[index - 1];
    identity::ModuleDependencyKind kind;
    switch (site.kind()) {
      case DetachedModuleDependencySiteKind::Import:
        kind = identity::ModuleDependencyKind::Import;
        break;
      case DetachedModuleDependencySiteKind::ForeignReexport:
        kind = identity::ModuleDependencyKind::ForeignReexport;
        break;
      case DetachedModuleDependencySiteKind::ModuleAlias:
        kind = identity::ModuleDependencyKind::ModuleAlias;
        break;
    }
    zc::Maybe<identity::DependencyAlias> dependencyAlias;
    auto alias = identity::DependencyAlias::fromCanonical(site.normalizedPath().front().text());
    ZC_IF_SOME(aliasValue, alias) {
      auto aliasKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          requester.crate().clone(), aliasValue.clone());
      if (aliasKey == zc::none) { return zc::none; }
      auto configured = context.get<incremental_module_resolution_query::DependencyAliasRootInput>(
          ZC_ASSERT_NONNULL(aliasKey));
      if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value) {
        return zc::none;
      }
      if (configured.value().target() != zc::none) { dependencyAlias = zc::mv(aliasValue); }
    }
    zc::Vector<identity::ModulePathSegment> path(site.normalizedPath().size());
    for (const auto& segment : site.normalizedPath()) { path.add(segment.clone()); }
    zc::Maybe<zc::Vector<identity::ModulePathSegment>> normalizedPath(zc::mv(path));
    auto policy = identity::ModuleResolutionPolicyKey::from(
        identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
        identity::SymlinkHandlingPolicy::ResolveThenConfine,
        identity::ModuleContainmentPolicy::DeclaredRootsOnly,
        identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
        identity::DependencyAliasLookupPolicy::ExactFirstSegment,
        identity::PreludeLookupPolicy::ConfiguredCratePrelude,
        identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
    if (policy == zc::none) { return zc::none; }
    auto request = identity::ModuleResolutionKey::from(
        requester.clone(), kind, zc::mv(normalizedPath), zc::mv(dependencyAlias),
        zc::mv(ZC_ASSERT_NONNULL(policy)));
    if (request == zc::none) { return zc::none; }
    requests.add(zc::mv(ZC_ASSERT_NONNULL(request)));
  }
  auto configured =
      context.get<incremental_module_resolution_query::ConfiguredPreludeInput>(requester.crate());
  if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  ZC_IF_SOME(target, configured.value().target()) {
    if (target.crate().unit().kind() != identity::CompilationUnitKind::Toolchain ||
        target.crate().unit().toolchain().component() != identity::ToolchainComponent::Core) {
      return zc::none;
    }
    zc::Maybe<zc::Vector<identity::ModulePathSegment>> noPath;
    zc::Maybe<identity::DependencyAlias> noAlias;
    auto policy = identity::ModuleResolutionPolicyKey::from(
        identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
        identity::SymlinkHandlingPolicy::ResolveThenConfine,
        identity::ModuleContainmentPolicy::DeclaredRootsOnly,
        identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
        identity::DependencyAliasLookupPolicy::ExactFirstSegment,
        identity::PreludeLookupPolicy::ConfiguredCratePrelude,
        identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
    if (policy == zc::none) { return zc::none; }
    auto request = identity::ModuleResolutionKey::from(
        requester.clone(), identity::ModuleDependencyKind::Prelude, zc::mv(noPath), zc::mv(noAlias),
        zc::mv(ZC_ASSERT_NONNULL(policy)));
    if (request == zc::none) { return zc::none; }
    requests.add(zc::mv(ZC_ASSERT_NONNULL(request)));
  }
  return ModuleDependencyRequestSetRecord::from(zc::mv(requests));
}

query::TypedQueryResult<ModuleDependencySetRecord> resolveDependencies(
    query::QueryContext& context, const identity::ModuleKey& requester,
    const ModuleDependencyRequestSetRecord& requests) {
  zc::Vector<identity::ModuleKey> dependencies(requests.requests().size());
  for (const auto& request : requests.requests()) {
    auto candidates =
        context.get<incremental_module_resolution_query::ResolveModuleRequest>(request);
    if (candidates.isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
          candidates.runtimeFailure());
    }
    if (candidates.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
      auto configured = context.get<incremental_module_resolution_query::ConfiguredPreludeInput>(
          requester.crate());
      if (configured.isRuntimeFailure()) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            configured.runtimeFailure());
      }
      auto target = configured.kind() == query::QueryValueKind::Value
                        ? configured.value().target()
                        : zc::Maybe<const identity::ModuleKey&>();
      if (target == zc::none || candidates.value().candidates().size() != 1) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      ZC_IF_SOME(expected, target) {
        if (!sameModule(expected, candidates.value().candidates()[0])) {
          return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
              query::QueryRuntimeFailure::ProviderRejected);
        }
        dependencies.add(expected.clone());
      }
      continue;
    }
    if (candidates.value().candidates().size() == 0) {
      auto failure = ModuleDependencyFailureRecord::missing(request.clone());
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleDependencySetRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
    if (candidates.value().candidates().size() != 1) {
      zc::Vector<identity::ModuleKey> values(candidates.value().candidates().size());
      for (const auto& candidate : candidates.value().candidates()) {
        values.add(candidate.clone());
      }
      auto failure = ModuleDependencyFailureRecord::ambiguous(request.clone(), zc::mv(values));
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleDependencySetRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
    dependencies.add(candidates.value().candidates()[0].clone());
  }
  auto result = ModuleDependencySetRecord::from(zc::mv(dependencies));
  if (result == zc::none) {
    return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<ModuleDependencySetRecord>::value(
      zc::mv(ZC_ASSERT_NONNULL(result)));
}

query::TypedQueryResult<ModuleDependencySetRecord> resolveVerifierDependencies(
    query::QueryContext& context, const identity::ModuleKey& requester,
    const ModuleDependencyRequestSetRecord& requests) {
  zc::TreeMap<zc::String, identity::ModuleKey> resolved;
  size_t cursor = 0;
  while (cursor < requests.requests().size()) {
    const auto& request = requests.requests()[cursor++];
    auto candidates =
        context.get<incremental_module_resolution_query::ResolveModuleRequest>(request);
    if (candidates.isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
          candidates.runtimeFailure());
    }
    if (candidates.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
      auto configured = context.get<incremental_module_resolution_query::ConfiguredPreludeInput>(
          requester.crate());
      if (configured.isRuntimeFailure()) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            configured.runtimeFailure());
      }
      auto target = configured.kind() == query::QueryValueKind::Value
                        ? configured.value().target()
                        : zc::Maybe<const identity::ModuleKey&>();
      if (target == zc::none || candidates.value().candidates().size() != 1 ||
          ZC_ASSERT_NONNULL(target).encode().asPtr() !=
              candidates.value().candidates()[0].encode().asPtr()) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      auto bytes = zc::encodeHex(ZC_ASSERT_NONNULL(target).encode().asPtr());
      if (resolved.find(bytes) == zc::none) {
        resolved.insert(zc::mv(bytes), ZC_ASSERT_NONNULL(target).clone());
      }
      continue;
    }
    if (candidates.value().candidates().size() == 0) {
      auto failure = ModuleDependencyFailureRecord::missing(request.clone());
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleDependencySetRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
    if (candidates.value().candidates().size() > 1) {
      zc::Vector<identity::ModuleKey> ambiguous(candidates.value().candidates().size());
      for (const auto& candidate : candidates.value().candidates()) {
        ambiguous.add(candidate.clone());
      }
      auto failure = ModuleDependencyFailureRecord::ambiguous(request.clone(), zc::mv(ambiguous));
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleDependencySetRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
    const auto& selected = candidates.value().candidates()[0];
    auto bytes = zc::encodeHex(selected.encode().asPtr());
    if (resolved.find(bytes) == zc::none) { resolved.insert(zc::mv(bytes), selected.clone()); }
  }
  zc::Vector<identity::ModuleKey> dependencies(resolved.size());
  for (auto& entry : resolved) { dependencies.add(zc::mv(entry.value)); }
  auto result = ModuleDependencySetRecord::from(zc::mv(dependencies));
  if (result == zc::none) {
    return query::TypedQueryResult<ModuleDependencySetRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<ModuleDependencySetRecord>::value(
      zc::mv(ZC_ASSERT_NONNULL(result)));
}

bool ledgerContains(zc::ArrayPtr<const ModuleGraphInputLedgerEntry> entries,
                    const ModuleGraphInputLedgerEntry& value) {
  for (const auto& entry : entries) {
    if (entry == value) { return true; }
  }
  return false;
}

bool validLedgerKey(ModuleGraphInputFamily family, zc::ArrayPtr<const uint8_t> bytes) {
  switch (family) {
    case ModuleGraphInputFamily::SelectedModuleCatalog:
      return SelectedModuleCatalogInput::decodeKey(bytes) != zc::none;
    case ModuleGraphInputFamily::ModuleDependencySite:
      return ModuleDependencySiteInput::decodeKey(bytes) != zc::none;
    case ModuleGraphInputFamily::ModuleCatalogPathBucket:
      return incremental_module_resolution_query::ModuleCatalogPathBucketInput::decodeKey(bytes) !=
             zc::none;
    case ModuleGraphInputFamily::RequesterModuleAncestry:
      return incremental_module_resolution_query::RequesterModuleAncestryInput::decodeKey(bytes) !=
             zc::none;
    case ModuleGraphInputFamily::ModuleSearchRoots:
      return incremental_module_resolution_query::ModuleSearchRootsInput::decodeKey(bytes) !=
             zc::none;
    case ModuleGraphInputFamily::DependencyAliasRoot:
      return incremental_module_resolution_query::DependencyAliasRootInput::decodeKey(bytes) !=
             zc::none;
    case ModuleGraphInputFamily::ConfiguredPrelude:
      return incremental_module_resolution_query::ConfiguredPreludeInput::decodeKey(bytes) !=
             zc::none;
  }
  return false;
}

bool eraseLedgerEntry(query::InputTransaction& transaction,
                      const ModuleGraphInputLedgerEntry& entry) {
  switch (entry.family()) {
    case ModuleGraphInputFamily::SelectedModuleCatalog: {
      auto key = SelectedModuleCatalogInput::decodeKey(entry.keyBytes());
      return key != zc::none &&
             transaction.erase<SelectedModuleCatalogInput>(ZC_ASSERT_NONNULL(key)).isApplied();
    }
    case ModuleGraphInputFamily::ModuleDependencySite: {
      auto key = ModuleDependencySiteInput::decodeKey(entry.keyBytes());
      return key != zc::none &&
             transaction.erase<ModuleDependencySiteInput>(ZC_ASSERT_NONNULL(key)).isApplied();
    }
    case ModuleGraphInputFamily::ModuleCatalogPathBucket: {
      auto key = incremental_module_resolution_query::ModuleCatalogPathBucketInput::decodeKey(
          entry.keyBytes());
      return key != zc::none &&
             transaction
                 .erase<incremental_module_resolution_query::ModuleCatalogPathBucketInput>(
                     ZC_ASSERT_NONNULL(key))
                 .isApplied();
    }
    case ModuleGraphInputFamily::RequesterModuleAncestry: {
      auto key = incremental_module_resolution_query::RequesterModuleAncestryInput::decodeKey(
          entry.keyBytes());
      return key != zc::none &&
             transaction
                 .erase<incremental_module_resolution_query::RequesterModuleAncestryInput>(
                     ZC_ASSERT_NONNULL(key))
                 .isApplied();
    }
    case ModuleGraphInputFamily::ModuleSearchRoots: {
      auto key =
          incremental_module_resolution_query::ModuleSearchRootsInput::decodeKey(entry.keyBytes());
      return key != zc::none &&
             transaction
                 .erase<incremental_module_resolution_query::ModuleSearchRootsInput>(
                     ZC_ASSERT_NONNULL(key))
                 .isApplied();
    }
    case ModuleGraphInputFamily::DependencyAliasRoot: {
      auto key = incremental_module_resolution_query::DependencyAliasRootInput::decodeKey(
          entry.keyBytes());
      return key != zc::none &&
             transaction
                 .erase<incremental_module_resolution_query::DependencyAliasRootInput>(
                     ZC_ASSERT_NONNULL(key))
                 .isApplied();
    }
    case ModuleGraphInputFamily::ConfiguredPrelude: {
      auto key =
          incremental_module_resolution_query::ConfiguredPreludeInput::decodeKey(entry.keyBytes());
      return key != zc::none &&
             transaction
                 .erase<incremental_module_resolution_query::ConfiguredPreludeInput>(
                     ZC_ASSERT_NONNULL(key))
                 .isApplied();
    }
  }
  return false;
}

zc::Vector<identity::CrateKey> cloneCrates(zc::ArrayPtr<const identity::CrateKey> crates) {
  zc::Vector<identity::CrateKey> result(crates.size());
  for (const auto& crate : crates) { result.add(crate.clone()); }
  return result;
}

bool canonicalizeCrates(zc::Vector<identity::CrateKey>& crates, bool requireNonEmpty = true) {
  if ((requireNonEmpty && crates.size() == 0) ||
      crates.size() > kMaximumCanonicalInputSequenceRecords) {
    return false;
  }
  sortByBytes(crates, [](const identity::CrateKey& crate) { return crate.encode(); });
  for (size_t index = 1; index < crates.size(); ++index) {
    if (sameCrate(crates[index - 1], crates[index])) { return false; }
  }
  return true;
}

zc::Maybe<zc::Vector<identity::CrateKey>> canonicalCrateUnion(
    zc::ArrayPtr<const identity::CrateKey> left, zc::ArrayPtr<const identity::CrateKey> right) {
  auto result = cloneCrates(left);
  for (const auto& crate : right) { result.add(crate.clone()); }
  if (!canonicalizeCrates(result)) { return zc::none; }
  return zc::mv(result);
}

bool sameCrates(zc::ArrayPtr<const identity::CrateKey> left,
                zc::ArrayPtr<const identity::CrateKey> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameCrate(left[index], right[index])) { return false; }
  }
  return true;
}

template <typename Entry, typename KeyEncoder>
bool canonicalizeEntries(zc::Vector<Entry>& entries, KeyEncoder&& encodeKey) {
  if (entries.size() == 0 || entries.size() > kMaximumCanonicalInputSequenceRecords) {
    return false;
  }
  sortByBytes(entries, [&](const Entry& entry) { return encodeKey(entry.key()); });
  for (size_t index = 1; index < entries.size(); ++index) {
    if (encodeKey(entries[index - 1].key()).asPtr() == encodeKey(entries[index].key()).asPtr()) {
      return false;
    }
  }
  return true;
}

zc::Vector<CompilationOptionsEntry> cloneCompilationOptions(
    zc::ArrayPtr<const CompilationOptionsEntry> entries) {
  zc::Vector<CompilationOptionsEntry> result(entries.size());
  for (const auto& entry : entries) { result.add(entry.clone()); }
  return result;
}

zc::Vector<ModuleSearchRootsEntry> cloneSearchRoots(
    zc::ArrayPtr<const ModuleSearchRootsEntry> entries) {
  zc::Vector<ModuleSearchRootsEntry> result(entries.size());
  for (const auto& entry : entries) { result.add(entry.clone()); }
  return result;
}

zc::Maybe<identity::CrateKey> decodeStableCrate(
    const incremental_binding_query::StableCrateQueryKey& key) {
  return decodeCrate(key.canonicalCrateBytes());
}

bool containsCrate(zc::ArrayPtr<const identity::CrateKey> crates,
                   const identity::CrateKey& candidate) {
  for (const auto& crate : crates) {
    if (sameCrate(crate, candidate)) { return true; }
  }
  return false;
}

bool samePackage(const identity::PackageKey& left, const identity::PackageKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool containsPackage(zc::ArrayPtr<const incremental_binding_query::StablePackageQueryKey> packages,
                     const identity::PackageKey& candidate) {
  const auto encoded = candidate.encode();
  for (const auto& package : packages) {
    if (package.canonicalPackageBytes() == encoded.asPtr()) { return true; }
  }
  return false;
}

zc::Maybe<identity::PackageDependencyEdgeKey> decodePackageEdge(
    const incremental_binding_query::StablePackageDependencyQueryKey& stable) {
  identity::CanonicalDecoder decoder(stable.canonicalEdgeBytes());
  auto edge = identity::PackageDependencyEdgeKey::decodeCanonical(decoder);
  if (edge == zc::none || !decoder.finished()) { return zc::none; }
  return edge;
}

zc::Maybe<identity::CrateDependencyEdgeKey> decodeCrateEdge(
    const incremental_binding_query::StableCrateDependencyQueryKey& stable) {
  identity::CanonicalDecoder decoder(stable.canonicalEdgeBytes());
  auto edge = identity::CrateDependencyEdgeKey::decodeCanonical(decoder);
  if (edge == zc::none || !decoder.finished()) { return zc::none; }
  return edge;
}

bool containsPackageEdge(
    zc::ArrayPtr<const incremental_binding_query::StablePackageDependencyQueryKey> edges,
    const identity::PackageDependencyEdgeKey& candidate) {
  const auto encoded = candidate.encode();
  for (const auto& edge : edges) {
    if (edge.canonicalEdgeBytes() == encoded.asPtr()) { return true; }
  }
  return false;
}

zc::Array<uint8_t> encodeTargetSpecification(
    const identity::CanonicalTargetSpecificationKey& target) {
  identity::CanonicalEncoder encoder;
  target.encode(encoder);
  return encoder.finish();
}

bool rootMatchesCrate(const package::VerifiedCompilationRoot& root,
                      const package::VerifiedPackageCompilationRequest& request,
                      const identity::CrateKey& crate) {
  const auto& semantic = crate.semanticOptions();
  return crate.unit().kind() == identity::CompilationUnitKind::UserPackage &&
         samePackage(crate.unit().userPackage(), root.packageKey()) &&
         crate.targetKind() == root.targetKind() && crate.targetName() == root.targetName() &&
         crate.compilation().domain() == identity::CompilationDomain::Target &&
         encodeTargetSpecification(crate.compilation().target()).asPtr() ==
             encodeTargetSpecification(request.target().semanticProjection()).asPtr() &&
         semantic.editionYear() == root.editionYear() &&
         semantic.useUnicode() == request.languageOptions().useUnicode &&
         semantic.allowDollarIdentifiers() == request.languageOptions().allowDollarIdentifiers &&
         semantic.supportRegexLiterals() == request.languageOptions().supportRegexLiterals &&
         crate.compilation().hasBuildScriptProducer() == root.requiresBuildScript();
}

bool validateProducerGraphClosure(const CompleteCompilationContextSources& sources,
                                  zc::ArrayPtr<const identity::CrateKey> completeCrates) {
  const auto packages = sources.packageGraph.resolvedPackages();
  const auto packageEdges = sources.packageGraph.resolvedPackageEdges();
  const auto selectedEdges = sources.packageGraph.selectedPackageEdges();
  if (packages.size() == 0 || sources.packageGraph.crates().size() == 0) { return false; }
  for (const auto& root : sources.packageRequest.roots()) {
    if (!containsPackage(packages, root.packageKey())) { return false; }
  }
  for (const auto& stable : packageEdges) {
    auto edge = decodePackageEdge(stable);
    if (edge == zc::none || !containsPackage(packages, ZC_ASSERT_NONNULL(edge).consumer()) ||
        !containsPackage(packages, ZC_ASSERT_NONNULL(edge).provider())) {
      return false;
    }
  }
  for (const auto& selected : selectedEdges) {
    bool present = false;
    for (const auto& resolved : packageEdges) {
      if (selected == resolved) {
        present = true;
        break;
      }
    }
    if (!present) { return false; }
  }
  for (const auto& stable : sources.packageGraph.crates()) {
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none || !containsCrate(completeCrates, ZC_ASSERT_NONNULL(crate))) {
      return false;
    }
    const auto& unit = ZC_ASSERT_NONNULL(crate).unit();
    if (unit.kind() == identity::CompilationUnitKind::UserPackage) {
      if (!containsPackage(packages, unit.userPackage())) { return false; }
    } else if (unit.toolchain().component() != identity::ToolchainComponent::Core) {
      return false;
    }
  }
  for (const auto& stable : sources.packageGraph.crateEdges()) {
    auto edge = decodeCrateEdge(stable);
    if (edge == zc::none || !containsCrate(completeCrates, ZC_ASSERT_NONNULL(edge).consumer()) ||
        !containsCrate(completeCrates, ZC_ASSERT_NONNULL(edge).provider())) {
      return false;
    }
    const auto& origin = ZC_ASSERT_NONNULL(edge).origin();
    if (origin.kind() == identity::CrateDependencyOriginKind::UserPackage) {
      const auto& packageEdge = origin.userPackageEdge();
      if (!containsPackageEdge(selectedEdges, packageEdge) ||
          ZC_ASSERT_NONNULL(edge).consumer().unit().kind() !=
              identity::CompilationUnitKind::UserPackage ||
          ZC_ASSERT_NONNULL(edge).provider().unit().kind() !=
              identity::CompilationUnitKind::UserPackage ||
          !samePackage(ZC_ASSERT_NONNULL(edge).consumer().unit().userPackage(),
                       packageEdge.consumer()) ||
          !samePackage(ZC_ASSERT_NONNULL(edge).provider().unit().userPackage(),
                       packageEdge.provider())) {
        return false;
      }
    } else if (ZC_ASSERT_NONNULL(edge).provider().unit().kind() !=
                   identity::CompilationUnitKind::Toolchain ||
               ZC_ASSERT_NONNULL(edge).provider().unit().toolchain().component() !=
                   identity::ToolchainComponent::Core) {
      return false;
    }
  }
  return true;
}

bool validateCompleteContextProducerSources(
    const CompleteCompilationContextSources& sources,
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots,
    zc::ArrayPtr<const identity::CrateKey> expectedRoots,
    zc::ArrayPtr<const identity::CrateKey> completeCrates) {
  auto independentlyProjected = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
      sources.packageRequest, sources.projectedCoreCrates);
  auto independentlyRooted =
      incremental_binding_query::PackageRootSetKey::fromVerified(sources.packageRequest);
  auto expectedOptions =
      identity::source_query::CanonicalCompilationOptions::fromVerified(sources.packageRequest);
  if (independentlyProjected == zc::none || independentlyRooted == zc::none ||
      expectedOptions == zc::none || ZC_ASSERT_NONNULL(independentlyProjected) != contextRoots ||
      ZC_ASSERT_NONNULL(independentlyRooted) != sources.packageRootSet ||
      sources.userRootCrates.size() != sources.packageRequest.roots().size() ||
      sources.compilationOptions.size() != completeCrates.size() ||
      sources.moduleSearchRoots.size() != completeCrates.size() ||
      !validateProducerGraphClosure(sources, completeCrates)) {
    return false;
  }
  for (const auto& requestRoot : sources.packageRequest.roots()) {
    size_t matches = 0;
    for (const auto& crate : sources.userRootCrates) {
      if (rootMatchesCrate(requestRoot, sources.packageRequest, crate)) { ++matches; }
    }
    if (matches != 1) { return false; }
  }
  for (const auto& root : sources.userRootCrates) {
    if (!containsCrate(completeCrates, root) ||
        root.unit().kind() != identity::CompilationUnitKind::UserPackage) {
      return false;
    }
  }
  for (const auto& core : sources.projectedCoreCrates) {
    if (core.unit().kind() != identity::CompilationUnitKind::Toolchain ||
        core.unit().toolchain().component() != identity::ToolchainComponent::Core ||
        !containsCrate(expectedRoots, core) || !containsCrate(completeCrates, core)) {
      return false;
    }
  }
  for (const auto& entry : sources.compilationOptions) {
    if (!containsCrate(completeCrates, entry.key()) || !entry.value().matchesCrate(entry.key()) ||
        entry.value() != ZC_ASSERT_NONNULL(expectedOptions)) {
      return false;
    }
  }
  for (const auto& entry : sources.moduleSearchRoots) {
    if (!containsCrate(completeCrates, entry.key()) ||
        !sameCrate(entry.key(), entry.value().crate()) || entry.value().roots().size() == 0) {
      return false;
    }
    bool foundCoreDistribution = false;
    for (const auto& root : entry.value().roots()) {
      if (!sameCrate(root.crate(), entry.key())) { return false; }
      if (root.kind() == binder::ModuleSearchRootKind::ToolchainCore) {
        if (entry.key().unit().kind() != identity::CompilationUnitKind::Toolchain ||
            entry.key().unit().toolchain().component() != identity::ToolchainComponent::Core ||
            root.toolchainCoreDistributionDigest() != sources.coreDistribution.digest()) {
          return false;
        }
        foundCoreDistribution = true;
      }
    }
    const bool isCore =
        entry.key().unit().kind() == identity::CompilationUnitKind::Toolchain &&
        entry.key().unit().toolchain().component() == identity::ToolchainComponent::Core;
    if (foundCoreDistribution != isCore) { return false; }
  }
  auto digest = source::core::computeCoreDistributionDigest(sources.coreDistribution.record());
  return digest != zc::none && ZC_ASSERT_NONNULL(digest) == sources.coreDistribution.digest();
}

void encodeCrateSequence(identity::CanonicalEncoder& encoder,
                         zc::ArrayPtr<const identity::CrateKey> crates) {
  encoder.encodeSequenceSize(crates.size());
  for (const auto& crate : crates) {
    auto bytes = crate.encode();
    encoder.encodeByteString(bytes.asPtr());
  }
}

template <typename Entry, typename KeyEncoder, typename ValueEncoder>
void encodeEntrySequence(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const Entry> entries,
                         KeyEncoder&& encodeKey, ValueEncoder&& encodeValue) {
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    identity::CanonicalEncoder nested;
    auto keyBytes = encodeKey(entry.key());
    auto valueBytes = encodeValue(entry.value());
    nested.encodeByteString(keyBytes.asPtr());
    nested.encodeByteString(valueBytes.asPtr());
    auto bytes = nested.finish();
    encoder.encodeByteString(bytes.asPtr());
  }
}

zc::Maybe<zc::Vector<identity::CrateKey>> decodeCrateSequence(identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kCanonicalByteStringPrefixBytes) {
    return zc::none;
  }
  zc::Vector<identity::CrateKey> result(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto bytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (bytes == zc::none) { return zc::none; }
    auto crate = decodeCrate(ZC_ASSERT_NONNULL(bytes).asPtr());
    if (crate == zc::none) { return zc::none; }
    result.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  if (!canonicalizeCrates(result)) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<zc::Vector<CompilationOptionsEntry>> decodeCompilationOptions(
    identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kCanonicalByteStringPrefixBytes) {
    return zc::none;
  }
  zc::Vector<CompilationOptionsEntry> result(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto record = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (record == zc::none) { return zc::none; }
    identity::CanonicalDecoder nested(ZC_ASSERT_NONNULL(record).asPtr());
    auto keyBytes = nested.decodeByteString(kMaximumCanonicalInputValueBytes);
    auto valueBytes = nested.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none || !nested.finished()) { return zc::none; }
    auto key = identity::source_query::CompilationOptionsInput::decodeKey(
        ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = identity::source_query::CompilationOptionsInput::decodeValue(
        ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    result.add(CompilationOptionsEntry::from(zc::mv(ZC_ASSERT_NONNULL(key)),
                                             zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  if (!canonicalizeEntries(result, [](const identity::CrateKey& key) {
        return identity::source_query::CompilationOptionsInput::encodeKey(key);
      })) {
    return zc::none;
  }
  return zc::mv(result);
}

zc::Maybe<zc::Vector<ModuleSearchRootsEntry>> decodeSearchRoots(
    identity::CanonicalDecoder& decoder) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0 ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / kCanonicalByteStringPrefixBytes) {
    return zc::none;
  }
  zc::Vector<ModuleSearchRootsEntry> result(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto record = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (record == zc::none) { return zc::none; }
    identity::CanonicalDecoder nested(ZC_ASSERT_NONNULL(record).asPtr());
    auto keyBytes = nested.decodeByteString(kMaximumCanonicalInputValueBytes);
    auto valueBytes = nested.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none || !nested.finished()) { return zc::none; }
    auto key = incremental_module_resolution_query::ModuleSearchRootsInput::decodeKey(
        ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = incremental_module_resolution_query::ModuleSearchRootsInput::decodeValue(
        ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    result.add(ModuleSearchRootsEntry::from(zc::mv(ZC_ASSERT_NONNULL(key)),
                                            zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  if (!canonicalizeEntries(result, [](const identity::CrateKey& key) {
        return incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(key);
      })) {
    return zc::none;
  }
  return zc::mv(result);
}

}  // namespace

struct CompleteCompilationContextAuthority::Impl final {
  Impl(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       package::CanonicalPackageCompilationRequest&& packageRequest,
       incremental_binding_query::PackageRootSetKey&& packageRootSet,
       incremental_binding_query::CanonicalPackageGraph&& packageGraph,
       zc::Vector<identity::CrateKey>&& userRootCrates,
       zc::Vector<identity::CrateKey>&& projectedCoreCrates,
       zc::Vector<identity::CrateKey>&& expectedRootCrates,
       zc::Vector<identity::CrateKey>&& completeCrates,
       zc::Vector<CompilationOptionsEntry>&& compilationOptions,
       zc::Vector<ModuleSearchRootsEntry>&& moduleSearchRoots,
       source::core::CoreDistributionRecord&& coreDistributionRecord,
       const identity::Sha256Digest& coreDistributionDigest) noexcept
      : contextRoots(zc::mv(contextRoots)),
        packageRequest(zc::mv(packageRequest)),
        packageRootSet(zc::mv(packageRootSet)),
        packageGraph(zc::mv(packageGraph)),
        userRootCrates(zc::mv(userRootCrates)),
        projectedCoreCrates(zc::mv(projectedCoreCrates)),
        expectedRootCrates(zc::mv(expectedRootCrates)),
        completeCrates(zc::mv(completeCrates)),
        compilationOptions(zc::mv(compilationOptions)),
        moduleSearchRoots(zc::mv(moduleSearchRoots)),
        coreDistributionRecord(zc::mv(coreDistributionRecord)),
        coreDistributionDigest(coreDistributionDigest) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  package::CanonicalPackageCompilationRequest packageRequest;
  incremental_binding_query::PackageRootSetKey packageRootSet;
  incremental_binding_query::CanonicalPackageGraph packageGraph;
  zc::Vector<identity::CrateKey> userRootCrates;
  zc::Vector<identity::CrateKey> projectedCoreCrates;
  zc::Vector<identity::CrateKey> expectedRootCrates;
  zc::Vector<identity::CrateKey> completeCrates;
  zc::Vector<CompilationOptionsEntry> compilationOptions;
  zc::Vector<ModuleSearchRootsEntry> moduleSearchRoots;
  source::core::CoreDistributionRecord coreDistributionRecord;
  identity::Sha256Digest coreDistributionDigest;
};

CompleteCompilationContextAuthority::CompleteCompilationContextAuthority(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CompleteCompilationContextAuthority::~CompleteCompilationContextAuthority() noexcept(false) =
    default;
CompleteCompilationContextAuthority::CompleteCompilationContextAuthority(
    CompleteCompilationContextAuthority&&) noexcept = default;
CompleteCompilationContextAuthority& CompleteCompilationContextAuthority::operator=(
    CompleteCompilationContextAuthority&&) noexcept = default;

zc::Maybe<CompleteCompilationContextAuthority> CompleteCompilationContextAuthority::fromVerified(
    const CompleteCompilationContextSources& sources) {
  auto packageRequest =
      package::CanonicalPackageCompilationRequest::fromVerified(sources.packageRequest);
  auto contextRoots = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
      sources.packageRequest, sources.projectedCoreCrates);
  auto userRoots = cloneCrates(sources.userRootCrates);
  auto projectedCore = cloneCrates(sources.projectedCoreCrates);
  if (packageRequest == zc::none || contextRoots == zc::none || !canonicalizeCrates(userRoots) ||
      !canonicalizeCrates(projectedCore)) {
    return zc::none;
  }
  auto expectedRoots = canonicalCrateUnion(userRoots.asPtr(), projectedCore.asPtr());
  zc::Vector<identity::CrateKey> packageCrates(sources.packageGraph.crates().size());
  for (const auto& stable : sources.packageGraph.crates()) {
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none) { return zc::none; }
    packageCrates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  auto completeCrates = canonicalCrateUnion(packageCrates.asPtr(), projectedCore.asPtr());
  auto options = cloneCompilationOptions(sources.compilationOptions);
  auto searchRoots = cloneSearchRoots(sources.moduleSearchRoots);
  if (expectedRoots == zc::none || completeCrates == zc::none ||
      !canonicalizeEntries(options,
                           [](const identity::CrateKey& key) {
                             return identity::source_query::CompilationOptionsInput::encodeKey(key);
                           }) ||
      !canonicalizeEntries(
          searchRoots,
          [](const identity::CrateKey& key) {
            return incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(key);
          }) ||
      !validateCompleteContextProducerSources(sources, ZC_ASSERT_NONNULL(contextRoots),
                                              ZC_ASSERT_NONNULL(expectedRoots).asPtr(),
                                              ZC_ASSERT_NONNULL(completeCrates).asPtr())) {
    return zc::none;
  }
  auto result = CompleteCompilationContextAuthority(zc::heap<Impl>(
      zc::mv(ZC_ASSERT_NONNULL(contextRoots)), zc::mv(ZC_ASSERT_NONNULL(packageRequest)),
      sources.packageRootSet.clone(), sources.packageGraph.clone(), zc::mv(userRoots),
      zc::mv(projectedCore), zc::mv(ZC_ASSERT_NONNULL(expectedRoots)),
      zc::mv(ZC_ASSERT_NONNULL(completeCrates)), zc::mv(options), zc::mv(searchRoots),
      sources.coreDistribution.record().clone(), sources.coreDistribution.digest()));
  return zc::mv(result);
}

CompleteCompilationContextAuthority CompleteCompilationContextAuthority::clone() const {
  return CompleteCompilationContextAuthority(zc::heap<Impl>(
      impl->contextRoots.clone(), impl->packageRequest.clone(), impl->packageRootSet.clone(),
      impl->packageGraph.clone(), cloneCrates(impl->userRootCrates.asPtr()),
      cloneCrates(impl->projectedCoreCrates.asPtr()), cloneCrates(impl->expectedRootCrates.asPtr()),
      cloneCrates(impl->completeCrates.asPtr()),
      cloneCompilationOptions(impl->compilationOptions.asPtr()),
      cloneSearchRoots(impl->moduleSearchRoots.asPtr()), impl->coreDistributionRecord.clone(),
      impl->coreDistributionDigest));
}

const incremental_binding_query::CompilationRootSetQueryKey&
CompleteCompilationContextAuthority::contextRoots() const noexcept {
  return impl->contextRoots;
}
const package::CanonicalPackageCompilationRequest&
CompleteCompilationContextAuthority::packageRequest() const noexcept {
  return impl->packageRequest;
}
const incremental_binding_query::PackageRootSetKey&
CompleteCompilationContextAuthority::packageRootSet() const noexcept {
  return impl->packageRootSet;
}
const incremental_binding_query::CanonicalPackageGraph&
CompleteCompilationContextAuthority::packageGraph() const noexcept {
  return impl->packageGraph;
}
zc::ArrayPtr<const identity::CrateKey> CompleteCompilationContextAuthority::userRootCrates()
    const noexcept {
  return impl->userRootCrates.asPtr();
}
zc::ArrayPtr<const identity::CrateKey> CompleteCompilationContextAuthority::projectedCoreCrates()
    const noexcept {
  return impl->projectedCoreCrates.asPtr();
}
zc::ArrayPtr<const identity::CrateKey> CompleteCompilationContextAuthority::expectedRootCrates()
    const noexcept {
  return impl->expectedRootCrates.asPtr();
}
zc::ArrayPtr<const identity::CrateKey> CompleteCompilationContextAuthority::completeCrates()
    const noexcept {
  return impl->completeCrates.asPtr();
}
zc::ArrayPtr<const CompilationOptionsEntry>
CompleteCompilationContextAuthority::compilationOptions() const noexcept {
  return impl->compilationOptions.asPtr();
}
zc::ArrayPtr<const ModuleSearchRootsEntry> CompleteCompilationContextAuthority::moduleSearchRoots()
    const noexcept {
  return impl->moduleSearchRoots.asPtr();
}
const source::core::CoreDistributionRecord&
CompleteCompilationContextAuthority::coreDistributionRecord() const noexcept {
  return impl->coreDistributionRecord;
}
const identity::Sha256Digest& CompleteCompilationContextAuthority::coreDistributionDigest()
    const noexcept {
  return impl->coreDistributionDigest;
}

zc::Array<uint8_t> CompleteCompilationContextAuthority::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  auto contextBytes = impl->contextRoots.encodeCanonical();
  auto requestBytes = impl->packageRequest.encodeCanonical();
  auto rootSetBytes = incremental_binding_query::PackageGraphInput::encodeKey(impl->packageRootSet);
  auto graphBytes = incremental_binding_query::PackageGraphInput::encodeValue(impl->packageGraph);
  encoder.encodeByteString(contextBytes.asPtr());
  encoder.encodeByteString(requestBytes.asPtr());
  encoder.encodeByteString(rootSetBytes.asPtr());
  encoder.encodeByteString(graphBytes.asPtr());
  encodeCrateSequence(encoder, impl->userRootCrates.asPtr());
  encodeCrateSequence(encoder, impl->projectedCoreCrates.asPtr());
  encodeCrateSequence(encoder, impl->expectedRootCrates.asPtr());
  encodeCrateSequence(encoder, impl->completeCrates.asPtr());
  encodeEntrySequence(
      encoder, impl->compilationOptions.asPtr(),
      [](const identity::CrateKey& key) {
        return identity::source_query::CompilationOptionsInput::encodeKey(key);
      },
      [](const identity::source_query::CanonicalCompilationOptions& value) {
        return identity::source_query::CompilationOptionsInput::encodeValue(value);
      });
  encodeEntrySequence(
      encoder, impl->moduleSearchRoots.asPtr(),
      [](const identity::CrateKey& key) {
        return incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(key);
      },
      [](const incremental_module_resolution_query::CanonicalModuleSearchRoots& value) {
        return incremental_module_resolution_query::ModuleSearchRootsInput::encodeValue(value);
      });
  auto distributionBytes = impl->coreDistributionRecord.encode();
  encoder.encodeByteString(distributionBytes.asPtr());
  encoder.encodeDigest(impl->coreDistributionDigest);
  return frame(kCompleteContextAuthorityDomain, encoder.finish().asPtr());
}

bool CompleteCompilationContextAuthority::operator==(
    const CompleteCompilationContextAuthority& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

zc::Maybe<CompleteCompilationContextAuthority> CompleteCompilationContextAuthority::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframeCanonicalInput(kCompleteContextAuthorityDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto contextBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  auto requestBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  auto rootSetBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  auto graphBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  if (contextBytes == zc::none || requestBytes == zc::none || rootSetBytes == zc::none ||
      graphBytes == zc::none) {
    return zc::none;
  }
  auto contextRoots = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(contextBytes).asPtr());
  auto packageRequest = package::CanonicalPackageCompilationRequest::decodeCanonical(
      ZC_ASSERT_NONNULL(requestBytes).asPtr());
  auto packageRootSet = incremental_binding_query::PackageGraphInput::decodeKey(
      ZC_ASSERT_NONNULL(rootSetBytes).asPtr());
  auto packageGraph = incremental_binding_query::PackageGraphInput::decodeValue(
      ZC_ASSERT_NONNULL(graphBytes).asPtr());
  auto userRoots = decodeCrateSequence(decoder);
  auto projectedCore = decodeCrateSequence(decoder);
  auto expectedRoots = decodeCrateSequence(decoder);
  auto completeCrates = decodeCrateSequence(decoder);
  auto options = decodeCompilationOptions(decoder);
  auto searchRoots = decodeSearchRoots(decoder);
  auto distributionBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  auto distributionDigest = decoder.decodeDigest();
  if (contextRoots == zc::none || packageRequest == zc::none || packageRootSet == zc::none ||
      packageGraph == zc::none || userRoots == zc::none || projectedCore == zc::none ||
      expectedRoots == zc::none || completeCrates == zc::none || options == zc::none ||
      searchRoots == zc::none || distributionBytes == zc::none || distributionDigest == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto distribution = source::core::CoreDistributionRecord::decodeCanonical(
      ZC_ASSERT_NONNULL(distributionBytes).asPtr());
  if (distribution == zc::none) { return zc::none; }
  auto expectedUnion = canonicalCrateUnion(ZC_ASSERT_NONNULL(userRoots).asPtr(),
                                           ZC_ASSERT_NONNULL(projectedCore).asPtr());
  zc::Vector<identity::CrateKey> packageCrates(ZC_ASSERT_NONNULL(packageGraph).crates().size());
  for (const auto& stable : ZC_ASSERT_NONNULL(packageGraph).crates()) {
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none) { return zc::none; }
    packageCrates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  auto completeUnion =
      canonicalCrateUnion(packageCrates.asPtr(), ZC_ASSERT_NONNULL(projectedCore).asPtr());
  auto computedDigest =
      source::core::computeCoreDistributionDigest(ZC_ASSERT_NONNULL(distribution));
  if (expectedUnion == zc::none || completeUnion == zc::none || computedDigest == zc::none ||
      !sameCrates(ZC_ASSERT_NONNULL(expectedUnion).asPtr(),
                  ZC_ASSERT_NONNULL(expectedRoots).asPtr()) ||
      !sameCrates(ZC_ASSERT_NONNULL(completeUnion).asPtr(),
                  ZC_ASSERT_NONNULL(completeCrates).asPtr()) ||
      ZC_ASSERT_NONNULL(options).size() != ZC_ASSERT_NONNULL(completeCrates).size() ||
      ZC_ASSERT_NONNULL(searchRoots).size() != ZC_ASSERT_NONNULL(completeCrates).size() ||
      ZC_ASSERT_NONNULL(computedDigest) != ZC_ASSERT_NONNULL(distributionDigest)) {
    return zc::none;
  }
  for (const auto& core : ZC_ASSERT_NONNULL(projectedCore)) {
    if (core.unit().kind() != identity::CompilationUnitKind::Toolchain ||
        core.unit().toolchain().component() != identity::ToolchainComponent::Core) {
      return zc::none;
    }
  }
  for (const auto& entry : ZC_ASSERT_NONNULL(options)) {
    if (!containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), entry.key()) ||
        !entry.value().matchesCrate(entry.key())) {
      return zc::none;
    }
  }
  for (const auto& entry : ZC_ASSERT_NONNULL(searchRoots)) {
    if (!containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), entry.key()) ||
        !sameCrate(entry.key(), entry.value().crate())) {
      return zc::none;
    }
  }
  zc::Vector<incremental_binding_query::CompilationRootKey> rebuiltRoots;
  for (const auto& root : ZC_ASSERT_NONNULL(packageRequest).roots()) {
    auto value = incremental_binding_query::CompilationRootKey::userPackage(root.package());
    if (value == zc::none) { return zc::none; }
    bool present = false;
    for (const auto& existing : rebuiltRoots) {
      if (existing == ZC_ASSERT_NONNULL(value)) {
        present = true;
        break;
      }
    }
    if (!present) { rebuiltRoots.add(zc::mv(ZC_ASSERT_NONNULL(value))); }
  }
  for (const auto& core : ZC_ASSERT_NONNULL(projectedCore)) {
    auto value = incremental_binding_query::CompilationRootKey::toolchainCore(core);
    if (value == zc::none) { return zc::none; }
    rebuiltRoots.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  auto rebuiltContext =
      incremental_binding_query::CompilationRootSetQueryKey::from(zc::mv(rebuiltRoots));
  if (rebuiltContext == zc::none ||
      ZC_ASSERT_NONNULL(rebuiltContext) != ZC_ASSERT_NONNULL(contextRoots)) {
    return zc::none;
  }
  for (const auto& root : ZC_ASSERT_NONNULL(packageRequest).roots()) {
    bool present = false;
    const auto packageBytes = root.package().encode();
    for (const auto& key : ZC_ASSERT_NONNULL(packageRootSet).packages()) {
      if (key.canonicalPackageBytes() == packageBytes.asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { return zc::none; }
  }
  for (const auto& key : ZC_ASSERT_NONNULL(packageRootSet).packages()) {
    bool present = false;
    for (const auto& root : ZC_ASSERT_NONNULL(packageRequest).roots()) {
      if (key.canonicalPackageBytes() == root.package().encode().asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { return zc::none; }
  }
  auto result = CompleteCompilationContextAuthority(zc::heap<Impl>(
      zc::mv(ZC_ASSERT_NONNULL(contextRoots)), zc::mv(ZC_ASSERT_NONNULL(packageRequest)),
      zc::mv(ZC_ASSERT_NONNULL(packageRootSet)), zc::mv(ZC_ASSERT_NONNULL(packageGraph)),
      zc::mv(ZC_ASSERT_NONNULL(userRoots)), zc::mv(ZC_ASSERT_NONNULL(projectedCore)),
      zc::mv(ZC_ASSERT_NONNULL(expectedRoots)), zc::mv(ZC_ASSERT_NONNULL(completeCrates)),
      zc::mv(ZC_ASSERT_NONNULL(options)), zc::mv(ZC_ASSERT_NONNULL(searchRoots)),
      zc::mv(ZC_ASSERT_NONNULL(distribution)), ZC_ASSERT_NONNULL(distributionDigest)));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return zc::mv(result);
}

bool CompleteCompilationContextAuthorityInputVerifier::verify(
    const CompleteCompilationContextAuthority& candidate,
    const CompleteCompilationContextSources& sources) {
  auto independentlyRooted = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
      sources.packageRequest, sources.projectedCoreCrates);
  auto independentlyPackageRooted =
      incremental_binding_query::PackageRootSetKey::fromVerified(sources.packageRequest);
  auto expectedOptions =
      identity::source_query::CanonicalCompilationOptions::fromVerified(sources.packageRequest);
  auto userRoots = cloneCrates(sources.userRootCrates);
  auto projectedCore = cloneCrates(sources.projectedCoreCrates);
  if (independentlyRooted == zc::none || independentlyPackageRooted == zc::none ||
      expectedOptions == zc::none || !canonicalizeCrates(userRoots) ||
      !canonicalizeCrates(projectedCore) ||
      !package::CanonicalPackageCompilationRequestProjectionVerifier::verify(
          candidate.packageRequest(), sources.packageRequest) ||
      candidate.contextRoots() != ZC_ASSERT_NONNULL(independentlyRooted) ||
      ZC_ASSERT_NONNULL(independentlyPackageRooted) != sources.packageRootSet ||
      candidate.packageRootSet() != ZC_ASSERT_NONNULL(independentlyPackageRooted) ||
      candidate.packageGraph() != sources.packageGraph) {
    return false;
  }
  auto expectedRoots = canonicalCrateUnion(userRoots.asPtr(), projectedCore.asPtr());
  zc::Vector<identity::CrateKey> packageCrates(sources.packageGraph.crates().size());
  for (const auto& stable : sources.packageGraph.crates()) {
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none) { return false; }
    packageCrates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  auto completeCrates = canonicalCrateUnion(packageCrates.asPtr(), projectedCore.asPtr());
  auto options = cloneCompilationOptions(sources.compilationOptions);
  auto searchRoots = cloneSearchRoots(sources.moduleSearchRoots);
  if (expectedRoots == zc::none || completeCrates == zc::none ||
      sources.userRootCrates.size() != sources.packageRequest.roots().size() ||
      sources.compilationOptions.size() != ZC_ASSERT_NONNULL(completeCrates).size() ||
      sources.moduleSearchRoots.size() != ZC_ASSERT_NONNULL(completeCrates).size() ||
      !canonicalizeEntries(options,
                           [](const identity::CrateKey& key) {
                             return identity::source_query::CompilationOptionsInput::encodeKey(key);
                           }) ||
      !canonicalizeEntries(
          searchRoots,
          [](const identity::CrateKey& key) {
            return incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(key);
          }) ||
      !sameCrates(candidate.userRootCrates(), userRoots.asPtr()) ||
      !sameCrates(candidate.projectedCoreCrates(), projectedCore.asPtr()) ||
      !sameCrates(candidate.expectedRootCrates(), ZC_ASSERT_NONNULL(expectedRoots).asPtr()) ||
      !sameCrates(candidate.completeCrates(), ZC_ASSERT_NONNULL(completeCrates).asPtr()) ||
      candidate.compilationOptions().size() != options.size() ||
      candidate.moduleSearchRoots().size() != searchRoots.size() ||
      candidate.coreDistributionRecord().encode().asPtr() !=
          sources.coreDistribution.record().encode().asPtr() ||
      candidate.coreDistributionDigest() != sources.coreDistribution.digest()) {
    return false;
  }
  const auto resolvedPackages = sources.packageGraph.resolvedPackages();
  const auto resolvedEdges = sources.packageGraph.resolvedPackageEdges();
  const auto selectedEdges = sources.packageGraph.selectedPackageEdges();
  if (resolvedPackages.size() == 0 || sources.packageGraph.crates().size() == 0) { return false; }
  for (const auto& requestRoot : sources.packageRequest.roots()) {
    if (!containsPackage(resolvedPackages, requestRoot.packageKey())) { return false; }
    size_t matches = 0;
    for (const auto& crate : userRoots) {
      const auto& semantic = crate.semanticOptions();
      const bool sameTarget =
          encodeTargetSpecification(crate.compilation().target()).asPtr() ==
          encodeTargetSpecification(sources.packageRequest.target().semanticProjection()).asPtr();
      if (crate.unit().kind() == identity::CompilationUnitKind::UserPackage &&
          samePackage(crate.unit().userPackage(), requestRoot.packageKey()) &&
          crate.targetKind() == requestRoot.targetKind() &&
          crate.targetName() == requestRoot.targetName() &&
          crate.compilation().domain() == identity::CompilationDomain::Target && sameTarget &&
          semantic.editionYear() == requestRoot.editionYear() &&
          semantic.useUnicode() == sources.packageRequest.languageOptions().useUnicode &&
          semantic.allowDollarIdentifiers() ==
              sources.packageRequest.languageOptions().allowDollarIdentifiers &&
          semantic.supportRegexLiterals() ==
              sources.packageRequest.languageOptions().supportRegexLiterals &&
          crate.compilation().hasBuildScriptProducer() == requestRoot.requiresBuildScript()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
  }
  for (const auto& stable : resolvedEdges) {
    auto edge = decodePackageEdge(stable);
    if (edge == zc::none ||
        !containsPackage(resolvedPackages, ZC_ASSERT_NONNULL(edge).consumer()) ||
        !containsPackage(resolvedPackages, ZC_ASSERT_NONNULL(edge).provider())) {
      return false;
    }
  }
  for (const auto& selected : selectedEdges) {
    bool found = false;
    for (const auto& resolved : resolvedEdges) {
      if (selected.canonicalEdgeBytes() == resolved.canonicalEdgeBytes()) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& stable : sources.packageGraph.crates()) {
    auto crate = decodeStableCrate(stable);
    if (crate == zc::none ||
        !containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), ZC_ASSERT_NONNULL(crate))) {
      return false;
    }
    const auto& unit = ZC_ASSERT_NONNULL(crate).unit();
    if (unit.kind() == identity::CompilationUnitKind::UserPackage) {
      if (!containsPackage(resolvedPackages, unit.userPackage())) { return false; }
    } else if (unit.toolchain().component() != identity::ToolchainComponent::Core) {
      return false;
    }
  }
  for (const auto& stable : sources.packageGraph.crateEdges()) {
    auto edge = decodeCrateEdge(stable);
    if (edge == zc::none ||
        !containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(),
                       ZC_ASSERT_NONNULL(edge).consumer()) ||
        !containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(),
                       ZC_ASSERT_NONNULL(edge).provider())) {
      return false;
    }
    const auto& origin = ZC_ASSERT_NONNULL(edge).origin();
    if (origin.kind() == identity::CrateDependencyOriginKind::UserPackage) {
      const auto& packageEdge = origin.userPackageEdge();
      if (!containsPackageEdge(selectedEdges, packageEdge) ||
          ZC_ASSERT_NONNULL(edge).consumer().unit().kind() !=
              identity::CompilationUnitKind::UserPackage ||
          ZC_ASSERT_NONNULL(edge).provider().unit().kind() !=
              identity::CompilationUnitKind::UserPackage ||
          !samePackage(ZC_ASSERT_NONNULL(edge).consumer().unit().userPackage(),
                       packageEdge.consumer()) ||
          !samePackage(ZC_ASSERT_NONNULL(edge).provider().unit().userPackage(),
                       packageEdge.provider())) {
        return false;
      }
    } else if (ZC_ASSERT_NONNULL(edge).provider().unit().kind() !=
                   identity::CompilationUnitKind::Toolchain ||
               ZC_ASSERT_NONNULL(edge).provider().unit().toolchain().component() !=
                   identity::ToolchainComponent::Core) {
      return false;
    }
  }
  for (const auto& root : userRoots) {
    if (!containsCrate(packageCrates.asPtr(), root) ||
        root.unit().kind() != identity::CompilationUnitKind::UserPackage) {
      return false;
    }
  }
  for (const auto& core : projectedCore) {
    if (core.unit().kind() != identity::CompilationUnitKind::Toolchain ||
        core.unit().toolchain().component() != identity::ToolchainComponent::Core ||
        !containsCrate(ZC_ASSERT_NONNULL(expectedRoots).asPtr(), core) ||
        !containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), core)) {
      return false;
    }
  }
  for (size_t index = 0; index < options.size(); ++index) {
    if (!containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), options[index].key()) ||
        !options[index].value().matchesCrate(options[index].key()) ||
        options[index].value() != ZC_ASSERT_NONNULL(expectedOptions) ||
        identity::source_query::CompilationOptionsInput::encodeKey(
            candidate.compilationOptions()[index].key())
                .asPtr() !=
            identity::source_query::CompilationOptionsInput::encodeKey(options[index].key())
                .asPtr() ||
        identity::source_query::CompilationOptionsInput::encodeValue(
            candidate.compilationOptions()[index].value())
                .asPtr() !=
            identity::source_query::CompilationOptionsInput::encodeValue(options[index].value())
                .asPtr()) {
      return false;
    }
  }
  for (size_t index = 0; index < searchRoots.size(); ++index) {
    if (!containsCrate(ZC_ASSERT_NONNULL(completeCrates).asPtr(), searchRoots[index].key()) ||
        !sameCrate(searchRoots[index].key(), searchRoots[index].value().crate()) ||
        searchRoots[index].value().roots().size() == 0) {
      return false;
    }
    bool foundCoreDistribution = false;
    for (const auto& root : searchRoots[index].value().roots()) {
      if (!sameCrate(root.crate(), searchRoots[index].key())) { return false; }
      if (root.kind() == binder::ModuleSearchRootKind::ToolchainCore) {
        if (searchRoots[index].key().unit().kind() != identity::CompilationUnitKind::Toolchain ||
            searchRoots[index].key().unit().toolchain().component() !=
                identity::ToolchainComponent::Core ||
            root.toolchainCoreDistributionDigest() != sources.coreDistribution.digest()) {
          return false;
        }
        foundCoreDistribution = true;
      }
    }
    const bool isCore =
        searchRoots[index].key().unit().kind() == identity::CompilationUnitKind::Toolchain &&
        searchRoots[index].key().unit().toolchain().component() ==
            identity::ToolchainComponent::Core;
    if (foundCoreDistribution != isCore ||
        incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(
            candidate.moduleSearchRoots()[index].key())
                .asPtr() != incremental_module_resolution_query::ModuleSearchRootsInput::encodeKey(
                                searchRoots[index].key())
                                .asPtr() ||
        incremental_module_resolution_query::ModuleSearchRootsInput::encodeValue(
            candidate.moduleSearchRoots()[index].value())
                .asPtr() !=
            incremental_module_resolution_query::ModuleSearchRootsInput::encodeValue(
                searchRoots[index].value())
                .asPtr()) {
      return false;
    }
  }
  auto digest = source::core::computeCoreDistributionDigest(sources.coreDistribution.record());
  return digest != zc::none && ZC_ASSERT_NONNULL(digest) == sources.coreDistribution.digest();
}

zc::Maybe<identity::Sha256Digest> computeCompleteCompilationContextWitness(
    const CompleteCompilationContextAuthority& authority) {
  auto bytes = authority.encodeCanonical();
  auto framed = frame(kCompleteContextWitnessDomain, bytes.asPtr());
  return identity::sha256(framed.asPtr());
}

zc::Maybe<binder::CanonicalInputPayloadDigest> computeCanonicalInputPayloadDigest(
    zc::StringPtr transactionDomain, zc::ArrayPtr<const uint8_t> payloadBytes) {
  if (transactionDomain.size() == 0 || payloadBytes.size() == 0) { return zc::none; }
  identity::CanonicalEncoder payload;
  payload.encodeByteString(transactionDomain.asBytes());
  payload.encodeByteString(payloadBytes);
  auto framed = frame(kInputTransactionDigestDomain, payload.finish().asPtr());
  auto digest = identity::sha256(framed.asPtr());
  if (digest == zc::none) { return zc::none; }
  return binder::CanonicalInputPayloadDigest::fromBytes(ZC_ASSERT_NONNULL(digest).bytes());
}

namespace {

zc::Array<uint8_t> encodeTransactionWitnessKey(
    const incremental_binding_query::CompilationRootSetQueryKey& key) {
  return key.encodeCanonical();
}

zc::Maybe<incremental_binding_query::CompilationRootSetQueryKey> decodeTransactionWitnessKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> encodeTransactionWitnessValue(const binder::CanonicalInputPayloadDigest& value) {
  return zc::heapArray<uint8_t>(value.bytes());
}

zc::Maybe<binder::CanonicalInputPayloadDigest> decodeTransactionWitnessValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::CanonicalInputPayloadDigest::fromBytes(bytes);
}

}  // namespace

#define ZOM_DEFINE_TRANSACTION_WITNESS_INPUT(TypeName)                                  \
  zc::Array<uint8_t> TypeName::encodeKey(const Key& key) {                              \
    return encodeTransactionWitnessKey(key);                                            \
  }                                                                                     \
  zc::Maybe<TypeName::Key> TypeName::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {     \
    return decodeTransactionWitnessKey(bytes);                                          \
  }                                                                                     \
  zc::Array<uint8_t> TypeName::encodeValue(const Value& value) {                        \
    return encodeTransactionWitnessValue(value);                                        \
  }                                                                                     \
  zc::Maybe<TypeName::Value> TypeName::decodeValue(zc::ArrayPtr<const uint8_t> bytes) { \
    return decodeTransactionWitnessValue(bytes);                                        \
  }

ZOM_DEFINE_TRANSACTION_WITNESS_INPUT(CoreDistributionTransactionWitnessInput)
ZOM_DEFINE_TRANSACTION_WITNESS_INPUT(ModuleStructureTransactionWitnessInput)
ZOM_DEFINE_TRANSACTION_WITNESS_INPUT(ContextualIdentityAuthorityTransactionWitnessInput)

#undef ZOM_DEFINE_TRANSACTION_WITNESS_INPUT

namespace {

zc::Maybe<identity::Sha256Digest> computeFinalSnapshotSuccessWitness(
    const query::QuerySnapshot& snapshot,
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots) {
  auto distributionWitness =
      snapshot.probeInput<CoreDistributionTransactionWitnessInput>(contextRoots);
  auto structureWitness = snapshot.probeInput<ModuleStructureTransactionWitnessInput>(contextRoots);
  auto identityWitness =
      snapshot.probeInput<ContextualIdentityAuthorityTransactionWitnessInput>(contextRoots);
  auto authority = snapshot.probeInput<CompleteCompilationContextAuthorityInput>(contextRoots);
  auto graph = snapshot.get<ModuleGraph>(contextRoots);
  auto scc = snapshot.get<ModuleGraphScc>(contextRoots);
  auto authorityMap =
      snapshot.probeInput<incremental_binding_query::ActiveDefinitionAuthorityReadyInput>(
          contextRoots);
  auto readiness =
      snapshot.probeInput<incremental_binding_query::CompleteRootIdentityReadinessInput>(
          contextRoots);
  if (distributionWitness.isRuntimeFailure() || structureWitness.isRuntimeFailure() ||
      identityWitness.isRuntimeFailure() || authority.isRuntimeFailure() ||
      graph.isRuntimeFailure() || scc.isRuntimeFailure() || authorityMap.isRuntimeFailure() ||
      readiness.isRuntimeFailure() || distributionWitness.kind() != query::QueryValueKind::Value ||
      structureWitness.kind() != query::QueryValueKind::Value ||
      identityWitness.kind() != query::QueryValueKind::Value ||
      authority.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || scc.kind() != query::QueryValueKind::Value ||
      authorityMap.kind() != query::QueryValueKind::Value ||
      readiness.kind() != query::QueryValueKind::Value ||
      authority.value().contextRoots() != contextRoots ||
      readiness.value().contextRoots() != contextRoots || graph.value().modules().size() == 0 ||
      scc.value().hasCycle(graph.value())) {
    return zc::none;
  }

  identity::CanonicalEncoder payload;
  auto authorityBytes = authority.value().encodeCanonical();
  auto graphBytes = graph.value().encodeCanonical();
  auto sccBytes = scc.value().encodeCanonical();
  auto authorityMapBytes =
      incremental_binding_query::ActiveDefinitionAuthorityReadyInput::encodeValue(
          authorityMap.value());
  auto readinessBytes = readiness.value().encodeCanonical();
  payload.encodeByteString(distributionWitness.value().bytes());
  payload.encodeByteString(structureWitness.value().bytes());
  payload.encodeByteString(identityWitness.value().bytes());
  payload.encodeByteString(authorityBytes.asPtr());
  payload.encodeByteString(graphBytes.asPtr());
  payload.encodeByteString(sccBytes.asPtr());
  payload.encodeByteString(authorityMapBytes.asPtr());
  payload.encodeByteString(readinessBytes.asPtr());

  auto framed = frame(kFinalSnapshotWitnessDomain, payload.finish().asPtr());
  return identity::sha256(framed.asPtr());
}

zc::Maybe<identity::Sha256Digest> computeFinalSnapshotFailureWitness(
    const query::QuerySnapshot& snapshot,
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots) {
  auto distributionWitness =
      snapshot.probeInput<CoreDistributionTransactionWitnessInput>(contextRoots);
  auto structureWitness = snapshot.probeInput<ModuleStructureTransactionWitnessInput>(contextRoots);
  auto identityWitness =
      snapshot.probeInput<ContextualIdentityAuthorityTransactionWitnessInput>(contextRoots);
  auto authority = snapshot.probeInput<CompleteCompilationContextAuthorityInput>(contextRoots);
  const bool identityWitnessMissing =
      identityWitness.isRuntimeFailure() &&
      identityWitness.runtimeFailure() == query::QueryRuntimeFailure::MissingInput;
  if (distributionWitness.isRuntimeFailure() || structureWitness.isRuntimeFailure() ||
      (!identityWitnessMissing && identityWitness.isRuntimeFailure()) ||
      authority.isRuntimeFailure() || distributionWitness.kind() != query::QueryValueKind::Value ||
      structureWitness.kind() != query::QueryValueKind::Value ||
      (!identityWitnessMissing && identityWitness.kind() != query::QueryValueKind::Value &&
       identityWitness.kind() != query::QueryValueKind::Absence) ||
      authority.kind() != query::QueryValueKind::Value ||
      authority.value().contextRoots() != contextRoots) {
    return zc::none;
  }

  auto rootsBytes = contextRoots.encodeCanonical();
  auto authorityBytes = authority.value().encodeCanonical();
  const auto basePayload = [&]() {
    identity::CanonicalEncoder payload;
    payload.encodeByteString(rootsBytes.asPtr());
    payload.encodeByteString(distributionWitness.value().bytes());
    payload.encodeByteString(structureWitness.value().bytes());
    if (!identityWitnessMissing && identityWitness.kind() == query::QueryValueKind::Value) {
      payload.encodeByteString("value"_zcc.asBytes());
      payload.encodeByteString(identityWitness.value().bytes());
    } else {
      payload.encodeByteString("absence"_zcc.asBytes());
    }
    payload.encodeByteString(authorityBytes.asPtr());
    return payload;
  };

  const auto encodeRuntimeFailure = [](query::QueryRuntimeFailure failure) {
    auto bytes = zc::heapArray<uint8_t>(1);
    bytes[0] = static_cast<uint8_t>(failure);
    return bytes;
  };

  auto graph = snapshot.get<ModuleGraph>(contextRoots);
  if (graph.isRuntimeFailure()) {
    auto payload = basePayload();
    auto failure = encodeRuntimeFailure(graph.runtimeFailure());
    payload.encodeByteString(failure.asPtr());
    auto framed = frame(kFinalFailureGraphRuntimeWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    auto payload = basePayload();
    payload.encodeByteString(graph.semanticFailureBytes());
    auto framed = frame(kFinalFailureGraphWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (graph.kind() != query::QueryValueKind::Value) {
    auto payload = basePayload();
    auto framed = frame(kFinalFailureGraphAbsenceWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }

  auto graphBytes = graph.value().encodeCanonical();
  auto scc = snapshot.get<ModuleGraphScc>(contextRoots);
  if (scc.isRuntimeFailure()) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    auto failure = encodeRuntimeFailure(scc.runtimeFailure());
    payload.encodeByteString(failure.asPtr());
    auto framed = frame(kFinalFailureSccRuntimeWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (scc.kind() == query::QueryValueKind::SemanticFailure) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(scc.semanticFailureBytes());
    auto framed = frame(kFinalFailureSccWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (scc.kind() != query::QueryValueKind::Value) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    auto framed = frame(kFinalFailureSccAbsenceWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }

  auto sccBytes = scc.value().encodeCanonical();
  if (scc.value().hasCycle(graph.value())) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    auto framed = frame(kFinalFailureSccWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }

  auto authorityMap =
      snapshot.probeInput<incremental_binding_query::ActiveDefinitionAuthorityReadyInput>(
          contextRoots);
  if (authorityMap.isRuntimeFailure()) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    auto failure = encodeRuntimeFailure(authorityMap.runtimeFailure());
    payload.encodeByteString(failure.asPtr());
    auto framed = frame(kFinalFailureAuthorityRuntimeWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (authorityMap.kind() == query::QueryValueKind::SemanticFailure) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    payload.encodeByteString(authorityMap.semanticFailureBytes());
    auto framed = frame(kFinalFailureAuthorityWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (authorityMap.kind() != query::QueryValueKind::Value) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    auto framed = frame(kFinalFailureAuthorityAbsenceWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }

  auto authorityMapBytes =
      incremental_binding_query::ActiveDefinitionAuthorityReadyInput::encodeValue(
          authorityMap.value());
  auto readiness =
      snapshot.probeInput<incremental_binding_query::CompleteRootIdentityReadinessInput>(
          contextRoots);
  if (readiness.isRuntimeFailure()) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    payload.encodeByteString(authorityMapBytes.asPtr());
    auto failure = encodeRuntimeFailure(readiness.runtimeFailure());
    payload.encodeByteString(failure.asPtr());
    auto framed = frame(kFinalFailureReadinessRuntimeWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (readiness.kind() == query::QueryValueKind::SemanticFailure) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    payload.encodeByteString(authorityMapBytes.asPtr());
    payload.encodeByteString(readiness.semanticFailureBytes());
    auto framed = frame(kFinalFailureReadinessWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (readiness.kind() != query::QueryValueKind::Value) {
    auto payload = basePayload();
    payload.encodeByteString(graphBytes.asPtr());
    payload.encodeByteString(sccBytes.asPtr());
    payload.encodeByteString(authorityMapBytes.asPtr());
    auto framed = frame(kFinalFailureReadinessAbsenceWitnessDomain, payload.finish().asPtr());
    return identity::sha256(framed.asPtr());
  }
  if (readiness.value().contextRoots() != contextRoots) { return zc::none; }
  return zc::none;
}

}  // namespace

zc::Maybe<identity::Sha256Digest> computeFinalSnapshotWitness(
    const query::QuerySnapshot& snapshot,
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots) {
  auto success = computeFinalSnapshotSuccessWitness(snapshot, contextRoots);
  if (success != zc::none) { return success; }
  return computeFinalSnapshotFailureWitness(snapshot, contextRoots);
}

zc::Array<uint8_t> CompleteCompilationContextAuthorityInput::encodeKey(const Key& key) {
  return key.encodeCanonical();
}
zc::Maybe<CompleteCompilationContextAuthorityInput::Key>
CompleteCompilationContextAuthorityInput::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}
zc::Array<uint8_t> CompleteCompilationContextAuthorityInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}
zc::Maybe<CompleteCompilationContextAuthorityInput::Value>
CompleteCompilationContextAuthorityInput::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return Value::decodeCanonical(bytes);
}
query::FinalAuthorityCheck CompleteCompilationContextAuthorityInput::verifyFinalAuthority(
    const query::QuerySnapshot& snapshot, const Key& key, const Value& value,
    const identity::Sha256Digest& witness) {
  if (key != value.contextRoots()) { return query::FinalAuthorityCheck::Rejected; }
  auto stored = snapshot.probeInput<CompleteCompilationContextAuthorityInput>(key);
  if (stored.isRuntimeFailure() || stored.kind() != query::QueryValueKind::Value ||
      stored.value() != value) {
    return query::FinalAuthorityCheck::Rejected;
  }
  auto success = computeFinalSnapshotSuccessWitness(snapshot, key);
  if (success != zc::none && ZC_ASSERT_NONNULL(success) == witness) {
    return query::FinalAuthorityCheck::VerifiedSuccess;
  }
  auto failure = computeFinalSnapshotFailureWitness(snapshot, key);
  if (failure != zc::none && ZC_ASSERT_NONNULL(failure) == witness) {
    return query::FinalAuthorityCheck::VerifiedFailure;
  }
  return query::FinalAuthorityCheck::Rejected;
}

SelectedModuleRecord::SelectedModuleRecord(identity::ModuleKey&& module,
                                           identity::SourceFileKey&& source) noexcept
    : moduleValue(zc::mv(module)), sourceValue(zc::mv(source)) {}

SelectedModuleRecord SelectedModuleRecord::clone() const {
  return SelectedModuleRecord(moduleValue.clone(), sourceValue.clone());
}

const identity::ModuleKey& SelectedModuleRecord::module() const noexcept { return moduleValue; }

const identity::SourceFileKey& SelectedModuleRecord::source() const noexcept { return sourceValue; }

zc::Array<uint8_t> SelectedModuleRecord::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  const auto moduleBytes = moduleValue.encode();
  const auto sourceBytes = sourceValue.encode();
  encoder.encodeByteString(moduleBytes.asPtr());
  encoder.encodeByteString(sourceBytes.asPtr());
  return encoder.finish();
}

SelectedModuleCatalog::SelectedModuleCatalog(identity::CrateKey&& crate,
                                             zc::Vector<SelectedModuleRecord>&& entries) noexcept
    : crateValue(zc::mv(crate)), entryValues(zc::mv(entries)) {}

zc::Maybe<SelectedModuleCatalog> SelectedModuleCatalog::from(
    identity::CrateKey&& crate, zc::Vector<SelectedModuleRecord>&& entries) {
  if (entries.size() == 0 || entries.size() > kMaximumModules) { return zc::none; }
  sortByBytes(entries, [](const SelectedModuleRecord& entry) { return entry.module().encode(); });
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!sameCrate(entries[index].module().crate(), crate) ||
        !entries[index].source().belongsTo(crate)) {
      return zc::none;
    }
    if (index != 0 && sameModule(entries[index - 1].module(), entries[index].module())) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (sameSource(entries[prior].source(), entries[index].source())) { return zc::none; }
    }
  }
  SelectedModuleCatalog result(zc::mv(crate), zc::mv(entries));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<SelectedModuleCatalog> SelectedModuleCatalog::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kSelectedCatalogValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto crateBytes = decoder.decodeByteString(kMaximumCrateKeyBytes);
  auto count = decoder.decodeSequenceSize(kMaximumModules);
  if (crateBytes == zc::none || count == zc::none || ZC_ASSERT_NONNULL(count) == 0) {
    return zc::none;
  }
  auto crate = decodeCrate(ZC_ASSERT_NONNULL(crateBytes).asPtr());
  if (crate == zc::none) { return zc::none; }
  zc::Vector<SelectedModuleRecord> entries(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto recordBytes =
        decoder.decodeByteString(kMaximumModuleKeyBytes + kMaximumSourceKeyBytes + 16);
    if (recordBytes == zc::none) { return zc::none; }
    identity::CanonicalDecoder recordDecoder(ZC_ASSERT_NONNULL(recordBytes).asPtr());
    auto moduleBytes = recordDecoder.decodeByteString(kMaximumModuleKeyBytes);
    auto sourceBytes = recordDecoder.decodeByteString(kMaximumSourceKeyBytes);
    if (moduleBytes == zc::none || sourceBytes == zc::none || !recordDecoder.finished()) {
      return zc::none;
    }
    auto module = decodeModule(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
    auto source = decodeSource(ZC_ASSERT_NONNULL(sourceBytes).asPtr());
    if (module == zc::none || source == zc::none) { return zc::none; }
    SelectedModuleRecord record(zc::mv(ZC_ASSERT_NONNULL(module)),
                                zc::mv(ZC_ASSERT_NONNULL(source)));
    if (record.encodeCanonical().asPtr() != ZC_ASSERT_NONNULL(recordBytes).asPtr()) {
      return zc::none;
    }
    entries.add(zc::mv(record));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(crate)), zc::mv(entries));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

SelectedModuleCatalog SelectedModuleCatalog::clone() const {
  zc::Vector<SelectedModuleRecord> entries(entryValues.size());
  for (const auto& entry : entryValues) { entries.add(entry.clone()); }
  return SelectedModuleCatalog(crateValue.clone(), zc::mv(entries));
}

const identity::CrateKey& SelectedModuleCatalog::crate() const noexcept { return crateValue; }

zc::ArrayPtr<const SelectedModuleRecord> SelectedModuleCatalog::entries() const noexcept {
  return entryValues.asPtr();
}

zc::Array<uint8_t> SelectedModuleCatalog::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  const auto crateBytes = crateValue.encode();
  payload.encodeByteString(crateBytes.asPtr());
  payload.encodeSequenceSize(entryValues.size());
  for (const auto& entry : entryValues) {
    const auto bytes = entry.encodeCanonical();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kSelectedCatalogValueDomain, payload.finish().asPtr());
}

DetachedModuleDependencySite::DetachedModuleDependencySite(
    DetachedModuleDependencySiteKind kind, zc::Vector<identity::ModulePathSegment>&& normalizedPath,
    uint32_t schemaPreorderOrdinal) noexcept
    : kindValue(kind),
      normalizedPathValue(zc::mv(normalizedPath)),
      schemaPreorderOrdinalValue(schemaPreorderOrdinal) {}

zc::Maybe<DetachedModuleDependencySite> DetachedModuleDependencySite::from(
    DetachedModuleDependencySiteKind kind, zc::Vector<identity::ModulePathSegment>&& normalizedPath,
    uint32_t schemaPreorderOrdinal) {
  if (normalizedPath.size() == 0 || normalizedPath.size() > 256 ||
      (kind != DetachedModuleDependencySiteKind::Import &&
       kind != DetachedModuleDependencySiteKind::ForeignReexport &&
       kind != DetachedModuleDependencySiteKind::ModuleAlias)) {
    return zc::none;
  }
  DetachedModuleDependencySite result(kind, zc::mv(normalizedPath), schemaPreorderOrdinal);
  if (result.encodeCanonical().size() > kMaximumModuleKeyBytes) { return zc::none; }
  return zc::mv(result);
}

DetachedModuleDependencySite DetachedModuleDependencySite::clone() const {
  zc::Vector<identity::ModulePathSegment> path(normalizedPathValue.size());
  for (const auto& segment : normalizedPathValue) { path.add(segment.clone()); }
  return DetachedModuleDependencySite(kindValue, zc::mv(path), schemaPreorderOrdinalValue);
}

DetachedModuleDependencySiteKind DetachedModuleDependencySite::kind() const noexcept {
  return kindValue;
}

zc::ArrayPtr<const identity::ModulePathSegment> DetachedModuleDependencySite::normalizedPath()
    const noexcept {
  return normalizedPathValue.asPtr();
}

uint32_t DetachedModuleDependencySite::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}

zc::Array<uint8_t> DetachedModuleDependencySite::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(kindValue));
  const auto pathBytes = encodePath(normalizedPathValue.asPtr());
  encoder.encodeByteString(pathBytes.asPtr());
  encoder.encodeUint32(schemaPreorderOrdinalValue);
  return encoder.finish();
}

DetachedModuleDependencySiteSet::DetachedModuleDependencySiteSet(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    const identity::Sha256Digest& sourceDigest,
    zc::Vector<DetachedModuleDependencySite>&& sites) noexcept
    : moduleValue(zc::mv(module)),
      sourceValue(zc::mv(source)),
      sourceDigestValue(sourceDigest),
      siteValues(zc::mv(sites)) {}

zc::Maybe<DetachedModuleDependencySiteSet> DetachedModuleDependencySiteSet::from(
    identity::ModuleKey&& module, identity::SourceFileKey&& source,
    const identity::Sha256Digest& sourceDigest, zc::Vector<DetachedModuleDependencySite>&& sites) {
  if (!source.belongsTo(module.crate()) || sites.size() > kMaximumDependencySites) {
    return zc::none;
  }
  for (size_t index = 1; index < sites.size(); ++index) {
    auto current = zc::mv(sites[index]);
    const auto currentBytes = current.encodeCanonical();
    size_t insertion = index;
    while (insertion != 0) {
      const auto& prior = sites[insertion - 1];
      if (current.schemaPreorderOrdinal() > prior.schemaPreorderOrdinal() ||
          (current.schemaPreorderOrdinal() == prior.schemaPreorderOrdinal() &&
           compareBytes(currentBytes.asPtr(), prior.encodeCanonical().asPtr()) >= 0)) {
        break;
      }
      sites[insertion] = zc::mv(sites[insertion - 1]);
      --insertion;
    }
    sites[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < sites.size(); ++index) {
    if (sites[index - 1].schemaPreorderOrdinal() == sites[index].schemaPreorderOrdinal()) {
      return zc::none;
    }
  }
  DetachedModuleDependencySiteSet result(zc::mv(module), zc::mv(source), sourceDigest,
                                         zc::mv(sites));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<DetachedModuleDependencySiteSet> DetachedModuleDependencySiteSet::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kDependencySiteValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto moduleBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  auto sourceBytes = decoder.decodeByteString(kMaximumSourceKeyBytes);
  auto digest = decoder.decodeDigest();
  auto count = decoder.decodeSequenceSize(kMaximumDependencySites);
  if (moduleBytes == zc::none || sourceBytes == zc::none || digest == zc::none ||
      count == zc::none) {
    return zc::none;
  }
  auto module = decodeModule(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
  auto source = decodeSource(ZC_ASSERT_NONNULL(sourceBytes).asPtr());
  if (module == zc::none || source == zc::none) { return zc::none; }
  zc::Vector<DetachedModuleDependencySite> sites(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto siteBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (siteBytes == zc::none) { return zc::none; }
    auto site = decodeSite(ZC_ASSERT_NONNULL(siteBytes).asPtr());
    if (site == zc::none) { return zc::none; }
    sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(module)), zc::mv(ZC_ASSERT_NONNULL(source)),
                     ZC_ASSERT_NONNULL(digest), zc::mv(sites));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

DetachedModuleDependencySiteSet DetachedModuleDependencySiteSet::clone() const {
  zc::Vector<DetachedModuleDependencySite> sites(siteValues.size());
  for (const auto& site : siteValues) { sites.add(site.clone()); }
  return DetachedModuleDependencySiteSet(moduleValue.clone(), sourceValue.clone(),
                                         sourceDigestValue, zc::mv(sites));
}

const identity::ModuleKey& DetachedModuleDependencySiteSet::module() const noexcept {
  return moduleValue;
}

const identity::SourceFileKey& DetachedModuleDependencySiteSet::source() const noexcept {
  return sourceValue;
}

const identity::Sha256Digest& DetachedModuleDependencySiteSet::sourceDigest() const noexcept {
  return sourceDigestValue;
}

zc::ArrayPtr<const DetachedModuleDependencySite> DetachedModuleDependencySiteSet::sites()
    const noexcept {
  return siteValues.asPtr();
}

zc::Array<uint8_t> DetachedModuleDependencySiteSet::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  const auto moduleBytes = moduleValue.encode();
  const auto sourceBytes = sourceValue.encode();
  payload.encodeByteString(moduleBytes.asPtr());
  payload.encodeByteString(sourceBytes.asPtr());
  payload.encodeDigest(sourceDigestValue);
  payload.encodeSequenceSize(siteValues.size());
  for (const auto& site : siteValues) {
    const auto bytes = site.encodeCanonical();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kDependencySiteValueDomain, payload.finish().asPtr());
}

zc::Array<uint8_t> SelectedModuleCatalogInput::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<SelectedModuleCatalogInput::Key> SelectedModuleCatalogInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeCrate(bytes);
}

zc::Array<uint8_t> SelectedModuleCatalogInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<SelectedModuleCatalogInput::Value> SelectedModuleCatalogInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return SelectedModuleCatalog::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleDependencySiteInput::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencySiteInput::Key> ModuleDependencySiteInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencySiteInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencySiteInput::Value> ModuleDependencySiteInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return DetachedModuleDependencySiteSet::decodeCanonical(bytes);
}

ActiveModuleSetRecord::ActiveModuleSetRecord(zc::Vector<identity::ModuleKey>&& modules) noexcept
    : moduleValues(zc::mv(modules)) {}

zc::Maybe<ActiveModuleSetRecord> ActiveModuleSetRecord::from(
    zc::Vector<identity::ModuleKey>&& modules) {
  if (modules.size() == 0 || modules.size() > kMaximumModules) { return zc::none; }
  sortByBytes(modules, [](const identity::ModuleKey& module) { return module.encode(); });
  for (size_t index = 1; index < modules.size(); ++index) {
    if (sameModule(modules[index - 1], modules[index])) { return zc::none; }
  }
  ActiveModuleSetRecord result(zc::mv(modules));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ActiveModuleSetRecord> ActiveModuleSetRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kActiveModulesValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto count = decoder.decodeSequenceSize(kMaximumModules);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<identity::ModuleKey> modules(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    auto module = decodeModule(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (module == zc::none) { return zc::none; }
    modules.add(zc::mv(ZC_ASSERT_NONNULL(module)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(modules));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ActiveModuleSetRecord ActiveModuleSetRecord::clone() const {
  zc::Vector<identity::ModuleKey> modules(moduleValues.size());
  for (const auto& module : moduleValues) { modules.add(module.clone()); }
  return ActiveModuleSetRecord(zc::mv(modules));
}

zc::ArrayPtr<const identity::ModuleKey> ActiveModuleSetRecord::modules() const noexcept {
  return moduleValues.asPtr();
}

zc::Array<uint8_t> ActiveModuleSetRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeSequenceSize(moduleValues.size());
  for (const auto& module : moduleValues) {
    const auto bytes = module.encode();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kActiveModulesValueDomain, payload.finish().asPtr());
}

ModuleDependencyRequestSetRecord::ModuleDependencyRequestSetRecord(
    zc::Vector<identity::ModuleResolutionKey>&& requests) noexcept
    : requestValues(zc::mv(requests)) {}

zc::Maybe<ModuleDependencyRequestSetRecord> ModuleDependencyRequestSetRecord::from(
    zc::Vector<identity::ModuleResolutionKey>&& requests) {
  if (requests.size() > kMaximumDependencySites) { return zc::none; }
  sortByBytes(requests,
              [](const identity::ModuleResolutionKey& request) { return request.encode(); });
  for (size_t index = 1; index < requests.size(); ++index) {
    if (requests[index - 1].encode().asPtr() == requests[index].encode().asPtr()) {
      return zc::none;
    }
  }
  ModuleDependencyRequestSetRecord result(zc::mv(requests));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleDependencyRequestSetRecord> ModuleDependencyRequestSetRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kDependencyRequestsValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto count = decoder.decodeSequenceSize(kMaximumDependencySites);
  if (count == zc::none) { return zc::none; }
  zc::Vector<identity::ModuleResolutionKey> requests(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    auto request =
        identity::ModuleResolutionKey::decodeCanonical(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (request == zc::none) { return zc::none; }
    requests.add(zc::mv(ZC_ASSERT_NONNULL(request)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(requests));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleDependencyRequestSetRecord ModuleDependencyRequestSetRecord::clone() const {
  zc::Vector<identity::ModuleResolutionKey> requests(requestValues.size());
  for (const auto& request : requestValues) { requests.add(request.clone()); }
  return ModuleDependencyRequestSetRecord(zc::mv(requests));
}

zc::ArrayPtr<const identity::ModuleResolutionKey> ModuleDependencyRequestSetRecord::requests()
    const noexcept {
  return requestValues.asPtr();
}

zc::Array<uint8_t> ModuleDependencyRequestSetRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeSequenceSize(requestValues.size());
  for (const auto& request : requestValues) {
    const auto bytes = request.encode();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kDependencyRequestsValueDomain, payload.finish().asPtr());
}

ModuleDependencySetRecord::ModuleDependencySetRecord(
    zc::Vector<identity::ModuleKey>&& dependencies) noexcept
    : dependencyValues(zc::mv(dependencies)) {}

zc::Maybe<ModuleDependencySetRecord> ModuleDependencySetRecord::from(
    zc::Vector<identity::ModuleKey>&& dependencies) {
  if (dependencies.size() > kMaximumModules) { return zc::none; }
  sortByBytes(dependencies, [](const identity::ModuleKey& module) { return module.encode(); });
  zc::Vector<identity::ModuleKey> unique(dependencies.size());
  for (auto& dependency : dependencies) {
    if (unique.size() == 0 || !sameModule(unique.back(), dependency)) {
      unique.add(zc::mv(dependency));
    }
  }
  ModuleDependencySetRecord result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleDependencySetRecord> ModuleDependencySetRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kDependenciesValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto count = decoder.decodeSequenceSize(kMaximumModules);
  if (count == zc::none) { return zc::none; }
  zc::Vector<identity::ModuleKey> dependencies(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    auto dependency = decodeModule(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (dependency == zc::none) { return zc::none; }
    dependencies.add(zc::mv(ZC_ASSERT_NONNULL(dependency)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(dependencies));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleDependencySetRecord ModuleDependencySetRecord::clone() const {
  zc::Vector<identity::ModuleKey> dependencies(dependencyValues.size());
  for (const auto& dependency : dependencyValues) { dependencies.add(dependency.clone()); }
  return ModuleDependencySetRecord(zc::mv(dependencies));
}

zc::ArrayPtr<const identity::ModuleKey> ModuleDependencySetRecord::dependencies() const noexcept {
  return dependencyValues.asPtr();
}

zc::Array<uint8_t> ModuleDependencySetRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeSequenceSize(dependencyValues.size());
  for (const auto& dependency : dependencyValues) {
    const auto bytes = dependency.encode();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kDependenciesValueDomain, payload.finish().asPtr());
}

ModuleDependencyFailureRecord::ModuleDependencyFailureRecord(
    ModuleDependencyFailureKind kind, identity::ModuleResolutionKey&& request,
    zc::Vector<identity::ModuleKey>&& candidates) noexcept
    : kindValue(kind), requestValue(zc::mv(request)), candidateValues(zc::mv(candidates)) {}

zc::Maybe<ModuleDependencyFailureRecord> ModuleDependencyFailureRecord::missing(
    identity::ModuleResolutionKey&& request) {
  if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) { return zc::none; }
  zc::Vector<identity::ModuleKey> candidates;
  return ModuleDependencyFailureRecord(ModuleDependencyFailureKind::Missing, zc::mv(request),
                                       zc::mv(candidates));
}

zc::Maybe<ModuleDependencyFailureRecord> ModuleDependencyFailureRecord::ambiguous(
    identity::ModuleResolutionKey&& request, zc::Vector<identity::ModuleKey>&& candidates) {
  if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude ||
      candidates.size() < 2) {
    return zc::none;
  }
  sortByBytes(candidates, [](const identity::ModuleKey& module) { return module.encode(); });
  for (size_t index = 1; index < candidates.size(); ++index) {
    if (sameModule(candidates[index - 1], candidates[index])) { return zc::none; }
  }
  return ModuleDependencyFailureRecord(ModuleDependencyFailureKind::Ambiguous, zc::mv(request),
                                       zc::mv(candidates));
}

zc::Maybe<ModuleDependencyFailureRecord> ModuleDependencyFailureRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kDependencyFailureDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto kind = decoder.decodeUint8();
  auto requestBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (kind == zc::none || requestBytes == zc::none) { return zc::none; }
  auto request =
      identity::ModuleResolutionKey::decodeCanonical(ZC_ASSERT_NONNULL(requestBytes).asPtr());
  if (request == zc::none) { return zc::none; }
  zc::Maybe<ModuleDependencyFailureRecord> result;
  if (ZC_ASSERT_NONNULL(kind) == static_cast<uint8_t>(ModuleDependencyFailureKind::Missing)) {
    result = missing(zc::mv(ZC_ASSERT_NONNULL(request)));
  } else if (ZC_ASSERT_NONNULL(kind) ==
             static_cast<uint8_t>(ModuleDependencyFailureKind::Ambiguous)) {
    auto count = decoder.decodeSequenceSize(kMaximumModules);
    if (count == zc::none || ZC_ASSERT_NONNULL(count) < 2) { return zc::none; }
    zc::Vector<identity::ModuleKey> candidates(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
    for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
      auto encoded = decoder.decodeByteString(kMaximumModuleKeyBytes);
      if (encoded == zc::none) { return zc::none; }
      auto candidate = decodeModule(ZC_ASSERT_NONNULL(encoded).asPtr());
      if (candidate == zc::none) { return zc::none; }
      candidates.add(zc::mv(ZC_ASSERT_NONNULL(candidate)));
    }
    result = ambiguous(zc::mv(ZC_ASSERT_NONNULL(request)), zc::mv(candidates));
  } else {
    return zc::none;
  }
  if (!decoder.finished() || result == zc::none ||
      ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleDependencyFailureRecord ModuleDependencyFailureRecord::clone() const {
  zc::Vector<identity::ModuleKey> candidates(candidateValues.size());
  for (const auto& candidate : candidateValues) { candidates.add(candidate.clone()); }
  return ModuleDependencyFailureRecord(kindValue, requestValue.clone(), zc::mv(candidates));
}

ModuleDependencyFailureKind ModuleDependencyFailureRecord::kind() const noexcept {
  return kindValue;
}

const identity::ModuleResolutionKey& ModuleDependencyFailureRecord::request() const noexcept {
  return requestValue;
}

zc::ArrayPtr<const identity::ModuleKey> ModuleDependencyFailureRecord::candidates() const noexcept {
  return candidateValues.asPtr();
}

zc::Array<uint8_t> ModuleDependencyFailureRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeUint8(static_cast<uint8_t>(kindValue));
  const auto requestBytes = requestValue.encode();
  payload.encodeByteString(requestBytes.asPtr());
  if (kindValue == ModuleDependencyFailureKind::Ambiguous) {
    payload.encodeSequenceSize(candidateValues.size());
    for (const auto& candidate : candidateValues) {
      const auto bytes = candidate.encode();
      payload.encodeByteString(bytes.asPtr());
    }
  }
  return frame(kDependencyFailureDomain, payload.finish().asPtr());
}

zc::Array<uint8_t> SelectedModuleSource::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<SelectedModuleSource::Key> SelectedModuleSource::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> SelectedModuleSource::encodeValue(const Value& value) {
  return value.encode();
}

zc::Maybe<SelectedModuleSource::Value> SelectedModuleSource::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeSource(bytes);
}

query::TypedQueryResult<SelectedModuleSource::Value> SelectedModuleSource::provide(
    query::QueryContext& context, const Key& key) {
  auto catalog = context.get<SelectedModuleCatalogInput>(key.crate());
  if (catalog.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(catalog.runtimeFailure());
  }
  if (catalog.kind() != query::QueryValueKind::Value ||
      !sameCrate(catalog.value().crate(), key.crate())) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  for (const auto& entry : catalog.value().entries()) {
    if (sameModule(entry.module(), key)) {
      return query::TypedQueryResult<Value>::value(entry.source().clone());
    }
  }
  return query::TypedQueryResult<Value>::absence();
}

bool SelectedModuleSource::verify(query::QueryContext& context, const Key& key,
                                       const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto catalog = context.get<SelectedModuleCatalogInput>(key.crate());
  if (catalog.isRuntimeFailure() || catalog.kind() != query::QueryValueKind::Value ||
      !sameCrate(catalog.value().crate(), key.crate())) {
    return false;
  }
  const SelectedModuleRecord* found = nullptr;
  for (size_t offset = 0; offset < catalog.value().entries().size(); ++offset) {
    const size_t index = catalog.value().entries().size() - offset - 1;
    const auto& entry = catalog.value().entries()[index];
    if (sameModule(entry.module(), key)) {
      if (found != nullptr) { return false; }
      found = &entry;
    }
  }
  if (found == nullptr) { return result.kind() == query::QueryValueKind::Absence; }
  return result.kind() == query::QueryValueKind::Value &&
         sameSource(found->source(), result.value()) && result.value().belongsTo(key.crate());
}

zc::Array<uint8_t> ActiveModules::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ActiveModules::Key> ActiveModules::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeCrate(bytes);
}

zc::Array<uint8_t> ActiveModules::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveModules::Value> ActiveModules::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveModuleSetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ActiveModules::Value> ActiveModules::provide(
    query::QueryContext& context, const Key& key) {
  auto stable = incremental_binding_query::StableCrateQueryKey::fromVerified(key);
  if (stable == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto sources =
      context.get<incremental_binding_query::ActiveSources>(ZC_ASSERT_NONNULL(stable));
  if (sources.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(sources.runtimeFailure());
  }
  auto catalog = context.get<SelectedModuleCatalogInput>(key);
  if (sources.kind() != query::QueryValueKind::Value || catalog.isRuntimeFailure() ||
      catalog.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        catalog.isRuntimeFailure() ? catalog.runtimeFailure()
                                   : query::QueryRuntimeFailure::ProviderRejected);
  }
  zc::Vector<identity::ModuleKey> modules(catalog.value().entries().size());
  for (const auto& entry : catalog.value().entries()) {
    auto source = identity::source_query::StableSourceQueryKey::fromVerified(entry.source());
    if (source == zc::none || !sources.value().contains(ZC_ASSERT_NONNULL(source)) ||
        !sameCrate(entry.module().crate(), key)) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    modules.add(entry.module().clone());
  }
  auto value = ActiveModuleSetRecord::from(zc::mv(modules));
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ActiveModules::verify(query::QueryContext& context, const Key& key,
                                const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto stable = incremental_binding_query::StableCrateQueryKey::fromVerified(key);
  if (stable == zc::none) { return false; }
  auto catalog = context.get<SelectedModuleCatalogInput>(key);
  auto sources =
      context.get<incremental_binding_query::ActiveSources>(ZC_ASSERT_NONNULL(stable));
  if (catalog.isRuntimeFailure() || sources.isRuntimeFailure() ||
      catalog.kind() != query::QueryValueKind::Value ||
      sources.kind() != query::QueryValueKind::Value ||
      result.value().modules().size() != catalog.value().entries().size()) {
    return false;
  }
  for (const auto& entry : catalog.value().entries()) {
    auto source = identity::source_query::StableSourceQueryKey::fromVerified(entry.source());
    if (source == zc::none || !sources.value().contains(ZC_ASSERT_NONNULL(source))) {
      return false;
    }
    size_t occurrences = 0;
    for (const auto& module : result.value().modules()) {
      if (sameModule(module, entry.module())) { ++occurrences; }
    }
    if (occurrences != 1) { return false; }
  }
  return true;
}

zc::Array<uint8_t> ModuleDependencySites::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencySites::Key> ModuleDependencySites::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencySites::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencySites::Value> ModuleDependencySites::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return DetachedModuleDependencySiteSet::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependencySites::Value> ModuleDependencySites::provide(
    query::QueryContext& context, const Key& key) {
  auto selected = context.get<SelectedModuleSource>(key);
  if (selected.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(selected.runtimeFailure());
  }
  if (selected.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<Value>::absence();
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto sites = context.get<ModuleDependencySiteInput>(key);
  if (sites.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(sites.runtimeFailure());
  }
  auto snapshotKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sites.kind() != query::QueryValueKind::Value || snapshotKey == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto snapshot =
      context.get<identity::source_query::SourceSnapshotInput>(ZC_ASSERT_NONNULL(snapshotKey));
  if (snapshot.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(snapshot.runtimeFailure());
  }
  if (snapshot.kind() != query::QueryValueKind::Value || !sameModule(sites.value().module(), key) ||
      !sameSource(sites.value().source(), selected.value()) ||
      sites.value().sourceDigest() != snapshot.value().contentDigest()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(sites.value().clone());
}

bool ModuleDependencySites::verify(query::QueryContext& context, const Key& key,
                                        const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto selected = context.get<SelectedModuleSource>(key);
  if (selected.isRuntimeFailure()) { return false; }
  if (selected.kind() == query::QueryValueKind::Absence) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (selected.kind() != query::QueryValueKind::Value ||
      result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  auto snapshotKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (snapshotKey == zc::none) { return false; }
  auto snapshot =
      context.get<identity::source_query::SourceSnapshotInput>(ZC_ASSERT_NONNULL(snapshotKey));
  auto input = context.get<ModuleDependencySiteInput>(key);
  return !snapshot.isRuntimeFailure() && !input.isRuntimeFailure() &&
         snapshot.kind() == query::QueryValueKind::Value &&
         input.kind() == query::QueryValueKind::Value && sameModule(result.value().module(), key) &&
         sameSource(result.value().source(), selected.value()) &&
         result.value().sourceDigest() == snapshot.value().contentDigest() &&
         result.value().encodeCanonical().asPtr() == input.value().encodeCanonical().asPtr();
}

zc::Array<uint8_t> ModuleDependencyRequests::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencyRequests::Key> ModuleDependencyRequests::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencyRequests::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencyRequests::Value> ModuleDependencyRequests::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleDependencyRequestSetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependencyRequests::Value>
ModuleDependencyRequests::provide(query::QueryContext& context, const Key& key) {
  auto sites = context.get<ModuleDependencySites>(key);
  if (sites.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(sites.runtimeFailure());
  }
  if (sites.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<Value>::absence();
  }
  if (sites.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto value = buildRequests(context, key, sites.value());
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ModuleDependencyRequests::verify(query::QueryContext& context, const Key& key,
                                           const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto sites = context.get<ModuleDependencySites>(key);
  if (sites.isRuntimeFailure()) { return false; }
  if (sites.kind() == query::QueryValueKind::Absence) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (sites.kind() != query::QueryValueKind::Value ||
      result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  auto expected = rebuildVerifierRequests(context, key, sites.value());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

zc::Array<uint8_t> ModuleDependencies::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencies::Key> ModuleDependencies::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencies::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencies::Value> ModuleDependencies::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleDependencySetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependencies::Value> ModuleDependencies::provide(
    query::QueryContext& context, const Key& key) {
  auto requests = context.get<ModuleDependencyRequests>(key);
  if (requests.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(requests.runtimeFailure());
  }
  if (requests.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<Value>::absence();
  }
  if (requests.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return resolveDependencies(context, key, requests.value());
}

bool ModuleDependencies::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto requests = context.get<ModuleDependencyRequests>(key);
  if (requests.isRuntimeFailure()) { return false; }
  if (requests.kind() == query::QueryValueKind::Absence) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (requests.kind() != query::QueryValueKind::Value) { return false; }
  auto expected = resolveVerifierDependencies(context, key, requests.value());
  if (expected.isRuntimeFailure() || expected.kind() != result.kind()) { return false; }
  switch (result.kind()) {
    case query::QueryValueKind::Value:
      return expected.value().encodeCanonical().asPtr() == result.value().encodeCanonical().asPtr();
    case query::QueryValueKind::Absence:
      return true;
    case query::QueryValueKind::SemanticFailure:
      return expected.semanticFailureBytes() == result.semanticFailureBytes() &&
             ModuleDependencyFailureRecord::decodeCanonical(result.semanticFailureBytes()) !=
                 zc::none;
  }
  return false;
}

ModuleGraphInputLedgerEntry::ModuleGraphInputLedgerEntry(ModuleGraphInputFamily family,
                                                         zc::Array<uint8_t>&& keyBytes) noexcept
    : familyValue(family), keyBytesValue(zc::mv(keyBytes)) {}

zc::Maybe<ModuleGraphInputLedgerEntry> ModuleGraphInputLedgerEntry::from(
    ModuleGraphInputFamily family, zc::Array<uint8_t>&& keyBytes) {
  if (!validLedgerKey(family, keyBytes.asPtr())) { return zc::none; }
  return ModuleGraphInputLedgerEntry(family, zc::mv(keyBytes));
}

ModuleGraphInputLedgerEntry ModuleGraphInputLedgerEntry::clone() const {
  return ModuleGraphInputLedgerEntry(familyValue, zc::heapArray<uint8_t>(keyBytesValue.asPtr()));
}

ModuleGraphInputFamily ModuleGraphInputLedgerEntry::family() const noexcept { return familyValue; }

zc::ArrayPtr<const uint8_t> ModuleGraphInputLedgerEntry::keyBytes() const noexcept {
  return keyBytesValue.asPtr();
}

zc::Array<uint8_t> ModuleGraphInputLedgerEntry::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(familyValue));
  encoder.encodeByteString(keyBytesValue.asPtr());
  return encoder.finish();
}

bool ModuleGraphInputLedgerEntry::operator==(
    const ModuleGraphInputLedgerEntry& other) const noexcept {
  return familyValue == other.familyValue && keyBytesValue.asPtr() == other.keyBytesValue.asPtr();
}

bool ModuleGraphInputLedgerEntry::operator<(
    const ModuleGraphInputLedgerEntry& other) const noexcept {
  if (familyValue != other.familyValue) {
    return static_cast<uint8_t>(familyValue) < static_cast<uint8_t>(other.familyValue);
  }
  return compareBytes(keyBytesValue.asPtr(), other.keyBytesValue.asPtr()) < 0;
}

VerifiedModuleGraphInputLedger::VerifiedModuleGraphInputLedger(
    zc::Vector<ModuleGraphInputLedgerEntry>&& entries) noexcept
    : entryValues(zc::mv(entries)) {}

VerifiedModuleGraphInputLedger VerifiedModuleGraphInputLedger::empty() {
  zc::Vector<ModuleGraphInputLedgerEntry> entries;
  return VerifiedModuleGraphInputLedger(zc::mv(entries));
}

zc::Maybe<VerifiedModuleGraphInputLedger> VerifiedModuleGraphInputLedger::from(
    zc::Vector<ModuleGraphInputLedgerEntry>&& entries) {
  if (entries.size() > kMaximumLedgerEntries) { return zc::none; }
  for (size_t index = 1; index < entries.size(); ++index) {
    auto current = zc::mv(entries[index]);
    size_t insertion = index;
    while (insertion != 0 && current < entries[insertion - 1]) {
      entries[insertion] = zc::mv(entries[insertion - 1]);
      --insertion;
    }
    entries[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < entries.size(); ++index) {
    if (entries[index - 1] == entries[index]) { return zc::none; }
  }
  return VerifiedModuleGraphInputLedger(zc::mv(entries));
}

VerifiedModuleGraphInputLedger VerifiedModuleGraphInputLedger::clone() const {
  zc::Vector<ModuleGraphInputLedgerEntry> entries(entryValues.size());
  for (const auto& entry : entryValues) { entries.add(entry.clone()); }
  return VerifiedModuleGraphInputLedger(zc::mv(entries));
}

zc::ArrayPtr<const ModuleGraphInputLedgerEntry> VerifiedModuleGraphInputLedger::entries()
    const noexcept {
  return entryValues.asPtr();
}

zc::Array<uint8_t> VerifiedModuleGraphInputLedger::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(entryValues.size());
  for (const auto& entry : entryValues) {
    const auto bytes = entry.encodeCanonical();
    encoder.encodeByteString(bytes.asPtr());
  }
  return encoder.finish();
}

bool VerifiedModuleGraphInputLedger::operator==(
    const VerifiedModuleGraphInputLedger& other) const noexcept {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

struct VerifiedModuleGraphInputPayload::Impl final {
  Impl(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       zc::Vector<identity::CrateKey>&& projectedCoreCrates,
       zc::Vector<SelectedModuleCatalog>&& selectedModuleCatalogs,
       zc::Vector<DetachedModuleDependencySiteSet>&& dependencySiteSets,
       zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
       zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&&
           catalogBuckets,
       zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
       zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
       zc::Vector<ConfiguredCratePrelude>&& configuredPreludes) noexcept
      : contextRoots(zc::mv(contextRoots)),
        projectedCoreCrates(zc::mv(projectedCoreCrates)),
        catalogs(zc::mv(selectedModuleCatalogs)),
        dependencySites(zc::mv(dependencySiteSets)),
        requesterAncestries(zc::mv(requesterAncestries)),
        catalogBuckets(zc::mv(catalogBuckets)),
        searchRoots(zc::mv(searchRoots)),
        dependencyAliases(zc::mv(dependencyAliases)),
        configuredPreludes(zc::mv(configuredPreludes)) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  zc::Vector<identity::CrateKey> projectedCoreCrates;
  zc::Vector<SelectedModuleCatalog> catalogs;
  zc::Vector<DetachedModuleDependencySiteSet> dependencySites;
  zc::Vector<identity::RequesterModuleAncestry> requesterAncestries;
  zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket> catalogBuckets;
  zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots> searchRoots;
  zc::Vector<ConfiguredDependencyAlias> dependencyAliases;
  zc::Vector<ConfiguredCratePrelude> configuredPreludes;
};

VerifiedModuleGraphInputPayload::VerifiedModuleGraphInputPayload(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedModuleGraphInputPayload::~VerifiedModuleGraphInputPayload() noexcept(false) = default;
VerifiedModuleGraphInputPayload::VerifiedModuleGraphInputPayload(
    VerifiedModuleGraphInputPayload&&) noexcept = default;
VerifiedModuleGraphInputPayload& VerifiedModuleGraphInputPayload::operator=(
    VerifiedModuleGraphInputPayload&&) noexcept = default;

zc::Maybe<VerifiedModuleGraphInputPayload> VerifiedModuleGraphInputPayload::from(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    zc::Vector<identity::CrateKey>&& projectedCoreCrates,
    zc::Vector<SelectedModuleCatalog>&& selectedModuleCatalogs,
    zc::Vector<DetachedModuleDependencySiteSet>&& dependencySiteSets,
    zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
    zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&& catalogBuckets,
    zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
    zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
    zc::Vector<ConfiguredCratePrelude>&& configuredPreludes) {
  if (projectedCoreCrates.empty() || selectedModuleCatalogs.empty() ||
      selectedModuleCatalogs.size() != searchRoots.size() ||
      selectedModuleCatalogs.size() != configuredPreludes.size()) {
    return zc::none;
  }
  sortByBytes(projectedCoreCrates, [](const identity::CrateKey& value) { return value.encode(); });
  sortByBytes(selectedModuleCatalogs,
              [](const SelectedModuleCatalog& value) { return value.crate().encode(); });
  sortByBytes(dependencySiteSets,
              [](const DetachedModuleDependencySiteSet& value) { return value.module().encode(); });
  sortByBytes(requesterAncestries, [](const identity::RequesterModuleAncestry& value) {
    return value.requester().encode();
  });
  sortByBytes(catalogBuckets,
              [](const incremental_module_resolution_query::CanonicalModuleCatalogBucket& value) {
                return value.key().encode();
              });
  sortByBytes(searchRoots,
              [](const incremental_module_resolution_query::CanonicalModuleSearchRoots& value) {
                return value.crate().encode();
              });
  sortByBytes(dependencyAliases,
              [](const ConfiguredDependencyAlias& value) { return value.key.encode(); });
  sortByBytes(configuredPreludes,
              [](const ConfiguredCratePrelude& value) { return value.crate.encode(); });
  const auto duplicates = [](const auto& values, const auto& bytes) {
    for (size_t index = 1; index < values.size(); ++index) {
      if (bytes(values[index - 1]).asPtr() == bytes(values[index]).asPtr()) { return true; }
    }
    return false;
  };
  if (duplicates(projectedCoreCrates,
                 [](const identity::CrateKey& value) { return value.encode(); }) ||
      duplicates(selectedModuleCatalogs,
                 [](const SelectedModuleCatalog& value) { return value.crate().encode(); }) ||
      duplicates(
          dependencySiteSets,
          [](const DetachedModuleDependencySiteSet& value) { return value.module().encode(); }) ||
      duplicates(requesterAncestries,
                 [](const identity::RequesterModuleAncestry& value) {
                   return value.requester().encode();
                 }) ||
      duplicates(
          catalogBuckets,
          [](const incremental_module_resolution_query::CanonicalModuleCatalogBucket& value) {
            return value.key().encode();
          }) ||
      duplicates(searchRoots,
                 [](const incremental_module_resolution_query::CanonicalModuleSearchRoots& value) {
                   return value.crate().encode();
                 }) ||
      duplicates(dependencyAliases,
                 [](const ConfiguredDependencyAlias& value) { return value.key.encode(); }) ||
      duplicates(configuredPreludes,
                 [](const ConfiguredCratePrelude& value) { return value.crate.encode(); })) {
    return zc::none;
  }
  return VerifiedModuleGraphInputPayload(zc::heap<Impl>(
      zc::mv(contextRoots), zc::mv(projectedCoreCrates), zc::mv(selectedModuleCatalogs),
      zc::mv(dependencySiteSets), zc::mv(requesterAncestries), zc::mv(catalogBuckets),
      zc::mv(searchRoots), zc::mv(dependencyAliases), zc::mv(configuredPreludes)));
}

VerifiedModuleGraphInputPayload VerifiedModuleGraphInputPayload::clone() const {
  zc::Vector<identity::CrateKey> projected(impl->projectedCoreCrates.size());
  for (const auto& value : impl->projectedCoreCrates) { projected.add(value.clone()); }
  zc::Vector<SelectedModuleCatalog> catalogs(impl->catalogs.size());
  for (const auto& value : impl->catalogs) { catalogs.add(value.clone()); }
  zc::Vector<DetachedModuleDependencySiteSet> sites(impl->dependencySites.size());
  for (const auto& value : impl->dependencySites) { sites.add(value.clone()); }
  zc::Vector<identity::RequesterModuleAncestry> ancestries(impl->requesterAncestries.size());
  for (const auto& value : impl->requesterAncestries) { ancestries.add(value.clone()); }
  zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket> buckets(
      impl->catalogBuckets.size());
  for (const auto& value : impl->catalogBuckets) { buckets.add(value.clone()); }
  zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots> roots(
      impl->searchRoots.size());
  for (const auto& value : impl->searchRoots) { roots.add(value.clone()); }
  zc::Vector<ConfiguredDependencyAlias> aliases(impl->dependencyAliases.size());
  for (const auto& value : impl->dependencyAliases) {
    aliases.add(ConfiguredDependencyAlias{value.key.clone(), value.target.clone()});
  }
  zc::Vector<ConfiguredCratePrelude> preludes(impl->configuredPreludes.size());
  for (const auto& value : impl->configuredPreludes) {
    preludes.add(ConfiguredCratePrelude{value.crate.clone(), value.target.clone()});
  }
  return VerifiedModuleGraphInputPayload(zc::heap<Impl>(
      impl->contextRoots.clone(), zc::mv(projected), zc::mv(catalogs), zc::mv(sites),
      zc::mv(ancestries), zc::mv(buckets), zc::mv(roots), zc::mv(aliases), zc::mv(preludes)));
}

const incremental_binding_query::CompilationRootSetQueryKey&
VerifiedModuleGraphInputPayload::contextRoots() const noexcept {
  return impl->contextRoots;
}
zc::ArrayPtr<const identity::CrateKey> VerifiedModuleGraphInputPayload::projectedCoreCrates()
    const noexcept {
  return impl->projectedCoreCrates.asPtr();
}
zc::ArrayPtr<const SelectedModuleCatalog> VerifiedModuleGraphInputPayload::selectedModuleCatalogs()
    const noexcept {
  return impl->catalogs.asPtr();
}
zc::ArrayPtr<const DetachedModuleDependencySiteSet>
VerifiedModuleGraphInputPayload::dependencySiteSets() const noexcept {
  return impl->dependencySites.asPtr();
}
zc::ArrayPtr<const identity::RequesterModuleAncestry>
VerifiedModuleGraphInputPayload::requesterAncestries() const noexcept {
  return impl->requesterAncestries.asPtr();
}
zc::ArrayPtr<const incremental_module_resolution_query::CanonicalModuleCatalogBucket>
VerifiedModuleGraphInputPayload::catalogBuckets() const noexcept {
  return impl->catalogBuckets.asPtr();
}
zc::ArrayPtr<const incremental_module_resolution_query::CanonicalModuleSearchRoots>
VerifiedModuleGraphInputPayload::searchRoots() const noexcept {
  return impl->searchRoots.asPtr();
}
zc::ArrayPtr<const ConfiguredDependencyAlias> VerifiedModuleGraphInputPayload::dependencyAliases()
    const noexcept {
  return impl->dependencyAliases.asPtr();
}
zc::ArrayPtr<const ConfiguredCratePrelude> VerifiedModuleGraphInputPayload::configuredPreludes()
    const noexcept {
  return impl->configuredPreludes.asPtr();
}

zc::Array<uint8_t> VerifiedModuleGraphInputPayload::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(impl->contextRoots.encodeCanonical().asPtr());
  encoder.encodeSequenceSize(impl->projectedCoreCrates.size());
  for (const auto& value : impl->projectedCoreCrates) {
    encoder.encodeByteString(value.encode().asPtr());
  }
  encoder.encodeSequenceSize(impl->catalogs.size());
  for (const auto& value : impl->catalogs) {
    encoder.encodeByteString(value.encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(impl->dependencySites.size());
  for (const auto& value : impl->dependencySites) {
    encoder.encodeByteString(value.encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(impl->requesterAncestries.size());
  for (const auto& value : impl->requesterAncestries) {
    encoder.encodeByteString(value.encode().asPtr());
  }
  encoder.encodeSequenceSize(impl->catalogBuckets.size());
  for (const auto& value : impl->catalogBuckets) {
    encoder.encodeByteString(value.encode().asPtr());
  }
  encoder.encodeSequenceSize(impl->searchRoots.size());
  for (const auto& value : impl->searchRoots) { encoder.encodeByteString(value.encode().asPtr()); }
  encoder.encodeSequenceSize(impl->dependencyAliases.size());
  for (const auto& value : impl->dependencyAliases) {
    encoder.encodeByteString(value.key.encode().asPtr());
    encoder.encodeByteString(value.target.encode().asPtr());
  }
  encoder.encodeSequenceSize(impl->configuredPreludes.size());
  for (const auto& value : impl->configuredPreludes) {
    encoder.encodeByteString(value.crate.encode().asPtr());
    encoder.encodeByteString(value.target.encode().asPtr());
  }
  return frame(kModuleStructureTransactionDomain, encoder.finish().asPtr());
}

bool VerifiedModuleGraphInputPayload::operator==(
    const VerifiedModuleGraphInputPayload& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

zc::Maybe<VerifiedModuleGraphInputPayload> VerifiedModuleGraphInputPayload::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframeCanonicalInput(kModuleStructureTransactionDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto contextBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
  if (contextBytes == zc::none) { return zc::none; }
  auto contextRoots = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(contextBytes).asPtr());
  if (contextRoots == zc::none) { return zc::none; }
  auto projected = decodeCanonicalSequence<identity::CrateKey>(
      decoder, [](zc::ArrayPtr<const uint8_t> value) { return decodeCrate(value); });
  auto catalogs = decodeCanonicalSequence<SelectedModuleCatalog>(
      decoder, [](zc::ArrayPtr<const uint8_t> value) {
        return SelectedModuleCatalog::decodeCanonical(value);
      });
  auto sites = decodeCanonicalSequence<DetachedModuleDependencySiteSet>(
      decoder, [](zc::ArrayPtr<const uint8_t> value) {
        return DetachedModuleDependencySiteSet::decodeCanonical(value);
      });
  auto ancestries = decodeCanonicalSequence<identity::RequesterModuleAncestry>(
      decoder, [](zc::ArrayPtr<const uint8_t> value) {
        return identity::RequesterModuleAncestry::decodeCanonical(value);
      });
  auto buckets =
      decodeCanonicalSequence<incremental_module_resolution_query::CanonicalModuleCatalogBucket>(
          decoder, [](zc::ArrayPtr<const uint8_t> value) {
            return incremental_module_resolution_query::ModuleCatalogPathBucketInput::decodeValue(
                value);
          });
  auto roots =
      decodeCanonicalSequence<incremental_module_resolution_query::CanonicalModuleSearchRoots>(
          decoder, [](zc::ArrayPtr<const uint8_t> value) {
            return incremental_module_resolution_query::ModuleSearchRootsInput::decodeValue(value);
          });
  if (projected == zc::none || catalogs == zc::none || sites == zc::none ||
      ancestries == zc::none || buckets == zc::none || roots == zc::none) {
    return zc::none;
  }
  auto aliasCount = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (aliasCount == zc::none) { return zc::none; }
  zc::Vector<ConfiguredDependencyAlias> aliases;
  aliases.reserve(ZC_ASSERT_NONNULL(aliasCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(aliasCount); ++index) {
    auto keyBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    auto targetBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (keyBytes == zc::none || targetBytes == zc::none) { return zc::none; }
    auto key = incremental_module_resolution_query::DependencyAliasRootInput::decodeKey(
        ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto target = incremental_module_resolution_query::DependencyAliasRootInput::decodeValue(
        ZC_ASSERT_NONNULL(targetBytes).asPtr());
    if (key == zc::none || target == zc::none) { return zc::none; }
    aliases.add(ConfiguredDependencyAlias{zc::mv(ZC_ASSERT_NONNULL(key)),
                                          zc::mv(ZC_ASSERT_NONNULL(target))});
  }
  auto preludeCount = decoder.decodeSequenceSize(kMaximumCanonicalInputSequenceRecords);
  if (preludeCount == zc::none) { return zc::none; }
  zc::Vector<ConfiguredCratePrelude> preludes;
  preludes.reserve(ZC_ASSERT_NONNULL(preludeCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(preludeCount); ++index) {
    auto crateBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    auto targetBytes = decoder.decodeByteString(kMaximumCanonicalInputValueBytes);
    if (crateBytes == zc::none || targetBytes == zc::none) { return zc::none; }
    auto crate = decodeCrate(ZC_ASSERT_NONNULL(crateBytes).asPtr());
    auto target = incremental_module_resolution_query::ConfiguredPreludeInput::decodeValue(
        ZC_ASSERT_NONNULL(targetBytes).asPtr());
    if (crate == zc::none || target == zc::none) { return zc::none; }
    preludes.add(ConfiguredCratePrelude{zc::mv(ZC_ASSERT_NONNULL(crate)),
                                        zc::mv(ZC_ASSERT_NONNULL(target))});
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(contextRoots)), zc::mv(ZC_ASSERT_NONNULL(projected)),
                     zc::mv(ZC_ASSERT_NONNULL(catalogs)), zc::mv(ZC_ASSERT_NONNULL(sites)),
                     zc::mv(ZC_ASSERT_NONNULL(ancestries)), zc::mv(ZC_ASSERT_NONNULL(buckets)),
                     zc::mv(ZC_ASSERT_NONNULL(roots)), zc::mv(aliases), zc::mv(preludes));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

struct VerifiedModuleGraphInputTransaction::Impl final {
  Impl(query::DatabaseRevision expectedPreviousRevision, VerifiedModuleGraphInputPayload&& payload,
       VerifiedModuleGraphInputLedger&& priorLedger, VerifiedModuleGraphInputLedger&& nextLedger,
       binder::CanonicalInputPayloadDigest&& payloadDigest) noexcept
      : expectedPreviousRevision(expectedPreviousRevision),
        payload(zc::mv(payload)),
        priorLedger(zc::mv(priorLedger)),
        nextLedger(zc::mv(nextLedger)),
        payloadDigest(zc::mv(payloadDigest)) {}

  query::DatabaseRevision expectedPreviousRevision;
  VerifiedModuleGraphInputPayload payload;
  VerifiedModuleGraphInputLedger priorLedger;
  VerifiedModuleGraphInputLedger nextLedger;
  binder::CanonicalInputPayloadDigest payloadDigest;
  bool committed = false;
};

VerifiedModuleGraphInputTransaction::VerifiedModuleGraphInputTransaction(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

VerifiedModuleGraphInputTransaction::VerifiedModuleGraphInputTransaction(
    VerifiedModuleGraphInputTransaction&&) noexcept = default;
VerifiedModuleGraphInputTransaction& VerifiedModuleGraphInputTransaction::operator=(
    VerifiedModuleGraphInputTransaction&&) noexcept = default;
VerifiedModuleGraphInputTransaction::~VerifiedModuleGraphInputTransaction() noexcept(false) =
    default;

bool ModuleGraphInputTransactionVerifier::verify(
    const ModuleGraphInputTransactionAuthority& authority,
    const VerifiedModuleGraphInputTransaction& candidate) {
  if (candidate.impl.get() == nullptr) { return false; }
  const auto& value = *candidate.impl->payload.impl;
  if (authority.resolver.catalog().size() == 0 ||
      authority.parsedModules.size() != authority.resolver.catalog().size()) {
    return false;
  }
  zc::Vector<identity::CrateKey> expectedCoreCrates(authority.coreInputs.projections().size());
  for (const auto& projection : authority.coreInputs.projections()) {
    expectedCoreCrates.add(projection.crate().clone());
  }
  sortByBytes(expectedCoreCrates, [](const identity::CrateKey& crate) { return crate.encode(); });
  auto expectedRoots = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
      authority.packageRequest, expectedCoreCrates.asPtr());
  if (expectedRoots == zc::none ||
      ZC_ASSERT_NONNULL(expectedRoots).encodeCanonical().asPtr() !=
          value.contextRoots.encodeCanonical().asPtr() ||
      expectedCoreCrates.size() != value.projectedCoreCrates.size()) {
    return false;
  }
  for (size_t index = 0; index < expectedCoreCrates.size(); ++index) {
    if (!sameCrate(expectedCoreCrates[index], value.projectedCoreCrates[index])) { return false; }
  }
  if (value.catalogs.size() == 0 || value.catalogs.size() != value.searchRoots.size() ||
      value.catalogs.size() != value.configuredPreludes.size()) {
    return false;
  }

  zc::TreeMap<zc::String, identity::CrateKey> coreRoots;
  size_t userRootCount = 0;
  for (const auto& root : value.contextRoots.roots()) {
    if (root.kind() == incremental_binding_query::CompilationRootKind::UserPackage) {
      ++userRootCount;
      continue;
    }
    identity::CanonicalDecoder decoder(root.toolchainCore().canonicalCrateBytes());
    auto crate = identity::CrateKey::decodeCanonical(decoder);
    if (crate == zc::none || !decoder.finished() ||
        ZC_ASSERT_NONNULL(crate).encode().asPtr() != root.toolchainCore().canonicalCrateBytes()) {
      return false;
    }
    auto key = zc::encodeHex(ZC_ASSERT_NONNULL(crate).encode().asPtr());
    if (coreRoots.find(key) != zc::none) { return false; }
    coreRoots.insert(zc::mv(key), zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  if (userRootCount == 0 || coreRoots.size() != value.projectedCoreCrates.size()) { return false; }
  for (const auto& projected : value.projectedCoreCrates) {
    auto key = zc::encodeHex(projected.encode().asPtr());
    auto expected = coreRoots.find(key);
    if (expected == zc::none ||
        ZC_ASSERT_NONNULL(expected).encode().asPtr() != projected.encode().asPtr()) {
      return false;
    }
  }

  zc::TreeMap<zc::String, const SelectedModuleRecord*> selectedModules;
  zc::TreeMap<zc::String, const SelectedModuleCatalog*> selectedCrates;
  for (const auto& catalog : value.catalogs) {
    auto crateKey = zc::encodeHex(catalog.crate().encode().asPtr());
    if (selectedCrates.find(crateKey) != zc::none) { return false; }
    selectedCrates.insert(zc::mv(crateKey), &catalog);
    for (const auto& selected : catalog.entries()) {
      if (selected.module().crate().encode().asPtr() != catalog.crate().encode().asPtr() ||
          !selected.source().belongsTo(catalog.crate())) {
        return false;
      }
      auto moduleKey = zc::encodeHex(selected.module().encode().asPtr());
      if (selectedModules.find(moduleKey) != zc::none) { return false; }
      selectedModules.insert(zc::mv(moduleKey), &selected);
    }
  }
  zc::TreeMap<zc::String, const binder::StructuralModuleCatalogEntry*> expectedCrates;
  for (const auto& expected : authority.resolver.catalog()) {
    auto crateKey = zc::encodeHex(expected.key.crate().encode().asPtr());
    if (expectedCrates.find(crateKey) == zc::none) {
      expectedCrates.insert(zc::mv(crateKey), &expected);
    }
  }
  if (selectedCrates.size() != expectedCrates.size()) { return false; }
  for (const auto& expected : authority.resolver.catalog()) {
    auto crateKey = zc::encodeHex(expected.key.crate().encode().asPtr());
    if (selectedCrates.find(crateKey) == zc::none) { return false; }
  }
  if (selectedModules.size() != authority.resolver.catalog().size()) { return false; }
  zc::TreeMap<zc::String, ConfiguredDependencyAlias> expectedAliases;
  for (const auto& expected : authority.resolver.catalog()) {
    auto key = zc::encodeHex(expected.key.encode().asPtr());
    auto selected = selectedModules.find(key);
    if (selected == zc::none ||
        ZC_ASSERT_NONNULL(selected)->module().encode().asPtr() != expected.key.encode().asPtr() ||
        ZC_ASSERT_NONNULL(selected)->source().encode().asPtr() !=
            expected.source.encode().asPtr()) {
      return false;
    }
    zc::Maybe<const binder::ParsedModuleGraphInput&> parsed;
    for (const auto& candidateParsed : authority.parsedModules) {
      if (candidateParsed.module != expected.module) { continue; }
      if (parsed != zc::none) { return false; }
      parsed = candidateParsed;
    }
    if (parsed == zc::none || ZC_ASSERT_NONNULL(parsed).parsedModule.source().encode().asPtr() !=
                                  expected.source.encode().asPtr()) {
      return false;
    }
    zc::Maybe<const DetachedModuleDependencySiteSet&> candidateSites;
    for (const auto& sites : value.dependencySites) {
      if (!sameModule(sites.module(), expected.key)) { continue; }
      if (candidateSites != zc::none) { return false; }
      candidateSites = sites;
    }
    auto reconstructed =
        reconstructVerifierSites(expected.key, ZC_ASSERT_NONNULL(parsed).parsedModule);
    if (candidateSites == zc::none || reconstructed == zc::none ||
        ZC_ASSERT_NONNULL(candidateSites).encodeCanonical().asPtr() !=
            ZC_ASSERT_NONNULL(reconstructed).encodeCanonical().asPtr()) {
      return false;
    }
    for (const auto& site : ZC_ASSERT_NONNULL(reconstructed).sites()) {
      auto alias = identity::DependencyAlias::fromCanonical(site.normalizedPath().front().text());
      if (alias == zc::none) { continue; }
      auto aliasKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          expected.key.crate().clone(), ZC_ASSERT_NONNULL(alias).clone());
      if (aliasKey == zc::none) { return false; }
      zc::Maybe<const identity::ModuleKey&> aliasTarget;
      for (const auto& root : authority.resolver.dependencyAliasRootInputs()) {
        if (!sameCrate(root.requester, expected.key.crate()) ||
            root.alias.text() != ZC_ASSERT_NONNULL(alias).text()) {
          continue;
        }
        if (aliasTarget != zc::none) { return false; }
        aliasTarget = root.target;
      }
      auto target = aliasTarget == zc::none
                        ? incremental_module_resolution_query::ExplicitModuleTarget::absent()
                        : incremental_module_resolution_query::ExplicitModuleTarget::present(
                              ZC_ASSERT_NONNULL(aliasTarget).clone());
      if (ZC_ASSERT_NONNULL(alias).text() == "core"_zc) {
        if (isCoreCrate(expected.key.crate())) {
          if (aliasTarget != zc::none) { return false; }
        } else {
          auto projected = identity::projectToolchainCoreCrate(expected.key.crate());
          if (projected == zc::none || aliasTarget == zc::none ||
              !sameCrate(ZC_ASSERT_NONNULL(aliasTarget).crate(), ZC_ASSERT_NONNULL(projected)) ||
              ZC_ASSERT_NONNULL(aliasTarget).path().size() != 1 ||
              ZC_ASSERT_NONNULL(aliasTarget).path()[0].text() != "core"_zc) {
            return false;
          }
        }
      }
      auto encoded = ZC_ASSERT_NONNULL(aliasKey).encode();
      auto sortKey = zc::encodeHex(encoded.asPtr());
      auto prior = expectedAliases.find(sortKey);
      if (prior == zc::none) {
        expectedAliases.insert(
            zc::mv(sortKey),
            ConfiguredDependencyAlias{zc::mv(ZC_ASSERT_NONNULL(aliasKey)), zc::mv(target)});
      } else if (ZC_ASSERT_NONNULL(prior).target.encode().asPtr() != target.encode().asPtr()) {
        return false;
      }
    }
  }
  zc::TreeMap<zc::String, incremental_module_resolution_query::CanonicalModuleCatalogBucket>
      expectedBuckets;
  const auto addExpectedBucket = [&](const identity::CrateKey& crate,
                                     zc::ArrayPtr<const identity::ModulePathSegment> path) {
    auto bucketKey =
        identity::ModuleCatalogPathBucketKey::from(crate.clone(), cloneVerifierPath(path));
    if (bucketKey == zc::none) { return false; }
    zc::Maybe<const identity::ModuleKey&> selected;
    for (const auto& entry : authority.resolver.catalog()) {
      if (!sameCrate(entry.key.crate(), crate) || !verifierPathEquals(entry.key.path(), path)) {
        continue;
      }
      if (selected != zc::none) { return false; }
      selected = entry.key;
    }
    auto bucket =
        selected == zc::none
            ? identity::ModuleCatalogPathBucket::absent(zc::mv(ZC_ASSERT_NONNULL(bucketKey)))
            : identity::ModuleCatalogPathBucket::present(zc::mv(ZC_ASSERT_NONNULL(bucketKey)),
                                                         ZC_ASSERT_NONNULL(selected).clone());
    if (bucket == zc::none) { return false; }
    auto canonical =
        incremental_module_resolution_query::CanonicalModuleCatalogBucket::fromVerified(
            ZC_ASSERT_NONNULL(bucket));
    const auto encoded = canonical.key().encode();
    auto sortKey = zc::encodeHex(encoded.asPtr());
    auto prior = expectedBuckets.find(sortKey);
    if (prior == zc::none) {
      expectedBuckets.insert(zc::mv(sortKey), zc::mv(canonical));
      return true;
    }
    return ZC_ASSERT_NONNULL(prior).encode().asPtr() == canonical.encode().asPtr();
  };
  for (const auto& entry : authority.resolver.catalog()) {
    if (!addExpectedBucket(entry.key.crate(), entry.key.path())) { return false; }
    zc::Maybe<const binder::ParsedModuleGraphInput&> parsed;
    zc::Maybe<const identity::RequesterModuleAncestry&> ancestry;
    for (const auto& candidateParsed : authority.parsedModules) {
      if (candidateParsed.module == entry.module) { parsed = candidateParsed; }
    }
    for (const auto& candidateAncestry : authority.resolver.requesterAncestryInputs()) {
      if (candidateAncestry.requester().encode().asPtr() == entry.key.encode().asPtr()) {
        ancestry = candidateAncestry;
      }
    }
    if (parsed == zc::none || ancestry == zc::none) { return false; }
    auto sites = reconstructVerifierSites(entry.key, ZC_ASSERT_NONNULL(parsed).parsedModule);
    if (sites == zc::none) { return false; }
    for (const auto& site : ZC_ASSERT_NONNULL(sites).sites()) {
      for (const auto& ancestor : ZC_ASSERT_NONNULL(ancestry).ancestry()) {
        auto candidatePath = cloneVerifierPath(ancestor.path());
        for (const auto& segment : site.normalizedPath()) { candidatePath.add(segment.clone()); }
        if (!addExpectedBucket(entry.key.crate(), candidatePath.asPtr())) { return false; }
      }
      if (!addExpectedBucket(entry.key.crate(), site.normalizedPath())) { return false; }
      auto alias = identity::DependencyAlias::fromCanonical(site.normalizedPath().front().text());
      if (alias == zc::none) { continue; }
      auto aliasKey = incremental_module_resolution_query::DependencyAliasRootQueryKey::from(
          entry.key.crate().clone(), zc::mv(ZC_ASSERT_NONNULL(alias)));
      if (aliasKey == zc::none) { return false; }
      auto configured =
          expectedAliases.find(zc::encodeHex(ZC_ASSERT_NONNULL(aliasKey).encode().asPtr()));
      if (configured == zc::none) { return false; }
      ZC_IF_SOME(target, ZC_ASSERT_NONNULL(configured).target.target()) {
        auto candidatePath = cloneVerifierPath(target.path());
        for (size_t index = 1; index < site.normalizedPath().size(); ++index) {
          candidatePath.add(site.normalizedPath()[index].clone());
        }
        if (!addExpectedBucket(target.crate(), candidatePath.asPtr())) { return false; }
      }
    }
  }
  for (const auto& catalog : value.catalogs) {
    if (isCoreCrate(catalog.crate())) { continue; }
    auto projected = identity::projectToolchainCoreCrate(catalog.crate());
    if (projected == zc::none) { return false; }
    zc::Vector<identity::ModulePathSegment> preludePath;
    auto coreSegment = identity::ModulePathSegment::fromCanonical("core"_zc);
    auto preludeSegment = identity::ModulePathSegment::fromCanonical("prelude"_zc);
    if (coreSegment == zc::none || preludeSegment == zc::none) { return false; }
    preludePath.add(zc::mv(ZC_ASSERT_NONNULL(coreSegment)));
    preludePath.add(zc::mv(ZC_ASSERT_NONNULL(preludeSegment)));
    if (!addExpectedBucket(ZC_ASSERT_NONNULL(projected), preludePath.asPtr())) { return false; }
  }
  if (selectedModules.size() != value.dependencySites.size() ||
      selectedModules.size() != value.requesterAncestries.size() ||
      authority.resolver.requesterAncestryInputs().size() != value.requesterAncestries.size()) {
    return false;
  }

  for (const auto& sites : value.dependencySites) {
    auto key = zc::encodeHex(sites.module().encode().asPtr());
    auto selected = selectedModules.find(key);
    if (selected == zc::none ||
        ZC_ASSERT_NONNULL(selected)->source().encode().asPtr() != sites.source().encode().asPtr()) {
      return false;
    }
  }
  for (const auto& ancestry : value.requesterAncestries) {
    auto key = zc::encodeHex(ancestry.requester().encode().asPtr());
    if (selectedModules.find(key) == zc::none || ancestry.ancestry().size() == 0 ||
        ancestry.ancestry().front().encode().asPtr() != ancestry.requester().encode().asPtr()) {
      return false;
    }
    size_t matches = 0;
    for (const auto& expected : authority.resolver.requesterAncestryInputs()) {
      if (expected.requester().encode().asPtr() != ancestry.requester().encode().asPtr()) {
        continue;
      }
      if (expected.encode().asPtr() != ancestry.encode().asPtr()) { return false; }
      ++matches;
    }
    if (matches != 1) { return false; }
  }

  for (const auto& roots : value.searchRoots) {
    auto key = zc::encodeHex(roots.crate().encode().asPtr());
    if (selectedCrates.find(key) == zc::none || roots.roots().size() == 0) { return false; }
    auto expected = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
        roots.crate(), authority.resolver.searchRootInputs());
    if (expected == zc::none ||
        ZC_ASSERT_NONNULL(expected).encode().asPtr() != roots.encode().asPtr()) {
      return false;
    }
    if (isCoreCrate(roots.crate())) {
      size_t matches = 0;
      for (const auto& projection : authority.coreInputs.projections()) {
        if (!sameCrate(projection.crate(), roots.crate())) { continue; }
        if (projection.searchRoots().encode().asPtr() != roots.encode().asPtr()) { return false; }
        ++matches;
      }
      if (matches != 1) { return false; }
    }
  }
  for (const auto& prelude : value.configuredPreludes) {
    auto crateKey = zc::encodeHex(prelude.crate.encode().asPtr());
    if (selectedCrates.find(crateKey) == zc::none) { return false; }
    const bool core =
        prelude.crate.unit().kind() == identity::CompilationUnitKind::Toolchain &&
        prelude.crate.unit().toolchain().component() == identity::ToolchainComponent::Core;
    auto target = prelude.target.target();
    if (core != (target == zc::none)) { return false; }
    ZC_IF_SOME(module, target) {
      auto projected = identity::projectToolchainCoreCrate(prelude.crate);
      auto moduleKey = zc::encodeHex(module.encode().asPtr());
      if (projected == zc::none ||
          module.crate().encode().asPtr() != ZC_ASSERT_NONNULL(projected).encode().asPtr() ||
          selectedModules.find(moduleKey) == zc::none || module.path().size() != 2 ||
          module.path()[0].text() != "core"_zc || module.path()[1].text() != "prelude"_zc) {
        return false;
      }
    }
  }
  if (expectedAliases.size() != value.dependencyAliases.size()) { return false; }
  for (const auto& alias : value.dependencyAliases) {
    auto expectedAlias = expectedAliases.find(zc::encodeHex(alias.key.encode().asPtr()));
    if (expectedAlias == zc::none ||
        ZC_ASSERT_NONNULL(expectedAlias).target.encode().asPtr() != alias.target.encode().asPtr()) {
      return false;
    }
    ZC_IF_SOME(target, alias.target.target()) {
      auto key = zc::encodeHex(target.encode().asPtr());
      if (selectedModules.find(key) == zc::none) { return false; }
    }
  }
  if (expectedBuckets.size() != value.catalogBuckets.size()) { return false; }
  for (const auto& bucket : value.catalogBuckets) {
    auto expectedBucket = expectedBuckets.find(zc::encodeHex(bucket.key().encode().asPtr()));
    if (expectedBucket == zc::none ||
        ZC_ASSERT_NONNULL(expectedBucket).encode().asPtr() != bucket.encode().asPtr()) {
      return false;
    }
    ZC_IF_SOME(target, bucket.module()) {
      auto key = zc::encodeHex(target.encode().asPtr());
      if (selectedModules.find(key) == zc::none) { return false; }
    }
  }

  zc::Vector<ModuleGraphInputLedgerEntry> entries(
      value.catalogs.size() + value.dependencySites.size() + value.requesterAncestries.size() +
      value.catalogBuckets.size() + value.searchRoots.size() + value.dependencyAliases.size() +
      value.configuredPreludes.size());
  const auto appendEntry = [&](ModuleGraphInputFamily family, zc::Array<uint8_t>&& keyBytes) {
    auto entry = ModuleGraphInputLedgerEntry::from(family, zc::mv(keyBytes));
    if (entry == zc::none) { return false; }
    entries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
    return true;
  };
  for (const auto& catalog : value.catalogs) {
    if (!appendEntry(ModuleGraphInputFamily::SelectedModuleCatalog, catalog.crate().encode())) {
      return false;
    }
  }
  for (const auto& sites : value.dependencySites) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleDependencySite, sites.module().encode())) {
      return false;
    }
  }
  for (const auto& ancestry : value.requesterAncestries) {
    if (!appendEntry(ModuleGraphInputFamily::RequesterModuleAncestry,
                     ancestry.requester().encode())) {
      return false;
    }
  }
  for (const auto& bucket : value.catalogBuckets) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleCatalogPathBucket, bucket.key().encode())) {
      return false;
    }
  }
  for (const auto& roots : value.searchRoots) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleSearchRoots, roots.crate().encode())) {
      return false;
    }
  }
  for (const auto& alias : value.dependencyAliases) {
    if (!appendEntry(ModuleGraphInputFamily::DependencyAliasRoot, alias.key.encode())) {
      return false;
    }
  }
  for (const auto& prelude : value.configuredPreludes) {
    if (!appendEntry(ModuleGraphInputFamily::ConfiguredPrelude, prelude.crate.encode())) {
      return false;
    }
  }
  auto expectedLedger = VerifiedModuleGraphInputLedger::from(zc::mv(entries));
  if (expectedLedger == zc::none || ZC_ASSERT_NONNULL(expectedLedger).encodeCanonical().asPtr() !=
                                        candidate.impl->nextLedger.encodeCanonical().asPtr()) {
    return false;
  }
  zc::Vector<ModuleGraphInputLedgerEntry> priorEntries(
      candidate.impl->priorLedger.entries().size());
  for (const auto& entry : candidate.impl->priorLedger.entries()) {
    priorEntries.add(entry.clone());
  }
  auto reconstructedPrior = VerifiedModuleGraphInputLedger::from(zc::mv(priorEntries));
  return reconstructedPrior != zc::none &&
         ZC_ASSERT_NONNULL(reconstructedPrior).encodeCanonical().asPtr() ==
             candidate.impl->priorLedger.encodeCanonical().asPtr();
}

bool VerifiedModuleGraphInputVerifier::verify(const ModuleGraphInputTransactionAuthority& authority,
                                              const VerifiedModuleGraphInputPayload& candidate) {
  if (candidate.impl.get() == nullptr) { return false; }
  auto decoded = VerifiedModuleGraphInputPayload::decodeCanonical(candidate.encodeCanonical());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate) { return false; }

  zc::Vector<ModuleGraphInputLedgerEntry> entries(
      candidate.selectedModuleCatalogs().size() + candidate.dependencySiteSets().size() +
      candidate.requesterAncestries().size() + candidate.catalogBuckets().size() +
      candidate.searchRoots().size() + candidate.dependencyAliases().size() +
      candidate.configuredPreludes().size());
  const auto appendEntry = [&](ModuleGraphInputFamily family, zc::Array<uint8_t>&& keyBytes) {
    auto entry = ModuleGraphInputLedgerEntry::from(family, zc::mv(keyBytes));
    if (entry == zc::none) { return false; }
    entries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
    return true;
  };
  for (const auto& catalog : candidate.selectedModuleCatalogs()) {
    if (!appendEntry(ModuleGraphInputFamily::SelectedModuleCatalog, catalog.crate().encode())) {
      return false;
    }
  }
  for (const auto& sites : candidate.dependencySiteSets()) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleDependencySite, sites.module().encode())) {
      return false;
    }
  }
  for (const auto& ancestry : candidate.requesterAncestries()) {
    if (!appendEntry(ModuleGraphInputFamily::RequesterModuleAncestry,
                     ancestry.requester().encode())) {
      return false;
    }
  }
  for (const auto& bucket : candidate.catalogBuckets()) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleCatalogPathBucket, bucket.key().encode())) {
      return false;
    }
  }
  for (const auto& roots : candidate.searchRoots()) {
    if (!appendEntry(ModuleGraphInputFamily::ModuleSearchRoots, roots.crate().encode())) {
      return false;
    }
  }
  for (const auto& alias : candidate.dependencyAliases()) {
    if (!appendEntry(ModuleGraphInputFamily::DependencyAliasRoot, alias.key.encode())) {
      return false;
    }
  }
  for (const auto& prelude : candidate.configuredPreludes()) {
    if (!appendEntry(ModuleGraphInputFamily::ConfiguredPrelude, prelude.crate.encode())) {
      return false;
    }
  }
  auto nextLedger = VerifiedModuleGraphInputLedger::from(zc::mv(entries));
  auto payloadBytes = candidate.encodeCanonical();
  auto payloadDigest =
      computeCanonicalInputPayloadDigest(kModuleStructureTransactionDomain, payloadBytes.asPtr());
  if (nextLedger == zc::none || payloadDigest == zc::none) { return false; }
  auto transaction =
      VerifiedModuleGraphInputTransaction(zc::heap<VerifiedModuleGraphInputTransaction::Impl>(
          query::DatabaseRevision(), candidate.clone(), VerifiedModuleGraphInputLedger::empty(),
          zc::mv(ZC_ASSERT_NONNULL(nextLedger)), zc::mv(ZC_ASSERT_NONNULL(payloadDigest))));
  return ModuleGraphInputTransactionVerifier::verify(authority, transaction);
}

zc::Maybe<VerifiedModuleGraphInputTransaction> VerifiedModuleGraphInputTransaction::prepare(
    const ModuleGraphInputTransactionAuthority& authority,
    query::DatabaseRevision expectedPreviousRevision,
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    zc::Vector<identity::CrateKey>&& projectedCoreCrates,
    zc::Vector<SelectedModuleCatalog>&& catalogs,
    zc::Vector<DetachedModuleDependencySiteSet>&& dependencySites,
    zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
    zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&& catalogBuckets,
    zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
    zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
    zc::Vector<ConfiguredCratePrelude>&& configuredPreludes,
    const VerifiedModuleGraphInputLedger& priorLedger) {
  if (catalogs.size() == 0 || searchRoots.size() != catalogs.size() ||
      configuredPreludes.size() != catalogs.size()) {
    return zc::none;
  }

  sortByBytes(projectedCoreCrates, [](const identity::CrateKey& crate) { return crate.encode(); });
  for (size_t index = 0; index < projectedCoreCrates.size(); ++index) {
    if (!isCoreCrate(projectedCoreCrates[index]) ||
        (index != 0 && sameCrate(projectedCoreCrates[index - 1], projectedCoreCrates[index]))) {
      return zc::none;
    }
  }

  sortByBytes(catalogs,
              [](const SelectedModuleCatalog& catalog) { return catalog.crate().encode(); });
  for (size_t index = 1; index < catalogs.size(); ++index) {
    if (sameCrate(catalogs[index - 1].crate(), catalogs[index].crate())) { return zc::none; }
  }

  sortByBytes(dependencySites,
              [](const DetachedModuleDependencySiteSet& sites) { return sites.module().encode(); });
  size_t selectedModuleCount = 0;
  for (const auto& catalog : catalogs) { selectedModuleCount += catalog.entries().size(); }
  if (dependencySites.size() != selectedModuleCount ||
      requesterAncestries.size() != selectedModuleCount) {
    return zc::none;
  }
  for (size_t index = 0; index < dependencySites.size(); ++index) {
    if (!isSelected(catalogs.asPtr(), dependencySites[index].module(),
                    dependencySites[index].source()) ||
        (index != 0 &&
         sameModule(dependencySites[index - 1].module(), dependencySites[index].module()))) {
      return zc::none;
    }
  }

  sortByBytes(requesterAncestries, [](const identity::RequesterModuleAncestry& ancestry) {
    return ancestry.requester().encode();
  });
  for (size_t index = 0; index < requesterAncestries.size(); ++index) {
    if (!isSelected(catalogs.asPtr(), requesterAncestries[index].requester()) ||
        (index != 0 && sameModule(requesterAncestries[index - 1].requester(),
                                  requesterAncestries[index].requester()))) {
      return zc::none;
    }
  }

  sortByBytes(searchRoots,
              [](const incremental_module_resolution_query::CanonicalModuleSearchRoots& roots) {
                return roots.crate().encode();
              });
  for (size_t index = 0; index < searchRoots.size(); ++index) {
    if (!sameCrate(searchRoots[index].crate(), catalogs[index].crate()) ||
        searchRoots[index].roots().size() == 0) {
      return zc::none;
    }
  }

  sortByBytes(configuredPreludes,
              [](const ConfiguredCratePrelude& prelude) { return prelude.crate.encode(); });
  for (size_t index = 0; index < configuredPreludes.size(); ++index) {
    const auto& prelude = configuredPreludes[index];
    if (!sameCrate(prelude.crate, catalogs[index].crate())) { return zc::none; }
    auto target = prelude.target.target();
    if (isCoreCrate(prelude.crate)) {
      if (target != zc::none) { return zc::none; }
    } else {
      if (target == zc::none) { return zc::none; }
      ZC_IF_SOME(value, target) {
        if (!isCoreCrate(value.crate()) || !isSelected(catalogs.asPtr(), value) ||
            value.path().size() != 2 || value.path()[0].text() != "core"_zc ||
            value.path()[1].text() != "prelude"_zc) {
          return zc::none;
        }
      }
    }
  }

  sortByBytes(catalogBuckets,
              [](const incremental_module_resolution_query::CanonicalModuleCatalogBucket& bucket) {
                return bucket.key().encode();
              });
  for (size_t index = 0; index < catalogBuckets.size(); ++index) {
    if (index != 0 && catalogBuckets[index - 1].key().encode().asPtr() ==
                          catalogBuckets[index].key().encode().asPtr()) {
      return zc::none;
    }
    ZC_IF_SOME(module, catalogBuckets[index].module()) {
      if (!isSelected(catalogs.asPtr(), module)) { return zc::none; }
    }
  }

  sortByBytes(dependencyAliases,
              [](const ConfiguredDependencyAlias& alias) { return alias.key.encode(); });
  for (size_t index = 0; index < dependencyAliases.size(); ++index) {
    if (index != 0 && dependencyAliases[index - 1].key.encode().asPtr() ==
                          dependencyAliases[index].key.encode().asPtr()) {
      return zc::none;
    }
    ZC_IF_SOME(target, dependencyAliases[index].target.target()) {
      if (!isSelected(catalogs.asPtr(), target)) { return zc::none; }
    }
  }

  zc::Vector<ModuleGraphInputLedgerEntry> nextEntries(
      catalogs.size() + dependencySites.size() + requesterAncestries.size() +
      catalogBuckets.size() + searchRoots.size() + dependencyAliases.size() +
      configuredPreludes.size());
  for (const auto& catalog : catalogs) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::SelectedModuleCatalog,
                                                   catalog.crate().encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& sites : dependencySites) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::ModuleDependencySite,
                                                   sites.module().encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& bucket : catalogBuckets) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::ModuleCatalogPathBucket,
                                                   bucket.key().encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& ancestry : requesterAncestries) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::RequesterModuleAncestry,
                                                   ancestry.requester().encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& roots : searchRoots) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::ModuleSearchRoots,
                                                   roots.crate().encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& alias : dependencyAliases) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::DependencyAliasRoot,
                                                   alias.key.encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  for (const auto& prelude : configuredPreludes) {
    auto entry = ModuleGraphInputLedgerEntry::from(ModuleGraphInputFamily::ConfiguredPrelude,
                                                   prelude.crate.encode());
    if (entry == zc::none) { return zc::none; }
    nextEntries.add(zc::mv(ZC_ASSERT_NONNULL(entry)));
  }
  auto nextLedger = VerifiedModuleGraphInputLedger::from(zc::mv(nextEntries));
  if (nextLedger == zc::none) { return zc::none; }

  auto payload = VerifiedModuleGraphInputPayload::from(
      zc::mv(contextRoots), zc::mv(projectedCoreCrates), zc::mv(catalogs), zc::mv(dependencySites),
      zc::mv(requesterAncestries), zc::mv(catalogBuckets), zc::mv(searchRoots),
      zc::mv(dependencyAliases), zc::mv(configuredPreludes));
  if (payload == zc::none ||
      !VerifiedModuleGraphInputVerifier::verify(authority, ZC_ASSERT_NONNULL(payload))) {
    return zc::none;
  }
  auto payloadBytes = ZC_ASSERT_NONNULL(payload).encodeCanonical();
  auto payloadDigest =
      computeCanonicalInputPayloadDigest(kModuleStructureTransactionDomain, payloadBytes.asPtr());
  if (payloadDigest == zc::none) { return zc::none; }
  VerifiedModuleGraphInputTransaction candidate(zc::heap<Impl>(
      expectedPreviousRevision, zc::mv(ZC_ASSERT_NONNULL(payload)), priorLedger.clone(),
      zc::mv(ZC_ASSERT_NONNULL(nextLedger)), zc::mv(ZC_ASSERT_NONNULL(payloadDigest))));
  if (!ModuleGraphInputTransactionVerifier::verify(authority, candidate)) { return zc::none; }
  return zc::mv(candidate);
}

const incremental_binding_query::CompilationRootSetQueryKey&
VerifiedModuleGraphInputTransaction::contextRoots() const noexcept {
  return impl->payload.contextRoots();
}

const VerifiedModuleGraphInputPayload& VerifiedModuleGraphInputTransaction::payload()
    const noexcept {
  return impl->payload;
}

const VerifiedModuleGraphInputLedger& VerifiedModuleGraphInputTransaction::priorLedger()
    const noexcept {
  return impl->priorLedger;
}

const VerifiedModuleGraphInputLedger& VerifiedModuleGraphInputTransaction::nextLedger()
    const noexcept {
  return impl->nextLedger;
}

query::InputCommitResult VerifiedModuleGraphInputTransaction::commit(
    query::QueryDatabase& database) {
  if (impl.get() == nullptr || impl->committed) {
    return query::InputCommitResult::rejected(query::InputTransactionFailure::TransactionClosed);
  }
  auto pending = database.beginInputTransaction(impl->expectedPreviousRevision);
  if (!pending.isOpened()) { return query::InputCommitResult::rejected(pending.failure()); }
  auto transaction = zc::mv(pending).takeTransaction();
  for (const auto& prior : impl->priorLedger.entries()) {
    if (!ledgerContains(impl->nextLedger.entries(), prior) &&
        !eraseLedgerEntry(transaction, prior)) {
      transaction.abandon();
      return query::InputCommitResult::rejected(
          query::InputTransactionFailure::MissingInputForErase);
    }
  }
  for (const auto& catalog : impl->payload.selectedModuleCatalogs()) {
    auto mutation = transaction.set<SelectedModuleCatalogInput>(catalog.crate(), catalog);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& sites : impl->payload.dependencySiteSets()) {
    auto mutation = transaction.set<ModuleDependencySiteInput>(sites.module(), sites);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& ancestry : impl->payload.requesterAncestries()) {
    auto mutation =
        transaction.set<incremental_module_resolution_query::RequesterModuleAncestryInput>(
            ancestry.requester(), ancestry);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& bucket : impl->payload.catalogBuckets()) {
    auto mutation =
        transaction.set<incremental_module_resolution_query::ModuleCatalogPathBucketInput>(
            bucket.key(), bucket);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& roots : impl->payload.searchRoots()) {
    auto mutation = transaction.set<incremental_module_resolution_query::ModuleSearchRootsInput>(
        roots.crate(), roots);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& alias : impl->payload.dependencyAliases()) {
    auto mutation = transaction.set<incremental_module_resolution_query::DependencyAliasRootInput>(
        alias.key, alias.target);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  for (const auto& prelude : impl->payload.configuredPreludes()) {
    auto mutation = transaction.set<incremental_module_resolution_query::ConfiguredPreludeInput>(
        prelude.crate, prelude.target);
    if (!mutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(mutation.failure());
    }
  }
  auto witnessMutation = transaction.set<ModuleStructureTransactionWitnessInput>(
      impl->payload.contextRoots(), impl->payloadDigest);
  if (!witnessMutation.isApplied()) {
    transaction.abandon();
    return query::InputCommitResult::rejected(witnessMutation.failure());
  }
  auto result = transaction.commit();
  if (!result.isCommitted()) { return result; }
  impl->committed = true;
  return result;
}

bool registerModuleGraphQueries(query::QueryDatabase& database) {
  return database.registerDescriptor<CoreDistributionTransactionWitnessInput>().isRegistered() &&
         database.registerDescriptor<ModuleStructureTransactionWitnessInput>().isRegistered() &&
         database.registerDescriptor<ContextualIdentityAuthorityTransactionWitnessInput>()
             .isRegistered() &&
         database.registerDescriptor<CompleteCompilationContextAuthorityInput>().isRegistered() &&
         database.registerDescriptor<SelectedModuleCatalogInput>().isRegistered() &&
         database.registerDescriptor<ModuleDependencySiteInput>().isRegistered() &&
         database.registerDescriptor<SelectedModuleSource>().isRegistered() &&
         database.registerDescriptor<ActiveModules>().isRegistered() &&
         database.registerDescriptor<ModuleDependencySites>().isRegistered() &&
         database.registerDescriptor<ModuleDependencyRequests>().isRegistered() &&
         database.registerDescriptor<ModuleDependencyProvenance>().isRegistered() &&
         database.registerDescriptor<ModuleDependencies>().isRegistered() &&
         database.registerDescriptor<MaterializeModuleGraph>().isRegistered() &&
         database.registerDescriptor<MaterializeModuleSkeleton>().isRegistered() &&
         database.registerDescriptor<MaterializeOwnerBody>().isRegistered() &&
         database.registerDescriptor<VerifyBoundModule>().isRegistered();
}

}  // namespace zomlang::compiler::driver::module_graph_query
