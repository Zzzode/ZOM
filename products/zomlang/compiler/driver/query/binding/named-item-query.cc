// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/binding/named-item-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/driver/query/binding/active-definition-authority-query.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"

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
    if (entry.key() == key) { return entry.record().encode().asPtr() == encoded.asPtr(); }
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
  if (parsed.isRuntimeRejected()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<parser::ParseSourceQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        Contract::encode(parsed.diagnostics()));
  }
  if (!parsed.isPublished()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
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
  if (parsed.isRuntimeRejected()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(parsed.runtimeFailure());
  }
  if (parsed.isSourceRejected()) {
    using Contract =
        query::CapabilityFailureContract<parser::ParseSourceQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>;
    return query::TypedQueryResult<LoadedNamedItemSource>::semanticFailure(
        Contract::encode(parsed.diagnostics()));
  }
  if (!parsed.isPublished()) {
    return query::TypedQueryResult<LoadedNamedItemSource>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto canonical =
      binder::CanonicalParsedModule::fromQueryResult(parsed.lease().capability().clone());
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

zc::Maybe<binder::BinderKeyFailure> inactiveOwnerFailure(const ContextualDefinitionKey& key) {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  return binder::BinderKeyFailure::from(
      binder::BinderKeyFailureKind::InactiveOwner,
      binder::BinderQueryOwner::definitionHeader(key.definition().clone()), zc::mv(noPath));
}

zc::Maybe<binder::BinderKeyFailure> missingSourceFailure(const identity::ModuleKey& module) {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  return binder::BinderKeyFailure::from(binder::BinderKeyFailureKind::MissingSelectedModuleSource,
                                        binder::BinderQueryOwner::module(module.clone()),
                                        zc::mv(noPath));
}

template <typename Descriptor>
query::CapabilityProviderResult<Descriptor> sourceRejectedFromBytes(
    zc::ArrayPtr<const uint8_t> bytes) {
  using Contract =
      query::CapabilityFailureContract<Descriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = Contract::decode(bytes);
  if (diagnostics == zc::none) {
    return query::CapabilityProviderResult<Descriptor>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::CapabilityProviderResult<Descriptor>::template sourceRejected<
      diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename TargetDescriptor, typename SourceDescriptor>
query::CapabilityProviderResult<TargetDescriptor> forwardSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  return sourceRejectedFromBytes<TargetDescriptor>(
      SourceContract::encode(source.diagnostics()).asPtr());
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
  if (implementations.semanticFailureBytes().size() != 0) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(implementations.semanticFailureBytes()));
  }
  if (implementations.isRuntimeFailure() || definitionSites.isRuntimeRejected() ||
      implementationSites.isRuntimeRejected()) {
    const auto failure = implementations.isRuntimeFailure() ? implementations.runtimeFailure()
                         : definitionSites.isRuntimeRejected()
                             ? definitionSites.runtimeFailure()
                             : implementationSites.runtimeFailure();
    return query::TypedQueryResult<Value>::runtimeFailure(failure);
  }
  if (implementations.kind() != query::QueryValueKind::Value || !definitionSites.isPublished() ||
      !implementationSites.isPublished()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto root = providerRoot(definitionSites.lease().capability(), key.definition().definition());
  if (root == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.lease().capability());
  auto projection = binder::ModuleBodySyntaxProducer::produceNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr());
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
  if (implementations.semanticFailureBytes().size() != 0) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == implementations.semanticFailureBytes();
  }
  if (result.kind() != query::QueryValueKind::Value || implementations.isRuntimeFailure() ||
      definitionSites.isRuntimeRejected() || implementationSites.isRuntimeRejected() ||
      implementations.kind() != query::QueryValueKind::Value || !definitionSites.isPublished() ||
      !implementationSites.isPublished()) {
    return false;
  }
  auto root = verifierRoot(definitionSites.lease().capability(), key.definition().definition());
  if (root == zc::none) { return false; }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.lease().capability());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstructNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr());
  if (!expected.is<binder::ModuleBodySyntaxProjection>()) { return false; }
  auto syntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(expected.get<binder::ModuleBodySyntaxProjection>().syntax));
  return syntax != zc::none && ZC_ASSERT_NONNULL(syntax) == result.value();
}

