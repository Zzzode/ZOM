// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/closure-free-variables.h"

#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;

struct ReferenceSiteOrderKey final {
  uint64_t start;
  uint64_t end;
  uint32_t schemaPreorderOrdinal;

  bool operator==(const ReferenceSiteOrderKey& other) const noexcept {
    return start == other.start && end == other.end &&
           schemaPreorderOrdinal == other.schemaPreorderOrdinal;
  }
  bool operator<(const ReferenceSiteOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return schemaPreorderOrdinal < other.schemaPreorderOrdinal;
  }
};

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::LabelAndClosure,
                             ordinal};
}

bool isCapturable(identity::DefinitionKind kind) {
  return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
         kind == identity::DefinitionKind::PatternBinding;
}

}  // namespace

class ClosureFreeVariableCursor final {
public:
  ClosureFreeVariableCursor(const VerifiedBindingInput& input, const ScopeArenaCandidate& arena,
                            zc::ArrayPtr<const DefinitionFact> definitions,
                            zc::ArrayPtr<const BindingResolution> nodeBindings)
      : input(input),
        tree(input.tree()),
        arena(arena),
        definitions(definitions),
        nodeBindings(nodeBindings) {}

  ClosureFreeVariableBuildResult run() {
    if (!initialize()) { return takeRejection(); }
    if (!rejectExplicitCaptureClauses()) { return takeRejection(); }
    if (!initializeDenseRows()) { return takeRejection(); }
    for (const auto& resolution : nodeBindings) {
      collect(resolution);
      if (rejected != zc::none) { return takeRejection(); }
    }
    if (!canonicalize()) { return takeRejection(); }
    return zc::mv(facts);
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  const ScopeArenaCandidate& arena;
  zc::ArrayPtr<const DefinitionFact> definitions;
  zc::ArrayPtr<const BindingResolution> nodeBindings;
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<uint32_t> schemaOrdinals;
  zc::Vector<ClosureFreeVariableFact> facts;
  zc::Maybe<BinderInvariantFact> rejected;

  void reject(BinderInvariantKind kind, ast::NodeId node) {
    if (rejected != zc::none) { return; }
    uint32_t ordinal = 0;
    if (tree.contains(node) && node.value < schemaOrdinals.size() &&
        schemaOrdinals[node.value] != kMissingIndex) {
      ordinal = schemaOrdinals[node.value];
    }
    rejected = failure(input, kind, ordinal);
  }

  BinderInvariantFact takeRejection() {
    ZC_IF_SOME(value, rejected) { return zc::mv(value); }
    ZC_UNREACHABLE;
  }

  bool initialize() {
    if (!tree.contains(tree.root()) || arena.scopes.empty() ||
        arena.scopes[0].kind != ScopeKind::Module || arena.nodeScopes.size() != tree.nodeCount() ||
        definitions.size() != input.definitions().definitions().size()) {
      reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
      return false;
    }

    scopeByNode.resize(tree.nodeCount() + 1);
    schemaOrdinals.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < scopeByNode.size(); ++index) {
      scopeByNode[index] = kMissingIndex;
      schemaOrdinals[index] = kMissingIndex;
    }

    for (size_t index = 0; index < arena.scopes.size(); ++index) {
      const auto& scope = arena.scopes[index];
      if (scope.id.module() != input.module() || scope.id.index() != index ||
          !scope.id.belongsTo(input.semanticContext()) ||
          (index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
      ZC_IF_SOME(parent, scope.parent) {
        if (parent.module() != input.module() || parent.index() >= index ||
            !parent.belongsTo(input.semanticContext())) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
      }
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        const auto& owner = scope.owner.value();
        if (!owner.is<DefinitionScopeOwner>() ||
            input.definitions().definitionKey(owner.get<DefinitionScopeOwner>().definition) ==
                zc::none) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
      }
    }

    for (const auto& fact : arena.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
          scopeByNode[fact.node.value] != kMissingIndex || fact.scope.module() != input.module() ||
          fact.scope.index() >= arena.scopes.size() ||
          arena.scopes[fact.scope.index()].id != fact.scope) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
      scopeByNode[fact.node.value] = fact.scope.index();
    }

    uint32_t ordinal = 0;
    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
      if (!tree.contains(node) || node.value >= schemaOrdinals.size() ||
          schemaOrdinals[node.value] != kMissingIndex || scopeByNode[node.value] == kMissingIndex) {
        valid = false;
        return;
      }
      schemaOrdinals[node.value] = ordinal++;
    });
    if (!valid || ordinal != tree.nodeCount()) {
      reject(BinderInvariantKind::InvalidEmitterOrdinal, tree.root());
      return false;
    }

    for (size_t index = 0; index < definitions.size(); ++index) {
      const auto& fact = definitions[index];
      if (!fact.identity.belongsTo(input.semanticContext()) ||
          input.definitions().definitionKey(fact.identity) == zc::none ||
          fact.declaringScope.module() != input.module() ||
          fact.declaringScope.index() >= arena.scopes.size() ||
          arena.scopes[fact.declaringScope.index()].id != fact.declaringScope) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      for (size_t prior = 0; prior < index; ++prior) {
        if (definitions[prior].identity == fact.identity) {
          reject(BinderInvariantKind::InvalidBindingFact, tree.root());
          return false;
        }
      }
    }
    return true;
  }

  bool rejectExplicitCaptureClauses() {
    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid || syntax.kind != ast::SyntaxKind::FunctionExpression) { return; }
      const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
      if (!captures) { return; }
      if (!tree.contains(captures)) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
      } else {
        reject(BinderInvariantKind::MissingRequiredResolution, node);
      }
      valid = false;
    });
    return valid;
  }

  zc::Maybe<size_t> definitionIndex(identity::DefId definition) const {
    zc::Maybe<size_t> result;
    for (size_t index = 0; index < definitions.size(); ++index) {
      if (definitions[index].identity != definition) { continue; }
      if (result != zc::none) { return zc::none; }
      result = index;
    }
    return result;
  }

  bool initializeDenseRows() {
    zc::TreeMap<zc::String, identity::DefId> order;
    size_t closureCount = 0;
    for (const auto& entry : input.definitions().definitions()) {
      if (entry.kind != identity::DefinitionKind::Closure) { continue; }
      auto factIndex = definitionIndex(entry.definition);
      if (factIndex == zc::none) {
        reject(BinderInvariantKind::MissingRequiredResolution, entry.node);
        return false;
      }
      ZC_IF_SOME(index, factIndex) {
        if (definitions[index].kind != identity::DefinitionKind::Closure) {
          reject(BinderInvariantKind::InvalidBindingFact, entry.node);
          return false;
        }
      }
      const auto bytes = entry.key.encode();
      order.insert(zc::str(bytes.asChars()), entry.definition);
      ++closureCount;
    }
    if (order.size() != closureCount) {
      reject(BinderInvariantKind::InvalidBindingFact, tree.root());
      return false;
    }
    for (const auto& ordered : order) {
      zc::Vector<FreeVariableFact> variables;
      facts.add(ClosureFreeVariableFact{ordered.value, zc::mv(variables)});
    }
    return true;
  }

  zc::Maybe<size_t> closureRow(identity::DefId closure) const {
    zc::Maybe<size_t> result;
    for (size_t index = 0; index < facts.size(); ++index) {
      if (facts[index].closure != closure) { continue; }
      if (result != zc::none) { return zc::none; }
      result = index;
    }
    return result;
  }

  zc::Maybe<uint32_t> owningCallableScope(const DefinitionFact& definition) const {
    uint32_t scopeIndex = definition.declaringScope.index();
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return zc::none; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        return scopeIndex;
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) { return zc::none; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return zc::none;
  }

  bool appendReference(size_t closureIndex, identity::DefId target, ast::NodeId site) {
    if (closureIndex >= facts.size()) { return false; }
    auto& variables = facts[closureIndex].variables;
    size_t variableIndex = variables.size();
    for (size_t index = 0; index < variables.size(); ++index) {
      if (variables[index].target == target) {
        variableIndex = index;
        break;
      }
    }
    if (variableIndex == variables.size()) {
      zc::Vector<ast::NodeId> sites;
      variables.add(FreeVariableFact{target, zc::mv(sites)});
    }
    for (const auto existing : variables[variableIndex].referenceSites) {
      if (existing == site) { return true; }
    }
    variables[variableIndex].referenceSites.add(site);
    return true;
  }

  void collect(const BindingResolution& resolution) {
    if (!tree.contains(resolution.node) || resolution.node.value >= scopeByNode.size() ||
        scopeByNode[resolution.node.value] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
      return;
    }
    if (!resolution.value.is<BoundNameResolution>()) { return; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration) {
      return;
    }
    const auto& bindingIdentity = bound.bindingIdentity.value();
    const auto& canonicalTarget = bound.canonicalTarget.value();
    if (!bindingIdentity.is<DefinitionBindingTarget>() ||
        !canonicalTarget.is<DefinitionBindingTarget>() ||
        bindingIdentity.get<DefinitionBindingTarget>().definition !=
            canonicalTarget.get<DefinitionBindingTarget>().definition) {
      reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
      return;
    }

    const auto target = bindingIdentity.get<DefinitionBindingTarget>().definition;
    auto targetIndex = definitionIndex(target);
    if (targetIndex == zc::none) {
      reject(BinderInvariantKind::MissingRequiredResolution, resolution.node);
      return;
    }
    const auto definitionIndexValue = ZC_ASSERT_NONNULL(targetIndex);
    const auto& targetDefinition = definitions[definitionIndexValue];
    if (!isCapturable(targetDefinition.kind)) { return; }
    auto targetCallableScope = owningCallableScope(targetDefinition);
    if (targetCallableScope == zc::none) { return; }

    const uint32_t targetScopeIndex = ZC_ASSERT_NONNULL(targetCallableScope);
    const auto& targetScope = arena.scopes[targetScopeIndex];
    const auto& targetOwner = targetScope.owner.value();
    if (!targetOwner.is<DefinitionScopeOwner>()) {
      reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
      return;
    }
    const auto targetCallable = targetOwner.get<DefinitionScopeOwner>().definition;

    zc::Vector<size_t> crossedClosures;
    uint32_t scopeIndex = scopeByNode[resolution.node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
        return;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        const auto& owner = scope.owner.value();
        if (!owner.is<DefinitionScopeOwner>()) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        const auto callable = owner.get<DefinitionScopeOwner>().definition;
        if (callable == targetCallable) {
          for (const auto closureIndex : crossedClosures) {
            if (!appendReference(closureIndex, target, resolution.node)) {
              reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
              return;
            }
          }
          return;
        }
        if (scope.kind == ScopeKind::Function) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        auto row = closureRow(callable);
        if (row == zc::none) {
          reject(BinderInvariantKind::MissingRequiredResolution, resolution.node);
          return;
        }
        crossedClosures.add(ZC_ASSERT_NONNULL(row));
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
        return;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
  }

  bool canonicalizeSites(FreeVariableFact& fact) {
    zc::TreeMap<ReferenceSiteOrderKey, ast::NodeId> order;
    for (const auto site : fact.referenceSites) {
      if (!tree.contains(site) || site.value >= schemaOrdinals.size() ||
          schemaOrdinals[site.value] == kMissingIndex) {
        reject(BinderInvariantKind::InvalidBindingFact, site);
        return false;
      }
      auto source = input.parsedModule().spanFor(tree.node(site).range);
      if (source == zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, site);
        return false;
      }
      ZC_IF_SOME(value, source) {
        order.insert(
            ReferenceSiteOrderKey{value.byteStart(), value.byteEnd(), schemaOrdinals[site.value]},
            site);
      }
    }
    if (order.size() != fact.referenceSites.size()) {
      reject(BinderInvariantKind::InvalidBindingFact, tree.root());
      return false;
    }
    zc::Vector<ast::NodeId> canonical;
    for (const auto& ordered : order) { canonical.add(ordered.value); }
    fact.referenceSites = zc::mv(canonical);
    return true;
  }

  bool canonicalize() {
    for (auto& closure : facts) {
      zc::TreeMap<zc::String, size_t> order;
      for (size_t index = 0; index < closure.variables.size(); ++index) {
        auto key = input.definitions().definitionKey(closure.variables[index].target);
        if (key == zc::none || !canonicalizeSites(closure.variables[index])) {
          if (rejected == zc::none) {
            reject(BinderInvariantKind::InvalidBindingFact, tree.root());
          }
          return false;
        }
        ZC_IF_SOME(value, key) {
          const auto bytes = value.encode();
          order.insert(zc::str(bytes.asChars()), index);
        }
      }
      if (order.size() != closure.variables.size()) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      zc::Vector<FreeVariableFact> canonical;
      for (const auto& ordered : order) { canonical.add(zc::mv(closure.variables[ordered.value])); }
      closure.variables = zc::mv(canonical);
    }
    return true;
  }
};

ClosureFreeVariableBuildResult ClosureFreeVariableBuilder::build(
    const VerifiedBindingInput& input, const ScopeArenaCandidate& arena,
    zc::ArrayPtr<const DefinitionFact> definitions,
    zc::ArrayPtr<const BindingResolution> nodeBindings) {
  return ClosureFreeVariableCursor(input, arena, definitions, nodeBindings).run();
}

}  // namespace zomlang::compiler::binder
