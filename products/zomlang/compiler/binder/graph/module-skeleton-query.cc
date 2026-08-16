// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/graph/module-skeleton-query.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/binder/stable/definition/header-producer.h"
#include "zomlang/compiler/binder/stable/header/verifier.h"
#include "zomlang/compiler/binder/stable/implementation/header-producer.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"

namespace zomlang::compiler::binder {
namespace {

namespace binding_query = driver::incremental_binding_query;
namespace graph_query = driver::module_graph_query;
namespace resolution_query = driver::incremental_module_resolution_query;

constexpr uint64_t kMaximumProjectionValueBytes = 134217728;

template <typename T>
zc::Maybe<CanonicalSequence<T>> admitFacts(zc::Vector<T>&& values);

bool hasProjectionDomain(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  return bytes.size() > domain.size() && bytes[domain.size()] == 0x00 &&
         bytes.first(domain.size()) == domain.asBytes();
}

zc::Array<uint8_t> withProjectionDomain(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> record) {
  zc::Vector<uint8_t> result(domain.size() + 1 + record.size());
  result.addAll(domain.asBytes());
  result.add(0x00);
  result.addAll(record);
  return result.releaseAsArray();
}

template <typename T>
zc::Array<uint8_t> encodeProjectionValue(zc::StringPtr domain, const T& value) {
  identity::CanonicalEncoder record;
  const auto encoded = StableBindingCodec<T>::encode(value);
  record.encodeByteString(encoded.asPtr());
  return withProjectionDomain(domain, record.finish().asPtr());
}

template <typename T>
zc::Maybe<T> decodeProjectionValue(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() > kMaximumProjectionValueBytes || !hasProjectionDomain(bytes, domain)) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto encoded = decoder.decodeByteString(kMaximumProjectionValueBytes);
  if (encoded == zc::none || !decoder.finished()) { return zc::none; }
  auto value = StableBindingCodec<T>::decode(ZC_ASSERT_NONNULL(encoded).asPtr());
  return value != zc::none &&
                 encodeProjectionValue(domain, ZC_ASSERT_NONNULL(value)).asPtr() == bytes
             ? zc::mv(value)
             : zc::none;
}

bool sameBindingName(const BindingNameKey& left, const BindingNameKey& right) {
  return StableBindingCodec<BindingNameKey>::encode(left).asPtr() ==
         StableBindingCodec<BindingNameKey>::encode(right).asPtr();
}

zc::Maybe<identity::ModuleKey> scopeModule(const StableScopeOwnerKey& scope) {
  const auto& value = scope.value();
  if (value.is<StableModuleScope>()) { return value.get<StableModuleScope>().module.clone(); }
  if (value.is<StableDefinitionScope>()) {
    return value.get<StableDefinitionScope>().definition.module().clone();
  }
  if (value.is<StableImplementationOccurrenceScope>()) {
    return value.get<StableImplementationOccurrenceScope>().occurrence.module().clone();
  }
  return zc::none;
}

bool isBindingVisibilityKey(const StableBindingTargetKey& key) {
  const auto& value = key.value();
  return !value.is<StableOwnerLocalBindingTarget>() &&
         !value.is<StableAnonymousOwnerBindingTarget>();
}

zc::Maybe<identity::DefinitionNamespace> semanticNamespace(Namespace nameSpace) {
  switch (nameSpace) {
    case Namespace::Value:
      return identity::DefinitionNamespace::Value;
    case Namespace::Type:
      return identity::DefinitionNamespace::Type;
    case Namespace::Module:
      return identity::DefinitionNamespace::Module;
    case Namespace::Label:
    case Namespace::Attribute:
      return zc::none;
  }
  return zc::none;
}

zc::Maybe<identity::SemanticImportOperation> semanticImportOperation(
    identity::ModuleDependencyKind kind) {
  if (kind == identity::ModuleDependencyKind::Import) {
    return identity::SemanticImportOperation::Import;
  }
  if (kind == identity::ModuleDependencyKind::ForeignReexport) {
    return identity::SemanticImportOperation::ForeignReexport;
  }
  return zc::none;
}

zc::Maybe<CanonicalSequence<StableImportFact>> projectImportedFacts(
    query::QueryContext& context, const identity::ModuleKey& module,
    const CanonicalParsedModule& parsed,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableImportFact> imports;
  const auto& tree = parsed.tree();
  for (const auto& dependency : dependencies.entries()) {
    const auto& request = dependency.request();
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) { continue; }
    auto operation = semanticImportOperation(request.dependencyKind());
    if (request.requester().encode().asPtr() != module.encode().asPtr() ||
        dependency.origin().kind() != graph_query::ModuleDependencyProvenanceOriginKind::Source) {
      return zc::none;
    }
    if (request.dependencyKind() == identity::ModuleDependencyKind::ModuleAlias) { continue; }
    if (operation == zc::none) { return zc::none; }
    auto resolution = context.get<resolution_query::ResolveModuleRequestQuery>(request.clone());
    if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
        resolution.value().candidates().size() != 1) {
      return zc::none;
    }
    const auto& targetModule = resolution.value().candidates()[0];
    auto exportNames = context.get<ModuleExportNames>(targetModule.clone());
    if (exportNames.isRuntimeFailure() || exportNames.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& site : dependency.origin().sites()) {
      if (!tree.contains(site.node())) { return zc::none; }
      const auto& syntax = tree.node(site.node());
      const bool import = request.dependencyKind() == identity::ModuleDependencyKind::Import;
      if (syntax.kind !=
          (import ? ast::SyntaxKind::ImportDeclaration : ast::SyntaxKind::ExportDeclaration)) {
        return zc::none;
      }
      const auto specifiers =
          ast::NodeList{syntax.payload.words[import ? ast::kImportDeclarationSpecifiersFirstWord
                                                    : ast::kExportDeclarationSpecifiersFirstWord],
                        syntax.payload.words[import ? ast::kImportDeclarationSpecifiersSizeWord
                                                    : ast::kExportDeclarationSpecifiersSizeWord]};
      if (!tree.contains(specifiers)) { return zc::none; }
      if (specifiers.empty()) {
        auto path = request.normalizedPath();
        if (path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) { return zc::none; }
        auto sourceName =
            identity::DeclaredDefinitionName::fromCanonical(ZC_ASSERT_NONNULL(path).back().text());
        const ast::IdentId alias(import ? syntax.payload.words[ast::kImportDeclarationAliasWord]
                                        : 0);
        auto localName = identity::DeclaredDefinitionName::fromCanonical(
            alias ? tree.ident(alias) : ZC_ASSERT_NONNULL(path).back().text());
        if (sourceName == zc::none || localName == zc::none) { return zc::none; }
        auto binding = identity::ImportBindingKey::from(
            module.clone(), request.clone(), ZC_ASSERT_NONNULL(operation),
            identity::DefinitionNamespace::Module, zc::mv(ZC_ASSERT_NONNULL(sourceName)),
            identity::DefinitionNamespace::Module, zc::mv(ZC_ASSERT_NONNULL(localName)));
        if (binding == zc::none) { return zc::none; }
        auto queryKey =
            StableSemanticImportQueryKey::from(module.clone(), zc::mv(ZC_ASSERT_NONNULL(binding)));
        if (queryKey == zc::none) { return zc::none; }
        zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
        auto target = StableBindingTargetKey::semanticImport(ZC_ASSERT_NONNULL(queryKey).clone());
        auto fact = StableImportFact::from(
            zc::mv(ZC_ASSERT_NONNULL(queryKey)), StableScopeOwnerKey::module(module.clone()),
            zc::mv(target), StableBindingTargetKey::module(targetModule.clone()), Namespace::Module,
            import ? BindingOrigin::ImportAlias : BindingOrigin::ReexportAlias, zc::mv(visibility),
            !import);
        if (fact == zc::none) { return zc::none; }
        imports.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
        continue;
      }
      for (const auto specifier : tree.list(specifiers)) {
        const auto expected =
            import ? ast::SyntaxKind::ImportSpecifier : ast::SyntaxKind::ExportSpecifier;
        if (!tree.contains(specifier) || tree.node(specifier).kind != expected) { return zc::none; }
        const auto& specifierSyntax = tree.node(specifier);
        const ast::IdentId sourceIdentifier(
            specifierSyntax.payload
                .words[import ? ast::kImportSpecifierNameWord : ast::kExportSpecifierNameWord]);
        const ast::IdentId aliasIdentifier(
            specifierSyntax.payload
                .words[import ? ast::kImportSpecifierAliasWord : ast::kExportSpecifierAliasWord]);
        if (!sourceIdentifier) { return zc::none; }
        auto sourceName =
            identity::DeclaredDefinitionName::fromCanonical(tree.ident(sourceIdentifier));
        auto localName = identity::DeclaredDefinitionName::fromCanonical(
            aliasIdentifier ? tree.ident(aliasIdentifier) : tree.ident(sourceIdentifier));
        if (sourceName == zc::none || localName == zc::none) { return zc::none; }
        for (const auto& exportedName : exportNames.value().values()) {
          if (exportedName.name() != ZC_ASSERT_NONNULL(sourceName)) { continue; }
          auto sourceNamespace = semanticNamespace(exportedName.nameSpace());
          if (sourceNamespace == zc::none) { return zc::none; }
          auto exported = context.get<ExportedBinding>(
              StableExportedBindingQueryKey::from(targetModule.clone(), exportedName.clone()));
          if (exported.isRuntimeFailure() || exported.kind() != query::QueryValueKind::Value) {
            return zc::none;
          }
          auto binding = identity::ImportBindingKey::from(
              module.clone(), request.clone(), ZC_ASSERT_NONNULL(operation),
              ZC_ASSERT_NONNULL(sourceNamespace), ZC_ASSERT_NONNULL(sourceName).clone(),
              ZC_ASSERT_NONNULL(sourceNamespace), ZC_ASSERT_NONNULL(localName).clone());
          if (binding == zc::none) { return zc::none; }
          auto queryKey = StableSemanticImportQueryKey::from(module.clone(),
                                                             zc::mv(ZC_ASSERT_NONNULL(binding)));
          if (queryKey == zc::none) { return zc::none; }
          zc::Maybe<MemberVisibility> visibility;
          ZC_IF_SOME(value, exported.value().visibility()) { visibility = value; }
          auto target = StableBindingTargetKey::semanticImport(ZC_ASSERT_NONNULL(queryKey).clone());
          auto fact = StableImportFact::from(
              zc::mv(ZC_ASSERT_NONNULL(queryKey)), StableScopeOwnerKey::module(module.clone()),
              zc::mv(target), exported.value().canonicalTarget().clone(), exportedName.nameSpace(),
              import ? BindingOrigin::ImportAlias : BindingOrigin::ReexportAlias,
              zc::mv(visibility), !import);
          if (fact == zc::none) { return zc::none; }
          imports.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
        }
      }
    }
  }
  return admitFacts(zc::mv(imports));
}

