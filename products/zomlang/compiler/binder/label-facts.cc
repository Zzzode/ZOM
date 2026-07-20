// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/label-facts.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;

struct OwnerCounter final {
  LabelOwner owner;
  uint64_t nextIndex;
};

struct LabelOrderEntry final {
  size_t factIndex;
  uint8_t ownerTag;
  zc::Array<uint8_t> ownerKey;
  uint32_t labelIndex;
};

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::LabelAndClosure,
                             ordinal};
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool isLoop(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::WhileStmt || kind == ast::SyntaxKind::ForStmt ||
         kind == ast::SyntaxKind::ForInStatement || kind == ast::SyntaxKind::DoWhileStatement;
}

bool orderLess(const LabelOrderEntry& left, const LabelOrderEntry& right) {
  if (left.ownerTag != right.ownerTag) { return left.ownerTag < right.ownerTag; }
  const int keyOrder = compareBytes(left.ownerKey.asPtr(), right.ownerKey.asPtr());
  if (keyOrder != 0) { return keyOrder < 0; }
  return left.labelIndex < right.labelIndex;
}

}  // namespace

zc::Maybe<uint32_t> checkedLabelIndex(uint64_t value) {
  if (value > static_cast<uint64_t>(UINT32_MAX)) { return zc::none; }
  return static_cast<uint32_t>(value);
}

