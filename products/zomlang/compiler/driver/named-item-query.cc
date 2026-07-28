// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/named-item-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr zc::StringPtr kFailureDomain = "zom.named-item-query-failure"_zc;

enum class NamedItemFailureKind : uint8_t {
  InactiveOwner = 0x01,
  UpstreamSourceRejected = 0x02,
  MissingSelectedModuleSource = 0x03,
  BoundaryMismatch = 0x04,
  MalformedDetachedSyntax = 0x05,
  MissingProvenance = 0x06
};

struct RecoveredAuthority final {
  RecoveredAuthority(identity::DefinitionIdentityRecord&& record,
                     StableModuleQueryKey&& module) noexcept
      : record(zc::mv(record)), module(zc::mv(module)) {}
  RecoveredAuthority(RecoveredAuthority&&) noexcept = default;
  RecoveredAuthority& operator=(RecoveredAuthority&&) noexcept = default;
  ZC_DISALLOW_COPY(RecoveredAuthority);

  identity::DefinitionIdentityRecord record;
  StableModuleQueryKey module;
};

struct LoadedNamedItemSource final {
  LoadedNamedItemSource(binder::CanonicalParsedModule&& parsed, ast::NodeId moduleNode) noexcept
      : parsed(zc::mv(parsed)), moduleNode(moduleNode) {}
  LoadedNamedItemSource(LoadedNamedItemSource&&) noexcept = default;
  LoadedNamedItemSource& operator=(LoadedNamedItemSource&&) noexcept = default;
  ZC_DISALLOW_COPY(LoadedNamedItemSource);

  binder::CanonicalParsedModule parsed;
  ast::NodeId moduleNode;
};

query::QueryKindContract semanticContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Evictable);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

query::QueryKindContract provenanceContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::RevisionLocal,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Array<uint8_t> encodeFailure(NamedItemFailureKind kind,
                                 zc::ArrayPtr<const uint8_t> payload = {}) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kFailureDomain.asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(kind));
  encoder.encodeByteString(payload);
  return encoder.finish();
}

