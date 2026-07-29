// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/module-graph-query-input.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/module-resolution.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

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
constexpr uint64_t kMaximumCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumPathSegmentBytes = 4096;
constexpr uint64_t kMaximumModules = 4096;
constexpr uint64_t kMaximumDependencySites = 1024 * 1024;
constexpr uint64_t kMaximumValueBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumLedgerEntries = 8 * 1024 * 1024;

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
        context.get<incremental_module_resolution_query::ResolveModuleRequestQuery>(request);
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
        context.get<incremental_module_resolution_query::ResolveModuleRequestQuery>(request);
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

}  // namespace

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

zc::Array<uint8_t> SelectedModuleSourceQuery::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<SelectedModuleSourceQuery::Key> SelectedModuleSourceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> SelectedModuleSourceQuery::encodeValue(const Value& value) {
  return value.encode();
}

zc::Maybe<SelectedModuleSourceQuery::Value> SelectedModuleSourceQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeSource(bytes);
}

query::TypedQueryResult<SelectedModuleSourceQuery::Value> SelectedModuleSourceQuery::provide(
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

bool SelectedModuleSourceQuery::verify(query::QueryContext& context, const Key& key,
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

zc::Array<uint8_t> ActiveModulesQuery::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ActiveModulesQuery::Key> ActiveModulesQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeCrate(bytes);
}

zc::Array<uint8_t> ActiveModulesQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveModulesQuery::Value> ActiveModulesQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ActiveModuleSetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ActiveModulesQuery::Value> ActiveModulesQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto stable = incremental_binding_query::StableCrateQueryKey::fromVerified(key);
  if (stable == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto sources =
      context.get<incremental_binding_query::ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stable));
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

bool ActiveModulesQuery::verify(query::QueryContext& context, const Key& key,
                                const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) { return false; }
  auto stable = incremental_binding_query::StableCrateQueryKey::fromVerified(key);
  if (stable == zc::none) { return false; }
  auto catalog = context.get<SelectedModuleCatalogInput>(key);
  auto sources =
      context.get<incremental_binding_query::ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stable));
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

zc::Array<uint8_t> ModuleDependencySitesQuery::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencySitesQuery::Key> ModuleDependencySitesQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencySitesQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencySitesQuery::Value> ModuleDependencySitesQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return DetachedModuleDependencySiteSet::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependencySitesQuery::Value> ModuleDependencySitesQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto selected = context.get<SelectedModuleSourceQuery>(key);
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

