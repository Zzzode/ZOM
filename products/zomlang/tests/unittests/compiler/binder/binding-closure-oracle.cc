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
enum class ClosureFreeVariableOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

struct OracleCaptureTriple final {
  size_t closureIndex;
  size_t targetIndex;
  ast::NodeId referenceSite;
};

struct ClosureOracleInventoryEntry final {
  ClosureOracleInventoryEntry(ast::NodeId node, identity::DefinitionKind kind,
                              zc::Maybe<BindingTarget>&& target,
                              zc::Maybe<AnonymousOwnerLocalKey>&& anonymous,
                              zc::Array<uint8_t>&& canonicalKey) noexcept
      : node(node),
        kind(kind),
        target(zc::mv(target)),
        anonymous(zc::mv(anonymous)),
        canonicalKey(zc::mv(canonicalKey)) {}
  ClosureOracleInventoryEntry(ClosureOracleInventoryEntry&&) noexcept = default;
  ClosureOracleInventoryEntry& operator=(ClosureOracleInventoryEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ClosureOracleInventoryEntry);
  ast::NodeId node;
  identity::DefinitionKind kind;
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

struct ClosureFreeOracleTripleOrderKey final {
  size_t closureRank;
  size_t targetRank;
  uint64_t start;
  uint64_t end;
  uint32_t schemaPreorderOrdinal;

