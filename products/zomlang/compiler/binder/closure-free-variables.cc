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

enum class ClosureCaptureDomain : uint8_t { Inferred, Explicit };

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
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

zc::String framedKey(uint8_t domain, zc::ArrayPtr<const uint8_t> key) {
  zc::Vector<uint8_t> bytes(key.size() + 1);
  bytes.add(domain);
  bytes.addAll(key);
  return zc::str(bytes.asPtr().asChars());
}

zc::Maybe<zc::String> targetKey(const VerifiedBindingInput& input, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    ZC_IF_SOME(key,
               input.definitions().definitionKey(value.get<DefinitionBindingTarget>().definition)) {
      const auto bytes = key.encode();
      return framedKey(0x01, bytes.asPtr());
    }
    return zc::none;
  }
  if (value.is<GenericParameterBindingTarget>()) {
    ZC_IF_SOME(key, input.definitions().genericParameterKey(
                        value.get<GenericParameterBindingTarget>().parameter)) {
      const auto bytes = key.encode();
      return framedKey(0x02, bytes.asPtr());
    }
    return zc::none;
  }
  if (value.is<CallableParameterBindingTarget>()) {
    ZC_IF_SOME(key, input.definitions().callableParameterKey(
                        value.get<CallableParameterBindingTarget>().parameter)) {
      const auto bytes = key.encode();
      return framedKey(0x03, bytes.asPtr());
    }
    return zc::none;
  }
  if (value.is<OwnerLocalBindingTarget>()) {
    const auto binding = value.get<OwnerLocalBindingTarget>().binding;
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.binding != binding) { continue; }
      const auto bytes = entry.key.encode();
      return framedKey(0x04, bytes.asPtr());
    }
  }
  return zc::none;
}

struct CapturableBinding final {
  CapturableBinding(BindingTarget&& target, ScopeId declaringScope) noexcept
      : target(zc::mv(target)), declaringScope(declaringScope) {}
  CapturableBinding(CapturableBinding&&) noexcept = default;
  CapturableBinding& operator=(CapturableBinding&&) noexcept = default;
  ZC_DISALLOW_COPY(CapturableBinding);

  BindingTarget target;
  ScopeId declaringScope;
  uint32_t captureBoundaryScope = kMissingIndex;
};

struct ClosureRecord final {
  ClosureRecord(AnonymousOwnerLocalKey&& key, ast::NodeId node, uint32_t scope,
                ClosureCaptureDomain domain) noexcept
      : key(zc::mv(key)), node(node), scope(scope), domain(domain) {}
  ClosureRecord(ClosureRecord&&) noexcept = default;
  ClosureRecord& operator=(ClosureRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ClosureRecord);

  AnonymousOwnerLocalKey key;
  ast::NodeId node;
  uint32_t scope;
  ClosureCaptureDomain domain;
  size_t factRow = kMissingSize;
};

}  // namespace

class ClosureFreeVariableCursor final {
public:
  ClosureFreeVariableCursor(const VerifiedBindingInput& input, const ScopeArenaCandidate& arena,
                            zc::ArrayPtr<const CallableParameterFact> callableParameters,
                            zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
                            zc::ArrayPtr<const BindingResolution> nodeBindings,
                            zc::ArrayPtr<const BoundThis> thisBindings)
      : input(input),
        tree(input.tree()),
        arena(arena),
        callableParameters(callableParameters),
        ownerLocalBindings(ownerLocalBindings),
        nodeBindings(nodeBindings),
        thisBindings(thisBindings) {}