bool ModuleDependencySitesQuery::verify(query::QueryContext& context, const Key& key,
                                        const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto selected = context.get<SelectedModuleSourceQuery>(key);
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

zc::Array<uint8_t> ModuleDependencyRequestsQuery::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependencyRequestsQuery::Key> ModuleDependencyRequestsQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependencyRequestsQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependencyRequestsQuery::Value> ModuleDependencyRequestsQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleDependencyRequestSetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependencyRequestsQuery::Value>
ModuleDependencyRequestsQuery::provide(query::QueryContext& context, const Key& key) {
  auto sites = context.get<ModuleDependencySitesQuery>(key);
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

bool ModuleDependencyRequestsQuery::verify(query::QueryContext& context, const Key& key,
                                           const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto sites = context.get<ModuleDependencySitesQuery>(key);
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

zc::Array<uint8_t> ModuleDependenciesQuery::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleDependenciesQuery::Key> ModuleDependenciesQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeModule(bytes);
}

zc::Array<uint8_t> ModuleDependenciesQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependenciesQuery::Value> ModuleDependenciesQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleDependencySetRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleDependenciesQuery::Value> ModuleDependenciesQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto requests = context.get<ModuleDependencyRequestsQuery>(key);
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

bool ModuleDependenciesQuery::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto requests = context.get<ModuleDependencyRequestsQuery>(key);
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

struct VerifiedModuleGraphInputTransaction::Impl final {
  Impl(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       zc::Vector<identity::CrateKey>&& projectedCoreCrates,
       zc::Vector<SelectedModuleCatalog>&& catalogs,
       zc::Vector<DetachedModuleDependencySiteSet>&& dependencySites,
       zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
       zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&&
           catalogBuckets,
       zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
       zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
       zc::Vector<ConfiguredCratePrelude>&& configuredPreludes,
       VerifiedModuleGraphInputLedger&& priorLedger,
       VerifiedModuleGraphInputLedger&& nextLedger) noexcept
      : contextRoots(zc::mv(contextRoots)),
        projectedCoreCrates(zc::mv(projectedCoreCrates)),
        catalogs(zc::mv(catalogs)),
        dependencySites(zc::mv(dependencySites)),
        requesterAncestries(zc::mv(requesterAncestries)),
        catalogBuckets(zc::mv(catalogBuckets)),
        searchRoots(zc::mv(searchRoots)),
        dependencyAliases(zc::mv(dependencyAliases)),
        configuredPreludes(zc::mv(configuredPreludes)),
        priorLedger(zc::mv(priorLedger)),
        nextLedger(zc::mv(nextLedger)) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  zc::Vector<identity::CrateKey> projectedCoreCrates;
  zc::Vector<SelectedModuleCatalog> catalogs;
  zc::Vector<DetachedModuleDependencySiteSet> dependencySites;
  zc::Vector<identity::RequesterModuleAncestry> requesterAncestries;
  zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket> catalogBuckets;
  zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots> searchRoots;
  zc::Vector<ConfiguredDependencyAlias> dependencyAliases;
  zc::Vector<ConfiguredCratePrelude> configuredPreludes;
  VerifiedModuleGraphInputLedger priorLedger;
  VerifiedModuleGraphInputLedger nextLedger;
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
  const auto& value = *candidate.impl;
  if (!authority.registries.crates().isFrozen() || !authority.registries.sourceFiles().isFrozen() ||
      !authority.registries.modules().isFrozen() || authority.resolver.catalog().size() == 0 ||
      authority.parsedModules.size() != authority.resolver.catalog().size() ||
      authority.registries.modules().size() != authority.resolver.catalog().size() ||
      authority.registries.sourceFiles().size() != authority.resolver.catalog().size() ||
      authority.registries.sourceSnapshots().size() != authority.resolver.catalog().size()) {
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
  if (selectedCrates.size() != authority.registries.crates().size()) { return false; }
  for (size_t slot = 0; slot < authority.registries.crates().size(); ++slot) {
    auto expectedCrate = authority.registries.crates().keyAt(slot);
    if (expectedCrate == zc::none ||
        selectedCrates.find(zc::encodeHex(ZC_ASSERT_NONNULL(expectedCrate).encode().asPtr())) ==
            zc::none) {
      return false;
    }
  }
  if (selectedModules.size() != authority.resolver.catalog().size()) { return false; }
  zc::TreeMap<zc::String, ConfiguredDependencyAlias> expectedAliases;
  for (const auto& expected : authority.resolver.catalog()) {
    auto key = zc::encodeHex(expected.key.encode().asPtr());
    auto selected = selectedModules.find(key);
    auto registeredModule = authority.registries.modules().find(expected.key);
    auto registeredSource = authority.registries.sourceFiles().find(expected.source);
    if (selected == zc::none ||
        ZC_ASSERT_NONNULL(selected)->module().encode().asPtr() != expected.key.encode().asPtr() ||
        ZC_ASSERT_NONNULL(selected)->source().encode().asPtr() !=
            expected.source.encode().asPtr() ||
        registeredModule == zc::none || ZC_ASSERT_NONNULL(registeredModule) != expected.module ||
        registeredSource == zc::none) {
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
                                        value.nextLedger.encodeCanonical().asPtr()) {
    return false;
  }
  zc::Vector<ModuleGraphInputLedgerEntry> priorEntries(value.priorLedger.entries().size());
  for (const auto& entry : value.priorLedger.entries()) { priorEntries.add(entry.clone()); }
  auto reconstructedPrior = VerifiedModuleGraphInputLedger::from(zc::mv(priorEntries));
  return reconstructedPrior != zc::none &&
         ZC_ASSERT_NONNULL(reconstructedPrior).encodeCanonical().asPtr() ==
             value.priorLedger.encodeCanonical().asPtr();
}

zc::Maybe<VerifiedModuleGraphInputTransaction> VerifiedModuleGraphInputTransaction::prepare(
    const ModuleGraphInputTransactionAuthority& authority,
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
  VerifiedModuleGraphInputTransaction candidate(
      zc::heap<Impl>(zc::mv(contextRoots), zc::mv(projectedCoreCrates), zc::mv(catalogs),
                     zc::mv(dependencySites), zc::mv(requesterAncestries), zc::mv(catalogBuckets),
                     zc::mv(searchRoots), zc::mv(dependencyAliases), zc::mv(configuredPreludes),
                     priorLedger.clone(), zc::mv(ZC_ASSERT_NONNULL(nextLedger))));
  if (!ModuleGraphInputTransactionVerifier::verify(authority, candidate)) { return zc::none; }
  return zc::mv(candidate);
}

const incremental_binding_query::CompilationRootSetQueryKey&
VerifiedModuleGraphInputTransaction::contextRoots() const noexcept {
  return impl->contextRoots;
}

const VerifiedModuleGraphInputLedger& VerifiedModuleGraphInputTransaction::priorLedger()
    const noexcept {
  return impl->priorLedger;
}

const VerifiedModuleGraphInputLedger& VerifiedModuleGraphInputTransaction::nextLedger()
    const noexcept {
  return impl->nextLedger;
}

bool VerifiedModuleGraphInputTransaction::commit(query::QueryDatabase& database) {
  if (impl.get() == nullptr || impl->committed) { return false; }
  auto snapshot = database.snapshot();
  auto pending = database.beginInputTransaction(snapshot.revision());
  if (!pending.isOpened()) { return false; }
  auto transaction = zc::mv(pending).takeTransaction();
  for (const auto& prior : impl->priorLedger.entries()) {
    if (!ledgerContains(impl->nextLedger.entries(), prior) &&
        !eraseLedgerEntry(transaction, prior)) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& catalog : impl->catalogs) {
    if (!transaction.set<SelectedModuleCatalogInput>(catalog.crate(), catalog).isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& sites : impl->dependencySites) {
    if (!transaction.set<ModuleDependencySiteInput>(sites.module(), sites).isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& ancestry : impl->requesterAncestries) {
    if (!transaction
             .set<incremental_module_resolution_query::RequesterModuleAncestryInput>(
                 ancestry.requester(), ancestry)
             .isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& bucket : impl->catalogBuckets) {
    if (!transaction
             .set<incremental_module_resolution_query::ModuleCatalogPathBucketInput>(bucket.key(),
                                                                                     bucket)
             .isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& roots : impl->searchRoots) {
    if (!transaction
             .set<incremental_module_resolution_query::ModuleSearchRootsInput>(roots.crate(), roots)
             .isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& alias : impl->dependencyAliases) {
    if (!transaction
             .set<incremental_module_resolution_query::DependencyAliasRootInput>(alias.key,
                                                                                 alias.target)
             .isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  for (const auto& prelude : impl->configuredPreludes) {
    if (!transaction
             .set<incremental_module_resolution_query::ConfiguredPreludeInput>(prelude.crate,
                                                                               prelude.target)
             .isApplied()) {
      transaction.abandon();
      return false;
    }
  }
  if (!transaction.commit().isCommitted()) { return false; }
  impl->committed = true;
  return true;
}

bool registerModuleGraphQueries(query::QueryDatabase& database) {
  return database.registerDescriptor<SelectedModuleCatalogInput>().isRegistered() &&
         database.registerDescriptor<ModuleDependencySiteInput>().isRegistered() &&
         database.registerDescriptor<SelectedModuleSourceQuery>().isRegistered() &&
         database.registerDescriptor<ActiveModulesQuery>().isRegistered() &&
         database.registerDescriptor<ModuleDependencySitesQuery>().isRegistered() &&
         database.registerDescriptor<ModuleDependencyRequestsQuery>().isRegistered() &&
         database.registerDescriptor<ModuleDependenciesQuery>().isRegistered();
}

}  // namespace zomlang::compiler::driver::module_graph_query