bool containsAuthority(const binder::NamedDefinitionInventory& inventory,
                       const identity::DefinitionKey& key,
                       const identity::DefinitionIdentityRecord& record) {
  auto encoded = record.encode();
  for (const auto& entry : inventory.entries()) {
    if (entry.key() == key) { return entry.canonicalRecord() == encoded.asPtr(); }
  }
  return false;
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename Context>
query::TypedQueryResult<RecoveredAuthority> providerAuthority(Context& context,
                                                              const ContextualDefinitionKey& key) {
  auto authority = context.template probeInput<ActiveDefinitionAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() == query::QueryValueKind::Absence) {
    auto readiness =
        context.template probeInput<ActiveDefinitionAuthorityReadyInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          readiness.runtimeFailure());
    }
    if (readiness.kind() == query::QueryValueKind::Absence) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    if (readiness.kind() == query::QueryValueKind::Value) {
      return query::TypedQueryResult<RecoveredAuthority>::semanticFailure(
          encodeFailure(NamedItemFailureKind::InactiveOwner));
    }
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  const auto& record = authority.value();
  const auto& routed = key.definition();
  auto module = StableModuleQueryKey::fromVerified(routed.module());
  bool contradictory = identity::DefinitionKey::compute(record) != routed.definition() ||
                       !sameModule(record.module(), routed.module()) || module == zc::none;
  if (!contradictory) {
    auto inventory = context.template get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(module));
    if (inventory.isRuntimeFailure()) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          inventory.runtimeFailure());
    }
    contradictory = inventory.kind() != query::QueryValueKind::Value ||
                    !containsAuthority(inventory.value(), routed.definition(), record);
  }
  if (!contradictory) {
    return query::TypedQueryResult<RecoveredAuthority>::value(
        RecoveredAuthority(record.clone(), zc::mv(ZC_ASSERT_NONNULL(module))));
  }

  auto readiness =
      context.template probeInput<ActiveDefinitionAuthorityReadyInput>(key.contextRoots());
  if (readiness.isRuntimeFailure()) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(readiness.runtimeFailure());
  }
  if (readiness.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Context>
query::TypedQueryResult<RecoveredAuthority> verifierAuthority(Context& context,
                                                              const ContextualDefinitionKey& key) {
  auto authority = context.template probeInput<ActiveDefinitionAuthorityInput>(key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.kind() == query::QueryValueKind::Absence) {
    auto readiness =
        context.template probeInput<ActiveDefinitionAuthorityReadyInput>(key.contextRoots());
    if (readiness.isRuntimeFailure()) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          readiness.runtimeFailure());
    }
    if (readiness.kind() == query::QueryValueKind::Absence) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    if (readiness.kind() == query::QueryValueKind::Value) {
      return query::TypedQueryResult<RecoveredAuthority>::semanticFailure(
          encodeFailure(NamedItemFailureKind::InactiveOwner));
    }
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto recomputed = identity::DefinitionKey::compute(authority.value());
  const auto& routed = key.definition();
  auto module = StableModuleQueryKey::fromVerified(routed.module());
  bool contradictory = recomputed != routed.definition() ||
                       !sameModule(authority.value().module(), routed.module()) ||
                       module == zc::none;
  if (!contradictory) {
    auto inventory = context.template get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(module));
    if (inventory.isRuntimeFailure()) {
      return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
          inventory.runtimeFailure());
    }
    contradictory = inventory.kind() != query::QueryValueKind::Value ||
                    !containsAuthority(inventory.value(), routed.definition(), authority.value());
  }
  if (!contradictory) {
    return query::TypedQueryResult<RecoveredAuthority>::value(
        RecoveredAuthority(authority.value().clone(), zc::mv(ZC_ASSERT_NONNULL(module))));
  }

  auto readiness =
      context.template probeInput<ActiveDefinitionAuthorityReadyInput>(key.contextRoots());
  if (readiness.isRuntimeFailure()) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(readiness.runtimeFailure());
  }
  if (readiness.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<RecoveredAuthority>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

zc::Maybe<ast::NodeId> providerModuleNode(const ast::Tree& tree,
                                          const identity::ModuleKey& module) {
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile ||
      module.path().size() == 0) {
    return zc::none;
  }
  const auto& source = tree.node(tree.root());
  const ast::NodeId declaration(source.payload.words[ast::kSourceFileModuleWord]);
  if (!declaration) { return ast::NodeId(); }
  if (!tree.contains(declaration) ||
      tree.node(declaration).kind != ast::SyntaxKind::ModuleDeclaration) {
    return zc::none;
  }
  const auto& syntax = tree.node(declaration);
  const auto form = static_cast<ast::ModuleDeclarationForm>(
      syntax.payload.words[ast::kModuleDeclarationFormWord]);
  if (form != ast::ModuleDeclarationForm::RootDeclaration &&
      form != ast::ModuleDeclarationForm::InlineRoot) {
    return zc::none;
  }
  auto name = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (name == zc::none || ZC_ASSERT_NONNULL(name) != module.path().back()) { return zc::none; }
  return declaration;
}