  ClosureFreeVariableBuildResult run() {
    if (!initialize()) { return takeRejection(); }
    initializeFactRows();
    if (rejected != zc::none) { return takeRejection(); }
    for (const auto& resolution : nodeBindings) {
      collect(resolution);
      if (rejected != zc::none) { return takeRejection(); }
    }
    for (const auto& binding : thisBindings) {
      collectReference(binding.expression,
                       BindingTarget::callableParameter(binding.binding.receiverParameter));
      if (rejected != zc::none) { return takeRejection(); }
    }
    if (!canonicalize()) { return takeRejection(); }
    return zc::mv(facts);
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  const ScopeArenaCandidate& arena;
  zc::ArrayPtr<const CallableParameterFact> callableParameters;
  zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings;
  zc::ArrayPtr<const BindingResolution> nodeBindings;
  zc::ArrayPtr<const BoundThis> thisBindings;
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<uint32_t> schemaOrdinals;
  zc::Vector<CapturableBinding> bindings;
  zc::TreeMap<zc::String, size_t> bindingIndices;
  zc::Vector<ClosureRecord> closures;
  zc::TreeMap<zc::String, size_t> closureIndices;
  zc::Vector<size_t> closureByScope;
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

  bool addBinding(BindingTarget&& target, ScopeId declaringScope, ast::NodeId node) {
    if (declaringScope.module() != input.module() ||
        declaringScope.index() >= arena.scopes.size() ||
        arena.scopes[declaringScope.index()].id != declaringScope) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return false;
    }
    auto key = targetKey(input, target);
    if (key == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return false;
    }
    ZC_IF_SOME(encoded, key) {
      if (bindingIndices.find(encoded) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, node);
        return false;
      }
      const size_t index = bindings.size();
      bindingIndices.insert(zc::mv(encoded), index);
      bindings.add(CapturableBinding(zc::mv(target), declaringScope));
    }
    return true;
  }

  zc::Maybe<size_t> findBinding(const BindingTarget& target) const {
    auto key = targetKey(input, target);
    ZC_IF_SOME(encoded, key) {
      auto found = bindingIndices.find(encoded);
      ZC_IF_SOME(index, found) {
        if (index < bindings.size() && sameTarget(bindings[index].target, target)) { return index; }
      }
    }
    return zc::none;
  }

  zc::Maybe<size_t> findClosure(const AnonymousOwnerLocalKey& key) const {
    const auto bytes = key.encode();
    auto found = closureIndices.find(framedKey(0x05, bytes.asPtr()));
    ZC_IF_SOME(index, found) {
      if (index < closures.size() && closures[index].key == key) { return index; }
    }
    return zc::none;
  }

  bool initialize() {
    if (!tree.contains(tree.root()) || arena.scopes.empty() ||
        arena.scopes[0].kind != ScopeKind::Module || arena.nodeScopes.size() != tree.nodeCount()) {
      reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
      return false;
    }
    scopeByNode.resize(tree.nodeCount() + 1);
    schemaOrdinals.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < scopeByNode.size(); ++index) {
      scopeByNode[index] = kMissingIndex;
      schemaOrdinals[index] = kMissingIndex;
    }
    closureByScope.resize(arena.scopes.size());
    for (auto& value : closureByScope) { value = kMissingSize; }

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
      if (scope.kind == ScopeKind::Function) {
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
      if (node.value >= schemaOrdinals.size() || schemaOrdinals[node.value] != kMissingIndex ||
          scopeByNode[node.value] == kMissingIndex) {
        valid = false;
        return;
      }
      schemaOrdinals[node.value] = ordinal++;
    });
    if (!valid || ordinal != tree.nodeCount()) {
      reject(BinderInvariantKind::InvalidEmitterOrdinal, tree.root());
      return false;
    }

    for (const auto& fact : callableParameters) {
      if (!addBinding(BindingTarget::callableParameter(fact.identity), fact.declaringScope,
                      fact.site.value().is<DeclarationDefinitionSite>()
                          ? fact.site.value().get<DeclarationDefinitionSite>().node
                          : tree.root())) {
        return false;
      }
    }
    for (const auto& fact : ownerLocalBindings) {
      if (fact.kind == OwnerLocalBindingKind::GenericParameter) { continue; }
      ast::NodeId node = tree.root();
      if (fact.site.value().is<DeclarationDefinitionSite>()) {
        node = fact.site.value().get<DeclarationDefinitionSite>().node;
      } else if (fact.site.value().is<PatternBindingSite>()) {
        node = fact.site.value().get<PatternBindingSite>().introducer;
      }
      if (!addBinding(BindingTarget::ownerLocal(fact.identity), fact.declaringScope, node)) {
        return false;
      }
    }

    for (const auto& entry : input.definitions().anonymousEntities()) {
      if (!tree.contains(entry.node)) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      const auto& syntax = tree.node(entry.node);
      ClosureCaptureDomain domain = ClosureCaptureDomain::Inferred;
      if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
        const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        if (captures && (!tree.contains(captures) ||
                         tree.node(captures).kind != ast::SyntaxKind::CaptureList)) {
          reject(BinderInvariantKind::InvalidBindingFact, entry.node);
          return false;
        }
        domain = captures ? ClosureCaptureDomain::Explicit : ClosureCaptureDomain::Inferred;
      } else if (syntax.kind != ast::SyntaxKind::LambdaExpression) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      const uint32_t scope = scopeByNode[entry.node.value];
      if (scope >= arena.scopes.size() || arena.scopes[scope].kind != ScopeKind::Closure ||
          !arena.scopes[scope].owner.value().is<AnonymousScopeOwner>() ||
          arena.scopes[scope].owner.value().get<AnonymousScopeOwner>().anonymous != entry.key ||
          closureByScope[scope] != kMissingSize) {
        reject(BinderInvariantKind::MalformedScopeGraph, entry.node);
        return false;
      }
      const auto bytes = entry.key.encode();
      auto encoded = framedKey(0x05, bytes.asPtr());
      if (closureIndices.find(encoded) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, entry.node);
        return false;
      }
      const size_t index = closures.size();
      closureIndices.insert(zc::mv(encoded), index);
      closures.add(ClosureRecord(entry.key.clone(), entry.node, scope, domain));
      closureByScope[scope] = index;
    }

    for (auto& binding : bindings) {
      uint32_t scopeIndex = binding.declaringScope.index();
      for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
        if (scopeIndex >= arena.scopes.size()) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
        const auto& scope = arena.scopes[scopeIndex];
        if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
          binding.captureBoundaryScope = scopeIndex;
          break;
        }
        if (scope.kind == ScopeKind::Module) {
          binding.captureBoundaryScope = scopeIndex;
          break;
        }
        if (scope.parent == zc::none) {
          reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
          return false;
        }
        ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
      }
      if (binding.captureBoundaryScope == kMissingIndex) {
        reject(BinderInvariantKind::MalformedScopeGraph, tree.root());
        return false;
      }
    }
    return true;
  }

  void initializeFactRows() {
    zc::TreeMap<zc::String, size_t> order;
    for (size_t index = 0; index < closures.size(); ++index) {
      if (closures[index].domain == ClosureCaptureDomain::Explicit) { continue; }
      const auto bytes = closures[index].key.encode();
      order.insert(framedKey(0x05, bytes.asPtr()), index);
    }
    for (const auto& ordered : order) {
      auto& closure = closures[ordered.value];
      closure.factRow = facts.size();
      zc::Vector<FreeVariableFact> variables;
      facts.add(ClosureFreeVariableFact{closure.key.clone(), zc::mv(variables)});
    }
  }

  bool appendReference(size_t factRow, const BindingTarget& target, ast::NodeId site) {
    if (factRow >= facts.size()) { return false; }
    auto& variables = facts[factRow].variables;
    size_t variableIndex = variables.size();
    for (size_t index = 0; index < variables.size(); ++index) {
      if (sameTarget(variables[index].target, target)) {
        variableIndex = index;
        break;
      }
    }
    if (variableIndex == variables.size()) {
      zc::Vector<ast::NodeId> sites;
      variables.add(FreeVariableFact{target.clone(), zc::mv(sites)});
    }
    for (const auto existing : variables[variableIndex].referenceSites) {
      if (existing == site) { return true; }
    }
    variables[variableIndex].referenceSites.add(site);
    return true;
  }

  void collectReference(ast::NodeId node, BindingTarget&& target) {
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidBindingFact, node);
      return;
    }
    auto bindingIndex = findBinding(target);
    if (bindingIndex == zc::none) { return; }
    const auto& binding = bindings[ZC_ASSERT_NONNULL(bindingIndex)];
    if (binding.captureBoundaryScope == kMissingIndex) {
      reject(BinderInvariantKind::MalformedScopeGraph, node);
      return;
    }

    zc::Vector<size_t> crossedClosures;
    uint32_t scopeIndex = scopeByNode[node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph, node);
        return;
      }
      if (scopeIndex == binding.captureBoundaryScope) {
        for (const auto closureIndex : crossedClosures) {
          const auto row = closures[closureIndex].factRow;
          if (row == kMissingSize || !appendReference(row, target, node)) {
            reject(BinderInvariantKind::InvalidBindingFact, node);
            return;
          }
        }
        return;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function) {
        reject(BinderInvariantKind::MalformedScopeGraph, node);
        return;
      }
      if (scope.kind == ScopeKind::Closure) {
        const size_t closureIndex = closureByScope[scopeIndex];
        if (closureIndex == kMissingSize || closureIndex >= closures.size()) {
          reject(BinderInvariantKind::MalformedScopeGraph, node);
          return;
        }
        if (closures[closureIndex].domain == ClosureCaptureDomain::Inferred) {
          crossedClosures.add(closureIndex);
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        reject(BinderInvariantKind::MalformedScopeGraph, node);
        return;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    reject(BinderInvariantKind::MalformedScopeGraph, node);
  }

  void collect(const BindingResolution& resolution) {
    if (!tree.contains(resolution.node)) {
      reject(BinderInvariantKind::InvalidBindingFact, resolution.node);
      return;
    }
    if (!resolution.value.is<BoundNameResolution>()) { return; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration ||
        !sameTarget(bound.bindingIdentity, bound.canonicalTarget)) {
      return;
    }
    collectReference(resolution.node, bound.bindingIdentity.clone());
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
        auto key = targetKey(input, closure.variables[index].target);
        if (key == zc::none || !canonicalizeSites(closure.variables[index])) {
          if (rejected == zc::none) {
            reject(BinderInvariantKind::InvalidBindingFact, tree.root());
          }
          return false;
        }
        ZC_IF_SOME(value, key) { order.insert(zc::mv(value), index); }
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
    zc::ArrayPtr<const CallableParameterFact> callableParameters,
    zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
    zc::ArrayPtr<const BindingResolution> nodeBindings,
    zc::ArrayPtr<const BoundThis> thisBindings) {
  (void)definitions;
  return ClosureFreeVariableCursor(input, arena, callableParameters, ownerLocalBindings,
                                   nodeBindings, thisBindings)
      .run();
}

}  // namespace zomlang::compiler::binder