zc::Maybe<CanonicalSequence<StableImportFact>> verifyImportedFacts(
    query::QueryContext& context, const identity::ModuleKey& module,
    const CanonicalParsedModule& parsed,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableImportFact> imports;
  const auto& tree = parsed.tree();
  for (const auto& dependency : dependencies.entries()) {
    const auto& request = dependency.request();
    if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) { continue; }
    if (request.requester().encode().asPtr() != module.encode().asPtr() ||
        dependency.origin().kind() != graph_query::ModuleDependencyProvenanceOriginKind::Source) {
      return zc::none;
    }
    identity::SemanticImportOperation operation;
    bool isImport = false;
    switch (request.dependencyKind()) {
      case identity::ModuleDependencyKind::Import:
        operation = identity::SemanticImportOperation::Import;
        isImport = true;
        break;
      case identity::ModuleDependencyKind::ForeignReexport:
        operation = identity::SemanticImportOperation::ForeignReexport;
        break;
      case identity::ModuleDependencyKind::ModuleAlias:
        continue;
      default:
        return zc::none;
    }
    auto resolution = context.get<resolution_query::ResolveModuleRequestQuery>(request.clone());
    if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
        resolution.value().candidates().size() != 1) {
      return zc::none;
    }
    const auto& targetModule = resolution.value().candidates()[0];
    auto exports = context.get<ModuleExportNames>(targetModule.clone());
    if (exports.isRuntimeFailure() || exports.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& originSite : dependency.origin().sites()) {
      if (!tree.contains(originSite.node())) { return zc::none; }
      const auto& syntax = tree.node(originSite.node());
      if (syntax.kind !=
          (isImport ? ast::SyntaxKind::ImportDeclaration : ast::SyntaxKind::ExportDeclaration)) {
        return zc::none;
      }
      const ast::NodeList specifiers{
          syntax.payload.words[isImport ? ast::kImportDeclarationSpecifiersFirstWord
                                        : ast::kExportDeclarationSpecifiersFirstWord],
          syntax.payload.words[isImport ? ast::kImportDeclarationSpecifiersSizeWord
                                        : ast::kExportDeclarationSpecifiersSizeWord]};
      if (!tree.contains(specifiers)) { return zc::none; }
      if (specifiers.empty()) {
        auto path = request.normalizedPath();
        if (path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) { return zc::none; }
        auto sourceName =
            identity::DeclaredDefinitionName::fromCanonical(ZC_ASSERT_NONNULL(path).back().text());
        const ast::IdentId alias(isImport ? syntax.payload.words[ast::kImportDeclarationAliasWord]
                                          : 0);
        auto localName = identity::DeclaredDefinitionName::fromCanonical(
            alias ? tree.ident(alias) : ZC_ASSERT_NONNULL(path).back().text());
        if (sourceName == zc::none || localName == zc::none) { return zc::none; }
        auto binding = identity::ImportBindingKey::from(
            module.clone(), request.clone(), operation, identity::DefinitionNamespace::Module,
            zc::mv(ZC_ASSERT_NONNULL(sourceName)), identity::DefinitionNamespace::Module,
            zc::mv(ZC_ASSERT_NONNULL(localName)));
        if (binding == zc::none) { return zc::none; }
        auto key =
            StableSemanticImportQueryKey::from(module.clone(), zc::mv(ZC_ASSERT_NONNULL(binding)));
        if (key == zc::none) { return zc::none; }
        zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
        auto target = StableBindingTargetKey::semanticImport(ZC_ASSERT_NONNULL(key).clone());
        auto fact = StableImportFact::from(
            zc::mv(ZC_ASSERT_NONNULL(key)), StableScopeOwnerKey::module(module.clone()),
            zc::mv(target), StableBindingTargetKey::module(targetModule.clone()), Namespace::Module,
            isImport ? BindingOrigin::ImportAlias : BindingOrigin::ReexportAlias,
            zc::mv(visibility), !isImport);
        if (fact == zc::none) { return zc::none; }
        imports.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
        continue;
      }
      for (const auto specifier : tree.list(specifiers)) {
        const auto expectedKind =
            isImport ? ast::SyntaxKind::ImportSpecifier : ast::SyntaxKind::ExportSpecifier;
        if (!tree.contains(specifier) || tree.node(specifier).kind != expectedKind) {
          return zc::none;
        }
        const auto& specifierSyntax = tree.node(specifier);
        const ast::IdentId imported(
            specifierSyntax.payload
                .words[isImport ? ast::kImportSpecifierNameWord : ast::kExportSpecifierNameWord]);
        const ast::IdentId renamed(
            specifierSyntax.payload
                .words[isImport ? ast::kImportSpecifierAliasWord : ast::kExportSpecifierAliasWord]);
        if (!imported) { return zc::none; }
        auto sourceName = identity::DeclaredDefinitionName::fromCanonical(tree.ident(imported));
        auto localName = identity::DeclaredDefinitionName::fromCanonical(
            renamed ? tree.ident(renamed) : tree.ident(imported));
        if (sourceName == zc::none || localName == zc::none) { return zc::none; }
        for (const auto& exportName : exports.value().values()) {
          if (exportName.name() != ZC_ASSERT_NONNULL(sourceName)) { continue; }
          auto nameSpace = semanticNamespace(exportName.nameSpace());
          if (nameSpace == zc::none) { return zc::none; }
          auto exported = context.get<ExportedBinding>(
              StableExportedBindingQueryKey::from(targetModule.clone(), exportName.clone()));
          if (exported.isRuntimeFailure() || exported.kind() != query::QueryValueKind::Value) {
            return zc::none;
          }
          auto binding = identity::ImportBindingKey::from(
              module.clone(), request.clone(), operation, ZC_ASSERT_NONNULL(nameSpace),
              ZC_ASSERT_NONNULL(sourceName).clone(), ZC_ASSERT_NONNULL(nameSpace),
              ZC_ASSERT_NONNULL(localName).clone());
          if (binding == zc::none) { return zc::none; }
          auto key = StableSemanticImportQueryKey::from(module.clone(),
                                                        zc::mv(ZC_ASSERT_NONNULL(binding)));
          if (key == zc::none) { return zc::none; }
          zc::Maybe<MemberVisibility> visibility;
          ZC_IF_SOME(value, exported.value().visibility()) { visibility = value; }
          auto target = StableBindingTargetKey::semanticImport(ZC_ASSERT_NONNULL(key).clone());
          auto fact = StableImportFact::from(
              zc::mv(ZC_ASSERT_NONNULL(key)), StableScopeOwnerKey::module(module.clone()),
              zc::mv(target), exported.value().canonicalTarget().clone(), exportName.nameSpace(),
              isImport ? BindingOrigin::ImportAlias : BindingOrigin::ReexportAlias,
              zc::mv(visibility), !isImport);
          if (fact == zc::none) { return zc::none; }
          imports.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
        }
      }
    }
  }
  return admitFacts(zc::mv(imports));
}

zc::Maybe<LocalSyntaxPath> moduleBodyPathForNode(const ModuleBodyProvenance& provenance,
                                                 ast::NodeId node) {
  zc::Maybe<LocalSyntaxPath> result;
  for (const auto& entry : provenance.entries()) {
    if (entry.node != node) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry.path.clone();
  }
  return result;
}

zc::Maybe<StableDeclarationFact> declarationForNode(
    const identity::ModuleKey& module, ast::NodeId node,
    const RevisionLocalDefinitionSites& definitionSites,
    const CanonicalSequence<StableDeclarationFact>& declarations) {
  zc::Maybe<StableDefinitionQueryKey> key;
  for (const auto& site : definitionSites.entries()) {
    if (site.node() != node) { continue; }
    if (key != zc::none) { return zc::none; }
    key = StableDefinitionQueryKey::from(module.clone(), site.definition().clone());
  }
  if (key == zc::none) { return zc::none; }
  zc::Maybe<StableDeclarationFact> result;
  for (const auto& declaration : declarations.values()) {
    if (declaration.queryKey() != ZC_ASSERT_NONNULL(key)) { continue; }
    if (result != zc::none) { return zc::none; }
    result = declaration.clone();
  }
  return result;
}

zc::Maybe<ModuleAliasExportNamesRevision> moduleAliasExportNamesRevision(
    const identity::ModuleKey& module, const CanonicalSequence<BindingNameKey>& exportNames) {
  identity::CanonicalEncoder record;
  const auto moduleBytes = module.encode();
  const auto exportBytes =
      StableBindingCodec<CanonicalSequence<BindingNameKey>>::encode(exportNames);
  record.encodeByteString(moduleBytes.asPtr());
  record.encodeByteString(exportBytes.asPtr());
  const auto preimage =
      withProjectionDomain("zom.binder.module-alias-target-surface"_zcc, record.finish().asPtr());
  auto digest = identity::sha256(preimage.asPtr());
  if (digest == zc::none) { return zc::none; }
  return ModuleAliasExportNamesRevision::fromDigest(ZC_ASSERT_NONNULL(digest));
}

zc::Maybe<CanonicalSequence<StableModuleAliasFact>> projectModuleAliasFacts(
    query::QueryContext& context, const identity::ModuleKey& module,
    const CanonicalParsedModule& parsed, const RevisionLocalDefinitionSites& definitionSites,
    const CanonicalSequence<StableDeclarationFact>& declarations,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableModuleAliasFact> aliases;
  const auto& tree = parsed.tree();
  for (const auto& dependency : dependencies.entries()) {
    const auto& request = dependency.request();
    if (request.dependencyKind() != identity::ModuleDependencyKind::ModuleAlias) { continue; }
    if (request.requester().encode().asPtr() != module.encode().asPtr() ||
        dependency.origin().kind() != graph_query::ModuleDependencyProvenanceOriginKind::Source) {
      return zc::none;
    }
    auto resolution = context.get<resolution_query::ResolveModuleRequestQuery>(request.clone());
    if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
        resolution.value().candidates().size() != 1) {
      return zc::none;
    }
    const auto& targetModule = resolution.value().candidates()[0];
    auto exportNames = context.get<ModuleExportNames>(targetModule.clone());
    if (exportNames.isRuntimeFailure() || exportNames.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    auto revision = moduleAliasExportNamesRevision(targetModule, exportNames.value());
    auto path = request.normalizedPath();
    if (revision == zc::none || path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) {
      return zc::none;
    }
    auto sourceName =
        identity::DeclaredDefinitionName::fromCanonical(ZC_ASSERT_NONNULL(path).back().text());
    if (sourceName == zc::none) { return zc::none; }
    for (const auto& site : dependency.origin().sites()) {
      if (!tree.contains(site.node())) { return zc::none; }
      const auto& syntax = tree.node(site.node());
      if (syntax.kind != ast::SyntaxKind::ModuleDeclaration ||
          static_cast<ast::ModuleDeclarationForm>(
              syntax.payload.words[ast::kModuleDeclarationFormWord]) !=
              ast::ModuleDeclarationForm::Alias) {
        return zc::none;
      }
      const ast::IdentId aliasIdentifier(
          syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]);
      if (!aliasIdentifier) { return zc::none; }
      auto localName = identity::DeclaredDefinitionName::fromCanonical(tree.ident(aliasIdentifier));
      auto alias = declarationForNode(module, site.node(), definitionSites, declarations);
      if (localName == zc::none || alias == zc::none) { return zc::none; }
      auto binding = identity::ImportBindingKey::from(
          module.clone(), request.clone(), identity::SemanticImportOperation::ModuleAlias,
          identity::DefinitionNamespace::Module, ZC_ASSERT_NONNULL(sourceName).clone(),
          identity::DefinitionNamespace::Module, zc::mv(ZC_ASSERT_NONNULL(localName)));
      if (binding == zc::none) { return zc::none; }
      auto queryKey =
          StableSemanticImportQueryKey::from(module.clone(), zc::mv(ZC_ASSERT_NONNULL(binding)));
      if (queryKey == zc::none) { return zc::none; }
      auto fact = StableModuleAliasFact::from(zc::mv(ZC_ASSERT_NONNULL(queryKey)),
                                              StableScopeOwnerKey::module(module.clone()),
                                              ZC_ASSERT_NONNULL(alias).queryKey().clone(),
                                              targetModule.clone(), ZC_ASSERT_NONNULL(revision));
      if (fact == zc::none) { return zc::none; }
      aliases.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
  }
  return admitFacts(zc::mv(aliases));
}

zc::Maybe<CanonicalSequence<StableModuleAliasFact>> verifyModuleAliasFacts(
    query::QueryContext& context, const identity::ModuleKey& module,
    const CanonicalParsedModule& parsed, const RevisionLocalDefinitionSites& definitionSites,
    const CanonicalSequence<StableDeclarationFact>& declarations,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableModuleAliasFact> aliases;
  const auto& tree = parsed.tree();
  for (const auto& dependency : dependencies.entries()) {
    const auto& request = dependency.request();
    if (request.dependencyKind() != identity::ModuleDependencyKind::ModuleAlias) { continue; }
    if (request.requester().encode().asPtr() != module.encode().asPtr() ||
        dependency.origin().kind() != graph_query::ModuleDependencyProvenanceOriginKind::Source) {
      return zc::none;
    }
    auto resolution = context.get<resolution_query::ResolveModuleRequestQuery>(request.clone());
    if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
        resolution.value().candidates().size() != 1) {
      return zc::none;
    }
    const auto& canonicalModule = resolution.value().candidates()[0];
    auto exports = context.get<ModuleExportNames>(canonicalModule.clone());
    if (exports.isRuntimeFailure() || exports.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    auto revision = moduleAliasExportNamesRevision(canonicalModule, exports.value());
    auto path = request.normalizedPath();
    if (revision == zc::none || path == zc::none || ZC_ASSERT_NONNULL(path).size() == 0) {
      return zc::none;
    }
    auto importedName =
        identity::DeclaredDefinitionName::fromCanonical(ZC_ASSERT_NONNULL(path).back().text());
    if (importedName == zc::none) { return zc::none; }
    for (const auto& originSite : dependency.origin().sites()) {
      if (!tree.contains(originSite.node())) { return zc::none; }
      const auto& syntax = tree.node(originSite.node());
      if (syntax.kind != ast::SyntaxKind::ModuleDeclaration ||
          static_cast<ast::ModuleDeclarationForm>(
              syntax.payload.words[ast::kModuleDeclarationFormWord]) !=
              ast::ModuleDeclarationForm::Alias) {
        return zc::none;
      }
      const ast::IdentId identifier(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]);
      if (!identifier) { return zc::none; }
      auto localName = identity::DeclaredDefinitionName::fromCanonical(tree.ident(identifier));
      auto definition =
          declarationForNode(module, originSite.node(), definitionSites, declarations);
      if (localName == zc::none || definition == zc::none) { return zc::none; }
      auto binding = identity::ImportBindingKey::from(
          module.clone(), request.clone(), identity::SemanticImportOperation::ModuleAlias,
          identity::DefinitionNamespace::Module, ZC_ASSERT_NONNULL(importedName).clone(),
          identity::DefinitionNamespace::Module, zc::mv(ZC_ASSERT_NONNULL(localName)));
      if (binding == zc::none) { return zc::none; }
      auto key =
          StableSemanticImportQueryKey::from(module.clone(), zc::mv(ZC_ASSERT_NONNULL(binding)));
      if (key == zc::none) { return zc::none; }
      auto fact = StableModuleAliasFact::from(zc::mv(ZC_ASSERT_NONNULL(key)),
                                              StableScopeOwnerKey::module(module.clone()),
                                              ZC_ASSERT_NONNULL(definition).queryKey().clone(),
                                              canonicalModule.clone(), ZC_ASSERT_NONNULL(revision));
      if (fact == zc::none) { return zc::none; }
      aliases.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
  }
  return admitFacts(zc::mv(aliases));
}

zc::Maybe<CanonicalSequence<StableReexportStep>> reexportChain(
    const identity::ModuleKey& module, const LocalSyntaxPath& exportPath,
    const StableBindingTargetKey& binding, const StableBindingTargetKey& canonicalTarget,
    bool includeStep) {
  zc::Vector<StableReexportStep> steps;
  if (includeStep) {
    steps.add(StableReexportStep::from(module.clone(), exportPath.clone(), binding.clone(),
                                       canonicalTarget.clone()));
  }
  return admitFacts(zc::mv(steps));
}