zc::Maybe<ast::NodeId> verifierModuleNode(const ast::Tree& tree,
                                          const identity::ModuleKey& module) {
  if (!tree.contains(tree.root())) { return zc::none; }
  const auto& root = tree.node(tree.root());
  if (root.kind != ast::SyntaxKind::SourceFile || module.path().size() == 0) { return zc::none; }
  const ast::NodeId declared(root.payload.words[ast::kSourceFileModuleWord]);
  if (!declared) { return ast::NodeId(); }
  if (!tree.contains(declared)) { return zc::none; }
  const auto& candidate = tree.node(declared);
  if (candidate.kind != ast::SyntaxKind::ModuleDeclaration) { return zc::none; }
  const auto form = static_cast<ast::ModuleDeclarationForm>(
      candidate.payload.words[ast::kModuleDeclarationFormWord]);
  if (form != ast::ModuleDeclarationForm::RootDeclaration &&
      form != ast::ModuleDeclarationForm::InlineRoot) {
    return zc::none;
  }
  auto declaredName = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(candidate.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  return declaredName != zc::none && ZC_ASSERT_NONNULL(declaredName) == module.path().back()
             ? zc::Maybe<ast::NodeId>(declared)
             : zc::none;
}

template <typename Context>
query::TypedQueryResult<LoadedNamedItemSource> providerSource(Context& context,
                                                              const RecoveredAuthority& authority) {
  auto selected = context.template get<module_graph_query::SelectedModuleSourceQuery>(
      authority.record.module());
  if (selected.isRuntimeFailure()) {
    if (selected.runtimeFailure() == query::QueryRuntimeFailure::MissingInput) {
      return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
          encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource));
    }
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        selected.runtimeFailure());
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource));
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeFailure()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        encodeFailure(NamedItemFailureKind::UpstreamSourceRejected, parsed.semanticFailureBytes()));
  }
  if (parsed.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.value().capability().clone());
  if (canonical == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto moduleNode =
      providerModuleNode(ZC_ASSERT_NONNULL(canonical).tree(), authority.record.module());
  if (moduleNode == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<LoadedNamedItemSource>::value(
      LoadedNamedItemSource(zc::mv(ZC_ASSERT_NONNULL(canonical)), ZC_ASSERT_NONNULL(moduleNode)));
}

template <typename Context>
query::TypedQueryResult<LoadedNamedItemSource> verifierSource(Context& context,
                                                              const RecoveredAuthority& authority) {
  auto selected = context.template get<module_graph_query::SelectedModuleSourceQuery>(
      authority.record.module());
  if (selected.isRuntimeFailure()) {
    if (selected.runtimeFailure() == query::QueryRuntimeFailure::MissingInput) {
      return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
          encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource));
    }
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        selected.runtimeFailure());
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource));
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.template getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
  if (parsed.isRuntimeFailure()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        encodeFailure(NamedItemFailureKind::UpstreamSourceRejected, parsed.semanticFailureBytes()));
  }
  if (parsed.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.value().capability().clone());
  if (canonical == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto moduleNode =
      verifierModuleNode(ZC_ASSERT_NONNULL(canonical).tree(), authority.record.module());
  if (moduleNode == zc::none) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<LoadedNamedItemSource>::value(
      LoadedNamedItemSource(zc::mv(ZC_ASSERT_NONNULL(canonical)), ZC_ASSERT_NONNULL(moduleNode)));
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

bool providerSiteLess(const binder::RevisionLocalDefinitionSite& left,
                      const binder::RevisionLocalDefinitionSite& right) {
  auto leftSource = left.site().source().encode();
  auto rightSource = right.site().source().encode();
  const int sourceOrder = compareBytes(leftSource.asPtr(), rightSource.asPtr());
  if (sourceOrder != 0) { return sourceOrder < 0; }
  if (left.byteStart() != right.byteStart()) { return left.byteStart() < right.byteStart(); }
  if (left.byteEnd() != right.byteEnd()) { return left.byteEnd() < right.byteEnd(); }
  const auto leftPath = left.site().moduleSyntaxPath();
  const auto rightPath = right.site().moduleSyntaxPath();
  const size_t shared = leftPath.size() < rightPath.size() ? leftPath.size() : rightPath.size();
  for (size_t index = 0; index < shared; ++index) {
    if (leftPath[index] != rightPath[index]) { return leftPath[index] < rightPath[index]; }
  }
  return leftPath.size() < rightPath.size();
}

bool verifierSiteLess(const binder::RevisionLocalDefinitionSite& candidate,
                      const binder::RevisionLocalDefinitionSite& selected) {
  auto candidateSource = candidate.site().source().encode();
  auto selectedSource = selected.site().source().encode();
  const int sourceOrder = compareBytes(candidateSource.asPtr(), selectedSource.asPtr());
  if (sourceOrder < 0) { return true; }
  if (sourceOrder > 0) { return false; }
  if (candidate.byteStart() < selected.byteStart()) { return true; }
  if (candidate.byteStart() > selected.byteStart()) { return false; }
  if (candidate.byteEnd() < selected.byteEnd()) { return true; }
  if (candidate.byteEnd() > selected.byteEnd()) { return false; }
  const auto candidatePath = candidate.site().moduleSyntaxPath();
  const auto selectedPath = selected.site().moduleSyntaxPath();
  for (size_t index = 0; index < candidatePath.size() && index < selectedPath.size(); ++index) {
    if (candidatePath[index] < selectedPath[index]) { return true; }
    if (candidatePath[index] > selectedPath[index]) { return false; }
  }
  return candidatePath.size() < selectedPath.size();
}

zc::Maybe<ast::NodeId> providerRoot(const binder::RevisionLocalDefinitionSites& sites,
                                    const identity::DefinitionKey& key) {
  zc::Maybe<const binder::RevisionLocalDefinitionSite&> selected;
  for (const auto& candidate : sites.entries()) {
    if (candidate.definition() != key) { continue; }
    ZC_IF_SOME(current, selected) {
      if (providerSiteLess(candidate, current)) { selected = candidate; }
    } else {
      selected = candidate;
    }
  }
  ZC_IF_SOME(value, selected) { return value.node(); }
  return zc::none;
}

zc::Maybe<ast::NodeId> verifierRoot(const binder::RevisionLocalDefinitionSites& sites,
                                    const identity::DefinitionKey& key) {
  zc::Maybe<const binder::RevisionLocalDefinitionSite&> selected;
  for (size_t index = sites.entries().size(); index != 0; --index) {
    const auto& candidate = sites.entries()[index - 1];
    if (candidate.definition() != key) { continue; }
    ZC_IF_SOME(current, selected) {
      if (verifierSiteLess(candidate, current)) { selected = candidate; }
    } else {
      selected = candidate;
    }
  }
  ZC_IF_SOME(value, selected) { return value.node(); }
  return zc::none;
}

zc::Vector<binder::ModuleBodyDefinitionBoundaryInput> definitionBoundaries(
    const binder::CanonicalParsedModule& parsed, ast::NodeId moduleNode,
    const binder::RevisionLocalDefinitionSites& sites) {
  const auto inventory = binder::DefinitionInventory::collect(parsed.tree());
  const ast::NodeId inventoryModuleNode =
      moduleNode == parsed.tree().root() ? ast::NodeId() : moduleNode;
  zc::Vector<binder::ModuleBodyDefinitionBoundaryInput> result(sites.entries().size());
  for (const auto& site : sites.entries()) {
    bool boundary = false;
    for (const auto& definition : inventory.definitions()) {
      if (definition.node == site.node() && definition.moduleNode == inventoryModuleNode &&
          definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        boundary = true;
        break;
      }
    }
    if (boundary) {
      result.add(binder::ModuleBodyDefinitionBoundaryInput{site.node(), site.definition().clone()});
    }
  }
  return result;
}

zc::Vector<binder::ModuleBodyImplementationBoundaryInput> implementationBoundaries(
    const binder::RevisionLocalImplementationSites& sites) {
  zc::Vector<binder::ModuleBodyImplementationBoundaryInput> result(sites.entries().size());
  for (const auto& site : sites.entries()) {
    result.add(
        binder::ModuleBodyImplementationBoundaryInput{site.node(), site.occurrence().clone()});
  }
  return result;
}

template <typename Value, typename Upstream>
query::TypedQueryResult<Value> propagateFailure(const query::TypedQueryResult<Upstream>& result) {
  if (result.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(result.runtimeFailure());
  }
  if (result.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(result.semanticFailureBytes()));
  }
  return query::TypedQueryResult<Value>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename Capability, typename Upstream>
query::CapabilityProviderResult<Capability> propagateCapabilityFailure(
    const query::TypedQueryResult<Upstream>& result) {
  if (result.isRuntimeFailure()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(result.runtimeFailure());
  }
  if (result.kind() == query::QueryValueKind::SemanticFailure) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        zc::heapArray<uint8_t>(result.semanticFailureBytes()));
  }
  return query::CapabilityProviderResult<Capability>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

