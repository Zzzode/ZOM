// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "binding-oracle-components.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

zc::Maybe<zc::Vector<uint32_t>> schemaPreorderOrdinals(const ast::Tree& tree) {
  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& value : ordinals) { value = UINT32_MAX; }
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node.value >= ordinals.size() || ordinals[node.value] != UINT32_MAX) { return; }
    ordinals[node.value] = ordinal++;
  });
  if (ordinal != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
}
bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}
struct ExplicitOracleSourceOrderKey final {
  uint64_t start;
  uint64_t end;
  size_t inventoryIndex;

  bool operator==(const ExplicitOracleSourceOrderKey& other) const noexcept {
    return start == other.start && end == other.end && inventoryIndex == other.inventoryIndex;
  }
  bool operator<(const ExplicitOracleSourceOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return inventoryIndex < other.inventoryIndex;
  }
};

struct ExplicitOracleActiveScope final {
  zc::HashMap<zc::String, size_t> values;
  zc::HashMap<zc::String, size_t> types;
  size_t receiver = static_cast<size_t>(-1);
};

struct ExplicitOracleReceiverRecord final {
  uint32_t scopeIndex;
  size_t inventoryIndex;
  ast::NodeId node;
  uint64_t start;
  uint64_t end;
};

struct ExplicitOracleReceiverOrderKey final {
  uint32_t scopeIndex;
  uint64_t start;
  uint64_t end;
  uint32_t node;

  bool operator==(const ExplicitOracleReceiverOrderKey& other) const noexcept {
    return scopeIndex == other.scopeIndex && start == other.start && end == other.end &&
           node == other.node;
  }
  bool operator<(const ExplicitOracleReceiverOrderKey& other) const noexcept {
    if (scopeIndex != other.scopeIndex) { return scopeIndex < other.scopeIndex; }
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return node < other.node;
  }
};

struct ExplicitOracleSpanKey final {
  uint64_t start;
  uint64_t end;

  bool operator==(const ExplicitOracleSpanKey& other) const noexcept {
    return start == other.start && end == other.end;
  }
  bool operator<(const ExplicitOracleSpanKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    return end < other.end;
  }
};

struct ExplicitOracleInventoryEntry final {
  ExplicitOracleInventoryEntry(ast::NodeId node, DefinitionSite&& site,
                               identity::DefinitionKind kind,
                               zc::Maybe<identity::DeclaredDefinitionName>&& bindingName,
                               identity::SourceSpan&& source, zc::Maybe<BindingTarget>&& target,
                               zc::Maybe<AnonymousOwnerLocalKey>&& anonymous,
                               zc::Array<uint8_t>&& canonicalKey) noexcept
      : node(node),
        site(zc::mv(site)),
        kind(kind),
        bindingName(zc::mv(bindingName)),
        source(zc::mv(source)),
        target(zc::mv(target)),
        anonymous(zc::mv(anonymous)),
        canonicalKey(zc::mv(canonicalKey)) {}
  ExplicitOracleInventoryEntry(ExplicitOracleInventoryEntry&&) noexcept = default;
  ExplicitOracleInventoryEntry& operator=(ExplicitOracleInventoryEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ExplicitOracleInventoryEntry);
  ast::NodeId node;
  DefinitionSite site;
  identity::DefinitionKind kind;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
  zc::Maybe<BindingTarget> target;
  zc::Maybe<AnonymousOwnerLocalKey> anonymous;
  zc::Array<uint8_t> canonicalKey;
};

bool sameTarget(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<GenericParameterBindingTarget>()) {
    return rightValue.is<GenericParameterBindingTarget>() &&
           leftValue.get<GenericParameterBindingTarget>().parameter ==
               rightValue.get<GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<CallableParameterBindingTarget>()) {
    return rightValue.is<CallableParameterBindingTarget>() &&
           leftValue.get<CallableParameterBindingTarget>().parameter ==
               rightValue.get<CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<OwnerLocalBindingTarget>()) {
    return rightValue.is<OwnerLocalBindingTarget>() &&
           leftValue.get<OwnerLocalBindingTarget>().binding ==
               rightValue.get<OwnerLocalBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

zc::Array<uint8_t> taggedCanonicalKey(uint8_t tag, zc::ArrayPtr<const uint8_t> key) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(tag);
  encoder.encodeByteString(key);
  return encoder.finish();
}