  bool operator==(const ClosureFreeOracleTripleOrderKey& other) const noexcept {
    return closureRank == other.closureRank && targetRank == other.targetRank &&
           start == other.start && end == other.end &&
           schemaPreorderOrdinal == other.schemaPreorderOrdinal;
  }
  bool operator<(const ClosureFreeOracleTripleOrderKey& other) const noexcept {
    if (closureRank != other.closureRank) { return closureRank < other.closureRank; }
    if (targetRank != other.targetRank) { return targetRank < other.targetRank; }
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    if (schemaPreorderOrdinal != other.schemaPreorderOrdinal) {
      return schemaPreorderOrdinal < other.schemaPreorderOrdinal;
    }
    return false;
  }
};

ClosureFreeVariableOracleResult verifyClosureFreeVariableFacts(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  zc::Vector<ClosureOracleInventoryEntry> inventory;
  for (const auto& entry : input.definitions().definitions()) {
    zc::Maybe<BindingTarget> target = BindingTarget::definition(entry.definition);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ClosureOracleInventoryEntry(entry.node, entry.record.kind(), zc::mv(target),
                                              zc::mv(noAnonymous),
                                              taggedCanonicalKey(0x01, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().callableParameters()) {
    zc::Maybe<BindingTarget> target = BindingTarget::callableParameter(entry.parameter);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ClosureOracleInventoryEntry(entry.node, identity::DefinitionKind::Parameter,
                                              zc::mv(target), zc::mv(noAnonymous),
                                              taggedCanonicalKey(0x03, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().ownerLocalBindings()) {
    zc::Maybe<BindingTarget> target = BindingTarget::ownerLocal(entry.binding);
    zc::Maybe<AnonymousOwnerLocalKey> noAnonymous;
    const auto encoded = entry.key.encode();
    inventory.add(ClosureOracleInventoryEntry(
        entry.node,
        entry.key.kind() == OwnerLocalBindingKind::Local ? identity::DefinitionKind::Local
                                                         : identity::DefinitionKind::PatternBinding,
        zc::mv(target), zc::mv(noAnonymous), taggedCanonicalKey(0x04, encoded.asPtr())));
  }
  for (const auto& entry : input.definitions().anonymousEntities()) {
    zc::Maybe<BindingTarget> noTarget;
    zc::Maybe<AnonymousOwnerLocalKey> anonymous = entry.key.clone();
    const auto encoded = entry.key.encode();
    inventory.add(ClosureOracleInventoryEntry(entry.node, identity::DefinitionKind::Closure,
                                              zc::mv(noTarget), zc::mv(anonymous),
                                              taggedCanonicalKey(0x06, encoded.asPtr())));
  }
  const auto& arena = candidate;
  if (arena.scopes.empty() || arena.nodeScopes.size() != tree.nodeCount()) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }

  zc::Vector<uint32_t> scopeByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  for (auto& scope : scopeByNode) { scope = UINT32_MAX; }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext()) || (index == 0 && scope.parent != zc::none) ||
        (index != 0 && scope.parent == zc::none)) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
    }
    const auto& owner = scope.owner.value();
    if (scope.kind == ScopeKind::Function &&
        (!owner.is<DefinitionScopeOwner>() ||
         input.definitions().definitionKey(owner.get<DefinitionScopeOwner>().definition) ==
             zc::none)) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    if (scope.kind == ScopeKind::Closure && !owner.is<AnonymousScopeOwner>()) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }
  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));

  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::TreeMap<zc::String, size_t> inventoryByCanonicalKey;
  zc::Vector<size_t> canonicalRankByInventory;
  canonicalRankByInventory.resize(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    canonicalRankByInventory[index] = kMissing;
    const auto& entry = inventory[index];
    auto canonicalKey = zc::str(entry.canonicalKey.asChars());
    if (inventoryByCanonicalKey.find(canonicalKey) != zc::none) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    inventoryByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }
  size_t canonicalRank = 0;
  for (const auto& ordered : inventoryByCanonicalKey) {
    canonicalRankByInventory[ordered.value] = canonicalRank++;
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

  enum class ClosureFreeOracleClosureSyntax : uint8_t { NotClosure, Inferred, Explicit };
  zc::Vector<ClosureFreeOracleClosureSyntax> closureSyntaxDomains;
  closureSyntaxDomains.resize(inventory.size());
  for (auto& domain : closureSyntaxDomains) { domain = ClosureFreeOracleClosureSyntax::NotClosure; }
  zc::Vector<size_t> closureOrder;
  for (const auto& ordered : inventoryByCanonicalKey) {
    const size_t entryIndex = ordered.value;
    const auto& entry = inventory[entryIndex];
    if (entry.kind != identity::DefinitionKind::Closure) { continue; }
    if (!tree.contains(entry.node)) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    const auto& syntax = tree.node(entry.node);
    if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
      const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
      if (captures) {
        if (!tree.contains(captures) || tree.node(captures).kind != ast::SyntaxKind::CaptureList) {
          return ClosureFreeVariableOracleResult::InvalidBindingFact;
        }
        closureSyntaxDomains[entryIndex] = ClosureFreeOracleClosureSyntax::Explicit;
        continue;
      }
    } else if (syntax.kind != ast::SyntaxKind::LambdaExpression) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    closureSyntaxDomains[entryIndex] = ClosureFreeOracleClosureSyntax::Inferred;
    closureOrder.add(entryIndex);
  }
  if (candidate.closureFreeVariables.size() < closureOrder.size()) {
    return ClosureFreeVariableOracleResult::MissingRequiredResolution;
  }
  if (candidate.closureFreeVariables.size() > closureOrder.size()) {
    return ClosureFreeVariableOracleResult::InvalidBindingFact;
  }
  for (size_t index = 0; index < closureOrder.size(); ++index) {
    if (inventory[closureOrder[index]].anonymous == zc::none ||
        candidate.closureFreeVariables[index].closure !=
            ZC_ASSERT_NONNULL(inventory[closureOrder[index]].anonymous)) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
  }

  const auto capturable = [](identity::DefinitionKind kind) {
    return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
           kind == identity::DefinitionKind::PatternBinding;
  };

  zc::Vector<uint32_t> definitionScopeIndices;
  zc::Vector<uint32_t> owningCallableScopeIndices;
  zc::Vector<size_t> callableInventoryByScope;
  zc::Vector<uint32_t> ownedCallableScopeByInventory;
  zc::Vector<uint32_t> nearestCallableScopeByScope;
  zc::Vector<uint32_t> parentCallableScopeByScope;
  zc::Vector<uint32_t> scopeEnter;
  zc::Vector<uint32_t> scopeExit;
  definitionScopeIndices.resize(inventory.size());
  owningCallableScopeIndices.resize(inventory.size());
  callableInventoryByScope.resize(arena.scopes.size());
  ownedCallableScopeByInventory.resize(inventory.size());
  nearestCallableScopeByScope.resize(arena.scopes.size());
  parentCallableScopeByScope.resize(arena.scopes.size());
  scopeEnter.resize(arena.scopes.size());
  scopeExit.resize(arena.scopes.size());
  for (auto& index : callableInventoryByScope) { index = kMissing; }
  for (auto& index : ownedCallableScopeByInventory) { index = UINT32_MAX; }
  for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    uint32_t parentCallableScope = UINT32_MAX;
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.index() >= scopeIndex) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      parentCallableScope = nearestCallableScopeByScope[parent.index()];
    }
    parentCallableScopeByScope[scopeIndex] = parentCallableScope;
    nearestCallableScopeByScope[scopeIndex] =
        scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure
            ? static_cast<uint32_t>(scopeIndex)
            : parentCallableScope;
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
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    callableInventoryByScope[scopeIndex] = ZC_ASSERT_NONNULL(callableIndex);
    ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] =
        static_cast<uint32_t>(scopeIndex);
  }

  zc::Vector<uint32_t> lastChild;
  zc::Vector<uint32_t> previousSibling;
  lastChild.resize(arena.scopes.size());
  previousSibling.resize(arena.scopes.size());
  for (auto& index : lastChild) { index = UINT32_MAX; }
  for (auto& index : previousSibling) { index = UINT32_MAX; }
  for (size_t scopeIndex = 1; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    if (scope.parent == zc::none) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    ZC_IF_SOME(parent, scope.parent) {
      previousSibling[scopeIndex] = lastChild[parent.index()];
      lastChild[parent.index()] = static_cast<uint32_t>(scopeIndex);
    }
  }
  struct ScopeTraversalEvent final {
    uint32_t scope;
    bool exiting;
  };
  zc::Vector<ScopeTraversalEvent> traversal;
  traversal.add(ScopeTraversalEvent{0, false});
  uint32_t nextScopeEntry = 0;
  while (!traversal.empty()) {
    const auto event = traversal.back();
    traversal.removeLast();
    if (event.exiting) {
      scopeExit[event.scope] = nextScopeEntry;
      continue;
    }
    scopeEnter[event.scope] = nextScopeEntry++;
    traversal.add(ScopeTraversalEvent{event.scope, true});
    uint32_t child = lastChild[event.scope];
    while (child != UINT32_MAX) {
      traversal.add(ScopeTraversalEvent{child, false});
      child = previousSibling[child];
    }
  }
  if (nextScopeEntry != arena.scopes.size()) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }

  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto node = inventory[index].node;
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    definitionScopeIndices[index] = scopeByNode[node.value];
    owningCallableScopeIndices[index] = nearestCallableScopeByScope[definitionScopeIndices[index]];
    if (owningCallableScopeIndices[index] != UINT32_MAX &&
        callableInventoryByScope[owningCallableScopeIndices[index]] == kMissing) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
  }

  zc::TreeMap<ClosureFreeOracleTripleOrderKey, OracleCaptureTriple> triples;
  struct OracleReference final {
    OracleReference(ast::NodeId node, BindingTarget&& target) noexcept
        : node(node), target(zc::mv(target)) {}
    OracleReference(OracleReference&&) noexcept = default;
    OracleReference& operator=(OracleReference&&) noexcept = default;
    ZC_DISALLOW_COPY(OracleReference);
    ast::NodeId node;
    BindingTarget target;
  };
  zc::Vector<OracleReference> references;
  for (const auto& resolution : candidate.nodeBindings) {
    if (!tree.contains(resolution.node) || resolution.node.value >= scopeByNode.size() ||
        scopeByNode[resolution.node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    if (!resolution.value.is<BoundNameResolution>()) { continue; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration) {
      continue;
    }
    if (!sameTarget(bound.bindingIdentity, bound.canonicalTarget)) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    references.add(OracleReference(resolution.node, bound.bindingIdentity.clone()));
  }
  for (const auto& binding : candidate.thisBindings) {
    if (!tree.contains(binding.expression) ||
        tree.node(binding.expression).kind != ast::SyntaxKind::ThisExpr) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    references.add(OracleReference(
        binding.expression, BindingTarget::callableParameter(binding.binding.receiverParameter)));
  }
  for (const auto& reference : references) {
    const auto& target = reference.target;
    auto targetInventoryIndex = inventoryIndex(target);
    if (targetInventoryIndex == zc::none) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    const size_t targetIndex = ZC_ASSERT_NONNULL(targetInventoryIndex);
    const auto& targetEntry = inventory[targetIndex];
    if (!capturable(targetEntry.kind)) { continue; }
    if (targetIndex >= owningCallableScopeIndices.size()) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    const uint32_t targetCallableScope = owningCallableScopeIndices[targetIndex];
    if (targetCallableScope == UINT32_MAX) {
      const uint32_t targetDefinitionScope = definitionScopeIndices[targetIndex];
      const uint32_t referenceScope = scopeByNode[reference.node.value];
      if (targetDefinitionScope >= scopeEnter.size() || referenceScope >= scopeEnter.size()) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (nearestCallableScopeByScope[referenceScope] != UINT32_MAX ||
          scopeEnter[referenceScope] < scopeEnter[targetDefinitionScope] ||
          scopeEnter[referenceScope] >= scopeExit[targetDefinitionScope]) {
        return ClosureFreeVariableOracleResult::InvalidBindingFact;
      }
      continue;
    }
    if (targetCallableScope >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetCallableScope] == kMissing) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetCallableScope];

    auto source = input.parsedModule().spanFor(tree.node(reference.node).range);
    if (source == zc::none || reference.node.value >= schemaOrdinals.size() ||
        schemaOrdinals[reference.node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    const auto start = ZC_ASSERT_NONNULL(source).byteStart();
    const auto end = ZC_ASSERT_NONNULL(source).byteEnd();

    zc::Vector<size_t> crossedClosures;
    uint32_t scopeIndex = nearestCallableScopeByScope[scopeByNode[reference.node.value]];
    bool reachedTarget = false;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex == UINT32_MAX) { break; }
      if (scopeIndex >= arena.scopes.size() ||
          (arena.scopes[scopeIndex].kind != ScopeKind::Function &&
           arena.scopes[scopeIndex].kind != ScopeKind::Closure)) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scopeIndex >= callableInventoryByScope.size() ||
          callableInventoryByScope[scopeIndex] == kMissing) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const size_t callableIndex = callableInventoryByScope[scopeIndex];
      if (callableIndex == targetCallableIndex) {
        reachedTarget = true;
        break;
      }
      if (scope.kind == ScopeKind::Function) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (callableIndex >= closureSyntaxDomains.size() ||
          inventory[callableIndex].kind != identity::DefinitionKind::Closure ||
          closureSyntaxDomains[callableIndex] == ClosureFreeOracleClosureSyntax::NotClosure) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (closureSyntaxDomains[callableIndex] == ClosureFreeOracleClosureSyntax::Inferred) {
        crossedClosures.add(callableIndex);
      }
      scopeIndex = parentCallableScopeByScope[scopeIndex];
    }
    if (!reachedTarget) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    for (const auto closureIndex : crossedClosures) {
      if (closureIndex >= canonicalRankByInventory.size() ||
          targetIndex >= canonicalRankByInventory.size() ||
          canonicalRankByInventory[closureIndex] == kMissing ||
          canonicalRankByInventory[targetIndex] == kMissing) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const ClosureFreeOracleTripleOrderKey key{canonicalRankByInventory[closureIndex],
                                                canonicalRankByInventory[targetIndex], start, end,
                                                schemaOrdinals[reference.node.value]};
      auto existing = triples.find(key);
      if (existing == zc::none) {
        triples.insert(key, OracleCaptureTriple{closureIndex, targetIndex, reference.node});
      } else {
        ZC_IF_SOME(triple, existing) {
          if (triple.closureIndex != closureIndex || triple.targetIndex != targetIndex ||
              triple.referenceSite != reference.node) {
            return ClosureFreeVariableOracleResult::InvalidBindingFact;
          }
        }
      }
    }
  }

  zc::Vector<OracleCaptureTriple> canonicalTriples;
  canonicalTriples.reserve(triples.size());
  for (const auto& ordered : triples) { canonicalTriples.add(ordered.value); }

  size_t tripleIndex = 0;
  for (const auto& closure : candidate.closureFreeVariables) {
    size_t variableIndex = 0;
    while (tripleIndex < canonicalTriples.size() &&
           inventory[canonicalTriples[tripleIndex].closureIndex].anonymous != zc::none &&
           ZC_ASSERT_NONNULL(inventory[canonicalTriples[tripleIndex].closureIndex].anonymous) ==
               closure.closure) {
      const auto targetIndex = canonicalTriples[tripleIndex].targetIndex;
      if (targetIndex >= inventory.size() || inventory[targetIndex].target == zc::none) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const auto& target = ZC_ASSERT_NONNULL(inventory[targetIndex].target);
      if (variableIndex >= closure.variables.size()) {
        return ClosureFreeVariableOracleResult::MissingRequiredResolution;
      }
      const auto& variable = closure.variables[variableIndex];
      if (!sameTarget(variable.target, target)) {
        return ClosureFreeVariableOracleResult::InvalidBindingFact;
      }
      size_t siteCount = 0;
      while (tripleIndex + siteCount < canonicalTriples.size() &&
             canonicalTriples[tripleIndex + siteCount].closureIndex ==
                 canonicalTriples[tripleIndex].closureIndex &&
             canonicalTriples[tripleIndex + siteCount].targetIndex == targetIndex) {
        ++siteCount;
      }
      if (variable.referenceSites.size() < siteCount) {
        return ClosureFreeVariableOracleResult::MissingRequiredResolution;
      }
      if (variable.referenceSites.size() > siteCount) {
        return ClosureFreeVariableOracleResult::InvalidBindingFact;
      }
      for (size_t siteIndex = 0; siteIndex < siteCount; ++siteIndex) {
        if (variable.referenceSites[siteIndex] !=
            canonicalTriples[tripleIndex + siteIndex].referenceSite) {
          return ClosureFreeVariableOracleResult::InvalidBindingFact;
        }
      }
      tripleIndex += siteCount;
      ++variableIndex;
    }
    if (variableIndex < closure.variables.size()) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
  }
  return tripleIndex == canonicalTriples.size()
             ? ClosureFreeVariableOracleResult::Valid
             : ClosureFreeVariableOracleResult::MissingRequiredResolution;
}

BinderInvariantKind closureFreeVariableOracleInvariant(ClosureFreeVariableOracleResult result) {
  switch (result) {
    case ClosureFreeVariableOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ClosureFreeVariableOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ClosureFreeVariableOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ClosureFreeVariableOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

}  // namespace

zc::Maybe<BinderInvariantKind> verifyClosureFreeVariableOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyClosureFreeVariableFacts(input, candidate);
  if (result == ClosureFreeVariableOracleResult::Valid) { return zc::none; }
  return closureFreeVariableOracleInvariant(result);
}

}  // namespace zomlang::compiler::binder