template <typename ResultValue, typename ExpectedValue>
bool sameFailure(const query::TypedQueryResult<ExpectedValue>& expected,
                 const query::TypedQueryResult<ResultValue>& result) {
  if (expected.isRuntimeFailure()) { return false; }
  return expected.kind() == query::QueryValueKind::SemanticFailure &&
         result.kind() == query::QueryValueKind::SemanticFailure &&
         expected.semanticFailureBytes() == result.semanticFailureBytes();
}

}  // namespace

zc::StringPtr NamedItemSyntaxQuery::domain() { return "zom.query.named-item-syntax"_zc; }

query::QueryKindContract NamedItemSyntaxQuery::contract() { return semanticContract(domain()); }

zc::Array<uint8_t> NamedItemSyntaxQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<NamedItemSyntaxQuery::Key> NamedItemSyntaxQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualDefinitionKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> NamedItemSyntaxQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<NamedItemSyntaxQuery::Value> NamedItemSyntaxQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return binder::NamedItemSyntax::decodeCanonical(bytes);
}

query::TypedQueryResult<NamedItemSyntaxQuery::Value> NamedItemSyntaxQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto authority = providerAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return propagateFailure<Value>(authority);
  }
  auto source = providerSource(context, authority.value());
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value) {
    return propagateFailure<Value>(source);
  }
  auto implementations = context.get<NamedImplementationInventoryQuery>(authority.value().module);
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  for (const auto failure :
       {implementations.semanticFailureBytes(), definitionSites.semanticFailureBytes(),
        implementationSites.semanticFailureBytes()}) {
    if (failure.size() != 0) {
      return query::TypedQueryResult<Value>::semanticFailure(
          encodeFailure(NamedItemFailureKind::UpstreamSourceRejected, failure));
    }
  }
  if (implementations.isRuntimeFailure() || definitionSites.isRuntimeFailure() ||
      implementationSites.isRuntimeFailure()) {
    const auto failure = implementations.isRuntimeFailure() ? implementations.runtimeFailure()
                         : definitionSites.isRuntimeFailure()
                             ? definitionSites.runtimeFailure()
                             : implementationSites.runtimeFailure();
    return query::TypedQueryResult<Value>::runtimeFailure(failure);
  }
  if (implementations.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto root = providerRoot(definitionSites.value().capability(), key.definition().definition());
  if (root == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.value().capability());
  auto implementationInputs = implementationBoundaries(implementationSites.value().capability());
  auto projection = binder::ModuleBodySyntaxProducer::produceNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr(),
      implementationInputs.asPtr());
  if (!projection.is<binder::ModuleBodySyntaxProjection>()) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(NamedItemFailureKind::BoundaryMismatch));
  }
  auto syntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().syntax));
  if (syntax == zc::none) {
    return query::TypedQueryResult<Value>::semanticFailure(
        encodeFailure(NamedItemFailureKind::MalformedDetachedSyntax));
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(syntax)));
}