zc::Maybe<StableLocalExportFact> localExport(const identity::ModuleKey& module,
                                             LocalSyntaxPath&& exportPath, BindingNameKey&& name,
                                             StableBindingTargetKey&& binding,
                                             StableBindingTargetKey&& canonicalTarget,
                                             bool includeStep) {
  auto chain = reexportChain(module, exportPath, binding, canonicalTarget, includeStep);
  if (chain == zc::none) { return zc::none; }
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  return StableLocalExportFact::from(module.clone(), zc::mv(exportPath), zc::mv(name),
                                     zc::mv(binding), zc::mv(canonicalTarget), zc::mv(visibility),
                                     zc::mv(ZC_ASSERT_NONNULL(chain)));
}

zc::Maybe<CanonicalSequence<StableLocalExportFact>> projectLocalExportFacts(
    const identity::ModuleKey& module, const CanonicalParsedModule& parsed,
    const ModuleBodyProvenance& provenance, const RevisionLocalDefinitionSites& definitionSites,
    const CanonicalSequence<StableDeclarationFact>& declarations,
    const CanonicalSequence<StableImportFact>& imports,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  const auto& tree = parsed.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  zc::Vector<ast::NodeId> exportNodes;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::ExportDeclaration &&
        moduleBodyPathForNode(provenance, node) != zc::none) {
      exportNodes.add(node);
    }
  });
  zc::Vector<StableLocalExportFact> exports;
  for (const auto node : exportNodes) {
    const auto& syntax = tree.node(node);
    auto path = moduleBodyPathForNode(provenance, node);
    if (path == zc::none) { return zc::none; }
    const ast::NodeId declaration(syntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    const ast::NodeId modulePath(syntax.payload.words[ast::kExportDeclarationPathWord]);
    const ast::NodeList specifiers{syntax.payload.words[ast::kExportDeclarationSpecifiersFirstWord],
                                   syntax.payload.words[ast::kExportDeclarationSpecifiersSizeWord]};
    if (!tree.contains(specifiers)) { return zc::none; }
    if (tree.contains(declaration)) {
      auto fact = declarationForNode(module, declaration, definitionSites, declarations);
      if (fact == zc::none) { return zc::none; }
      auto target = StableBindingTargetKey::definition(ZC_ASSERT_NONNULL(fact).queryKey().clone());
      auto name = BindingNameKey::from(ZC_ASSERT_NONNULL(fact).nameSpace(),
                                       ZC_ASSERT_NONNULL(fact).name().clone());
      if (name == zc::none) { return zc::none; }
      auto exportFact = localExport(
          module, ZC_ASSERT_NONNULL(path).clone(), zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(target),
          StableBindingTargetKey::definition(ZC_ASSERT_NONNULL(fact).queryKey().clone()), false);
      if (exportFact == zc::none) { return zc::none; }
      exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
      continue;
    }
    if (tree.contains(modulePath)) {
      for (const auto& dependency : dependencies.entries()) {
        if (dependency.request().dependencyKind() !=
                identity::ModuleDependencyKind::ForeignReexport ||
            dependency.origin().kind() !=
                graph_query::ModuleDependencyProvenanceOriginKind::Source) {
          continue;
        }
        bool ownsExport = false;
        for (const auto& site : dependency.origin().sites()) {
          if (site.node() == node) { ownsExport = true; }
        }
        if (!ownsExport) { continue; }
        for (const auto& import : imports.values()) {
          if (import.queryKey().binding().operation() !=
                  identity::SemanticImportOperation::ForeignReexport ||
              import.queryKey().binding().resolution().encode().asPtr() !=
                  dependency.request().encode().asPtr()) {
            continue;
          }
          auto name = BindingNameKey::from(import.nameSpace(),
                                           import.queryKey().binding().localName().clone());
          if (name == zc::none) { return zc::none; }
          auto exportFact =
              localExport(module, ZC_ASSERT_NONNULL(path).clone(), zc::mv(ZC_ASSERT_NONNULL(name)),
                          import.target().clone(), import.canonicalTarget().clone(), true);
          if (exportFact == zc::none) { return zc::none; }
          exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
        }
      }
      continue;
    }
    for (const auto specifier : tree.list(specifiers)) {
      if (!tree.contains(specifier) ||
          tree.node(specifier).kind != ast::SyntaxKind::ExportSpecifier) {
        return zc::none;
      }
      const auto& specifierSyntax = tree.node(specifier);
      const ast::IdentId sourceIdentifier(
          specifierSyntax.payload.words[ast::kExportSpecifierNameWord]);
      const ast::IdentId aliasIdentifier(
          specifierSyntax.payload.words[ast::kExportSpecifierAliasWord]);
      if (!sourceIdentifier) { return zc::none; }
      const auto sourceName = tree.ident(sourceIdentifier);
      const auto exportedName = tree.ident(aliasIdentifier ? aliasIdentifier : sourceIdentifier);
      zc::Maybe<StableBindingTargetKey> binding;
      zc::Maybe<StableBindingTargetKey> canonicalTarget;
      zc::Maybe<Namespace> nameSpace;
      for (const auto& declarationFact : declarations.values()) {
        if (declarationFact.name().text() != sourceName) { continue; }
        if (binding != zc::none) { return zc::none; }
        binding = StableBindingTargetKey::definition(declarationFact.queryKey().clone());
        canonicalTarget = StableBindingTargetKey::definition(declarationFact.queryKey().clone());
        nameSpace = declarationFact.nameSpace();
      }
      for (const auto& import : imports.values()) {
        if (import.queryKey().binding().localName().text() != sourceName) { continue; }
        if (binding != zc::none) { return zc::none; }
        binding = import.target().clone();
        canonicalTarget = import.canonicalTarget().clone();
        nameSpace = import.nameSpace();
      }
      if (binding == zc::none || canonicalTarget == zc::none || nameSpace == zc::none) {
        return zc::none;
      }
      auto name = identity::DeclaredDefinitionName::fromCanonical(exportedName);
      if (name == zc::none) { return zc::none; }
      auto bindingName =
          BindingNameKey::from(ZC_ASSERT_NONNULL(nameSpace), zc::mv(ZC_ASSERT_NONNULL(name)));
      if (bindingName == zc::none) { return zc::none; }
      auto exportFact = localExport(
          module, ZC_ASSERT_NONNULL(path).clone(), zc::mv(ZC_ASSERT_NONNULL(bindingName)),
          zc::mv(ZC_ASSERT_NONNULL(binding)), zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)), true);
      if (exportFact == zc::none) { return zc::none; }
      exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
    }
  }
  return admitFacts(zc::mv(exports));
}

zc::Maybe<CanonicalSequence<StableLocalExportFact>> verifyLocalExportFacts(
    const identity::ModuleKey& module, const CanonicalParsedModule& parsed,
    const ModuleBodyProvenance& provenance, const RevisionLocalDefinitionSites& definitionSites,
    const CanonicalSequence<StableDeclarationFact>& declarations,
    const CanonicalSequence<StableImportFact>& imports,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  const auto& tree = parsed.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  zc::Vector<StableLocalExportFact> exports;
  for (const auto& provenanceEntry : provenance.entries()) {
    if (!tree.contains(provenanceEntry.node) ||
        tree.node(provenanceEntry.node).kind != ast::SyntaxKind::ExportDeclaration) {
      continue;
    }
    const auto& syntax = tree.node(provenanceEntry.node);
    const ast::NodeId declaration(syntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    const ast::NodeId modulePath(syntax.payload.words[ast::kExportDeclarationPathWord]);
    const ast::NodeList specifiers{syntax.payload.words[ast::kExportDeclarationSpecifiersFirstWord],
                                   syntax.payload.words[ast::kExportDeclarationSpecifiersSizeWord]};
    if (!tree.contains(specifiers)) { return zc::none; }
    const auto makeFact = [&](BindingNameKey&& name, StableBindingTargetKey&& target,
                              StableBindingTargetKey&& canonical, bool reexport) {
      zc::Vector<StableReexportStep> steps;
      if (reexport) {
        steps.add(StableReexportStep::from(module.clone(), provenanceEntry.path.clone(),
                                           target.clone(), canonical.clone()));
      }
      auto chain = StableBindingSequenceBuilder<StableReexportStep>::from(zc::mv(steps));
      if (chain == zc::none) { return zc::Maybe<StableLocalExportFact>(); }
      zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
      return StableLocalExportFact::from(module.clone(), provenanceEntry.path.clone(), zc::mv(name),
                                         zc::mv(target), zc::mv(canonical), zc::mv(visibility),
                                         zc::mv(ZC_ASSERT_NONNULL(chain)));
    };
    if (tree.contains(declaration)) {
      auto fact = declarationForNode(module, declaration, definitionSites, declarations);
      if (fact == zc::none) { return zc::none; }
      auto name = BindingNameKey::from(ZC_ASSERT_NONNULL(fact).nameSpace(),
                                       ZC_ASSERT_NONNULL(fact).name().clone());
      if (name == zc::none) { return zc::none; }
      auto target = StableBindingTargetKey::definition(ZC_ASSERT_NONNULL(fact).queryKey().clone());
      auto exportFact = makeFact(
          zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(target),
          StableBindingTargetKey::definition(ZC_ASSERT_NONNULL(fact).queryKey().clone()), false);
      if (exportFact == zc::none) { return zc::none; }
      exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
      continue;
    }
    if (tree.contains(modulePath)) {
      for (const auto& dependency : dependencies.entries()) {
        if (dependency.request().dependencyKind() !=
                identity::ModuleDependencyKind::ForeignReexport ||
            dependency.origin().kind() !=
                graph_query::ModuleDependencyProvenanceOriginKind::Source) {
          continue;
        }
        bool matches = false;
        for (const auto& site : dependency.origin().sites()) {
          matches = matches || site.node() == provenanceEntry.node;
        }
        if (!matches) { continue; }
        for (const auto& imported : imports.values()) {
          if (imported.queryKey().binding().operation() !=
                  identity::SemanticImportOperation::ForeignReexport ||
              imported.queryKey().binding().resolution().encode().asPtr() !=
                  dependency.request().encode().asPtr()) {
            continue;
          }
          auto name = BindingNameKey::from(imported.nameSpace(),
                                           imported.queryKey().binding().localName().clone());
          if (name == zc::none) { return zc::none; }
          auto exportFact = makeFact(zc::mv(ZC_ASSERT_NONNULL(name)), imported.target().clone(),
                                     imported.canonicalTarget().clone(), true);
          if (exportFact == zc::none) { return zc::none; }
          exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
        }
      }
      continue;
    }
    for (const auto specifier : tree.list(specifiers)) {
      if (!tree.contains(specifier) ||
          tree.node(specifier).kind != ast::SyntaxKind::ExportSpecifier) {
        return zc::none;
      }
      const auto& specifierSyntax = tree.node(specifier);
      const ast::IdentId source(specifierSyntax.payload.words[ast::kExportSpecifierNameWord]);
      const ast::IdentId alias(specifierSyntax.payload.words[ast::kExportSpecifierAliasWord]);
      if (!source) { return zc::none; }
      const auto sourceName = tree.ident(source);
      zc::Maybe<StableBindingTargetKey> target;
      zc::Maybe<StableBindingTargetKey> canonical;
      zc::Maybe<Namespace> nameSpace;
      for (const auto& declared : declarations.values()) {
        if (declared.name().text() != sourceName) { continue; }
        if (target != zc::none) { return zc::none; }
        target = StableBindingTargetKey::definition(declared.queryKey().clone());
        canonical = StableBindingTargetKey::definition(declared.queryKey().clone());
        nameSpace = declared.nameSpace();
      }
      for (const auto& imported : imports.values()) {
        if (imported.queryKey().binding().localName().text() != sourceName) { continue; }
        if (target != zc::none) { return zc::none; }
        target = imported.target().clone();
        canonical = imported.canonicalTarget().clone();
        nameSpace = imported.nameSpace();
      }
      if (target == zc::none || canonical == zc::none || nameSpace == zc::none) { return zc::none; }
      auto name =
          identity::DeclaredDefinitionName::fromCanonical(tree.ident(alias ? alias : source));
      if (name == zc::none) { return zc::none; }
      auto bindingName =
          BindingNameKey::from(ZC_ASSERT_NONNULL(nameSpace), zc::mv(ZC_ASSERT_NONNULL(name)));
      if (bindingName == zc::none) { return zc::none; }
      auto exportFact =
          makeFact(zc::mv(ZC_ASSERT_NONNULL(bindingName)), zc::mv(ZC_ASSERT_NONNULL(target)),
                   zc::mv(ZC_ASSERT_NONNULL(canonical)), true);
      if (exportFact == zc::none) { return zc::none; }
      exports.add(zc::mv(ZC_ASSERT_NONNULL(exportFact)));
    }
  }
  return admitFacts(zc::mv(exports));
}

zc::Array<uint8_t> encodeBindingVisibilityValue(const zc::Maybe<MemberVisibility>& value) {
  identity::CanonicalEncoder record;
  ZC_IF_SOME(visibility, value) {
    record.encodeSome();
    record.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    record.encodeNone();
  }
  return withProjectionDomain("zom.query.binding-visibility-value"_zcc, record.finish().asPtr());
}

zc::Maybe<zc::Maybe<MemberVisibility>> decodeBindingVisibilityValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.query.binding-visibility-value"_zcc;
  if (bytes.size() > kMaximumProjectionValueBytes || !hasProjectionDomain(bytes, domain)) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  zc::Maybe<MemberVisibility> result;
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto encoded = decoder.decodeUint8();
    if (encoded == zc::none) { return zc::none; }
    const auto visibility = static_cast<MemberVisibility>(ZC_ASSERT_NONNULL(encoded));
    if (visibility < MemberVisibility::Public || visibility > MemberVisibility::Protected) {
      return zc::none;
    }
    result = visibility;
  } else if (ZC_ASSERT_NONNULL(tag) != 0x00) {
    return zc::none;
  }
  return decoder.finished() && encodeBindingVisibilityValue(result).asPtr() == bytes
             ? zc::Maybe<zc::Maybe<MemberVisibility>>(zc::mv(result))
             : zc::none;
}