zc::Array<uint8_t> NamedItemProvenanceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<NamedItemProvenanceQuery::Key> NamedItemProvenanceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualDefinitionKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<NamedItemProvenanceQuery> NamedItemProvenanceQuery::provide(
    query::CapabilityQueryContext<NamedItemProvenanceQuery>& context, const Key& key) {
  auto authority = providerAuthority(context, key);
  if (authority.isRuntimeFailure()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        authority.runtimeFailure());
  }
  if (authority.kind() == query::QueryValueKind::SemanticFailure) {
    auto failure = inactiveOwnerFailure(key);
    if (failure == zc::none || authority.semanticFailureBytes() !=
                                   encodeFailure(NamedItemFailureKind::InactiveOwner).asPtr()) {
      return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::keyRejected<
        binder::BinderKeyFailure>(zc::mv(ZC_ASSERT_NONNULL(failure)));
  }
  auto source = providerSource(context, authority.value());
  if (source.isRuntimeFailure()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        source.runtimeFailure());
  }
  if (source.kind() == query::QueryValueKind::SemanticFailure) {
    if (source.semanticFailureBytes() ==
        encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource).asPtr()) {
      auto failure = missingSourceFailure(key.definition().module());
      if (failure == zc::none) {
        return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      return query::CapabilityProviderResult<NamedItemProvenanceQuery>::keyRejected<
          binder::BinderKeyFailure>(zc::mv(ZC_ASSERT_NONNULL(failure)));
    }
    return sourceRejectedFromBytes<NamedItemProvenanceQuery>(source.semanticFailureBytes());
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(authority.value().module);
  if (admission.isRuntimeRejected()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        admission.runtimeFailure());
  }
  if (admission.isSourceRejected()) {
    return forwardSourceRejection<NamedItemProvenanceQuery>(admission);
  }
  if (!admission.isPublished()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitionInventoryResult =
      context.get<NamedDefinitionInventoryQuery>(authority.value().module);
  if (definitionInventoryResult.isRuntimeFailure()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        definitionInventoryResult.runtimeFailure());
  }
  if (definitionInventoryResult.kind() != query::QueryValueKind::Value ||
      !containsAuthority(definitionInventoryResult.value(), key.definition().definition(),
                         authority.value().record)) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  if (definitionSites.isRuntimeRejected()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        definitionSites.runtimeFailure());
  }
  if (!definitionSites.isPublished()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  if (implementationSites.isRuntimeRejected()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        implementationSites.runtimeFailure());
  }
  if (!implementationSites.isPublished()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto syntax = context.get<NamedItemSyntaxQuery>(key);
  if (syntax.isRuntimeFailure()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        syntax.runtimeFailure());
  }
  if (syntax.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto root = providerRoot(definitionSites.lease().capability(), key.definition().definition());
  if (root == zc::none) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.lease().capability());
  auto projection = binder::ModuleBodySyntaxProducer::produceNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr());
  if (!projection.is<binder::ModuleBodySyntaxProjection>()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto expectedSyntax = binder::NamedItemSyntax::from(
      authority.value().record.module().clone(),
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().syntax));
  if (expectedSyntax == zc::none || ZC_ASSERT_NONNULL(expectedSyntax) != syntax.value()) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto provenance = binder::NamedItemProvenance::from(
      zc::mv(projection.get<binder::ModuleBodySyntaxProjection>().provenance));
  if (provenance == zc::none) {
    return query::CapabilityProviderResult<NamedItemProvenanceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(provenance)));
  auto stableWitness =
      query::CapabilityCandidateContract<NamedItemProvenanceQuery>::encode(*candidate);
  return query::CapabilityProviderResult<NamedItemProvenanceQuery>::candidate(
      zc::mv(candidate), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> NamedItemProvenanceQuery::verify(
    query::CapabilityQueryContext<NamedItemProvenanceQuery>& context, const Key& key,
    const Capability& candidate) {
  auto authority = verifierAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto source = verifierSource(context, authority.value());
  if (source.isRuntimeFailure() || source.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(authority.value().module);
  if (!admission.isPublished()) { return zc::none; }
  auto definitionsInventory = context.get<NamedDefinitionInventoryQuery>(authority.value().module);
  if (definitionsInventory.isRuntimeFailure() ||
      definitionsInventory.kind() != query::QueryValueKind::Value ||
      !containsAuthority(definitionsInventory.value(), key.definition().definition(),
                         authority.value().record)) {
    return zc::none;
  }
  auto definitionSites =
      context.getCapability<RevisionLocalDefinitionSitesQuery>(authority.value().module);
  auto implementationSites =
      context.getCapability<RevisionLocalImplementationSitesQuery>(authority.value().module);
  if (!definitionSites.isPublished() || !implementationSites.isPublished()) { return zc::none; }
  auto syntax = context.get<NamedItemSyntaxQuery>(key);
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto root = verifierRoot(definitionSites.lease().capability(), key.definition().definition());
  if (root == zc::none) { return zc::none; }
  auto definitions = definitionBoundaries(source.value().parsed, source.value().moduleNode,
                                          definitionSites.lease().capability());
  auto expected = binder::ModuleBodySyntaxVerifier::reconstructNamedItem(
      source.value().parsed, authority.value().record.module(), source.value().moduleNode,
      ZC_ASSERT_NONNULL(root), key.definition().definition(), definitions.asPtr());
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

query::CapabilityRejectionCheck verifyNamedItemSourceRejection(
    query::CapabilityQueryContext<NamedItemProvenanceQuery>& context,
    const NamedItemProvenanceQuery::Key& key,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> diagnostics) {
  auto authority = verifierAuthority(context, key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto source = verifierSource(context, authority.value());
  if (source.isRuntimeFailure()) { return query::CapabilityRejectionCheck::Rejected; }
  auto actual = binder::encodeStableBindingDiagnosticFacts(diagnostics);
  if (actual == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  if (source.kind() == query::QueryValueKind::SemanticFailure) {
    if (source.semanticFailureBytes() ==
        encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource).asPtr()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    return source.semanticFailureBytes() == ZC_ASSERT_NONNULL(actual).asPtr()
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  auto admission = context.getCapability<StableIdentityAdmissionQuery>(authority.value().module);
  if (!admission.isSourceRejected()) { return query::CapabilityRejectionCheck::Rejected; }
  auto expected = binder::encodeStableBindingDiagnosticFacts(admission.diagnostics().values());
  return expected != zc::none &&
                 ZC_ASSERT_NONNULL(expected).asPtr() == ZC_ASSERT_NONNULL(actual).asPtr()
             ? query::CapabilityRejectionCheck::Verified
             : query::CapabilityRejectionCheck::Rejected;
}

query::CapabilityRejectionCheck verifyNamedItemKeyRejection(
    query::CapabilityQueryContext<NamedItemProvenanceQuery>& context,
    const NamedItemProvenanceQuery::Key& key, const binder::BinderKeyFailure& failure) {
  auto authority = verifierAuthority(context, key);
  if (authority.isRuntimeFailure()) { return query::CapabilityRejectionCheck::Rejected; }
  if (authority.kind() == query::QueryValueKind::SemanticFailure) {
    auto expected = inactiveOwnerFailure(key);
    return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  if (authority.kind() != query::QueryValueKind::Value) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto source = verifierSource(context, authority.value());
  if (source.isRuntimeFailure()) { return query::CapabilityRejectionCheck::Rejected; }
  if (source.kind() == query::QueryValueKind::SemanticFailure) {
    if (source.semanticFailureBytes() !=
        encodeFailure(NamedItemFailureKind::MissingSelectedModuleSource).asPtr()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    auto expected = missingSourceFailure(key.definition().module());
    return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
               ? query::CapabilityRejectionCheck::Verified
               : query::CapabilityRejectionCheck::Rejected;
  }
  return query::CapabilityRejectionCheck::Rejected;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query

namespace zomlang::compiler::query {

using NamedItemProvenanceDescriptor = driver::incremental_binding_query::NamedItemProvenanceQuery;

StableWitnessBytes CapabilityCandidateContract<NamedItemProvenanceDescriptor>::encode(
    const NamedItemProvenanceDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<NamedItemProvenanceDescriptor::Capability>> CapabilityCandidateContract<
    NamedItemProvenanceDescriptor>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto candidate = binder::NamedItemProvenance::decodeCanonical(bytes);
  if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::heap<NamedItemProvenanceDescriptor::Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
}

zc::Array<uint8_t> CapabilityFailureContract<
    NamedItemProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<NamedItemProvenanceDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    NamedItemProvenanceDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<NamedItemProvenanceDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<NamedItemProvenanceDescriptor>& context,
           const NamedItemProvenanceDescriptor::Key& key, const Sequence& diagnostics) {
  return driver::incremental_binding_query::verifyNamedItemSourceRejection(context, key,
                                                                           diagnostics.values());
}

zc::Array<uint8_t> CapabilityFailureContract<
    NamedItemProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    NamedItemProvenanceDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck
CapabilityFailureContract<NamedItemProvenanceDescriptor, KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<NamedItemProvenanceDescriptor>& context,
           const NamedItemProvenanceDescriptor::Key& key, const binder::BinderKeyFailure& failure) {
  return driver::incremental_binding_query::verifyNamedItemKeyRejection(context, key, failure);
}

}  // namespace zomlang::compiler::query