bool NamedItemSyntaxQuery::verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result) {
  auto authority = verifierAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return sameFailure(authority, result);
  }
  auto source = verifierSource(context, authority.value());
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value) {
    return sameFailure(source, result);
  }
  auto implementations = context.get<NamedImplementationInventoryQuery>(authority.value().module);
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  for (const auto failure :
       {implementations.semanticFailureBytes(), definitionSites.semanticFailureBytes(),
        implementationSites.semanticFailureBytes()}) {
    if (failure.size() != 0) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() ==
                 encodeFailure(NamedItemFailureKind::UpstreamSourceRejected, failure).asPtr();
    }
  }
  if (result.kind() != query::QueryValueKind::Value || implementations.isRuntimeFailure() ||
      definitionSites.isRuntimeFailure() || implementationSites.isRuntimeFailure() ||
      implementations.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return false;
  }
  auto root = verifierRoot(definitionSites.value().capability(), key.definition().definition());
  if (root == zc::none) { return false; }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.value().capability());
  auto implementationInputs = implementationBoundaries(implementationSites.value().capability());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstructNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr(),
      implementationInputs.asPtr());
  if (!expected.is<binder::ModuleBodySyntaxProjection>()) { return false; }
  auto syntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(expected.get<binder::ModuleBodySyntaxProjection>().syntax));
  return syntax != zc::none && ZC_ASSERT_NONNULL(syntax) == result.value();
}

zc::StringPtr NamedItemProvenanceQuery::domain() { return "zom.query.named-item-provenance"_zc; }