LabelFactsBuildResult LabelBuilder::build(const VerifiedBindingInput& input,
                                          const ScopeArenaCandidate& arena) {
  const auto& tree = input.tree();
  if (!tree.contains(tree.root()) || arena.scopes.empty() ||
      arena.scopes[0].kind != ScopeKind::Module || arena.nodeScopes.size() != tree.nodeCount()) {
    return failure(input, BinderInvariantKind::MalformedScopeGraph, 0);
  }

  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<uint32_t> schemaOrdinals;
  scopeByNode.resize(tree.nodeCount() + 1);
  schemaOrdinals.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < scopeByNode.size(); ++index) {
    scopeByNode[index] = kMissingIndex;
    schemaOrdinals[index] = kMissingIndex;
  }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext()) || (index == 0 && scope.parent != zc::none) ||
        (index != 0 && scope.parent == zc::none)) {
      return failure(input, BinderInvariantKind::MalformedScopeGraph, 0);
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, 0);
      }
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != kMissingIndex || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return failure(input, BinderInvariantKind::MalformedScopeGraph, 0);
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }

  uint32_t ordinal = 0;
  bool preorderValid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (!tree.contains(node) || node.value >= schemaOrdinals.size() ||
        schemaOrdinals[node.value] != kMissingIndex || scopeByNode[node.value] == kMissingIndex) {
      preorderValid = false;
      return;
    }
    schemaOrdinals[node.value] = ordinal++;
  });
  if (!preorderValid || ordinal != tree.nodeCount()) {
    return failure(input, BinderInvariantKind::InvalidEmitterOrdinal, 0);
  }

  LabelFactsCandidate candidate;
  zc::Vector<OwnerCounter> counters;
  zc::Maybe<BinderInvariantFact> rejected;

  const auto reject = [&](BinderInvariantKind kind, ast::NodeId node) {
    if (rejected != zc::none) { return; }
    const uint32_t nodeOrdinal = tree.contains(node) && node.value < schemaOrdinals.size() &&
                                         schemaOrdinals[node.value] != kMissingIndex
                                     ? schemaOrdinals[node.value]
                                     : 0;
    rejected = failure(input, kind, nodeOrdinal);
  };

  const auto ownerFor = [&](ast::NodeId node) -> zc::Maybe<LabelOwner> {
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == kMissingIndex) {
      return zc::none;
    }
    uint32_t scopeIndex = scopeByNode[node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return zc::none; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function) {
        const auto& owner = scope.owner.value();
        if (!owner.is<DefinitionScopeOwner>()) { return zc::none; }
        return LabelOwner::callable(owner.get<DefinitionScopeOwner>().definition);
      }
      if (scope.kind == ScopeKind::Closure) {
        const auto& owner = scope.owner.value();
        if (!owner.is<AnonymousScopeOwner>()) { return zc::none; }
        return LabelOwner::anonymous(input.module(),
                                     owner.get<AnonymousScopeOwner>().anonymous.clone());
      }
      if (scope.kind == ScopeKind::Module) {
        const auto& owner = scope.owner.value();
        if (!owner.is<ModuleScopeOwner>() ||
            owner.get<ModuleScopeOwner>().module != input.module()) {
          return zc::none;
        }
        return LabelOwner::module(input.module());
      }
      if (scope.parent == zc::none) { return zc::none; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return zc::none;
  };

  const auto targetFor = [&](ast::NodeId statement) -> zc::Maybe<LabelTarget> {
    ast::NodeId target = statement;
    for (size_t traversed = 0; traversed <= tree.nodeCount(); ++traversed) {
      if (!tree.contains(target)) { return zc::none; }
      const auto& syntax = tree.node(target);
      if (syntax.kind != ast::SyntaxKind::LabeledStatement) { break; }
      target = ast::NodeId(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      if (traversed == tree.nodeCount()) { return zc::none; }
    }
    if (!tree.contains(target) || target.value >= scopeByNode.size() ||
        scopeByNode[target.value] == kMissingIndex) {
      return zc::none;
    }
    const auto scopeIndex = scopeByNode[target.value];
    if (scopeIndex >= arena.scopes.size()) { return zc::none; }
    const auto& scope = arena.scopes[scopeIndex];
    const auto kind = tree.node(target).kind;
    if (kind == ast::SyntaxKind::BlockStmt && scope.kind == ScopeKind::Block) {
      return LabelTarget::block(scope.id);
    }
    if (isLoop(kind) && scope.kind == ScopeKind::Loop) { return LabelTarget::loop(scope.id); }
    return zc::none;
  };

  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (rejected != zc::none || syntax.kind != ast::SyntaxKind::LabeledStatement) { return; }
    const ast::NodeId statement(syntax.payload.words[ast::kLabeledStatementStatementWord]);
    if (!tree.contains(statement)) {
      reject(BinderInvariantKind::MissingRequiredResolution, node);
      return;
    }
    auto owner = ownerFor(node);
    auto target = targetFor(statement);
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
    auto name = identity::SemanticIdentifier::fromSource(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kLabeledStatementLabelWord])));
    if (owner == zc::none || target == zc::none || source == zc::none || name == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    ZC_IF_SOME(ownerValue, owner) {
      size_t counterIndex = counters.size();
      for (size_t index = 0; index < counters.size(); ++index) {
        if (counters[index].owner == ownerValue) {
          counterIndex = index;
          break;
        }
      }
      if (counterIndex == counters.size()) { counters.add(OwnerCounter{ownerValue.clone(), 0}); }
      auto labelIndex = checkedLabelIndex(counters[counterIndex].nextIndex);
      if (labelIndex == zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return;
      }
      ZC_IF_SOME(nameValue, name) {
        ZC_IF_SOME(sourceValue, source) {
          for (const auto& prior : candidate.labels) {
            if (prior.owner == ownerValue && prior.name == nameValue) {
              candidate.duplicates.add(LabelDuplicateFact{nameValue.clone(), sourceValue.clone(),
                                                          prior.source.clone(),
                                                          schemaOrdinals[node.value]});
              break;
            }
          }
          ZC_IF_SOME(indexValue, labelIndex) {
            ZC_IF_SOME(targetValue, target) {
              LabelId identity(ownerValue.clone(), indexValue);
              candidate.labels.add(LabelFact{zc::mv(identity), zc::mv(nameValue),
                                             ownerValue.clone(), statement, zc::mv(targetValue),
                                             zc::mv(sourceValue)});
              ++counters[counterIndex].nextIndex;
            }
          }
        }
      }
    }
  });
  ZC_IF_SOME(fact, rejected) { return zc::mv(fact); }

  zc::Vector<LabelOrderEntry> order;
  for (size_t index = 0; index < candidate.labels.size(); ++index) {
    const auto& fact = candidate.labels[index];
    const auto& owner = fact.owner.value();
    if (owner.is<ModuleLabelOwner>()) {
      if (owner.get<ModuleLabelOwner>().module != input.module()) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, 0);
      }
      order.add(LabelOrderEntry{index, 0x01, input.moduleKey().encode(), fact.identity.index()});
      continue;
    }
    if (owner.is<CallableLabelOwner>()) {
      auto key = input.definitions().definitionKey(owner.get<CallableLabelOwner>().callable);
      if (key == zc::none) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, 0);
      }
      ZC_IF_SOME(value, key) {
        order.add(LabelOrderEntry{index, 0x02, value.encode(), fact.identity.index()});
      }
      continue;
    }
    const auto& anonymous = owner.get<AnonymousLabelOwner>();
    if (anonymous.module != input.module()) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, 0);
    }
    bool found = false;
    for (const auto& entry : input.definitions().anonymousEntities()) {
      if (entry.key == anonymous.anonymous) {
        found = true;
        break;
      }
    }
    if (!found) { return failure(input, BinderInvariantKind::MissingRequiredResolution, 0); }
    order.add(LabelOrderEntry{index, 0x03, anonymous.anonymous.encode(), fact.identity.index()});
  }
  for (size_t index = 1; index < order.size(); ++index) {
    auto current = zc::mv(order[index]);
    size_t insertion = index;
    while (insertion > 0 && orderLess(current, order[insertion - 1])) {
      order[insertion] = zc::mv(order[insertion - 1]);
      --insertion;
    }
    order[insertion] = zc::mv(current);
  }
  zc::Vector<LabelFact> sorted;
  for (const auto& entry : order) { sorted.add(zc::mv(candidate.labels[entry.factIndex])); }
  candidate.labels = zc::mv(sorted);
  return candidate;
}

}  // namespace zomlang::compiler::binder
