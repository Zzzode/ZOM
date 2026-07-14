// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/closure-free-variables.h"

#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;
constexpr size_t kMissingSize = static_cast<size_t>(-1);

enum class ClosureCaptureDomain : uint8_t { NotClosure, Inferred, Explicit };

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
  zc::Vector<uint32_t> owningCallableScopeIndices;
  zc::Vector<size_t> callableDefinitionIndices;
  zc::Vector<ClosureCaptureDomain> closureCaptureDomains;
  zc::Vector<size_t> closureFactRows;
  zc::TreeMap<zc::String, size_t> definitionIndices;
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

    owningCallableScopeIndices.resize(definitions.size());
    closureCaptureDomains.resize(definitions.size());
    closureFactRows.resize(definitions.size());
    callableDefinitionIndices.resize(arena.scopes.size());
    for (size_t index = 0; index < callableDefinitionIndices.size(); ++index) {
      callableDefinitionIndices[index] = kMissingSize;
    }
    for (size_t index = 0; index < definitions.size(); ++index) {
      owningCallableScopeIndices[index] = kMissingIndex;
      closureCaptureDomains[index] = ClosureCaptureDomain::NotClosure;
      closureFactRows[index] = kMissingSize;
      const auto& fact = definitions[index];
      auto key = input.definitions().definitionKey(fact.identity);
      if (!fact.identity.belongsTo(input.semanticContext()) || key == zc::none ||
          fact.declaringScope.module() != input.module() ||
          fact.declaringScope.index() >= arena.scopes.size() ||
          arena.scopes[fact.declaringScope.index()].id != fact.declaringScope) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      ZC_IF_SOME(value, key) {
        const auto bytes = value.encode();
        auto encoded = zc::str(bytes.asChars());
        if (definitionIndices.find(encoded) != zc::none) {
          reject(BinderInvariantKind::InvalidBindingFact, tree.root());
          return false;
        }
        definitionIndices.insert(zc::mv(encoded), index);
      }
    }

    zc::Vector<bool> matchedDefinitions;
    matchedDefinitions.resize(definitions.size());
    for (size_t index = 0; index < matchedDefinitions.size(); ++index) {
      matchedDefinitions[index] = false;
    }
    for (const auto& entry : input.definitions().definitions()) {
      auto factIndex = definitionIndex(entry.definition);
      if (factIndex == zc::none) {
        reject(BinderInvariantKind::MissingRequiredResolution, entry.node);
        return false;
      }
      const size_t index = ZC_ASSERT_NONNULL(factIndex);
      if (index >= definitions.size() || matchedDefinitions[index] ||
          definitions[index].identity != entry.definition ||
          definitions[index].kind != entry.kind) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      matchedDefinitions[index] = true;
      if (entry.kind != identity::DefinitionKind::Closure) { continue; }
      if (!tree.contains(entry.node)) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      const auto& syntax = tree.node(entry.node);
      if (syntax.kind == ast::SyntaxKind::LambdaExpression) {
        closureCaptureDomains[index] = ClosureCaptureDomain::Inferred;
        continue;
      }
      if (syntax.kind != ast::SyntaxKind::FunctionExpression) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
      if (captures &&
          (!tree.contains(captures) || tree.node(captures).kind != ast::SyntaxKind::CaptureList)) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      closureCaptureDomains[index] =
          captures ? ClosureCaptureDomain::Explicit : ClosureCaptureDomain::Inferred;
    }
    for (size_t index = 0; index < matchedDefinitions.size(); ++index) {
      if (!matchedDefinitions[index] ||
          (definitions[index].kind == identity::DefinitionKind::Closure &&
           closureCaptureDomains[index] == ClosureCaptureDomain::NotClosure)) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
    }

    for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind != ScopeKind::Function && scope.kind != ScopeKind::Closure) { continue; }
      const auto& owner = scope.owner.value();
      if (!owner.is<DefinitionScopeOwner>()) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
      auto factIndex = definitionIndex(owner.get<DefinitionScopeOwner>().definition);
      if (factIndex == zc::none) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
      const size_t index = ZC_ASSERT_NONNULL(factIndex);
      const bool closureDefinition = definitions[index].kind == identity::DefinitionKind::Closure;
      if ((scope.kind == ScopeKind::Closure) != closureDefinition) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
      callableDefinitionIndices[scopeIndex] = index;
    }

    for (size_t index = 0; index < definitions.size(); ++index) {
      uint32_t scopeIndex = definitions[index].declaringScope.index();
      for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
        if (scopeIndex >= arena.scopes.size()) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
        const auto& scope = arena.scopes[scopeIndex];
        if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
          if (callableDefinitionIndices[scopeIndex] == kMissingSize) {
            reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
            return false;
          }
          owningCallableScopeIndices[index] = scopeIndex;
          break;
        }
        if (scope.kind == ScopeKind::Module) { break; }
        if (scope.parent == zc::none) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
        ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
      }
    }
    return true;
  }

  zc::Maybe<size_t> definitionIndex(identity::DefId definition) const {
    auto key = input.definitions().definitionKey(definition);
    if (key == zc::none) { return zc::none; }
    ZC_IF_SOME(value, key) {
      const auto bytes = value.encode();
      auto index = definitionIndices.find(zc::str(bytes.asChars()));
      ZC_IF_SOME(found, index) {
        if (found < definitions.size() && definitions[found].identity == definition) {
          return found;
        }
      }
    }
    return zc::none;
  }

  bool initializeDenseRows() {
    for (const auto& ordered : definitionIndices) {
      const size_t definitionIndexValue = ordered.value;
      if (definitionIndexValue >= definitions.size()) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      if (definitions[definitionIndexValue].kind != identity::DefinitionKind::Closure) { continue; }
      if (closureCaptureDomains[definitionIndexValue] == ClosureCaptureDomain::Explicit) {
        continue;
      }
      if (closureCaptureDomains[definitionIndexValue] != ClosureCaptureDomain::Inferred ||
          closureFactRows[definitionIndexValue] != kMissingSize) {
        reject(BinderInvariantKind::InvalidBindingFact, tree.root());
        return false;
      }
      zc::Vector<FreeVariableFact> variables;
      closureFactRows[definitionIndexValue] = facts.size();
      facts.add(
          ClosureFreeVariableFact{definitions[definitionIndexValue].identity, zc::mv(variables)});
    }
    return true;
  }

  zc::Maybe<uint32_t> owningCallableScope(size_t definitionIndexValue) const {
    if (definitionIndexValue >= owningCallableScopeIndices.size() ||
        owningCallableScopeIndices[definitionIndexValue] == kMissingIndex) {
      return zc::none;
    }
    return owningCallableScopeIndices[definitionIndexValue];
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
    auto targetCallableScope = owningCallableScope(definitionIndexValue);
    if (targetCallableScope == zc::none) {
      uint32_t scopeIndex = scopeByNode[resolution.node.value];
      const uint32_t targetDeclaringScope = targetDefinition.declaringScope.index();
      for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
        if (scopeIndex >= arena.scopes.size()) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        if (scopeIndex == targetDeclaringScope) { return; }
        const auto& scope = arena.scopes[scopeIndex];
        if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||
            scope.kind == ScopeKind::Module || scope.parent == zc::none) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
      }
      reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
      return;
    }

    const uint32_t targetScopeIndex = ZC_ASSERT_NONNULL(targetCallableScope);
    if (targetScopeIndex >= callableDefinitionIndices.size() ||
        callableDefinitionIndices[targetScopeIndex] == kMissingSize) {
      reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
      return;
    }

    zc::Vector<size_t> crossedClosures;
    uint32_t scopeIndex = scopeByNode[resolution.node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
        return;
      }
      if (scopeIndex == targetScopeIndex) {
        for (const auto closureIndex : crossedClosures) {
          if (!appendReference(closureIndex, target, resolution.node)) {
            reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
            return;
          }
        }
        return;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableDefinitionIndices.size() ||
            callableDefinitionIndices[scopeIndex] == kMissingSize) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        const size_t callableIndex = callableDefinitionIndices[scopeIndex];
        if (scope.kind == ScopeKind::Function) {
          reject(BinderInvariantKind::MalformedScopeGraph, resolution.node);
          return;
        }
        if (callableIndex >= definitions.size() ||
            closureCaptureDomains[callableIndex] == ClosureCaptureDomain::NotClosure) {
          reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
          return;
        }
        if (closureCaptureDomains[callableIndex] == ClosureCaptureDomain::Inferred) {
          const size_t row = closureFactRows[callableIndex];
          if (row >= facts.size() || facts[row].closure != definitions[callableIndex].identity) {
            reject(BinderInvariantKind::MissingRequiredResolution, resolution.node);
            return;
          }
          crossedClosures.add(row);
        }
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