query::QueryKindContract NamedItemProvenanceQuery::contract() {
  return provenanceContract(domain());
}

zc::Array<uint8_t> NamedItemProvenanceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<NamedItemProvenanceQuery::Key> NamedItemProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualDefinitionKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<NamedItemProvenanceQuery::Capability>
NamedItemProvenanceQuery::provide(query::CapabilityQueryContext& context, const Key& key) {
  auto authority = providerAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return propagateCapabilityFailure<Capability>(authority);
  }
  auto source = providerSource(context, authority.value());
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value) {
    return propagateCapabilityFailure<Capability>(source);
  }
  auto syntax = context.get<NamedItemSyntaxQuery>(key);
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  for (const auto failure : {syntax.semanticFailureBytes(), definitionSites.semanticFailureBytes(),
                             implementationSites.semanticFailureBytes()}) {
    if (failure.size() != 0) {
      return query::CapabilityProviderResult<Capability>::semanticFailure(
          zc::heapArray<uint8_t>(failure));
    }
  }
  if (syntax.isRuntimeFailure() || definitionSites.isRuntimeFailure() ||
      implementationSites.isRuntimeFailure()) {
    const auto failure = syntax.isRuntimeFailure() ? syntax.runtimeFailure()
                         : definitionSites.isRuntimeFailure()
                             ? definitionSites.runtimeFailure()
                             : implementationSites.runtimeFailure();
    return query::CapabilityProviderResult<Capability>::runtimeFailure(failure);
  }
  if (syntax.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto root = providerRoot(definitionSites.value().capability(), key.definition().definition());
  if (root == zc::none) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        encodeFailure(NamedItemFailureKind::MissingProvenance));
  }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.value().capability());
  auto implementationInputs = implementationBoundaries(implementationSites.value().capability());
  auto projection = binder::ModuleBodySyntaxProducer::produceNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr(),
      implementationInputs.asPtr());
  if (!projection.is<binder::ModuleBodySyntaxProjection>()) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        encodeFailure(NamedItemFailureKind::BoundaryMismatch));
  }
  auto expectedSyntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().syntax));
  if (expectedSyntax == zc::none || ZC_ASSERT_NONNULL(expectedSyntax) != syntax.value()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto provenance = binder::NamedItemProvenance::from(
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().provenance));
  if (provenance == zc::none) {
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        encodeFailure(NamedItemFailureKind::MissingProvenance));
  }
  auto stableWitness = ZC_ASSERT_NONNULL(provenance).encodeCanonical();
  return query::CapabilityProviderResult<Capability>::value(
      zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(provenance))), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> NamedItemProvenanceQuery::verify(
    query::CapabilityQueryContext& context, const Key& key, const Capability& candidate) {
  auto authority = verifierAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto source = verifierSource(context, authority.value());
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto syntax = context.get<NamedItemSyntaxQuery>(key);
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  if (syntax.isRuntimeFailure() || definitionSites.isRuntimeFailure() ||
      implementationSites.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
      definitionSites.kind() != query::QueryValueKind::Value ||
      implementationSites.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto root = verifierRoot(definitionSites.value().capability(), key.definition().definition());
  if (root == zc::none) { return zc::none; }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.value().capability());
  auto implementationInputs = implementationBoundaries(implementationSites.value().capability());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstructNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr(),
      implementationInputs.asPtr());
  if (!expected.is<binder::ModuleBodySyntaxProjection>()) { return zc::none; }
  auto expectedSyntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(expected.get<binder::ModuleBodySyntaxProjection>().syntax));
  auto expectedProvenance = binder::NamedItemProvenance::from(
      zc::mv(expected.get<binder::ModuleBodySyntaxProjection>().provenance));
  if (expectedSyntax == zc::none || expectedProvenance == zc::none ||
      ZC_ASSERT_NONNULL(expectedSyntax) != syntax.value() ||
      ZC_ASSERT_NONNULL(expectedProvenance) != candidate) {
    return zc::none;
  }
  auto witness = candidate.encodeCanonical();
  auto decoded = binder::NamedItemProvenance::decodeCanonical(witness.asPtr());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate) { return zc::none; }
  return zc::mv(witness);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