zc::Array<uint8_t> encodeImportTargetValue(const zc::Maybe<StableImportFact>& value) {
  identity::CanonicalEncoder record;
  ZC_IF_SOME(import, value) {
    const auto encoded = StableBindingCodec<StableImportFact>::encode(import);
    record.encodeSome();
    record.encodeByteString(encoded.asPtr());
  } else {
    record.encodeNone();
  }
  return withProjectionDomain("zom.query.import-target-value"_zcc, record.finish().asPtr());
}

zc::Maybe<zc::Maybe<StableImportFact>> decodeImportTargetValue(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr auto domain = "zom.query.import-target-value"_zcc;
  if (bytes.size() > kMaximumProjectionValueBytes || !hasProjectionDomain(bytes, domain)) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes.slice(domain.size() + 1, bytes.size()));
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  zc::Maybe<StableImportFact> result;
  if (ZC_ASSERT_NONNULL(tag) == 0x01) {
    auto encoded = decoder.decodeByteString(kMaximumProjectionValueBytes);
    if (encoded == zc::none) { return zc::none; }
    auto import = StableBindingCodec<StableImportFact>::decode(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (import == zc::none) { return zc::none; }
    result = zc::mv(ZC_ASSERT_NONNULL(import));
  } else if (ZC_ASSERT_NONNULL(tag) != 0x00) {
    return zc::none;
  }
  return decoder.finished() && encodeImportTargetValue(result).asPtr() == bytes
             ? zc::Maybe<zc::Maybe<StableImportFact>>(zc::mv(result))
             : zc::none;
}

query::TypedQueryResult<zc::Maybe<MemberVisibility>> readDefinitionVisibility(
    query::QueryContext& context, const StableDefinitionQueryKey& key) {
  auto definition = context.get<DefinitionBindingHeader>(key.clone());
  if (definition.isRuntimeFailure()) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::runtimeFailure(
        definition.runtimeFailure());
  }
  if (definition.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::semanticFailure(
        zc::heapArray<uint8_t>(definition.semanticFailureBytes()));
  }
  zc::Maybe<MemberVisibility> visibility;
  if (definition.kind() == query::QueryValueKind::Value) {
    ZC_IF_SOME(value, definition.value().visibility()) { visibility = value; }
  }
  return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::value(zc::mv(visibility));
}

query::TypedQueryResult<zc::Maybe<MemberVisibility>> readModuleVisibility(
    query::QueryContext& context, const identity::ModuleKey& key) {
  auto exports = context.get<ModuleExportNames>(key.clone());
  if (exports.isRuntimeFailure()) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::runtimeFailure(
        exports.runtimeFailure());
  }
  if (exports.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::semanticFailure(
        zc::heapArray<uint8_t>(exports.semanticFailureBytes()));
  }
  if (exports.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
  return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::value(zc::mv(visibility));
}

query::TypedQueryResult<zc::Maybe<MemberVisibility>> readImportVisibility(
    query::QueryContext& context, const StableSemanticImportQueryKey& key) {
  auto import = context.get<ImportTarget>(key.clone());
  if (import.isRuntimeFailure()) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::runtimeFailure(
        import.runtimeFailure());
  }
  if (import.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::semanticFailure(
        zc::heapArray<uint8_t>(import.semanticFailureBytes()));
  }
  if (import.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Maybe<MemberVisibility> visibility;
  ZC_IF_SOME(value, import.value()) {
    ZC_IF_SOME(importVisibility, value.visibility()) { visibility = importVisibility; }
  }
  return query::TypedQueryResult<zc::Maybe<MemberVisibility>>::value(zc::mv(visibility));
}