bool explicitOracleOwnsScope(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::Closure:
      return true;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
      return false;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<Namespace> explicitOracleNamespace(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
      return Namespace::Value;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::TypeParameter:
      return Namespace::Type;
    case DefinitionKind::ModuleAlias:
      return Namespace::Module;
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Closure:
      return zc::none;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<DefinitionActivation> explicitOracleActivation(
    const ast::Tree& tree, const ExplicitOracleInventoryEntry& entry) {
  using identity::DefinitionKind;
  switch (entry.kind) {
    case DefinitionKind::TypeParameter:
      return DefinitionActivation::GenericList;
    case DefinitionKind::Parameter:
      return DefinitionActivation::ParameterList;
    case DefinitionKind::Closure:
      return DefinitionActivation::ExpressionIntroduction;
    case DefinitionKind::Local:
      return DefinitionActivation::AfterInitializer;
    case DefinitionKind::PatternBinding: {
      if (!entry.site.value().is<PatternBindingSite>()) { return zc::none; }
      const auto introducer = entry.site.value().get<PatternBindingSite>().introducer;
      if (!tree.contains(introducer)) { return zc::none; }
      if (tree.node(introducer).kind == ast::SyntaxKind::ForInStatement) {
        return DefinitionActivation::LoopPattern;
      }
      if (tree.node(introducer).kind == ast::SyntaxKind::MatchArmStmt) {
        return DefinitionActivation::MatchPattern;
      }
      return zc::none;
    }
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return DefinitionActivation::ModuleSkeleton;
    case DefinitionKind::ModuleAlias:
      return DefinitionActivation::ImportSurface;
  }
  ZC_UNREACHABLE;
}

enum class ExplicitCaptureOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

ExplicitCaptureOracleResult verifyExplicitCaptureFacts(const VerifiedBindingInput& input,
                                                       const BindingMetadataCandidate& candidate) {
  constexpr size_t kMissing = static_cast<size_t>(-1);
  const auto& tree = input.tree();
  zc::Vector<ExplicitOracleInventoryEntry> inventory;
  for (const auto& entry : input.definitions().definitions()) {
    zc::Maybe<identity::DeclaredDefinitionName> name;
    ZC_IF_SOME(value, entry.bindingName) { name = value.clone(); }
    zc::Maybe<BindingTarget> target = BindingTarget::definition(entry.definition);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ExplicitOracleInventoryEntry(
        entry.node, entry.site.clone(), entry.record.kind(), zc::mv(name), entry.source.clone(),
        zc::mv(target), zc::mv(noAnonymous), taggedCanonicalKey(0x01, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().genericParameters()) {
    zc::Maybe<identity::DeclaredDefinitionName> name = entry.bindingName.clone();
    zc::Maybe<BindingTarget> target = BindingTarget::genericParameter(entry.parameter);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ExplicitOracleInventoryEntry(
        entry.node, entry.site.clone(), identity::DefinitionKind::TypeParameter, zc::mv(name),
        entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
        taggedCanonicalKey(0x02, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().callableParameters()) {
    zc::Maybe<identity::DeclaredDefinitionName> name;
    ZC_IF_SOME(value, entry.bindingName) { name = value.clone(); }
    zc::Maybe<BindingTarget> target = BindingTarget::callableParameter(entry.parameter);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ExplicitOracleInventoryEntry(
        entry.node, entry.site.clone(), identity::DefinitionKind::Parameter, zc::mv(name),
        entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
        taggedCanonicalKey(0x03, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().ownerLocalBindings()) {
    zc::Maybe<identity::DeclaredDefinitionName> name = entry.key.name().clone();
    zc::Maybe<BindingTarget> target = BindingTarget::ownerLocal(entry.binding);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ExplicitOracleInventoryEntry(
        entry.node, entry.site.clone(),
        entry.key.kind() == OwnerLocalBindingKind::Local ? identity::DefinitionKind::Local
                                                         : identity::DefinitionKind::PatternBinding,
        zc::mv(name), entry.source.clone(), zc::mv(target), zc::mv(noAnonymous),
        taggedCanonicalKey(0x04, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().anonymousEntities()) {
    zc::Maybe<identity::DeclaredDefinitionName> noName;
    zc::Maybe<BindingTarget> noTarget;
    zc::Maybe<AnonymousOwnerLocalKey> anonymous = entry.key.clone();
    const auto encoded = entry.key.encode();
    inventory.add(ExplicitOracleInventoryEntry(
        entry.node, entry.site.clone(), identity::DefinitionKind::Closure, zc::mv(noName),
        entry.source.clone(), zc::mv(noTarget), zc::mv(anonymous),
        taggedCanonicalKey(0x06, encoded.asPtr())));
  }
  const auto& arena = candidate;
  if (arena.scopes.empty() || arena.nodeScopes.size() != tree.nodeCount()) {
    return ExplicitCaptureOracleResult::MalformedScopeGraph;
  }

  zc::Vector<uint32_t> scopeByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  for (auto& value : scopeByNode) { value = UINT32_MAX; }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext()) || (index == 0 && scope.parent != zc::none) ||
        (index != 0 && scope.parent == zc::none)) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }
  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) { return ExplicitCaptureOracleResult::MalformedScopeGraph; }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));

  zc::TreeMap<zc::String, size_t> inventoryByCanonicalKey;
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    auto canonicalKey = zc::str(entry.canonicalKey.asChars());
    if (inventoryByCanonicalKey.find(canonicalKey) != zc::none) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    inventoryByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }
  const auto inventoryIndex = [&](const BindingTarget& target) -> zc::Maybe<size_t> {
    for (size_t index = 0; index < inventory.size(); ++index) {
      ZC_IF_SOME(candidateTarget, inventory[index].target) {
        if (sameTarget(candidateTarget, target)) { return index; }
      }
    }
    return zc::none;
  };
  const auto anonymousIndex = [&](const AnonymousOwnerLocalKey& key) -> zc::Maybe<size_t> {
    for (size_t index = 0; index < inventory.size(); ++index) {
      ZC_IF_SOME(candidateAnonymous, inventory[index].anonymous) {
        if (candidateAnonymous == key) { return index; }
      }
    }
    return zc::none;
  };

  zc::Vector<size_t> resolutionByNode;
  resolutionByNode.resize(tree.nodeCount() + 1);
  for (auto& value : resolutionByNode) { value = kMissing; }
  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto node = candidate.nodeBindings[index].node;
    if (!tree.contains(node) || node.value >= resolutionByNode.size() ||
        resolutionByNode[node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    resolutionByNode[node.value] = index;
  }
  zc::Vector<size_t> thisBindingByNode;
  thisBindingByNode.resize(tree.nodeCount() + 1);
  for (auto& value : thisBindingByNode) { value = kMissing; }
  for (size_t index = 0; index < candidate.thisBindings.size(); ++index) {
    const auto node = candidate.thisBindings[index].expression;
    if (!tree.contains(node) || node.value >= thisBindingByNode.size() ||
        tree.node(node).kind != ast::SyntaxKind::ThisExpr ||
        thisBindingByNode[node.value] != kMissing || resolutionByNode[node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    thisBindingByNode[node.value] = index;
  }
  const auto capturable = [](identity::DefinitionKind kind) {
    return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
           kind == identity::DefinitionKind::PatternBinding;
  };
  enum class ExplicitOracleClosureSyntax : uint8_t { NotClosure, Inferred, Explicit };

  zc::Vector<uint32_t> definitionScopeIndices;
  definitionScopeIndices.resize(inventory.size());
  zc::Vector<uint32_t> owningCallableScopeIndices;
  owningCallableScopeIndices.resize(inventory.size());
  zc::Vector<bool> receiverDefinitions;
  receiverDefinitions.resize(inventory.size());
  zc::Vector<zc::Maybe<identity::SourceSpan>> receiverSources;
  receiverSources.resize(inventory.size());
  zc::Vector<ExplicitOracleClosureSyntax> closureSyntaxDomains;
  closureSyntaxDomains.resize(inventory.size());
  zc::Vector<zc::Vector<size_t>> definitionsByIntroducer;
  definitionsByIntroducer.resize(tree.nodeCount() + 1);
  zc::Vector<zc::Vector<size_t>> definitionsByScope;
  definitionsByScope.resize(arena.scopes.size());
  zc::Vector<ExplicitOracleActiveScope> activeScopes;
  activeScopes.reserve(arena.scopes.size());
  for (size_t index = 0; index < arena.scopes.size(); ++index) { activeScopes.add(); }
  zc::TreeMap<ExplicitOracleReceiverOrderKey, ExplicitOracleReceiverRecord> receiverOrder;
  size_t closureCount = 0;
  for (size_t index = 0; index < inventory.size(); ++index) {
    definitionScopeIndices[index] = UINT32_MAX;
    owningCallableScopeIndices[index] = UINT32_MAX;
    receiverDefinitions[index] = false;
    closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::NotClosure;
    const auto& entry = inventory[index];
    if (!tree.contains(entry.node) || entry.node.value >= scopeByNode.size() ||
        scopeByNode[entry.node.value] == UINT32_MAX) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    uint32_t declaringScope = scopeByNode[entry.node.value];
    if (explicitOracleOwnsScope(entry.kind)) {
      if (declaringScope >= arena.scopes.size() ||
          arena.scopes[declaringScope].parent == zc::none) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      ZC_IF_SOME(parent, arena.scopes[declaringScope].parent) { declaringScope = parent.index(); }
    }
    if (declaringScope >= definitionsByScope.size()) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    definitionScopeIndices[index] = declaringScope;
    definitionsByScope[declaringScope].add(index);

    ast::NodeId introducer = entry.node;
    if (entry.site.value().is<PatternBindingSite>()) {
      introducer = entry.site.value().get<PatternBindingSite>().introducer;
    }
    if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    definitionsByIntroducer[introducer.value].add(index);

    if (entry.kind == identity::DefinitionKind::Closure) {
      ++closureCount;
      const auto& closureSyntax = tree.node(entry.node);
      if (closureSyntax.kind == ast::SyntaxKind::LambdaExpression) {
        closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Inferred;
      } else if (closureSyntax.kind == ast::SyntaxKind::FunctionExpression) {
        const ast::NodeId captureList(
            closureSyntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        if (!captureList) {
          closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Inferred;
        } else {
          if (!tree.contains(captureList) ||
              tree.node(captureList).kind != ast::SyntaxKind::CaptureList) {
            return ExplicitCaptureOracleResult::InvalidBindingFact;
          }
          closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Explicit;
        }
      } else {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
    }

    if (entry.kind != identity::DefinitionKind::Parameter ||
        tree.node(entry.node).kind != ast::SyntaxKind::FunctionParameterDecl) {
      continue;
    }
    auto tokenSource =
        input.parsedModule().functionParameterNameSpan(entry.node, ast::SyntaxKind::ThisKeyword);
    if (tokenSource == zc::none) { continue; }
    if (entry.bindingName != zc::none ||
        (arena.scopes[declaringScope].kind != ScopeKind::Function &&
         arena.scopes[declaringScope].kind != ScopeKind::Closure)) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    ZC_IF_SOME(source, tokenSource) {
      receiverDefinitions[index] = true;
      receiverSources[index] = source.clone();
      const ExplicitOracleReceiverOrderKey orderKey{declaringScope, source.byteStart(),
                                                    source.byteEnd(), entry.node.value};
      if (receiverOrder.find(orderKey) != zc::none) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      receiverOrder.insert(orderKey,
                           ExplicitOracleReceiverRecord{declaringScope, index, entry.node,
                                                        source.byteStart(), source.byteEnd()});
    }
  }

  zc::Vector<size_t> explicitOrder;
  explicitOrder.reserve(closureCount);
  for (const auto& ordered : inventoryByCanonicalKey) {
    if (closureSyntaxDomains[ordered.value] == ExplicitOracleClosureSyntax::Explicit) {
      explicitOrder.add(ordered.value);
    }
  }
  zc::Vector<ExplicitOracleReceiverRecord> receivers;
  receivers.reserve(receiverOrder.size());
  for (const auto& ordered : receiverOrder) { receivers.add(ordered.value); }

  zc::Vector<size_t> callableInventoryByScope;
  callableInventoryByScope.resize(arena.scopes.size());
  zc::Vector<uint32_t> ownedCallableScopeByInventory;
  ownedCallableScopeByInventory.resize(inventory.size());
  for (auto& index : callableInventoryByScope) { index = kMissing; }
  for (auto& index : ownedCallableScopeByInventory) { index = UINT32_MAX; }
  for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    if (scope.kind != ScopeKind::Function && scope.kind != ScopeKind::Closure) { continue; }
    const auto& owner = scope.owner.value();
    zc::Maybe<size_t> callableIndex;
    if (scope.kind == ScopeKind::Closure && owner.is<AnonymousScopeOwner>()) {
      callableIndex = anonymousIndex(owner.get<AnonymousScopeOwner>().anonymous);
    } else if (scope.kind == ScopeKind::Function && owner.is<DefinitionScopeOwner>()) {
      callableIndex =
          inventoryIndex(BindingTarget::definition(owner.get<DefinitionScopeOwner>().definition));
    }
    if (callableIndex == zc::none ||
        (scope.kind == ScopeKind::Closure &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind != identity::DefinitionKind::Closure) ||
        (scope.kind == ScopeKind::Function &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind == identity::DefinitionKind::Closure) ||
        ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] != UINT32_MAX) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    callableInventoryByScope[scopeIndex] = ZC_ASSERT_NONNULL(callableIndex);
    ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] =
        static_cast<uint32_t>(scopeIndex);
  }
  for (size_t index = 0; index < inventory.size(); ++index) {
    uint32_t scopeIndex = definitionScopeIndices[index];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        owningCallableScopeIndices[index] = scopeIndex;
        break;
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
  }

  zc::Vector<size_t> candidateParameterCounts;
  zc::Vector<size_t> candidateParameterSlots;
  zc::Vector<bool> mentionedByScopeBinding;
  zc::Vector<bool> mentionedBySurface;
  candidateParameterCounts.resize(inventory.size());
  candidateParameterSlots.resize(inventory.size());
  mentionedByScopeBinding.resize(inventory.size());
  mentionedBySurface.resize(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    candidateParameterCounts[index] = 0;
    candidateParameterSlots[index] = kMissing;
    mentionedByScopeBinding[index] = false;
    mentionedBySurface[index] = false;
  }
  for (size_t index = 0; index < candidate.callableParameters.size(); ++index) {
    auto parameterIndex = inventoryIndex(
        BindingTarget::callableParameter(candidate.callableParameters[index].identity));
    ZC_IF_SOME(found, parameterIndex) {
      if (candidateParameterCounts[found] == 0) { candidateParameterSlots[found] = index; }
      ++candidateParameterCounts[found];
    }
  }
  const auto markTarget = [&](const BindingTarget& target, zc::Vector<bool>& census) {
    auto targetIndex = inventoryIndex(target);
    ZC_IF_SOME(found, targetIndex) { census[found] = true; }
  };
  for (const auto& scope : candidate.scopes) {
    for (const auto& binding : scope.bindings) {
      markTarget(binding.binding.bindingIdentity, mentionedByScopeBinding);
      markTarget(binding.binding.canonicalTarget, mentionedByScopeBinding);
    }
  }
  const auto censusSurface = [&](const ExportSurfaceEntry& surface) {
    markTarget(surface.bindingIdentity, mentionedBySurface);
    markTarget(surface.canonicalTarget, mentionedBySurface);
    for (const auto& step : surface.reexportChain) {
      auto aliasIndex = inventoryIndex(step.bindingIdentity);
      ZC_IF_SOME(found, aliasIndex) { mentionedBySurface[found] = true; }
      markTarget(step.canonicalTarget, mentionedBySurface);
    }
  };
  for (const auto& surface : candidate.currentSurface.visibleEntries) { censusSurface(surface); }
  for (const auto& surface : candidate.currentSurface.exports) { censusSurface(surface); }

  zc::TreeMap<ExplicitOracleSpanKey, size_t> sourceFailureCountsBySpan;
  zc::Vector<zc::Vector<size_t>> receiverFailuresBySchema;
  zc::Vector<zc::Vector<size_t>> duplicateFailuresBySchema;
  receiverFailuresBySchema.resize(tree.nodeCount());
  duplicateFailuresBySchema.resize(tree.nodeCount());
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const auto& failureFact = candidate.sourceFailures[index];
    const ExplicitOracleSpanKey spanKey{failureFact.primary.byteStart(),
                                        failureFact.primary.byteEnd()};
    auto spanCount = sourceFailureCountsBySpan.find(spanKey);
    ZC_IF_SOME(count, spanCount) {
      ++count;
    } else {
      sourceFailureCountsBySpan.insert(spanKey, 1);
    }

    const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
    const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
    if (ordinal >= tree.nodeCount()) { continue; }
    if (site == static_cast<uint8_t>(BinderEmitterSite::ModuleSkeleton)) {
      receiverFailuresBySchema[ordinal].add(index);
    }
    if (site == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) &&
        failureFact.diagnostic == BinderDiagnosticCode::DuplicateIdentifier) {
      duplicateFailuresBySchema[ordinal].add(index);
    }
  }

  zc::Vector<bool> receiverActivated;
  receiverActivated.resize(inventory.size());
  for (auto& activated : receiverActivated) { activated = false; }
  size_t firstReceiverIndex = 0;
  for (size_t receiverIndex = 0; receiverIndex < receivers.size(); ++receiverIndex) {
    const auto& wanted = receivers[receiverIndex];
    const auto& entry = inventory[wanted.inventoryIndex];
    const size_t parameterMatches = candidateParameterCounts[wanted.inventoryIndex];
    if (parameterMatches == 0) { return ExplicitCaptureOracleResult::MissingRequiredResolution; }
    if (parameterMatches != 1) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    const size_t factSlot = candidateParameterSlots[wanted.inventoryIndex];
    if (factSlot >= candidate.callableParameters.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const auto& fact = candidate.callableParameters[factSlot];
    const auto& entrySite = entry.site.value();
    const auto& factSite = fact.site.value();
    if (!fact.receiver || fact.name != zc::none || entry.bindingName != zc::none ||
        fact.declaringScope != arena.scopes[wanted.scopeIndex].id ||
        !sameSpan(fact.source, entry.source) || !entrySite.is<DeclarationDefinitionSite>() ||
        !factSite.is<DeclarationDefinitionSite>() ||
        entrySite.get<DeclarationDefinitionSite>().node != entry.node ||
        factSite.get<DeclarationDefinitionSite>().node != entry.node) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    if (mentionedByScopeBinding[wanted.inventoryIndex] ||
        mentionedBySurface[wanted.inventoryIndex] ||
        resolutionByNode[entry.node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }

    const bool first =
        receiverIndex == 0 || receivers[receiverIndex - 1].scopeIndex != wanted.scopeIndex;
    if (first) { firstReceiverIndex = receiverIndex; }
    const size_t previousIndex = firstReceiverIndex;
    if (receiverSources[wanted.inventoryIndex] == zc::none ||
        receiverSources[receivers[previousIndex].inventoryIndex] == zc::none) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const auto& primary = ZC_ASSERT_NONNULL(receiverSources[wanted.inventoryIndex]);
    const auto& previous =
        ZC_ASSERT_NONNULL(receiverSources[receivers[previousIndex].inventoryIndex]);
    const ExplicitOracleSpanKey primaryKey{primary.byteStart(), primary.byteEnd()};
    size_t relatedFailures = 0;
    ZC_IF_SOME(count, sourceFailureCountsBySpan.find(primaryKey)) { relatedFailures = count; }
    const uint32_t schemaOrdinal = schemaOrdinals[entry.node.value];
    if (schemaOrdinal >= receiverFailuresBySchema.size()) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    const auto& matchingFailures = receiverFailuresBySchema[schemaOrdinal];
    const size_t failureMatches = matchingFailures.size();
    bool failureValid = true;
    for (const auto failureIndex : matchingFailures) {
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      if (first || failureFact.diagnostic != BinderDiagnosticCode::RedeclareParameter ||
          static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, primary) || failureFact.notes.size() != 1 ||
          failureFact.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere ||
          !sameSpan(failureFact.notes[0].source, previous)) {
        failureValid = false;
      }
    }
    if (first && relatedFailures != 0) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    if (first && failureMatches != 0) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    if (!first && failureMatches == 0) {
      return relatedFailures == 0 ? ExplicitCaptureOracleResult::MissingRequiredResolution
                                  : ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    if (!first && (failureMatches != 1 || !failureValid)) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
  }

  const auto activateDefinition = [&](size_t index) {
    if (index >= inventory.size() || definitionScopeIndices[index] == UINT32_MAX ||
        definitionScopeIndices[index] >= activeScopes.size()) {
      return false;
    }
    const auto& entry = inventory[index];
    auto& active = activeScopes[definitionScopeIndices[index]];
    if (receiverDefinitions[index]) {
      receiverActivated[index] = true;
      if (active.receiver == kMissing) { active.receiver = index; }
      return true;
    }
    auto nameSpace = explicitOracleNamespace(entry.kind);
    if (nameSpace == zc::none || entry.bindingName == zc::none) { return true; }
    ZC_IF_SOME(value, nameSpace) {
      if (value != Namespace::Value && value != Namespace::Type) { return true; }
      ZC_IF_SOME(name, entry.bindingName) {
        auto& bindings = value == Namespace::Value ? active.values : active.types;
        if (bindings.find(name.text()) == zc::none) {
          bindings.insert(zc::str(name.text()), index);
        }
      }
    }
    return true;
  };

  const auto activateIntroducer = [&](ast::NodeId introducer,
                                      DefinitionActivation expectedActivation) {
    if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
      return false;
    }
    zc::TreeMap<ExplicitOracleSourceOrderKey, size_t> order;
    for (const auto index : definitionsByIntroducer[introducer.value]) {
      auto activation = explicitOracleActivation(tree, inventory[index]);
      if (activation == zc::none || activation != expectedActivation) { continue; }
      const auto& source = inventory[index].source;
      order.insert(ExplicitOracleSourceOrderKey{source.byteStart(), source.byteEnd(), index},
                   index);
    }
    for (const auto& ordered : order) {
      if (!activateDefinition(ordered.value)) { return false; }
    }
    return true;
  };

  const auto seedDefinitions = [&](DefinitionActivation expectedActivation) {
    for (size_t scopeIndex = 0; scopeIndex < definitionsByScope.size(); ++scopeIndex) {
      zc::TreeMap<ExplicitOracleSourceOrderKey, size_t> order;
      for (const auto index : definitionsByScope[scopeIndex]) {
        auto activation = explicitOracleActivation(tree, inventory[index]);
        if (activation == zc::none || activation != expectedActivation) { continue; }
        const auto& source = inventory[index].source;
        order.insert(ExplicitOracleSourceOrderKey{source.byteStart(), source.byteEnd(), index},
                     index);
      }
      for (const auto& ordered : order) {
        if (!activateDefinition(ordered.value)) { return false; }
      }
    }
    return true;
  };
  if (!seedDefinitions(DefinitionActivation::ModuleSkeleton) ||
      !seedDefinitions(DefinitionActivation::GenericList)) {
    return ExplicitCaptureOracleResult::MalformedScopeGraph;
  }

  const auto activeDefinition = [&](uint32_t scopeIndex, Namespace nameSpace,
                                    zc::StringPtr name) -> zc::Maybe<size_t> {
    uint32_t current = scopeIndex;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (current >= arena.scopes.size()) { return zc::none; }
      const auto& bindings = nameSpace == Namespace::Value ? activeScopes[current].values
                                                           : activeScopes[current].types;
      auto found = bindings.find(name);
      ZC_IF_SOME(index, found) { return index; }
      const auto& scope = arena.scopes[current];
      if (scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  };

  const auto activeReceiver = [&](uint32_t scopeIndex) -> zc::Maybe<size_t> {
    uint32_t current = scopeIndex;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (current >= arena.scopes.size()) { return zc::none; }
      if (activeScopes[current].receiver != kMissing) { return activeScopes[current].receiver; }
      const auto& scope = arena.scopes[current];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Module ||
          scope.parent == zc::none) {
        break;
      }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  };

  zc::Vector<zc::TreeMap<size_t, size_t>> expectedTargetsByClosure;
  expectedTargetsByClosure.reserve(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) { expectedTargetsByClosure.add(); }
  zc::Vector<bool> explicitClosureProcessed;
  explicitClosureProcessed.resize(inventory.size());
  for (auto& processed : explicitClosureProcessed) { processed = false; }
  zc::Vector<bool> expectedCaptureSeen;
  zc::Vector<bool> expectedCaptureBound;
  zc::Vector<size_t> expectedCaptureTarget;
  zc::Vector<uint16_t> expectedCaptureDiagnostic;
  zc::Vector<bool> expectedThisSeen;
  expectedCaptureSeen.resize(tree.nodeCount() + 1);
  expectedCaptureBound.resize(tree.nodeCount() + 1);
  expectedCaptureTarget.resize(tree.nodeCount() + 1);
  expectedCaptureDiagnostic.resize(tree.nodeCount() + 1);
  expectedThisSeen.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < expectedCaptureSeen.size(); ++index) {
    expectedCaptureSeen[index] = false;
    expectedCaptureBound[index] = false;
    expectedCaptureTarget[index] = kMissing;
    expectedCaptureDiagnostic[index] = 0;
    expectedThisSeen[index] = false;
  }

  const auto owningCallableScope = [&](size_t targetIndex) -> zc::Maybe<uint32_t> {
    if (targetIndex >= owningCallableScopeIndices.size() ||
        owningCallableScopeIndices[targetIndex] == UINT32_MAX) {
      return zc::none;
    }
    return owningCallableScopeIndices[targetIndex];
  };

  enum class ExplicitOracleCaptureAccess : uint8_t { Allowed, Denied, Malformed };
  const auto captureAccess = [&](uint32_t referenceScope,
                                 size_t targetIndex) -> ExplicitOracleCaptureAccess {
    if (targetIndex >= inventory.size() || !capturable(inventory[targetIndex].kind)) {
      return ExplicitOracleCaptureAccess::Malformed;
    }
    auto targetScope = owningCallableScope(targetIndex);
    if (targetScope == zc::none) { return ExplicitOracleCaptureAccess::Denied; }
    const uint32_t targetScopeIndex = ZC_ASSERT_NONNULL(targetScope);
    if (targetScopeIndex >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetScopeIndex] == kMissing) {
      return ExplicitOracleCaptureAccess::Malformed;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetScopeIndex];
    uint32_t scopeIndex = referenceScope;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return ExplicitOracleCaptureAccess::Malformed; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableInventoryByScope.size() ||
            callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitOracleCaptureAccess::Malformed;
        }
        const size_t callableIndex = callableInventoryByScope[scopeIndex];
        if (callableIndex == targetCallableIndex) { return ExplicitOracleCaptureAccess::Allowed; }
        if (scope.kind == ScopeKind::Function) { return ExplicitOracleCaptureAccess::Denied; }
        if (callableIndex >= closureSyntaxDomains.size() ||
            inventory[callableIndex].kind != identity::DefinitionKind::Closure ||
            closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::NotClosure) {
          return ExplicitOracleCaptureAccess::Malformed;
        }
        if (closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::Explicit) {
          if (!explicitClosureProcessed[callableIndex]) {
            return ExplicitOracleCaptureAccess::Malformed;
          }
          if (expectedTargetsByClosure[callableIndex].find(targetIndex) == zc::none) {
            return ExplicitOracleCaptureAccess::Denied;
          }
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        return ExplicitOracleCaptureAccess::Denied;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return ExplicitOracleCaptureAccess::Malformed;
  };

  ExplicitCaptureOracleResult oracleResult = ExplicitCaptureOracleResult::Valid;
  const auto visit = [&](auto& self, ast::NodeId node) -> void {
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    const uint32_t scopeIndex = scopeByNode[node.value];
    if (scopeIndex >= arena.scopes.size()) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& syntax = tree.node(node);

    if (syntax.kind == ast::SyntaxKind::ThisExpr) {
      if (expectedThisSeen[node.value]) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      expectedThisSeen[node.value] = true;
      auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::ThisKeyword);
      if (source == zc::none) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      auto expectedReceiver = activeReceiver(scopeByNode[node.value]);
      bool expectedSuccess = false;
      ZC_IF_SOME(receiverIndex, expectedReceiver) {
        const auto access = captureAccess(scopeByNode[node.value], receiverIndex);
        if (access == ExplicitOracleCaptureAccess::Malformed) {
          oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
          return;
        }
        expectedSuccess = access == ExplicitOracleCaptureAccess::Allowed;
      }
      if (expectedSuccess) {
        if (thisBindingByNode[node.value] == kMissing || resolutionByNode[node.value] != kMissing) {
          oracleResult = ExplicitCaptureOracleResult::MissingRequiredResolution;
          return;
        }
        const auto& binding = candidate.thisBindings[thisBindingByNode[node.value]];
        auto targetIndex =
            inventoryIndex(BindingTarget::callableParameter(binding.binding.receiverParameter));
        if (targetIndex == zc::none || expectedReceiver == zc::none ||
            ZC_ASSERT_NONNULL(expectedReceiver) != ZC_ASSERT_NONNULL(targetIndex) ||
            !receiverDefinitions[ZC_ASSERT_NONNULL(targetIndex)] ||
            !sameSpan(binding.source, ZC_ASSERT_NONNULL(source))) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        return;
      }
      if (thisBindingByNode[node.value] != kMissing || resolutionByNode[node.value] == kMissing) {
        oracleResult = ExplicitCaptureOracleResult::MissingRequiredResolution;
        return;
      }
      const auto& resolution = candidate.nodeBindings[resolutionByNode[node.value]];
      if (!resolution.value.is<FailedBindingResolution>()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
      if (failureIndex >= candidate.sourceFailures.size()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
      const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
      if (failureFact.diagnostic != BinderDiagnosticCode::UndefinedIdentifier ||
          site != static_cast<uint8_t>(BinderEmitterSite::BodyBinding) ||
          ordinal != schemaOrdinals[node.value] ||
          static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, ZC_ASSERT_NONNULL(source)) || !failureFact.notes.empty()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::LetStmt) {
      const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
      if (!tree.contains(declarations) ||
          tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& list = tree.node(declarations);
      const ast::NodeList declarators{
          list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
          list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
      if (!tree.contains(declarators)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto declarator : tree.list(declarators)) {
        if (!tree.contains(declarator) ||
            tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        const auto& declaration = tree.node(declarator);
        const ast::NodeId type(declaration.payload.words[ast::kVariableDeclaratorTyWord]);
        const ast::NodeId initializer(declaration.payload.words[ast::kVariableDeclaratorInitWord]);
        const ast::NodeId pattern(declaration.payload.words[ast::kVariableDeclaratorPatternWord]);
        if (tree.contains(type)) { self(self, type); }
        if (tree.contains(initializer)) { self(self, initializer); }
        if (tree.contains(pattern)) { self(self, pattern); }
        if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
        if (!activateIntroducer(declarator, DefinitionActivation::AfterInitializer)) {
          oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
          return;
        }
      }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::ForInStatement) {
      const ast::NodeId expression(syntax.payload.words[ast::kForInStatementExpressionWord]);
      const ast::NodeId binding(syntax.payload.words[ast::kForInStatementBindingWord]);
      const ast::NodeId body(syntax.payload.words[ast::kForInStatementBodyWord]);
      if (tree.contains(expression)) { self(self, expression); }
      if (tree.contains(binding)) { self(self, binding); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(node, DefinitionActivation::LoopPattern)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      if (tree.contains(body)) { self(self, body); }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::MatchArmStmt) {
      const ast::NodeId pattern(syntax.payload.words[ast::kMatchArmStmtPatternWord]);
      const ast::NodeId guard(syntax.payload.words[ast::kMatchArmStmtGuardWord]);
      const ast::NodeId body(syntax.payload.words[ast::kMatchArmStmtBodyWord]);
      if (tree.contains(pattern)) { self(self, pattern); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(node, DefinitionActivation::MatchPattern)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      if (tree.contains(guard)) { self(self, guard); }
      if (tree.contains(body)) { self(self, body); }
      return;
    }

    const bool callable = syntax.kind == ast::SyntaxKind::FunctionDecl ||
                          syntax.kind == ast::SyntaxKind::MethodDecl ||
                          syntax.kind == ast::SyntaxKind::ConstructorDecl ||
                          syntax.kind == ast::SyntaxKind::DestructorDecl ||
                          syntax.kind == ast::SyntaxKind::ExternDecl ||
                          syntax.kind == ast::SyntaxKind::FunctionExpression ||
                          syntax.kind == ast::SyntaxKind::LambdaExpression;
    if (!callable) {
      if (syntax.kind == ast::SyntaxKind::FunctionParameterDecl ||
          syntax.kind == ast::SyntaxKind::CaptureList ||
          syntax.kind == ast::SyntaxKind::CaptureItem) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) { self(self, child); });
      return;
    }

    ast::NodeId parameterList;
    ast::NodeId genericParameters;
    ast::NodeId captures;
    ast::NodeId returnType;
    ast::NodeId raisesType;
    ast::NodeId body;
    ast::NodeId expressionBody;
    bool isExtern = false;
    bool isClosure = false;
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]);
        genericParameters = ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kFunctionDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kFunctionDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kFunctionDeclBodyWord]);
        break;
      case ast::SyntaxKind::MethodDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]);
        genericParameters = ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kMethodDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kMethodDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kMethodDeclBodyWord]);
        break;
      case ast::SyntaxKind::ConstructorDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kConstructorDeclParamsIdWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kConstructorDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kConstructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::DestructorDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kDestructorDeclParamsIdWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kDestructorDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kDestructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::ExternDecl:
        isExtern = true;
        returnType = ast::NodeId(syntax.payload.words[ast::kExternDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kExternDeclRaisesTyWord]);
        break;
      case ast::SyntaxKind::FunctionExpression:
        isClosure = true;
        parameterList = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionParamsIdWord]);
        genericParameters =
            ast::NodeId(syntax.payload.words[ast::kFunctionExpressionTypeParamsIdWord]);
        captures = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionBodyWord]);
        break;
      case ast::SyntaxKind::LambdaExpression:
        isClosure = true;
        parameterList = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionBodyWord]);
        expressionBody = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionExprBodyWord]);
        break;
      default:
        ZC_UNREACHABLE;
    }

    if (isClosure && !activateIntroducer(node, DefinitionActivation::ExpressionIntroduction)) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    if (tree.contains(genericParameters)) { self(self, genericParameters); }
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }

    if (tree.contains(captures)) {
      if (syntax.kind != ast::SyntaxKind::FunctionExpression ||
          tree.node(captures).kind != ast::SyntaxKind::CaptureList || scopeIndex == 0 ||
          arena.scopes[scopeIndex].kind != ScopeKind::Closure ||
          arena.scopes[scopeIndex].parent == zc::none) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      if (scopeIndex >= callableInventoryByScope.size() ||
          callableInventoryByScope[scopeIndex] == kMissing) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      const size_t closureInventoryIndex = callableInventoryByScope[scopeIndex];
      if (inventory[closureInventoryIndex].node != node ||
          inventory[closureInventoryIndex].kind != identity::DefinitionKind::Closure) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& captureList = tree.node(captures);
      const ast::NodeList items{captureList.payload.words[ast::kCaptureListCapturesFirstWord],
                                captureList.payload.words[ast::kCaptureListCapturesSizeWord]};
      if (!tree.contains(items) ||
          captureList.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const uint32_t enclosingScope = ZC_ASSERT_NONNULL(arena.scopes[scopeIndex].parent).index();
      for (const auto item : tree.list(items)) {
        if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
            scopeByNode[item.value] != scopeIndex || expectedCaptureSeen[item.value]) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        expectedCaptureSeen[item.value] = true;
        const auto& captureSyntax = tree.node(item);
        const auto mode =
            static_cast<ast::CaptureMode>(captureSyntax.payload.words[ast::kCaptureItemModeWord]);
        size_t tokenOrdinal = 0;
        ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
        if (mode == ast::CaptureMode::ByRef) {
          tokenOrdinal = 1;
        } else if (mode == ast::CaptureMode::This) {
          tokenKind = ast::SyntaxKind::ThisKeyword;
        } else if (mode != ast::CaptureMode::ByValue) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        auto itemSource = input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
        if (itemSource == zc::none) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        const ast::IdentId identifier(captureSyntax.payload.words[ast::kCaptureItemNameWord]);
        zc::Maybe<size_t> target;
        BinderDiagnosticCode failureDiagnostic = BinderDiagnosticCode::UndefinedIdentifier;
        if (mode == ast::CaptureMode::This) {
          if (tree.ident(identifier) != "this"_zc) {
            oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
            return;
          }
          target = activeReceiver(enclosingScope);
        } else {
          auto semanticName = identity::SemanticIdentifier::fromSource(tree.ident(identifier));
          if (semanticName == zc::none) {
            oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
            return;
          }
          ZC_IF_SOME(name, semanticName) {
            target = activeDefinition(enclosingScope, Namespace::Value, name.text());
            if (target == zc::none &&
                activeDefinition(enclosingScope, Namespace::Type, name.text()) != zc::none) {
              failureDiagnostic = BinderDiagnosticCode::SymbolNamespaceMismatch;
            }
          }
        }
        bool accepted = false;
        ZC_IF_SOME(targetIndex, target) {
          if (capturable(inventory[targetIndex].kind)) {
            const auto access = captureAccess(enclosingScope, targetIndex);
            if (access == ExplicitOracleCaptureAccess::Malformed) {
              oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
              return;
            }
            if (access == ExplicitOracleCaptureAccess::Allowed) {
              expectedCaptureBound[item.value] = true;
              expectedCaptureTarget[item.value] = targetIndex;
              auto& expectedTargets = expectedTargetsByClosure[closureInventoryIndex];
              if (expectedTargets.find(targetIndex) == zc::none) {
                expectedTargets.insert(targetIndex, item.value);
              }
              accepted = true;
            }
          }
        }
        if (!accepted) {
          expectedCaptureDiagnostic[item.value] = static_cast<uint16_t>(failureDiagnostic);
        }
      }
      explicitClosureProcessed[closureInventoryIndex] = true;
    }

    zc::Vector<ast::NodeId> parameters;
    if (isExtern) {
      const ast::NodeList values{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                                 syntax.payload.words[ast::kExternDeclParamsSizeWord]};
      if (!tree.contains(values)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto parameter : tree.list(values)) { parameters.add(parameter); }
    } else {
      if (!tree.contains(parameterList) ||
          tree.node(parameterList).kind != ast::SyntaxKind::FunctionParameterList) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& list = tree.node(parameterList);
      const ast::NodeList values{list.payload.words[ast::kFunctionParameterListParamsFirstWord],
                                 list.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      if (!tree.contains(values)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto parameter : tree.list(values)) { parameters.add(parameter); }
    }
    for (const auto parameter : parameters) {
      if (!tree.contains(parameter) ||
          tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl ||
          scopeByNode[parameter.value] != scopeIndex) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      const auto& parameterSyntax = tree.node(parameter);
      const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
      const ast::NodeId attributes(
          parameterSyntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
      if (tree.contains(type) &&
          !input.parsedModule().functionParameterHasImplicitSelfType(parameter)) {
        self(self, type);
      }
      if (tree.contains(attributes)) { self(self, attributes); }
    }
    if (tree.contains(returnType)) { self(self, returnType); }
    if (tree.contains(raisesType)) { self(self, raisesType); }
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
    for (const auto parameter : parameters) {
      const auto& parameterSyntax = tree.node(parameter);
      const ast::NodeId defaultValue(
          parameterSyntax.payload.words[ast::kFunctionParameterDeclDefaultWord]);
      if (tree.contains(defaultValue)) { self(self, defaultValue); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(parameter, DefinitionActivation::ParameterList)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
    }
    if (tree.contains(body)) { self(self, body); }
    if (tree.contains(expressionBody)) { self(self, expressionBody); }
  };
  visit(visit, tree.root());
  if (oracleResult != ExplicitCaptureOracleResult::Valid) { return oracleResult; }
  bool completeThisCensus = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::ThisExpr && !expectedThisSeen[node.value]) {
      completeThisCensus = false;
    }
  });
  if (!completeThisCensus) { return ExplicitCaptureOracleResult::MissingRequiredResolution; }
  for (const auto& wanted : receivers) {
    if (!receiverActivated[wanted.inventoryIndex]) {
      return ExplicitCaptureOracleResult::MissingRequiredResolution;
    }
  }
  for (size_t index = 0; index < receivers.size();) {
    const auto scopeIndex = receivers[index].scopeIndex;
    const auto expectedReceiver = receivers[index].inventoryIndex;
    if (activeScopes[scopeIndex].receiver != expectedReceiver) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    while (index < receivers.size() && receivers[index].scopeIndex == scopeIndex) { ++index; }
  }

  if (candidate.explicitClosureCaptures.size() < explicitOrder.size()) {
    return ExplicitCaptureOracleResult::MissingRequiredResolution;
  }
  if (candidate.explicitClosureCaptures.size() > explicitOrder.size()) {
    return ExplicitCaptureOracleResult::InvalidBindingFact;
  }

  zc::Vector<size_t> explicitCaptureRowByClosure;
  zc::Vector<size_t> explicitCaptureRowCounts;
  zc::Vector<size_t> inferredCaptureRowByClosure;
  zc::Vector<size_t> inferredCaptureRowCounts;
  zc::Vector<zc::TreeMap<size_t, size_t>> validatedCaptureTargetsByClosure;
  explicitCaptureRowByClosure.resize(inventory.size());
  explicitCaptureRowCounts.resize(inventory.size());
  inferredCaptureRowByClosure.resize(inventory.size());
  inferredCaptureRowCounts.resize(inventory.size());
  validatedCaptureTargetsByClosure.reserve(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    explicitCaptureRowByClosure[index] = kMissing;
    explicitCaptureRowCounts[index] = 0;
    inferredCaptureRowByClosure[index] = kMissing;
    inferredCaptureRowCounts[index] = 0;
    validatedCaptureTargetsByClosure.add();
  }
  for (size_t rowIndex = 0; rowIndex < candidate.explicitClosureCaptures.size(); ++rowIndex) {
    auto closureIndex = anonymousIndex(candidate.explicitClosureCaptures[rowIndex].closure);
    if (closureIndex == zc::none ||
        inventory[ZC_ASSERT_NONNULL(closureIndex)].kind != identity::DefinitionKind::Closure) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const size_t index = ZC_ASSERT_NONNULL(closureIndex);
    if (explicitCaptureRowCounts[index] == 0) { explicitCaptureRowByClosure[index] = rowIndex; }
    ++explicitCaptureRowCounts[index];
  }
  for (size_t rowIndex = 0; rowIndex < candidate.closureFreeVariables.size(); ++rowIndex) {
    auto closureIndex = anonymousIndex(candidate.closureFreeVariables[rowIndex].closure);
    if (closureIndex == zc::none ||
        inventory[ZC_ASSERT_NONNULL(closureIndex)].kind != identity::DefinitionKind::Closure) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const size_t index = ZC_ASSERT_NONNULL(closureIndex);
    if (inferredCaptureRowCounts[index] == 0) { inferredCaptureRowByClosure[index] = rowIndex; }
    ++inferredCaptureRowCounts[index];
  }

  const auto duplicateFailure = [&](ast::NodeId item, const identity::SourceSpan& primary,
                                    zc::Maybe<const identity::SourceSpan&> previous) {
    if (item.value >= schemaOrdinals.size() || schemaOrdinals[item.value] == UINT32_MAX ||
        schemaOrdinals[item.value] >= duplicateFailuresBySchema.size()) {
      return false;
    }
    const auto& matchingFailures = duplicateFailuresBySchema[schemaOrdinals[item.value]];
    const size_t matches = matchingFailures.size();
    bool valid = true;
    for (const auto failureIndex : matchingFailures) {
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      if (previous == zc::none || static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, primary) || failureFact.notes.size() != 1 ||
          failureFact.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere ||
          !sameSpan(failureFact.notes[0].source, ZC_ASSERT_NONNULL(previous))) {
        valid = false;
      }
    }
    return valid && matches == (previous == zc::none ? 0 : 1);
  };

  for (size_t rowIndex = 0; rowIndex < explicitOrder.size(); ++rowIndex) {
    const size_t closureInventoryIndex = explicitOrder[rowIndex];
    const auto& entry = inventory[closureInventoryIndex];
    const auto& syntax = tree.node(entry.node);
    const ast::NodeId listNode(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
    const auto& list = tree.node(listNode);
    auto listSource = input.parsedModule().spanFor(list.range);
    const auto& actual = candidate.explicitClosureCaptures[rowIndex];
    if (listSource == zc::none || entry.anonymous == zc::none ||
        actual.closure != ZC_ASSERT_NONNULL(entry.anonymous) || actual.captureList != listNode ||
        !sameSpan(actual.source, ZC_ASSERT_NONNULL(listSource)) ||
        explicitCaptureRowCounts[closureInventoryIndex] != 1 ||
        explicitCaptureRowByClosure[closureInventoryIndex] != rowIndex ||
        inferredCaptureRowCounts[closureInventoryIndex] != 0) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const ast::NodeList items{list.payload.words[ast::kCaptureListCapturesFirstWord],
                              list.payload.words[ast::kCaptureListCapturesSizeWord]};
    if (!tree.contains(items) || list.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    size_t captureIndex = 0;
    for (const auto item : tree.list(items)) {
      if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
          item.value >= resolutionByNode.size() || resolutionByNode[item.value] == kMissing ||
          !expectedCaptureSeen[item.value]) {
        return ExplicitCaptureOracleResult::MissingRequiredResolution;
      }
      const auto& captureSyntax = tree.node(item);
      const auto mode =
          static_cast<ast::CaptureMode>(captureSyntax.payload.words[ast::kCaptureItemModeWord]);
      size_t tokenOrdinal = 0;
      ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
      if (mode == ast::CaptureMode::ByRef) {
        tokenOrdinal = 1;
      } else if (mode == ast::CaptureMode::This) {
        tokenKind = ast::SyntaxKind::ThisKeyword;
      } else if (mode != ast::CaptureMode::ByValue) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      auto itemSource = input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
      if (itemSource == zc::none) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
      const auto& resolution = candidate.nodeBindings[resolutionByNode[item.value]];
      if (!expectedCaptureBound[item.value]) {
        if (!resolution.value.is<FailedBindingResolution>()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
        if (failureIndex >= candidate.sourceFailures.size()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        const auto& failureFact = candidate.sourceFailures[failureIndex];
        const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
        const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
        if (failureFact.diagnostic !=
                static_cast<BinderDiagnosticCode>(expectedCaptureDiagnostic[item.value]) ||
            site != static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) ||
            ordinal != schemaOrdinals[item.value] ||
            static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
            !sameSpan(failureFact.primary, ZC_ASSERT_NONNULL(itemSource)) ||
            !failureFact.notes.empty()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        continue;
      }
      if (!resolution.value.is<BoundNameResolution>()) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      if (expectedCaptureTarget[item.value] == kMissing ||
          expectedCaptureTarget[item.value] >= inventory.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      if (captureIndex >= actual.captures.size()) {
        return ExplicitCaptureOracleResult::MissingRequiredResolution;
      }
      const auto& bound = resolution.value.get<BoundNameResolution>();
      const size_t targetIndex = expectedCaptureTarget[item.value];
      if (inventory[targetIndex].target == zc::none) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const auto& target = ZC_ASSERT_NONNULL(inventory[targetIndex].target);
      if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration ||
          !sameTarget(bound.bindingIdentity, target) ||
          !sameTarget(bound.canonicalTarget, target)) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      if (!capturable(inventory[targetIndex].kind)) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      const auto& targetEntry = inventory[targetIndex];
      const ast::IdentId itemName(captureSyntax.payload.words[ast::kCaptureItemNameWord]);
      if (mode == ast::CaptureMode::This) {
        if (!receiverDefinitions[targetIndex] || tree.ident(itemName) != "this"_zc) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      } else {
        auto name = identity::SemanticIdentifier::fromSource(tree.ident(itemName));
        if (name == zc::none || targetEntry.bindingName == zc::none ||
            ZC_ASSERT_NONNULL(name).text() != ZC_ASSERT_NONNULL(targetEntry.bindingName).text()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      }
      const auto& capture = actual.captures[captureIndex];
      if (capture.item != item || !sameTarget(capture.target, target) ||
          !sameSpan(capture.source, ZC_ASSERT_NONNULL(itemSource))) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      zc::Maybe<const identity::SourceSpan&> previous;
      auto& validatedTargets = validatedCaptureTargetsByClosure[closureInventoryIndex];
      auto firstCaptureIndex = validatedTargets.find(targetIndex);
      ZC_IF_SOME(firstIndex, firstCaptureIndex) {
        if (firstIndex >= captureIndex) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
        previous = actual.captures[firstIndex].source;
      }
      if (!duplicateFailure(item, capture.source, previous)) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      if (firstCaptureIndex == zc::none) { validatedTargets.insert(targetIndex, captureIndex); }
      ++captureIndex;
    }
    if (captureIndex != actual.captures.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
  }

  size_t partitionCount = 0;
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    if (entry.kind != identity::DefinitionKind::Closure) { continue; }
    if (explicitCaptureRowCounts[index] > 1 || inferredCaptureRowCounts[index] > 1) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const bool explicitFact = explicitCaptureRowCounts[index] == 1;
    const bool inferredFact = inferredCaptureRowCounts[index] == 1;
    if (explicitFact && inferredFact) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
  }
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    if (entry.kind != identity::DefinitionKind::Closure) { continue; }
    const bool explicitFact = explicitCaptureRowCounts[index] == 1;
    const bool inferredFact = inferredCaptureRowCounts[index] == 1;
    if (!explicitFact && !inferredFact) {
      return ExplicitCaptureOracleResult::MissingRequiredResolution;
    }
    if ((explicitFact && closureSyntaxDomains[index] != ExplicitOracleClosureSyntax::Explicit) ||
        (inferredFact && closureSyntaxDomains[index] != ExplicitOracleClosureSyntax::Inferred) ||
        (explicitFact &&
         explicitCaptureRowByClosure[index] >= candidate.explicitClosureCaptures.size()) ||
        (inferredFact &&
         inferredCaptureRowByClosure[index] >= candidate.closureFreeVariables.size())) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    ++partitionCount;
  }
  if (partitionCount != closureCount) { return ExplicitCaptureOracleResult::InvalidBindingFact; }

  for (const auto& resolution : candidate.nodeBindings) {
    if (!resolution.value.is<BoundNameResolution>() || !tree.contains(resolution.node) ||
        resolution.node.value >= scopeByNode.size() ||
        scopeByNode[resolution.node.value] == UINT32_MAX) {
      continue;
    }
    if (tree.node(resolution.node).kind == ast::SyntaxKind::CaptureItem) { continue; }
    if (tree.node(resolution.node).kind == ast::SyntaxKind::ThisExpr) { continue; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    if (!sameTarget(bound.bindingIdentity, bound.canonicalTarget)) { continue; }
    auto targetIndex = inventoryIndex(bound.bindingIdentity);
    if (targetIndex == zc::none || !capturable(inventory[ZC_ASSERT_NONNULL(targetIndex)].kind)) {
      continue;
    }
    const size_t targetInventoryIndex = ZC_ASSERT_NONNULL(targetIndex);
    auto targetScope = owningCallableScope(targetInventoryIndex);
    if (targetScope == zc::none) {
      if (targetInventoryIndex >= definitionScopeIndices.size() ||
          definitionScopeIndices[targetInventoryIndex] == UINT32_MAX) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const uint32_t targetDefinitionScope = definitionScopeIndices[targetInventoryIndex];
      uint32_t directScopeIndex = scopeByNode[resolution.node.value];
      bool reachedDirectTarget = false;
      for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
        if (directScopeIndex >= arena.scopes.size()) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        if (directScopeIndex == targetDefinitionScope) {
          reachedDirectTarget = true;
          break;
        }
        const auto& directScope = arena.scopes[directScopeIndex];
        if (directScope.kind == ScopeKind::Function || directScope.kind == ScopeKind::Closure) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        if (directScope.parent == zc::none) { break; }
        ZC_IF_SOME(parent, directScope.parent) { directScopeIndex = parent.index(); }
      }
      if (!reachedDirectTarget) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
      continue;
    }
    const uint32_t targetCallableScope = ZC_ASSERT_NONNULL(targetScope);
    if (targetCallableScope >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetCallableScope] == kMissing) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetCallableScope];
    uint32_t scopeIndex = scopeByNode[resolution.node.value];
    bool reachedTarget = false;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableInventoryByScope.size() ||
            callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        const size_t callableIndex = callableInventoryByScope[scopeIndex];
        if (callableIndex == targetCallableIndex) {
          reachedTarget = true;
          break;
        }
        if (scope.kind == ScopeKind::Function) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        if (callableIndex >= closureSyntaxDomains.size() ||
            closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::NotClosure) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        if (closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::Explicit &&
            validatedCaptureTargetsByClosure[callableIndex].find(targetInventoryIndex) ==
                zc::none) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    if (!reachedTarget) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
  }
  return ExplicitCaptureOracleResult::Valid;
}

BinderInvariantKind explicitCaptureOracleInvariant(ExplicitCaptureOracleResult result) {
  switch (result) {
    case ExplicitCaptureOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ExplicitCaptureOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ExplicitCaptureOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ExplicitCaptureOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

}  // namespace

zc::Maybe<BinderInvariantKind> verifyExplicitCaptureOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyExplicitCaptureFacts(input, candidate);
  if (result == ExplicitCaptureOracleResult::Valid) { return zc::none; }
  return explicitCaptureOracleInvariant(result);
}

}  // namespace zomlang::compiler::binder