struct HeaderCommonAuthority final {
  CanonicalParsedModule parsed;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

template <typename Inventory>
struct HeaderProviderAuthority final {
  CanonicalParsedModule parsed;
  Inventory inventory;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

using DefinitionHeaderAuthority = HeaderProviderAuthority<NamedDefinitionInventory>;
using ImplementationHeaderAuthority = HeaderProviderAuthority<NamedImplementationInventory>;

struct HeaderVerificationAuthority final {
  CanonicalParsedModule parsed;
  NamedDefinitionInventory definitions;
  NamedImplementationInventory implementations;
  RevisionLocalDefinitionSites definitionSites;
  RevisionLocalImplementationSites implementationSites;
};

struct HeaderSourceRejection final {
  CanonicalNonEmptySequence<diagnostics::DiagnosticFact> diagnostics;
};

struct HeaderKeyRejection final {
  BinderKeyFailure failure;
};

struct HeaderRuntimeRejection final {
  query::QueryRuntimeFailure failure;
};

struct ProjectionSkeletonValue final {
  BoundModuleSkeleton skeleton;
};

struct ProjectionSkeletonSourceRejection final {
  CanonicalNonEmptySequence<diagnostics::DiagnosticFact> diagnostics;
};

struct ProjectionSkeletonKeyRejection final {};

struct ProjectionSkeletonRuntimeRejection final {
  query::QueryRuntimeFailure failure;
};

using ProjectionSkeletonRead =
    zc::OneOf<ProjectionSkeletonValue, ProjectionSkeletonSourceRejection,
              ProjectionSkeletonKeyRejection, ProjectionSkeletonRuntimeRejection>;

template <typename Authority>
using HeaderAuthorityRead =
    zc::OneOf<Authority, HeaderSourceRejection, HeaderKeyRejection, HeaderRuntimeRejection>;

ProjectionSkeletonRead readProjectionSkeleton(query::QueryContext& context,
                                              const identity::ModuleKey& module) {
  auto result = context.get<BindModuleSkeleton>(module.clone());
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return ProjectionSkeletonRead(ProjectionSkeletonRuntimeRejection{
        result.isRuntimeFailure() ? result.runtimeFailure()
                                  : query::QueryRuntimeFailure::InvariantViolation});
  }
  const auto& source = result.value();
  if (source.storage().is<BinderSourceRejected>()) {
    return ProjectionSkeletonRead(ProjectionSkeletonSourceRejection{
        source.storage().get<BinderSourceRejected>().diagnostics.clone()});
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return ProjectionSkeletonRead(ProjectionSkeletonKeyRejection{});
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>()) {
    return ProjectionSkeletonRead(
        ProjectionSkeletonRuntimeRejection{query::QueryRuntimeFailure::InvariantViolation});
  }
  const auto& value = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (value.diagnostics.values().size() != 0) {
    return ProjectionSkeletonRead(
        ProjectionSkeletonRuntimeRejection{query::QueryRuntimeFailure::InvariantViolation});
  }
  return ProjectionSkeletonRead(ProjectionSkeletonValue{value.value.clone()});
}

zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>> cloneDiagnostics(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics) {
  zc::Vector<diagnostics::DiagnosticFact> copies(diagnostics.size());
  for (const auto& diagnostic : diagnostics) { copies.add(diagnostic.clone()); }
  return StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(zc::mv(copies));
}

zc::Maybe<CanonicalNonEmptySequence<diagnostics::DiagnosticFact>> decodeDiagnostics(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto diagnostics = decodeStableBindingDiagnosticFacts(bytes);
  if (diagnostics == zc::none) { return zc::none; }
  return StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::fromNonEmpty(
      zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename Authority>
HeaderAuthorityRead<Authority> runtimeRejection(query::QueryRuntimeFailure failure) {
  return HeaderAuthorityRead<Authority>(HeaderRuntimeRejection{failure});
}

template <typename Authority>
HeaderAuthorityRead<Authority> sourceRejection(
    CanonicalNonEmptySequence<diagnostics::DiagnosticFact>&& diagnostics) {
  return HeaderAuthorityRead<Authority>(HeaderSourceRejection{zc::mv(diagnostics)});
}

template <typename Authority>
HeaderAuthorityRead<Authority> keyRejection(BinderKeyFailure&& failure) {
  return HeaderAuthorityRead<Authority>(HeaderKeyRejection{zc::mv(failure)});
}

template <typename Authority>
HeaderAuthorityRead<Authority> makeKeyRejection(BinderKeyFailureKind kind,
                                                BinderQueryOwner&& owner) {
  zc::Maybe<LocalSyntaxPath> noPath;
  auto failure = BinderKeyFailure::from(kind, zc::mv(owner), zc::mv(noPath));
  if (failure == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return keyRejection<Authority>(zc::mv(ZC_ASSERT_NONNULL(failure)));
}

template <typename Authority, typename Value>
HeaderAuthorityRead<Authority> semanticReadFailure(const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return runtimeRejection<Authority>(result.runtimeFailure()); }
  if (result.kind() != query::QueryValueKind::SemanticFailure) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto diagnostics = decodeDiagnostics(result.semanticFailureBytes());
  if (diagnostics == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return sourceRejection<Authority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename ToAuthority, typename FromAuthority>
HeaderAuthorityRead<ToAuthority> propagateHeaderFailure(HeaderAuthorityRead<FromAuthority>&& read) {
  if (read.template is<HeaderSourceRejection>()) {
    return sourceRejection<ToAuthority>(
        zc::mv(read.template get<HeaderSourceRejection>().diagnostics));
  }
  if (read.template is<HeaderKeyRejection>()) {
    return keyRejection<ToAuthority>(zc::mv(read.template get<HeaderKeyRejection>().failure));
  }
  if (read.template is<HeaderRuntimeRejection>()) {
    return runtimeRejection<ToAuthority>(read.template get<HeaderRuntimeRejection>().failure);
  }
  return runtimeRejection<ToAuthority>(query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Context>
HeaderAuthorityRead<HeaderCommonAuthority> readHeaderCommonAuthority(
    Context& context, const identity::ModuleKey& module,
    const binding_query::StableModuleQueryKey& moduleKey, const BinderQueryOwner& owner) {
  auto selected = context.template get<graph_query::SelectedModuleSourceQuery>(module);
  if (selected.isRuntimeFailure()) {
    return runtimeRejection<HeaderCommonAuthority>(selected.runtimeFailure());
  }
  if (selected.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<HeaderCommonAuthority>(
        BinderKeyFailureKind::MissingSelectedModuleSource, owner.clone());
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(parsed.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (!parsed.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto definitionSites =
      context.template getCapability<binding_query::RevisionLocalDefinitionSitesQuery>(moduleKey);
  if (definitionSites.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(definitionSites.runtimeFailure());
  }
  if (definitionSites.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(definitionSites.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (definitionSites.isKeyRejected()) {
    return keyRejection<HeaderCommonAuthority>(definitionSites.keyFailure().clone());
  }
  if (!definitionSites.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto implementationSites =
      context.template getCapability<binding_query::RevisionLocalImplementationSitesQuery>(
          moduleKey);
  if (implementationSites.isRuntimeRejected()) {
    return runtimeRejection<HeaderCommonAuthority>(implementationSites.runtimeFailure());
  }
  if (implementationSites.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(implementationSites.diagnostics().values());
    if (diagnostics == zc::none) {
      return runtimeRejection<HeaderCommonAuthority>(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return sourceRejection<HeaderCommonAuthority>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (implementationSites.isKeyRejected()) {
    return keyRejection<HeaderCommonAuthority>(implementationSites.keyFailure().clone());
  }
  if (!implementationSites.isPublished()) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::ProviderRejected);
  }

  auto canonical = CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
  if (canonical == zc::none) {
    return runtimeRejection<HeaderCommonAuthority>(query::QueryRuntimeFailure::InvariantViolation);
  }
  return HeaderAuthorityRead<HeaderCommonAuthority>(HeaderCommonAuthority{
      zc::mv(ZC_ASSERT_NONNULL(canonical)), definitionSites.lease().capability().clone(),
      implementationSites.lease().capability().clone()});
}

template <typename InventoryQuery, typename Inventory, typename Context>
HeaderAuthorityRead<HeaderProviderAuthority<Inventory>> readHeaderProviderAuthority(
    Context& context, const identity::ModuleKey& module, BinderQueryOwner&& owner) {
  using Authority = HeaderProviderAuthority<Inventory>;
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(module);
  if (moduleKey == zc::none) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvalidKeyEncoding);
  }

  auto inventory = context.template get<InventoryQuery>(ZC_ASSERT_NONNULL(moduleKey));
  if (inventory.isRuntimeFailure() || inventory.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<Authority>(inventory);
  }
  auto common = readHeaderCommonAuthority(context, module, ZC_ASSERT_NONNULL(moduleKey), owner);
  if (!common.template is<HeaderCommonAuthority>()) {
    return propagateHeaderFailure<Authority>(zc::mv(common));
  }
  if (inventory.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<Authority>(BinderKeyFailureKind::InactiveOwner, zc::mv(owner));
  }
  if (inventory.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<Authority>(query::QueryRuntimeFailure::InvariantViolation);
  }

  auto& authority = common.template get<HeaderCommonAuthority>();
  return HeaderAuthorityRead<Authority>(
      Authority{zc::mv(authority.parsed), inventory.value().clone(),
                zc::mv(authority.definitionSites), zc::mv(authority.implementationSites)});
}

template <typename Context>
HeaderAuthorityRead<HeaderVerificationAuthority> readHeaderVerificationAuthority(
    Context& context, const identity::ModuleKey& module, BinderQueryOwner&& owner) {
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(module);
  if (moduleKey == zc::none) {
    return runtimeRejection<HeaderVerificationAuthority>(
        query::QueryRuntimeFailure::InvalidKeyEncoding);
  }

  auto definitions = context.template get<binding_query::NamedDefinitionInventoryQuery>(
      ZC_ASSERT_NONNULL(moduleKey));
  if (definitions.isRuntimeFailure() ||
      definitions.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<HeaderVerificationAuthority>(definitions);
  }
  auto implementations = context.template get<binding_query::NamedImplementationInventoryQuery>(
      ZC_ASSERT_NONNULL(moduleKey));
  if (implementations.isRuntimeFailure() ||
      implementations.kind() == query::QueryValueKind::SemanticFailure) {
    return semanticReadFailure<HeaderVerificationAuthority>(implementations);
  }
  auto common = readHeaderCommonAuthority(context, module, ZC_ASSERT_NONNULL(moduleKey), owner);
  if (!common.template is<HeaderCommonAuthority>()) {
    return propagateHeaderFailure<HeaderVerificationAuthority>(zc::mv(common));
  }
  if (definitions.kind() == query::QueryValueKind::Absence ||
      implementations.kind() == query::QueryValueKind::Absence) {
    return makeKeyRejection<HeaderVerificationAuthority>(BinderKeyFailureKind::InactiveOwner,
                                                         zc::mv(owner));
  }
  if (definitions.kind() != query::QueryValueKind::Value ||
      implementations.kind() != query::QueryValueKind::Value) {
    return runtimeRejection<HeaderVerificationAuthority>(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto& authority = common.template get<HeaderCommonAuthority>();
  return HeaderAuthorityRead<HeaderVerificationAuthority>(HeaderVerificationAuthority{
      zc::mv(authority.parsed), definitions.value().clone(), implementations.value().clone(),
      zc::mv(authority.definitionSites), zc::mv(authority.implementationSites)});
}

template <typename Value, typename Authority>
query::TypedQueryResult<Value> publishReadFailure(HeaderAuthorityRead<Authority>&& read) {
  if (read.template is<HeaderSourceRejection>()) {
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(zc::mv(read.template get<HeaderSourceRejection>().diagnostics)));
  }
  if (read.template is<HeaderKeyRejection>()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(zc::mv(read.template get<HeaderKeyRejection>().failure)));
  }
  if (read.template is<HeaderRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        read.template get<HeaderRuntimeRejection>().failure);
  }
  return query::TypedQueryResult<Value>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Value, typename Authority>
bool verifyReadFailure(const HeaderAuthorityRead<Authority>& read,
                       const query::TypedQueryResult<Value>& result) {
  if (read.template is<HeaderRuntimeRejection>() || result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  if (read.template is<HeaderSourceRejection>()) {
    return result.value().storage().template is<BinderSourceRejected>() &&
           result.value().storage().template get<BinderSourceRejected>().diagnostics ==
               read.template get<HeaderSourceRejection>().diagnostics;
  }
  if (read.template is<HeaderKeyRejection>()) {
    return result.value().storage().template is<BinderKeyRejected>() &&
           result.value().storage().template get<BinderKeyRejected>().failure ==
               read.template get<HeaderKeyRejection>().failure;
  }
  return false;
}

CanonicalSequence<diagnostics::DiagnosticFact> emptyDiagnostics() {
  zc::Vector<diagnostics::DiagnosticFact> diagnostics;
  auto sequence =
      StableBindingSequenceBuilder<diagnostics::DiagnosticFact>::from(zc::mv(diagnostics));
  return zc::mv(ZC_ASSERT_NONNULL(sequence));
}

template <typename T>
void sortFacts(zc::Vector<T>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = StableBindingCodec<T>::encode(current);
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = StableBindingCodec<T>::encode(values[insertion - 1]);
      if (previousBytes.asPtr() < currentBytes.asPtr()) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename T>
zc::Maybe<CanonicalSequence<T>> admitFacts(zc::Vector<T>&& values) {
  sortFacts(values);
  return StableBindingSequenceBuilder<T>::from(zc::mv(values));
}

void sortBodyOwners(zc::Vector<StableOwnerBodyQueryKey>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = current.encodeCanonical();
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = values[insertion - 1].encodeCanonical();
      if (previousBytes.asPtr() < currentBytes.asPtr()) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

bool hasRole(const CanonicalSequence<ScopeRole>& roles, ScopeRole role) {
  for (const auto candidate : roles.values()) {
    if (candidate == role) { return true; }
  }
  return false;
}

ScopeKind definitionScopeKind(ScopeRole role) {
  return role == ScopeRole::Parameters ? ScopeKind::Function : ScopeKind::TypeBody;
}

ScopeKind implementationScopeKind() { return ScopeKind::ImplBody; }

zc::Maybe<StableScopeOwnerKey> definitionScope(const StableDefinitionQueryKey& key,
                                               ScopeRole role) {
  return StableScopeOwnerKey::definition(key.clone(), role);
}

zc::Maybe<StableScopeOwnerKey> implementationScope(
    const StableImplementationOccurrenceQueryKey& key, ScopeRole role) {
  return StableScopeOwnerKey::implementationOccurrence(key.clone(), role);
}

bool addScope(zc::Vector<StableScopeFact>& facts, StableScopeOwnerKey&& owner,
              zc::Maybe<StableScopeOwnerKey>&& parent, ScopeKind kind) {
  auto fact = StableScopeFact::from(zc::mv(owner), zc::mv(parent), kind);
  if (fact == zc::none) { return false; }
  facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  return true;
}

template <typename Header>
bool addHeaderScopes(zc::Vector<StableScopeFact>& facts, const Header& header,
                     bool implementation) {
  const auto& roles = header.declaredScopeRoles();
  if (!hasRole(roles, implementation ? ScopeRole::Implementation : ScopeRole::Declaration)) {
    return false;
  }
  const auto moduleScope = StableScopeOwnerKey::module(header.queryKey().module().clone());
  zc::Maybe<StableScopeOwnerKey> declaration;
  zc::Maybe<StableScopeOwnerKey> generic;
  for (const auto role : roles.values()) {
    zc::Maybe<StableScopeOwnerKey> owner;
    if constexpr (requires { definitionScope(header.queryKey(), role); }) {
      owner = definitionScope(header.queryKey(), role);
    } else {
      owner = implementationScope(header.queryKey(), role);
    }
    if (owner == zc::none) { return false; }
    zc::Maybe<StableScopeOwnerKey> parent;
    if (role == ScopeRole::Declaration || role == ScopeRole::Generic) {
      parent = role == ScopeRole::Declaration ? moduleScope.clone()
               : implementation               ? moduleScope.clone()
                                              : ZC_ASSERT_NONNULL(declaration).clone();
    } else if (role == ScopeRole::Implementation) {
      parent = generic == zc::none ? moduleScope.clone() : ZC_ASSERT_NONNULL(generic).clone();
    } else {
      parent = generic == zc::none ? ZC_ASSERT_NONNULL(declaration).clone()
                                   : ZC_ASSERT_NONNULL(generic).clone();
    }
    const auto kind = implementation ? implementationScopeKind() : definitionScopeKind(role);
    auto scope = ZC_ASSERT_NONNULL(owner).clone();
    if (!addScope(facts, zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(parent), kind)) { return false; }
    if (role == ScopeRole::Declaration) {
      declaration = zc::mv(scope);
    } else if (role == ScopeRole::Generic) {
      generic = zc::mv(scope);
    }
  }
  return true;
}

zc::Maybe<BoundModuleSkeleton> buildModuleSkeleton(
    query::QueryContext& context, const identity::ModuleKey& module,
    const NamedDefinitionInventory& definitions,
    const NamedImplementationInventory& implementations,
    const RevisionLocalDefinitionSites& definitionSites,
    const RevisionLocalImplementationSites& implementationSites,
    const ModuleBodyProvenance& moduleProvenance, const CanonicalParsedModule& parsed,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableScopeFact> scopeFacts;
  zc::Maybe<StableScopeOwnerKey> noParent;
  if (!addScope(scopeFacts, StableScopeOwnerKey::module(module.clone()), zc::mv(noParent),
                ScopeKind::Module)) {
    return zc::none;
  }

  zc::Vector<StableDeclarationFact> declarations;
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  zc::Vector<StableGenericParameterDeclarationFact> genericParameters;
  zc::Vector<StableCallableParameterDeclarationFact> callableParameters;
  zc::Vector<StableOwnerBodyQueryKey> bodyOwnerValues;
  auto moduleOwner =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  if (moduleOwner == zc::none) { return zc::none; }
  bodyOwnerValues.add(zc::mv(ZC_ASSERT_NONNULL(moduleOwner)));

  for (const auto& entry : definitions.entries()) {
    zc::Maybe<const RevisionLocalDefinitionSite&> site;
    for (const auto& candidate : definitionSites.entries()) {
      if (candidate.definition() == entry.key()) {
        if (site != zc::none) { return zc::none; }
        site = candidate;
      }
    }
    if (site == zc::none) { return zc::none; }
    const auto key = StableDefinitionQueryKey::from(module.clone(), entry.key().clone());
    auto header = DefinitionHeaderProducer::produce(DefinitionHeaderInput{
        parsed, key, entry, ZC_ASSERT_NONNULL(site), definitionSites, implementationSites});
    if (header == zc::none || !addHeaderScopes(scopeFacts, ZC_ASSERT_NONNULL(header), false)) {
      return zc::none;
    }
    const auto& value = ZC_ASSERT_NONNULL(header);
    auto declaration = StableDeclarationFact::from(
        value.queryKey().clone(), value.record().clone(),
        StableScopeOwnerKey::module(module.clone()), value.kind(), value.nameSpace(),
        value.name().clone(), value.activation(),
        value.visibility() == zc::none
            ? zc::Maybe<MemberVisibility>()
            : zc::Maybe<MemberVisibility>(ZC_ASSERT_NONNULL(value.visibility())));
    if (declaration == zc::none) { return zc::none; }
    declarations.add(zc::mv(ZC_ASSERT_NONNULL(declaration)));
    auto genericScope = definitionScope(value.queryKey(), ScopeRole::Generic);
    for (const auto& parameter : value.genericParameters().values()) {
      if (genericScope == zc::none) { return zc::none; }
      auto fact = StableGenericParameterDeclarationFact::from(
          StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(genericScope).clone(), parameter.name().clone());
      if (fact == zc::none) { return zc::none; }
      genericParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
    auto parameterScope = definitionScope(value.queryKey(), ScopeRole::Parameters);
    for (const auto& parameter : value.callableParameters().values()) {
      if (parameterScope == zc::none) { return zc::none; }
      zc::Maybe<identity::DeclaredDefinitionName> name;
      ZC_IF_SOME(valueName, parameter.name()) { name = valueName.clone(); }
      auto fact = StableCallableParameterDeclarationFact::from(
          StableCallableParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(parameterScope).clone(), zc::mv(name));
      if (fact == zc::none) { return zc::none; }
      callableParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
    if (value.bodyDisposition() == DefinitionBodyDisposition::ExecutableBody) {
      auto owner = StableOwnerBodyQueryKey::from(
          module.clone(), StableBodyOwnerKey::definition(value.queryKey().definition().clone()));
      if (owner == zc::none) { return zc::none; }
      bodyOwnerValues.add(zc::mv(ZC_ASSERT_NONNULL(owner)));
    }
  }

  for (const auto& site : implementationSites.entries()) {
    zc::Maybe<const NamedImplementationInventoryEntry&> entry;
    for (const auto& candidate : implementations.entries()) {
      if (candidate.key() == site.occurrence().implementation()) {
        if (entry != zc::none) { return zc::none; }
        entry = candidate;
      }
    }
    if (entry == zc::none) { return zc::none; }
    auto key =
        StableImplementationOccurrenceQueryKey::from(module.clone(), site.occurrence().clone());
    if (key == zc::none) { return zc::none; }
    auto header = ImplementationHeaderProducer::produce(
        ImplementationHeaderInput{parsed, ZC_ASSERT_NONNULL(key), ZC_ASSERT_NONNULL(entry), site,
                                  definitionSites, implementationSites});
    if (header == zc::none || !addHeaderScopes(scopeFacts, ZC_ASSERT_NONNULL(header), true)) {
      return zc::none;
    }
    const auto& value = ZC_ASSERT_NONNULL(header);
    auto occurrence = StableImplementationOccurrenceFact::from(
        value.queryKey().clone(), value.authority().clone(), value.record().clone(),
        StableScopeOwnerKey::module(module.clone()));
    if (occurrence == zc::none) { return zc::none; }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
    auto genericScope = implementationScope(value.queryKey(), ScopeRole::Generic);
    for (const auto& parameter : value.genericParameters().values()) {
      if (genericScope == zc::none) { return zc::none; }
      auto fact = StableGenericParameterDeclarationFact::from(
          StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(genericScope).clone(), parameter.name().clone());
      if (fact == zc::none) { return zc::none; }
      genericParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
  }

  auto scopes = admitFacts(zc::mv(scopeFacts));
  auto declarationFacts = admitFacts(zc::mv(declarations));
  auto occurrenceFacts = admitFacts(zc::mv(occurrences));
  auto genericFacts = admitFacts(zc::mv(genericParameters));
  auto callableFacts = admitFacts(zc::mv(callableParameters));
  zc::Vector<StableNodeScopeFact> nodeScopes;
  for (const auto& entry : moduleProvenance.entries()) {
    auto fact =
        StableNodeScopeFact::from(StableNodeSyntaxRoot::moduleBody(module.clone()),
                                  entry.path.clone(), StableScopeOwnerKey::module(module.clone()));
    if (fact == zc::none) { return zc::none; }
    nodeScopes.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  zc::Vector<StableFailedLookupFact> failures;
  auto admittedNodes = admitFacts(zc::mv(nodeScopes));
  auto importedFacts = projectImportedFacts(context, module, parsed, dependencies);
  auto admittedAliases =
      declarationFacts == zc::none
          ? zc::Maybe<CanonicalSequence<StableModuleAliasFact>>()
          : projectModuleAliasFacts(context, module, parsed, definitionSites,
                                    ZC_ASSERT_NONNULL(declarationFacts), dependencies);
  auto admittedExports =
      declarationFacts == zc::none || importedFacts == zc::none
          ? zc::Maybe<CanonicalSequence<StableLocalExportFact>>()
          : projectLocalExportFacts(module, parsed, moduleProvenance, definitionSites,
                                    ZC_ASSERT_NONNULL(declarationFacts),
                                    ZC_ASSERT_NONNULL(importedFacts), dependencies);
  auto admittedFailures = admitFacts(zc::mv(failures));
  sortBodyOwners(bodyOwnerValues);
  auto owners =
      StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(bodyOwnerValues));
  if (scopes == zc::none || declarationFacts == zc::none || occurrenceFacts == zc::none ||
      genericFacts == zc::none || callableFacts == zc::none || admittedNodes == zc::none ||
      admittedAliases == zc::none || importedFacts == zc::none || admittedExports == zc::none ||
      owners == zc::none || admittedFailures == zc::none) {
    return zc::none;
  }
  return BoundModuleSkeleton::from(
      module.clone(), zc::mv(ZC_ASSERT_NONNULL(scopes)), zc::mv(ZC_ASSERT_NONNULL(admittedNodes)),
      zc::mv(ZC_ASSERT_NONNULL(declarationFacts)), zc::mv(ZC_ASSERT_NONNULL(occurrenceFacts)),
      zc::mv(ZC_ASSERT_NONNULL(genericFacts)), zc::mv(ZC_ASSERT_NONNULL(callableFacts)),
      zc::mv(ZC_ASSERT_NONNULL(admittedAliases)), zc::mv(ZC_ASSERT_NONNULL(importedFacts)),
      zc::mv(ZC_ASSERT_NONNULL(admittedExports)), zc::mv(ZC_ASSERT_NONNULL(owners)),
      zc::mv(ZC_ASSERT_NONNULL(admittedFailures)));
}

zc::Maybe<StableDefinitionHeader> readVerifiedDefinitionHeader(
    query::QueryContext& context, const StableDefinitionQueryKey& key) {
  auto result = context.get<DefinitionHeaderSyntax>(key.clone());
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value ||
      !result.value().storage().is<BinderQueryValue<StableDefinitionHeader>>()) {
    return zc::none;
  }
  const auto& header = result.value().storage().get<BinderQueryValue<StableDefinitionHeader>>();
  if (header.diagnostics.values().size() != 0) { return zc::none; }
  return header.value.clone();
}

zc::Maybe<StableImplementationOccurrenceHeader> readVerifiedImplementationHeader(
    query::QueryContext& context, const StableImplementationOccurrenceQueryKey& key) {
  auto result = context.get<ImplementationOccurrenceHeaderSyntax>(key.clone());
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value ||
      !result.value().storage().is<BinderQueryValue<StableImplementationOccurrenceHeader>>()) {
    return zc::none;
  }
  const auto& header =
      result.value().storage().get<BinderQueryValue<StableImplementationOccurrenceHeader>>();
  if (header.diagnostics.values().size() != 0) { return zc::none; }
  return header.value.clone();
}

zc::Maybe<BoundModuleSkeleton> rebuildModuleSkeletonForVerification(
    query::QueryContext& context, const identity::ModuleKey& module,
    const HeaderVerificationAuthority& authority, const ModuleBodyProvenance& moduleProvenance,
    const graph_query::ModuleDependencyProvenanceMap& dependencies) {
  zc::Vector<StableScopeFact> scopeFacts;
  zc::Maybe<StableScopeOwnerKey> noParent;
  if (!addScope(scopeFacts, StableScopeOwnerKey::module(module.clone()), zc::mv(noParent),
                ScopeKind::Module)) {
    return zc::none;
  }

  zc::Vector<StableDeclarationFact> declarations;
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  zc::Vector<StableGenericParameterDeclarationFact> genericParameters;
  zc::Vector<StableCallableParameterDeclarationFact> callableParameters;
  zc::Vector<StableOwnerBodyQueryKey> bodyOwnerValues;
  auto moduleOwner =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  if (moduleOwner == zc::none) { return zc::none; }
  bodyOwnerValues.add(zc::mv(ZC_ASSERT_NONNULL(moduleOwner)));

  for (const auto& entry : authority.definitions.entries()) {
    const auto key = StableDefinitionQueryKey::from(module.clone(), entry.key().clone());
    auto header = readVerifiedDefinitionHeader(context, key);
    if (header == zc::none || !addHeaderScopes(scopeFacts, ZC_ASSERT_NONNULL(header), false)) {
      return zc::none;
    }
    const auto& value = ZC_ASSERT_NONNULL(header);
    auto declaration = StableDeclarationFact::from(
        value.queryKey().clone(), value.record().clone(),
        StableScopeOwnerKey::module(module.clone()), value.kind(), value.nameSpace(),
        value.name().clone(), value.activation(),
        value.visibility() == zc::none
            ? zc::Maybe<MemberVisibility>()
            : zc::Maybe<MemberVisibility>(ZC_ASSERT_NONNULL(value.visibility())));
    if (declaration == zc::none) { return zc::none; }
    declarations.add(zc::mv(ZC_ASSERT_NONNULL(declaration)));
    auto genericScope = definitionScope(value.queryKey(), ScopeRole::Generic);
    for (const auto& parameter : value.genericParameters().values()) {
      if (genericScope == zc::none) { return zc::none; }
      auto fact = StableGenericParameterDeclarationFact::from(
          StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(genericScope).clone(), parameter.name().clone());
      if (fact == zc::none) { return zc::none; }
      genericParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
    auto parameterScope = definitionScope(value.queryKey(), ScopeRole::Parameters);
    for (const auto& parameter : value.callableParameters().values()) {
      if (parameterScope == zc::none) { return zc::none; }
      zc::Maybe<identity::DeclaredDefinitionName> name;
      ZC_IF_SOME(valueName, parameter.name()) { name = valueName.clone(); }
      auto fact = StableCallableParameterDeclarationFact::from(
          StableCallableParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(parameterScope).clone(), zc::mv(name));
      if (fact == zc::none) { return zc::none; }
      callableParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
    if (value.bodyDisposition() == DefinitionBodyDisposition::ExecutableBody) {
      auto owner = StableOwnerBodyQueryKey::from(
          module.clone(), StableBodyOwnerKey::definition(value.queryKey().definition().clone()));
      if (owner == zc::none) { return zc::none; }
      bodyOwnerValues.add(zc::mv(ZC_ASSERT_NONNULL(owner)));
    }
  }

  for (const auto& site : authority.implementationSites.entries()) {
    auto key =
        StableImplementationOccurrenceQueryKey::from(module.clone(), site.occurrence().clone());
    if (key == zc::none) { return zc::none; }
    auto header = readVerifiedImplementationHeader(context, ZC_ASSERT_NONNULL(key));
    if (header == zc::none || !addHeaderScopes(scopeFacts, ZC_ASSERT_NONNULL(header), true)) {
      return zc::none;
    }
    const auto& value = ZC_ASSERT_NONNULL(header);
    auto occurrence = StableImplementationOccurrenceFact::from(
        value.queryKey().clone(), value.authority().clone(), value.record().clone(),
        StableScopeOwnerKey::module(module.clone()));
    if (occurrence == zc::none) { return zc::none; }
    occurrences.add(zc::mv(ZC_ASSERT_NONNULL(occurrence)));
    auto genericScope = implementationScope(value.queryKey(), ScopeRole::Generic);
    for (const auto& parameter : value.genericParameters().values()) {
      if (genericScope == zc::none) { return zc::none; }
      auto fact = StableGenericParameterDeclarationFact::from(
          StableGenericParameterQueryKey::from(module.clone(), parameter.key().clone()),
          parameter.record().clone(), parameter.site().clone(),
          ZC_ASSERT_NONNULL(genericScope).clone(), parameter.name().clone());
      if (fact == zc::none) { return zc::none; }
      genericParameters.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
    }
  }

  zc::Vector<StableNodeScopeFact> nodeScopes;
  for (const auto& entry : moduleProvenance.entries()) {
    auto fact =
        StableNodeScopeFact::from(StableNodeSyntaxRoot::moduleBody(module.clone()),
                                  entry.path.clone(), StableScopeOwnerKey::module(module.clone()));
    if (fact == zc::none) { return zc::none; }
    nodeScopes.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  zc::Vector<StableFailedLookupFact> failures;
  auto scopes = admitFacts(zc::mv(scopeFacts));
  auto admittedNodes = admitFacts(zc::mv(nodeScopes));
  auto declarationFacts = admitFacts(zc::mv(declarations));
  auto occurrenceFacts = admitFacts(zc::mv(occurrences));
  auto genericFacts = admitFacts(zc::mv(genericParameters));
  auto callableFacts = admitFacts(zc::mv(callableParameters));
  auto importedFacts = verifyImportedFacts(context, module, authority.parsed, dependencies);
  auto admittedAliases =
      declarationFacts == zc::none
          ? zc::Maybe<CanonicalSequence<StableModuleAliasFact>>()
          : verifyModuleAliasFacts(context, module, authority.parsed, authority.definitionSites,
                                   ZC_ASSERT_NONNULL(declarationFacts), dependencies);
  auto admittedExports =
      declarationFacts == zc::none || importedFacts == zc::none
          ? zc::Maybe<CanonicalSequence<StableLocalExportFact>>()
          : verifyLocalExportFacts(module, authority.parsed, moduleProvenance,
                                   authority.definitionSites, ZC_ASSERT_NONNULL(declarationFacts),
                                   ZC_ASSERT_NONNULL(importedFacts), dependencies);
  auto admittedFailures = admitFacts(zc::mv(failures));
  sortBodyOwners(bodyOwnerValues);
  auto owners =
      StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(bodyOwnerValues));
  if (scopes == zc::none || admittedNodes == zc::none || declarationFacts == zc::none ||
      occurrenceFacts == zc::none || genericFacts == zc::none || callableFacts == zc::none ||
      admittedAliases == zc::none || importedFacts == zc::none || admittedExports == zc::none ||
      admittedFailures == zc::none || owners == zc::none) {
    return zc::none;
  }
  return BoundModuleSkeleton::from(
      module.clone(), zc::mv(ZC_ASSERT_NONNULL(scopes)), zc::mv(ZC_ASSERT_NONNULL(admittedNodes)),
      zc::mv(ZC_ASSERT_NONNULL(declarationFacts)), zc::mv(ZC_ASSERT_NONNULL(occurrenceFacts)),
      zc::mv(ZC_ASSERT_NONNULL(genericFacts)), zc::mv(ZC_ASSERT_NONNULL(callableFacts)),
      zc::mv(ZC_ASSERT_NONNULL(admittedAliases)), zc::mv(ZC_ASSERT_NONNULL(importedFacts)),
      zc::mv(ZC_ASSERT_NONNULL(admittedExports)), zc::mv(ZC_ASSERT_NONNULL(owners)),
      zc::mv(ZC_ASSERT_NONNULL(admittedFailures)));
}

}  // namespace

zc::Array<uint8_t> DefinitionHeaderSyntax::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<DefinitionHeaderSyntax::Key> DefinitionHeaderSyntax::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> DefinitionHeaderSyntax::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<DefinitionHeaderSyntax::Value> DefinitionHeaderSyntax::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<DefinitionHeaderSyntax::Value> DefinitionHeaderSyntax::provide(
    query::QueryContext& context, const Key& key) {
  auto read = readHeaderProviderAuthority<binding_query::NamedDefinitionInventoryQuery,
                                          NamedDefinitionInventory>(
      context, key.module(), BinderQueryOwner::definitionHeader(key.clone()));
  if (!read.is<DefinitionHeaderAuthority>()) { return publishReadFailure<Value>(zc::mv(read)); }
  const auto& authority = read.get<DefinitionHeaderAuthority>();

  zc::Maybe<const NamedDefinitionInventoryEntry&> selectedEntry;
  for (const auto& entry : authority.inventory.entries()) {
    if (entry.key() != key.definition()) { continue; }
    if (selectedEntry != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedEntry = entry;
  }
  zc::Maybe<const RevisionLocalDefinitionSite&> selectedSite;
  for (const auto& site : authority.definitionSites.entries()) {
    if (site.definition() != key.definition()) { continue; }
    if (selectedSite != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedSite = site;
  }
  if (selectedEntry == zc::none && selectedSite == zc::none) {
    return publishReadFailure<Value>(makeKeyRejection<DefinitionHeaderAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::definitionHeader(key.clone())));
  }
  if (selectedEntry == zc::none || selectedSite == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto header = DefinitionHeaderProducer::produce(DefinitionHeaderInput{
      authority.parsed, key, ZC_ASSERT_NONNULL(selectedEntry), ZC_ASSERT_NONNULL(selectedSite),
      authority.definitionSites, authority.implementationSites});
  if (header == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(header)), emptyDiagnostics()));
}

bool DefinitionHeaderSyntax::verify(query::QueryContext& context, const Key& key,
                                    const query::TypedQueryResult<Value>& result) {
  auto read = readHeaderVerificationAuthority(context, key.module(),
                                              BinderQueryOwner::definitionHeader(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return verifyReadFailure(read, result); }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  const auto& authority = read.get<HeaderVerificationAuthority>();

  size_t entryCount = 0;
  size_t siteCount = 0;
  for (const auto& entry : authority.definitions.entries()) {
    if (entry.key() == key.definition()) { ++entryCount; }
  }
  for (const auto& site : authority.definitionSites.entries()) {
    if (site.definition() == key.definition()) { ++siteCount; }
  }
  if (entryCount == 0 && siteCount == 0) {
    auto expected = makeKeyRejection<HeaderVerificationAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::definitionHeader(key.clone()));
    return verifyReadFailure(expected, result);
  }
  if (entryCount != 1 || siteCount != 1) { return false; }
  if (!result.value().storage().is<BinderQueryValue<StableDefinitionHeader>>()) { return false; }
  const auto& value = result.value().storage().get<BinderQueryValue<StableDefinitionHeader>>();
  if (value.diagnostics.values().size() != 0) { return false; }
  return StableHeaderVerifier::verifyDefinition(
      StableHeaderVerificationContext{authority.parsed, authority.definitions,
                                      authority.implementations, authority.definitionSites,
                                      authority.implementationSites},
      key, value.value);
}

zc::Array<uint8_t> ImplementationOccurrenceHeaderSyntax::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ImplementationOccurrenceHeaderSyntax::Key>
ImplementationOccurrenceHeaderSyntax::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ImplementationOccurrenceHeaderSyntax::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<ImplementationOccurrenceHeaderSyntax::Value>
ImplementationOccurrenceHeaderSyntax::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ImplementationOccurrenceHeaderSyntax::Value>
ImplementationOccurrenceHeaderSyntax::provide(query::QueryContext& context, const Key& key) {
  auto read = readHeaderProviderAuthority<binding_query::NamedImplementationInventoryQuery,
                                          NamedImplementationInventory>(
      context, key.module(), BinderQueryOwner::implementationHeader(key.clone()));
  if (!read.is<ImplementationHeaderAuthority>()) { return publishReadFailure<Value>(zc::mv(read)); }
  const auto& authority = read.get<ImplementationHeaderAuthority>();

  zc::Maybe<const NamedImplementationInventoryEntry&> selectedEntry;
  for (const auto& entry : authority.inventory.entries()) {
    if (entry.key() != key.occurrence().implementation()) { continue; }
    if (selectedEntry != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedEntry = entry;
  }
  zc::Maybe<const RevisionLocalImplementationSite&> selectedSite;
  for (const auto& site : authority.implementationSites.entries()) {
    if (!site.occurrence().sameAs(key.occurrence())) { continue; }
    if (selectedSite != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selectedSite = site;
  }
  if (selectedEntry == zc::none && selectedSite == zc::none) {
    return publishReadFailure<Value>(makeKeyRejection<ImplementationHeaderAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::implementationHeader(key.clone())));
  }
  if (selectedEntry == zc::none || selectedSite == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto header = ImplementationHeaderProducer::produce(ImplementationHeaderInput{
      authority.parsed, key, ZC_ASSERT_NONNULL(selectedEntry), ZC_ASSERT_NONNULL(selectedSite),
      authority.definitionSites, authority.implementationSites});
  if (header == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(header)), emptyDiagnostics()));
}

bool ImplementationOccurrenceHeaderSyntax::verify(query::QueryContext& context, const Key& key,
                                                  const query::TypedQueryResult<Value>& result) {
  auto read = readHeaderVerificationAuthority(context, key.module(),
                                              BinderQueryOwner::implementationHeader(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return verifyReadFailure(read, result); }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  const auto& authority = read.get<HeaderVerificationAuthority>();

  size_t entryCount = 0;
  size_t siteCount = 0;
  for (const auto& entry : authority.implementations.entries()) {
    if (entry.key() == key.occurrence().implementation()) { ++entryCount; }
  }
  for (const auto& site : authority.implementationSites.entries()) {
    if (site.occurrence().sameAs(key.occurrence())) { ++siteCount; }
  }
  if (entryCount == 0 && siteCount == 0) {
    auto expected = makeKeyRejection<HeaderVerificationAuthority>(
        BinderKeyFailureKind::InactiveOwner, BinderQueryOwner::implementationHeader(key.clone()));
    return verifyReadFailure(expected, result);
  }
  if (entryCount != 1 || siteCount != 1) { return false; }
  if (!result.value().storage().is<BinderQueryValue<StableImplementationOccurrenceHeader>>()) {
    return false;
  }
  const auto& value =
      result.value().storage().get<BinderQueryValue<StableImplementationOccurrenceHeader>>();
  if (value.diagnostics.values().size() != 0) { return false; }
  return StableHeaderVerifier::verifyImplementationOccurrence(
      StableHeaderVerificationContext{authority.parsed, authority.definitions,
                                      authority.implementations, authority.definitionSites,
                                      authority.implementationSites},
      key, value.value);
}

zc::Array<uint8_t> BindModuleSkeleton::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<BindModuleSkeleton::Key> BindModuleSkeleton::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto key = identity::ModuleKey::decodeCanonical(decoder);
  if (key == zc::none || !decoder.finished()) { return zc::none; }
  return key;
}

zc::Array<uint8_t> BindModuleSkeleton::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<BindModuleSkeleton::Value> BindModuleSkeleton::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<BindModuleSkeleton::Value> BindModuleSkeleton::provide(
    query::QueryContext& context, const Key& key) {
  auto read = readHeaderVerificationAuthority(context, key, BinderQueryOwner::module(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return publishReadFailure<Value>(zc::mv(read)); }
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(key);
  if (moduleKey == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvalidKeyEncoding);
  }
  auto provenance =
      context.getCapability<binding_query::ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
  if (provenance.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(provenance.runtimeFailure());
  }
  if (provenance.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(provenance.diagnostics().values());
    if (diagnostics == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(zc::mv(ZC_ASSERT_NONNULL(diagnostics))));
  }
  if (provenance.isKeyRejected()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(provenance.keyFailure().clone()));
  }
  if (!provenance.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto dependencyProvenance =
      context.getCapability<graph_query::ModuleDependencyProvenanceQuery>(key.clone());
  if (dependencyProvenance.isRuntimeRejected()) {
    return query::TypedQueryResult<Value>::runtimeFailure(dependencyProvenance.runtimeFailure());
  }
  if (dependencyProvenance.isSourceRejected()) {
    auto diagnostics = cloneDiagnostics(dependencyProvenance.diagnostics().values());
    if (diagnostics == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::TypedQueryResult<Value>::value(
        Value::sourceRejected(zc::mv(ZC_ASSERT_NONNULL(diagnostics))));
  }
  if (dependencyProvenance.isKeyRejected()) {
    return query::TypedQueryResult<Value>::value(
        Value::keyRejected(dependencyProvenance.keyFailure().clone()));
  }
  if (!dependencyProvenance.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& authority = read.get<HeaderVerificationAuthority>();
  auto skeleton = buildModuleSkeleton(
      context, key, authority.definitions, authority.implementations, authority.definitionSites,
      authority.implementationSites, provenance.lease().capability(), authority.parsed,
      dependencyProvenance.lease().capability());
  if (skeleton == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(
      Value::value(zc::mv(ZC_ASSERT_NONNULL(skeleton)), emptyDiagnostics()));
}

bool BindModuleSkeleton::verify(query::QueryContext& context, const Key& key,
                                const query::TypedQueryResult<Value>& result) {
  auto read = readHeaderVerificationAuthority(context, key, BinderQueryOwner::module(key.clone()));
  if (!read.is<HeaderVerificationAuthority>()) { return verifyReadFailure(read, result); }
  if (result.kind() != query::QueryValueKind::Value ||
      !result.value().storage().is<BinderQueryValue<BoundModuleSkeleton>>()) {
    return false;
  }
  auto moduleKey = binding_query::StableModuleQueryKey::fromVerified(key);
  if (moduleKey == zc::none) { return false; }
  auto provenance =
      context.getCapability<binding_query::ModuleBodyProvenanceQuery>(ZC_ASSERT_NONNULL(moduleKey));
  if (!provenance.isPublished()) { return false; }
  auto dependencyProvenance =
      context.getCapability<graph_query::ModuleDependencyProvenanceQuery>(key.clone());
  if (!dependencyProvenance.isPublished()) { return false; }
  const auto& value = result.value().storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (value.diagnostics.values().size() != 0) { return false; }
  const auto& authority = read.get<HeaderVerificationAuthority>();
  auto expected =
      rebuildModuleSkeletonForVerification(context, key, authority, provenance.lease().capability(),
                                           dependencyProvenance.lease().capability());
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == value.value;
}

zc::Array<uint8_t> ModuleExportNames::encodeKey(const Key& key) { return key.encode(); }

zc::Maybe<ModuleExportNames::Key> ModuleExportNames::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto key = identity::ModuleKey::decodeCanonical(decoder);
  if (key == zc::none || !decoder.finished()) { return zc::none; }
  return key;
}

zc::Array<uint8_t> ModuleExportNames::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<ModuleExportNames::Value> ModuleExportNames::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ModuleExportNames::Value> ModuleExportNames::provide(
    query::QueryContext& context, const Key& key) {
  auto source = readProjectionSkeleton(context, key);
  if (source.is<ProjectionSkeletonRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  }
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (source.is<ProjectionSkeletonKeyRejection>()) {
    return query::TypedQueryResult<Value>::absence();
  }
  zc::Vector<BindingNameKey> names;
  for (const auto& exportFact :
       source.get<ProjectionSkeletonValue>().skeleton.localExports().values()) {
    names.add(exportFact.name().clone());
  }
  auto value = admitFacts(zc::mv(names));
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ModuleExportNames::verify(query::QueryContext& context, const Key& key,
                               const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>() ||
      result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) { return false; }
  zc::Vector<BindingNameKey> names;
  for (const auto& exportFact : bound.value.localExports().values()) {
    names.add(exportFact.name().clone());
  }
  auto expected = admitFacts(zc::mv(names));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> ImplementationBindingHeader::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ImplementationBindingHeader::Key> ImplementationBindingHeader::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> ImplementationBindingHeader::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}

zc::Maybe<ImplementationBindingHeader::Value> ImplementationBindingHeader::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ImplementationBindingHeader::Value> ImplementationBindingHeader::provide(
    query::QueryContext& context, const Key& key) {
  auto source = readProjectionSkeleton(context, key.module());
  if (source.is<ProjectionSkeletonRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  }
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (source.is<ProjectionSkeletonKeyRejection>()) {
    return query::TypedQueryResult<Value>::absence();
  }
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  for (const auto& occurrence :
       source.get<ProjectionSkeletonValue>().skeleton.implementationOccurrences().values()) {
    if (occurrence.authority() == key) { occurrences.add(occurrence.clone()); }
  }
  if (occurrences.size() == 0) { return query::TypedQueryResult<Value>::absence(); }
  auto value = admitFacts(zc::mv(occurrences));
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ImplementationBindingHeader::verify(query::QueryContext& context, const Key& key,
                                         const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.module().clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>()) { return false; }
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) { return false; }
  zc::Vector<StableImplementationOccurrenceFact> occurrences;
  for (const auto& occurrence : bound.value.implementationOccurrences().values()) {
    if (occurrence.authority() == key) { occurrences.add(occurrence.clone()); }
  }
  if (occurrences.size() == 0) { return result.kind() == query::QueryValueKind::Absence; }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  auto expected = admitFacts(zc::mv(occurrences));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> DefinitionBindingHeader::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<DefinitionBindingHeader::Key> DefinitionBindingHeader::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return Key::decodeCanonical(bytes);
}

zc::Array<uint8_t> DefinitionBindingHeader::encodeValue(const Value& value) {
  return encodeProjectionValue("zom.query.definition-binding-header-value"_zcc, value);
}

zc::Maybe<DefinitionBindingHeader::Value> DefinitionBindingHeader::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeProjectionValue<Value>("zom.query.definition-binding-header-value"_zcc, bytes);
}

query::TypedQueryResult<DefinitionBindingHeader::Value> DefinitionBindingHeader::provide(
    query::QueryContext& context, const Key& key) {
  auto source = readProjectionSkeleton(context, key.module());
  if (source.is<ProjectionSkeletonRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  }
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (source.is<ProjectionSkeletonKeyRejection>()) {
    return query::TypedQueryResult<Value>::absence();
  }
  zc::Maybe<StableDeclarationFact> selected;
  for (const auto& declaration :
       source.get<ProjectionSkeletonValue>().skeleton.declarations().values()) {
    if (declaration.queryKey() != key) { continue; }
    if (selected != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selected = declaration.clone();
  }
  return selected == zc::none
             ? query::TypedQueryResult<Value>::absence()
             : query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(selected)));
}

bool DefinitionBindingHeader::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.module().clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>()) { return false; }
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) { return false; }
  zc::Maybe<StableDeclarationFact> selected;
  for (const auto& declaration : bound.value.declarations().values()) {
    if (declaration.queryKey() != key) { continue; }
    if (selected != zc::none) { return false; }
    selected = declaration.clone();
  }
  if (selected == zc::none) { return result.kind() == query::QueryValueKind::Absence; }
  return result.kind() == query::QueryValueKind::Value &&
         ZC_ASSERT_NONNULL(selected) == result.value();
}

zc::Array<uint8_t> ExportedBinding::encodeKey(const Key& key) {
  return StableBindingCodec<Key>::encode(key);
}

zc::Maybe<ExportedBinding::Key> ExportedBinding::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Key>::decode(bytes);
}

zc::Array<uint8_t> ExportedBinding::encodeValue(const Value& value) {
  return encodeProjectionValue("zom.query.exported-binding-value"_zcc, value);
}

zc::Maybe<ExportedBinding::Value> ExportedBinding::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return decodeProjectionValue<Value>("zom.query.exported-binding-value"_zcc, bytes);
}

query::TypedQueryResult<ExportedBinding::Value> ExportedBinding::provide(
    query::QueryContext& context, const Key& key) {
  auto source = readProjectionSkeleton(context, key.module());
  if (source.is<ProjectionSkeletonRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  }
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (source.is<ProjectionSkeletonKeyRejection>()) {
    return query::TypedQueryResult<Value>::absence();
  }
  zc::Maybe<StableExportedBinding> selected;
  for (const auto& exportFact :
       source.get<ProjectionSkeletonValue>().skeleton.localExports().values()) {
    if (!sameBindingName(exportFact.name(), key.name())) { continue; }
    if (selected != zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    zc::Maybe<MemberVisibility> visibility;
    ZC_IF_SOME(value, exportFact.visibility()) { visibility = value; }
    auto value =
        StableExportedBinding::from(exportFact.name().clone(), exportFact.binding().clone(),
                                    exportFact.canonicalTarget().clone(), zc::mv(visibility), true);
    if (value == zc::none) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    selected = zc::mv(ZC_ASSERT_NONNULL(value));
  }
  return selected == zc::none
             ? query::TypedQueryResult<Value>::absence()
             : query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(selected)));
}

bool ExportedBinding::verify(query::QueryContext& context, const Key& key,
                             const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.module().clone());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>()) { return false; }
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) { return false; }
  zc::Maybe<StableExportedBinding> expected;
  for (const auto& exportFact : bound.value.localExports().values()) {
    if (!sameBindingName(exportFact.name(), key.name())) { continue; }
    if (expected != zc::none) { return false; }
    zc::Maybe<MemberVisibility> visibility;
    ZC_IF_SOME(value, exportFact.visibility()) { visibility = value; }
    auto value =
        StableExportedBinding::from(exportFact.name().clone(), exportFact.binding().clone(),
                                    exportFact.canonicalTarget().clone(), zc::mv(visibility), true);
    if (value == zc::none) { return false; }
    expected = zc::mv(ZC_ASSERT_NONNULL(value));
  }
  return expected == zc::none ? result.kind() == query::QueryValueKind::Absence
                              : result.kind() == query::QueryValueKind::Value &&
                                    ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> ScopeNameBucket::encodeKey(const Key& key) {
  return StableBindingCodec<Key>::encode(key);
}
zc::Maybe<ScopeNameBucket::Key> ScopeNameBucket::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Key>::decode(bytes);
}
zc::Array<uint8_t> ScopeNameBucket::encodeValue(const Value& value) {
  return StableBindingCodec<Value>::encode(value);
}
zc::Maybe<ScopeNameBucket::Value> ScopeNameBucket::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Value>::decode(bytes);
}

query::TypedQueryResult<ScopeNameBucket::Value> ScopeNameBucket::provide(
    query::QueryContext& context, const Key& key) {
  auto module = scopeModule(key.scope());
  if (module == zc::none) { return query::TypedQueryResult<Value>::absence(); }
  auto source = readProjectionSkeleton(context, ZC_ASSERT_NONNULL(module));
  if (source.is<ProjectionSkeletonRuntimeRejection>())
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  if (source.is<ProjectionSkeletonKeyRejection>()) return query::TypedQueryResult<Value>::absence();
  zc::Vector<StableBindingTargetKey> targets;
  const auto& skeleton = source.get<ProjectionSkeletonValue>().skeleton;
  for (const auto& declaration : skeleton.declarations().values()) {
    if (declaration.declaringScope() == key.scope() &&
        declaration.nameSpace() == key.name().nameSpace() &&
        declaration.name() == key.name().name() &&
        (declaration.activation() == DefinitionActivation::ModuleSkeleton ||
         declaration.activation() == DefinitionActivation::ImportSurface))
      targets.add(StableBindingTargetKey::definition(declaration.queryKey().clone()));
  }
  for (const auto& parameter : skeleton.genericParameterDeclarations().values()) {
    if (parameter.declaringScope() == key.scope() && key.name().nameSpace() == Namespace::Type &&
        parameter.name() == key.name().name())
      targets.add(StableBindingTargetKey::genericParameter(parameter.queryKey().clone()));
  }
  for (const auto& parameter : skeleton.callableParameterDeclarations().values()) {
    ZC_IF_SOME(name, parameter.name()) {
      if (parameter.declaringScope() == key.scope() && key.name().nameSpace() == Namespace::Value &&
          name == key.name().name())
        targets.add(StableBindingTargetKey::callableParameter(parameter.queryKey().clone()));
    }
  }
  for (const auto& import : skeleton.imports().values()) {
    if (import.declaringScope() == key.scope() && import.nameSpace() == key.name().nameSpace() &&
        import.queryKey().binding().localName() == key.name().name()) {
      targets.add(import.target().clone());
    }
  }
  auto value = admitFacts(zc::mv(targets));
  return value == zc::none
             ? query::TypedQueryResult<Value>::runtimeFailure(
                   query::QueryRuntimeFailure::InvariantViolation)
             : query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ScopeNameBucket::verify(query::QueryContext& context, const Key& key,
                             const query::TypedQueryResult<Value>& result) {
  auto module = scopeModule(key.scope());
  if (module == zc::none) return result.kind() == query::QueryValueKind::Absence;
  auto skeleton = context.get<BindModuleSkeleton>(ZC_ASSERT_NONNULL(module));
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) return false;
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>() ||
      result.kind() != query::QueryValueKind::Value)
    return false;
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) return false;
  zc::Vector<StableBindingTargetKey> targets;
  for (const auto& declaration : bound.value.declarations().values())
    if (declaration.declaringScope() == key.scope() &&
        declaration.nameSpace() == key.name().nameSpace() &&
        declaration.name() == key.name().name() &&
        (declaration.activation() == DefinitionActivation::ModuleSkeleton ||
         declaration.activation() == DefinitionActivation::ImportSurface))
      targets.add(StableBindingTargetKey::definition(declaration.queryKey().clone()));
  for (const auto& parameter : bound.value.genericParameterDeclarations().values())
    if (parameter.declaringScope() == key.scope() && key.name().nameSpace() == Namespace::Type &&
        parameter.name() == key.name().name())
      targets.add(StableBindingTargetKey::genericParameter(parameter.queryKey().clone()));
  for (const auto& parameter : bound.value.callableParameterDeclarations().values()) {
    ZC_IF_SOME(name, parameter.name()) {
      if (parameter.declaringScope() == key.scope() && key.name().nameSpace() == Namespace::Value &&
          name == key.name().name())
        targets.add(StableBindingTargetKey::callableParameter(parameter.queryKey().clone()));
    }
  }
  for (const auto& import : bound.value.imports().values()) {
    if (import.declaringScope() == key.scope() && import.nameSpace() == key.name().nameSpace() &&
        import.queryKey().binding().localName() == key.name().name()) {
      targets.add(import.target().clone());
    }
  }
  auto expected = admitFacts(zc::mv(targets));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected) == result.value();
}

zc::Array<uint8_t> ImportTarget::encodeKey(const Key& key) {
  return StableBindingCodec<Key>::encode(key);
}

zc::Maybe<ImportTarget::Key> ImportTarget::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return StableBindingCodec<Key>::decode(bytes);
}

zc::Array<uint8_t> ImportTarget::encodeValue(const Value& value) {
  return encodeImportTargetValue(value);
}

zc::Maybe<ImportTarget::Value> ImportTarget::decodeValue(zc::ArrayPtr<const uint8_t> bytes) {
  return decodeImportTargetValue(bytes);
}

query::TypedQueryResult<ImportTarget::Value> ImportTarget::provide(query::QueryContext& context,
                                                                   const Key& key) {
  auto source = readProjectionSkeleton(context, key.requester());
  if (source.is<ProjectionSkeletonRuntimeRejection>()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        source.get<ProjectionSkeletonRuntimeRejection>().failure);
  }
  if (source.is<ProjectionSkeletonSourceRejection>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.get<ProjectionSkeletonSourceRejection>().diagnostics.values());
    return diagnostics == zc::none ? query::TypedQueryResult<Value>::runtimeFailure(
                                         query::QueryRuntimeFailure::InvariantViolation)
                                   : query::TypedQueryResult<Value>::semanticFailure(
                                         zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
  }
  zc::Maybe<StableImportFact> selected;
  if (source.is<ProjectionSkeletonValue>()) {
    for (const auto& import : source.get<ProjectionSkeletonValue>().skeleton.imports().values()) {
      if (import.queryKey() != key) { continue; }
      if (selected != zc::none) {
        return query::TypedQueryResult<Value>::runtimeFailure(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      selected = import.clone();
    }
  }
  return query::TypedQueryResult<Value>::value(zc::mv(selected));
}

bool ImportTarget::verify(query::QueryContext& context, const Key& key,
                          const query::TypedQueryResult<Value>& result) {
  auto skeleton = context.get<BindModuleSkeleton>(key.requester());
  if (skeleton.isRuntimeFailure() || skeleton.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& source = skeleton.value();
  if (source.storage().is<BinderSourceRejected>()) {
    auto diagnostics = encodeStableBindingDiagnosticFacts(
        source.storage().get<BinderSourceRejected>().diagnostics.values());
    return diagnostics != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(diagnostics).asPtr();
  }
  if (source.storage().is<BinderKeyRejected>()) {
    return result.kind() == query::QueryValueKind::Value && result.value() == Value();
  }
  if (!source.storage().is<BinderQueryValue<BoundModuleSkeleton>>() ||
      result.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& bound = source.storage().get<BinderQueryValue<BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0) { return false; }
  zc::Maybe<StableImportFact> expected;
  for (const auto& import : bound.value.imports().values()) {
    if (import.queryKey() != key) { continue; }
    if (expected != zc::none) { return false; }
    expected = import.clone();
  }
  return expected == result.value();
}

zc::Array<uint8_t> BindingVisibility::encodeKey(const Key& key) {
  return StableBindingCodec<Key>::encode(key);
}

zc::Maybe<BindingVisibility::Key> BindingVisibility::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  auto key = StableBindingCodec<Key>::decode(bytes);
  return key != zc::none && isBindingVisibilityKey(ZC_ASSERT_NONNULL(key)) ? zc::mv(key) : zc::none;
}

zc::Array<uint8_t> BindingVisibility::encodeValue(const Value& value) {
  return encodeBindingVisibilityValue(value);
}

zc::Maybe<BindingVisibility::Value> BindingVisibility::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return decodeBindingVisibilityValue(bytes);
}

query::TypedQueryResult<BindingVisibility::Value> BindingVisibility::provide(
    query::QueryContext& context, const Key& key) {
  const auto& target = key.value();
  if (target.is<StableDefinitionBindingTarget>()) {
    return readDefinitionVisibility(context,
                                    target.get<StableDefinitionBindingTarget>().definition);
  }
  if (target.is<StableModuleBindingTarget>()) {
    return readModuleVisibility(context, target.get<StableModuleBindingTarget>().module);
  }
  if (target.is<StableSemanticImportBindingTarget>()) {
    return readImportVisibility(context, target.get<StableSemanticImportBindingTarget>().import);
  }
  zc::Maybe<MemberVisibility> visibility;
  return query::TypedQueryResult<Value>::value(zc::mv(visibility));
}

bool BindingVisibility::verify(query::QueryContext& context, const Key& key,
                               const query::TypedQueryResult<Value>& result) {
  if (!isBindingVisibilityKey(key)) { return false; }
  const auto& target = key.value();
  if (target.is<StableDefinitionBindingTarget>()) {
    auto definition = context.get<DefinitionBindingHeader>(
        target.get<StableDefinitionBindingTarget>().definition.clone());
    if (definition.isRuntimeFailure() || (definition.kind() != result.kind() &&
                                          definition.kind() != query::QueryValueKind::Absence)) {
      return false;
    }
    if (definition.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             definition.semanticFailureBytes() == result.semanticFailureBytes();
    }
    zc::Maybe<MemberVisibility> visibility;
    if (definition.kind() == query::QueryValueKind::Value) {
      ZC_IF_SOME(value, definition.value().visibility()) { visibility = value; }
    }
    return result.kind() == query::QueryValueKind::Value && result.value() == visibility;
  }
  if (target.is<StableModuleBindingTarget>()) {
    auto exports =
        context.get<ModuleExportNames>(target.get<StableModuleBindingTarget>().module.clone());
    if (exports.isRuntimeFailure()) { return false; }
    if (exports.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             exports.semanticFailureBytes() == result.semanticFailureBytes();
    }
    zc::Maybe<MemberVisibility> visibility = MemberVisibility::Public;
    return exports.kind() == query::QueryValueKind::Value &&
           result.kind() == query::QueryValueKind::Value && result.value() == visibility;
  }
  if (target.is<StableSemanticImportBindingTarget>()) {
    auto import =
        context.get<ImportTarget>(target.get<StableSemanticImportBindingTarget>().import.clone());
    if (import.isRuntimeFailure()) { return false; }
    if (import.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             import.semanticFailureBytes() == result.semanticFailureBytes();
    }
    if (import.kind() != query::QueryValueKind::Value ||
        result.kind() != query::QueryValueKind::Value) {
      return false;
    }
    zc::Maybe<MemberVisibility> visibility;
    ZC_IF_SOME(value, import.value()) {
      ZC_IF_SOME(importVisibility, value.visibility()) { visibility = importVisibility; }
    }
    return visibility == result.value();
  }
  return result.kind() == query::QueryValueKind::Value && result.value() == Value();
}

bool registerStableHeaderSyntaxQueries(query::QueryDatabase& database) {
  if (!database.registerDescriptor<DefinitionHeaderSyntax>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ImplementationOccurrenceHeaderSyntax>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<BindModuleSkeleton>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ModuleExportNames>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ImplementationBindingHeader>().isRegistered()) { return false; }
  if (!database.registerDescriptor<DefinitionBindingHeader>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ExportedBinding>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ScopeNameBucket>().isRegistered()) { return false; }
  if (!database.registerDescriptor<ImportTarget>().isRegistered()) { return false; }
  if (!database.registerDescriptor<BindingVisibility>().isRegistered()) { return false; }
  return true;
}

}  // namespace zomlang::compiler::binder
