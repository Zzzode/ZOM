// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-capture-validator.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingScope = UINT32_MAX;

enum class CaptureDomain : uint8_t { Inferred, Explicit };

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

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

struct TargetInfo final {
  TargetInfo(BindingTarget&& target, uint32_t declaringScope, bool capturable) noexcept
      : target(zc::mv(target)), declaringScope(declaringScope), capturable(capturable) {}
  TargetInfo(TargetInfo&&) noexcept = default;
  TargetInfo& operator=(TargetInfo&&) noexcept = default;
  ZC_DISALLOW_COPY(TargetInfo);

  BindingTarget target;
  uint32_t declaringScope;
  bool capturable;
};

struct ClosureInfo final {
  ClosureInfo(AnonymousOwnerLocalKey&& key, ast::NodeId node, uint32_t scope, CaptureDomain domain,
              ast::NodeId captureList) noexcept
      : key(zc::mv(key)), node(node), scope(scope), domain(domain), captureList(captureList) {}
  ClosureInfo(ClosureInfo&&) noexcept = default;
  ClosureInfo& operator=(ClosureInfo&&) noexcept = default;
  ZC_DISALLOW_COPY(ClosureInfo);

  AnonymousOwnerLocalKey key;
  ast::NodeId node;
  uint32_t scope;
  CaptureDomain domain;
  ast::NodeId captureList;
};

struct ExpectedVariable final {
  ExpectedVariable(size_t closureIndex, BindingTarget&& target) noexcept
      : closureIndex(closureIndex), target(zc::mv(target)) {}
  ExpectedVariable(ExpectedVariable&&) noexcept = default;
  ExpectedVariable& operator=(ExpectedVariable&&) noexcept = default;
  ZC_DISALLOW_COPY(ExpectedVariable);

  size_t closureIndex;
  BindingTarget target;
  zc::Vector<ast::NodeId> sites;
};

class CaptureSemanticValidator final {
public:
  CaptureSemanticValidator(const VerifiedBindingInput& input,
                           const BindingMetadataCandidate& candidate) noexcept
      : input(input), candidate(candidate), tree(input.tree()) {}

  zc::Maybe<BinderInvariantKind> verify() {
    if (!buildScopeIndex() || !buildClosures() || !buildTargets()) { return failure; }
    if (!verifyClosureDomains() || !verifyExplicitCaptures() || !collectReferences() ||
        !verifyInferredCaptures()) {
      return failure;
    }
    return zc::none;
  }

private:
  bool reject(BinderInvariantKind kind) {
    if (failure == zc::none) { failure = kind; }
    return false;
  }

  bool buildScopeIndex() {
    if (candidate.scopes.empty() || candidate.nodeScopes.size() != tree.nodeCount()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }
    scopeByNode.resize(tree.nodeCount() + 1);
    for (auto& scope : scopeByNode) { scope = kMissingScope; }
    nearestCallable.resize(candidate.scopes.size());
    parentCallable.resize(candidate.scopes.size());
    for (size_t index = 0; index < candidate.scopes.size(); ++index) {
      const auto& scope = candidate.scopes[index];
      if (scope.id.index() != index || scope.id.module() != input.module() ||
          (index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      uint32_t parentBoundary = kMissingScope;
      ZC_IF_SOME(parent, scope.parent) {
        if (parent.index() >= index || candidate.scopes[parent.index()].id != parent) {
          return reject(BinderInvariantKind::MalformedScopeGraph);
        }
        parentBoundary = nearestCallable[parent.index()];
      }
      parentCallable[index] = parentBoundary;
      nearestCallable[index] = scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure
                                   ? static_cast<uint32_t>(index)
                                   : parentBoundary;
    }
    for (const auto& fact : candidate.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
          scopeByNode[fact.node.value] != kMissingScope ||
          fact.scope.index() >= candidate.scopes.size() ||
          candidate.scopes[fact.scope.index()].id != fact.scope) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      scopeByNode[fact.node.value] = fact.scope.index();
    }
    for (ast::NodeId node(1); node.value < scopeByNode.size(); ++node.value) {
      if (tree.contains(node) && scopeByNode[node.value] == kMissingScope) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
    }
    return true;
  }

  bool buildClosures() {
    for (const auto& entry : input.definitions().anonymousEntities()) {
      if (!tree.contains(entry.node) || entry.node.value >= scopeByNode.size()) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      const uint32_t scopeIndex = scopeByNode[entry.node.value];
      if (scopeIndex >= candidate.scopes.size() ||
          candidate.scopes[scopeIndex].kind != ScopeKind::Closure) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      const auto& owner = candidate.scopes[scopeIndex].owner.value();
      if (!owner.is<AnonymousScopeOwner>() ||
          owner.get<AnonymousScopeOwner>().anonymous != entry.key) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      const auto& syntax = tree.node(entry.node);
      CaptureDomain domain = CaptureDomain::Inferred;
      ast::NodeId captureList;
      if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
        captureList = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        if (captureList) {
          if (!tree.contains(captureList) ||
              tree.node(captureList).kind != ast::SyntaxKind::CaptureList) {
            return reject(BinderInvariantKind::InvalidBindingFact);
          }
          domain = CaptureDomain::Explicit;
        }
      } else if (syntax.kind != ast::SyntaxKind::LambdaExpression) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      for (const auto& closure : closures) {
        if (closure.key == entry.key || closure.scope == scopeIndex) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
      closures.add(ClosureInfo(entry.key.clone(), entry.node, scopeIndex, domain, captureList));
    }
    for (size_t scopeIndex = 0; scopeIndex < candidate.scopes.size(); ++scopeIndex) {
      if (candidate.scopes[scopeIndex].kind != ScopeKind::Closure) { continue; }
      if (closureIndexForScope(static_cast<uint32_t>(scopeIndex)) == zc::none) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
    }
    return true;
  }

  bool addTarget(TargetInfo&& target) {
    if (target.declaringScope >= candidate.scopes.size()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }
    for (const auto& existing : targets) {
      if (sameTarget(existing.target, target.target)) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
    }
    targets.add(zc::mv(target));
    return true;
  }

  bool buildTargets() {
    for (const auto& fact : candidate.definitions) {
      const bool capturable = fact.kind == identity::DefinitionKind::Parameter ||
                              fact.kind == identity::DefinitionKind::Local ||
                              fact.kind == identity::DefinitionKind::PatternBinding;
      if (!addTarget(TargetInfo(BindingTarget::definition(fact.identity),
                                fact.declaringScope.index(), capturable))) {
        return false;
      }
    }
    for (const auto& fact : candidate.callableParameters) {
      if (!addTarget(TargetInfo(BindingTarget::callableParameter(fact.identity),
                                fact.declaringScope.index(), true))) {
        return false;
      }
    }
    for (const auto& fact : candidate.ownerLocalBindings) {
      const bool capturable = fact.kind == OwnerLocalBindingKind::CallableParameter ||
                              fact.kind == OwnerLocalBindingKind::Local ||
                              fact.kind == OwnerLocalBindingKind::PatternBinding;
      if (!addTarget(TargetInfo(BindingTarget::ownerLocal(fact.identity),
                                fact.declaringScope.index(), capturable))) {
        return false;
      }
    }
    return true;
  }

  bool verifyClosureDomains() {
    size_t inferredCount = 0;
    size_t explicitCount = 0;
    for (size_t rowIndex = 0; rowIndex < candidate.closureFreeVariables.size(); ++rowIndex) {
      const auto& row = candidate.closureFreeVariables[rowIndex];
      auto closureIndex = closureIndexForKey(row.closure);
      if (closureIndex == zc::none ||
          closures[ZC_ASSERT_NONNULL(closureIndex)].domain != CaptureDomain::Inferred) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      for (size_t prior = 0; prior < rowIndex; ++prior) {
        if (candidate.closureFreeVariables[prior].closure == row.closure) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
    }
    for (size_t rowIndex = 0; rowIndex < candidate.explicitClosureCaptures.size(); ++rowIndex) {
      const auto& row = candidate.explicitClosureCaptures[rowIndex];
      auto closureIndex = closureIndexForKey(row.closure);
      if (closureIndex == zc::none ||
          closures[ZC_ASSERT_NONNULL(closureIndex)].domain != CaptureDomain::Explicit) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      for (size_t prior = 0; prior < rowIndex; ++prior) {
        if (candidate.explicitClosureCaptures[prior].closure == row.closure) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
    }
    for (size_t closureIndex = 0; closureIndex < closures.size(); ++closureIndex) {
      const auto& closure = closures[closureIndex];
      size_t freeRows = 0;
      size_t explicitRows = 0;
      for (const auto& row : candidate.closureFreeVariables) {
        if (row.closure == closure.key) { ++freeRows; }
      }
      for (const auto& row : candidate.explicitClosureCaptures) {
        if (row.closure == closure.key) { ++explicitRows; }
      }
      if (closure.domain == CaptureDomain::Inferred) {
        ++inferredCount;
        if (freeRows == 0) { return reject(BinderInvariantKind::MissingRequiredResolution); }
        if (freeRows != 1 || explicitRows != 0) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      } else {
        ++explicitCount;
        if (explicitRows == 0) { return reject(BinderInvariantKind::MissingRequiredResolution); }
        if (explicitRows != 1 || freeRows != 0) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
    }
    if (candidate.closureFreeVariables.size() < inferredCount ||
        candidate.explicitClosureCaptures.size() < explicitCount) {
      return reject(BinderInvariantKind::MissingRequiredResolution);
    }
    if (candidate.closureFreeVariables.size() != inferredCount ||
        candidate.explicitClosureCaptures.size() != explicitCount) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    return true;
  }

  bool verifyExplicitCaptures() {
    for (size_t closureIndex = 0; closureIndex < closures.size(); ++closureIndex) {
      const auto& closure = closures[closureIndex];
      if (closure.domain != CaptureDomain::Explicit) { continue; }
      auto rowIndex = explicitRowIndex(closure.key);
      if (rowIndex == zc::none) { return reject(BinderInvariantKind::MissingRequiredResolution); }
      const auto& row = candidate.explicitClosureCaptures[ZC_ASSERT_NONNULL(rowIndex)];
      if (row.captureList != closure.captureList || !tree.contains(row.captureList) ||
          tree.node(row.captureList).kind != ast::SyntaxKind::CaptureList) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      auto listSource = input.parsedModule().spanFor(tree.node(row.captureList).range);
      if (listSource == zc::none || !sameSpan(row.source, ZC_ASSERT_NONNULL(listSource))) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      const auto& list = tree.node(row.captureList);
      const ast::NodeList items{list.payload.words[ast::kCaptureListCapturesFirstWord],
                                list.payload.words[ast::kCaptureListCapturesSizeWord]};
      if (!tree.contains(items) ||
          list.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      if (closure.scope >= candidate.scopes.size() ||
          candidate.scopes[closure.scope].parent == zc::none) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      const uint32_t enclosingScope =
          ZC_ASSERT_NONNULL(candidate.scopes[closure.scope].parent).index();
      size_t captureIndex = 0;
      for (const auto item : tree.list(items)) {
        if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
            item.value >= scopeByNode.size() || scopeByNode[item.value] != closure.scope) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
        auto resolutionIndex = bindingIndex(item);
        if (resolutionIndex == zc::none) {
          return reject(BinderInvariantKind::MissingRequiredResolution);
        }
        const auto& resolution = candidate.nodeBindings[ZC_ASSERT_NONNULL(resolutionIndex)].value;
        if (resolution.is<FailedBindingResolution>()) { continue; }
        if (!resolution.is<BoundNameResolution>()) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
        const auto& bound = resolution.get<BoundNameResolution>();
        if (bound.nameSpace != Namespace::Value ||
            bound.origin != BindingOrigin::LocalDeclaration ||
            !sameTarget(bound.bindingIdentity, bound.canonicalTarget)) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
        auto targetIndex = findTarget(bound.canonicalTarget);
        if (targetIndex == zc::none || !targets[ZC_ASSERT_NONNULL(targetIndex)].capturable ||
            !isAncestor(targets[ZC_ASSERT_NONNULL(targetIndex)].declaringScope, enclosingScope)) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
        if (captureIndex >= row.captures.size()) {
          return reject(BinderInvariantKind::MissingRequiredResolution);
        }
        const auto& capture = row.captures[captureIndex++];
        auto itemSource = captureItemSource(item);
        if (itemSource == zc::none || capture.item != item ||
            !sameTarget(capture.target, bound.canonicalTarget) ||
            !sameSpan(capture.source, ZC_ASSERT_NONNULL(itemSource))) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
      if (captureIndex != row.captures.size()) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
    }
    return true;
  }

  bool collectReferences() {
    for (const auto& resolution : candidate.nodeBindings) {
      if (!resolution.value.is<BoundNameResolution>()) { continue; }
      const auto& bound = resolution.value.get<BoundNameResolution>();
      if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration) {
        continue;
      }
      if (!sameTarget(bound.bindingIdentity, bound.canonicalTarget) ||
          !processReference(resolution.node, bound.canonicalTarget)) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
    }
    for (const auto& binding : candidate.thisBindings) {
      if (!processReference(binding.expression,
                            BindingTarget::callableParameter(binding.binding.receiverParameter))) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
    }
    for (auto& expected : expectedVariables) { sortSites(expected.sites); }
    return true;
  }

  bool processReference(ast::NodeId site, const BindingTarget& target) {
    auto targetIndex = findTarget(target);
    if (targetIndex == zc::none) { return false; }
    const auto& targetInfo = targets[ZC_ASSERT_NONNULL(targetIndex)];
    if (!targetInfo.capturable) { return true; }
    if (!tree.contains(site) || site.value >= scopeByNode.size()) { return false; }
    const uint32_t referenceScope = scopeByNode[site.value];
    if (referenceScope >= candidate.scopes.size() ||
        !isAncestor(targetInfo.declaringScope, referenceScope)) {
      return false;
    }
    const uint32_t targetBoundary = nearestCallable[targetInfo.declaringScope];
    uint32_t boundary = nearestCallable[referenceScope];
    for (size_t traversed = 0; traversed <= candidate.scopes.size(); ++traversed) {
      if (boundary == targetBoundary) { return true; }
      if (boundary == kMissingScope || boundary >= candidate.scopes.size()) { return false; }
      const auto& scope = candidate.scopes[boundary];
      if (scope.kind == ScopeKind::Function) { return false; }
      if (scope.kind != ScopeKind::Closure) { return false; }
      auto closureIndex = closureIndexForScope(boundary);
      if (closureIndex == zc::none) { return false; }
      const auto& closure = closures[ZC_ASSERT_NONNULL(closureIndex)];
      if (closure.domain == CaptureDomain::Explicit) {
        if (!explicitClosureContains(closure.key, target)) { return false; }
      } else if (!addExpected(ZC_ASSERT_NONNULL(closureIndex), target, site)) {
        return false;
      }
      boundary = parentCallable[boundary];
    }
    return false;
  }

  bool addExpected(size_t closureIndex, const BindingTarget& target, ast::NodeId site) {
    for (auto& expected : expectedVariables) {
      if (expected.closureIndex != closureIndex || !sameTarget(expected.target, target)) {
        continue;
      }
      for (const auto existing : expected.sites) {
        if (existing == site) { return false; }
      }
      expected.sites.add(site);
      return true;
    }
    ExpectedVariable expected(closureIndex, target.clone());
    expected.sites.add(site);
    expectedVariables.add(zc::mv(expected));
    return true;
  }

  bool verifyInferredCaptures() {
    for (const auto& expected : expectedVariables) {
      const auto& closure = closures[expected.closureIndex];
      auto rowIndex = freeRowIndex(closure.key);
      if (rowIndex == zc::none) { return reject(BinderInvariantKind::MissingRequiredResolution); }
      const auto& row = candidate.closureFreeVariables[ZC_ASSERT_NONNULL(rowIndex)];
      zc::Maybe<const FreeVariableFact&> variable;
      for (const auto& candidateVariable : row.variables) {
        if (sameTarget(candidateVariable.target, expected.target)) {
          if (variable != zc::none) { return reject(BinderInvariantKind::InvalidBindingFact); }
          variable = candidateVariable;
        }
      }
      if (variable == zc::none) { return reject(BinderInvariantKind::MissingRequiredResolution); }
      const auto& sites = ZC_ASSERT_NONNULL(variable).referenceSites;
      if (sites.size() < expected.sites.size()) {
        return reject(BinderInvariantKind::MissingRequiredResolution);
      }
      if (sites.size() != expected.sites.size()) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      for (size_t index = 0; index < sites.size(); ++index) {
        if (sites[index] != expected.sites[index]) {
          return reject(BinderInvariantKind::InvalidBindingFact);
        }
      }
    }
    for (size_t closureIndex = 0; closureIndex < closures.size(); ++closureIndex) {
      if (closures[closureIndex].domain != CaptureDomain::Inferred) { continue; }
      auto rowIndex = freeRowIndex(closures[closureIndex].key);
      if (rowIndex == zc::none) { return reject(BinderInvariantKind::MissingRequiredResolution); }
      for (const auto& variable :
           candidate.closureFreeVariables[ZC_ASSERT_NONNULL(rowIndex)].variables) {
        bool found = false;
        for (const auto& expected : expectedVariables) {
          if (expected.closureIndex == closureIndex &&
              sameTarget(expected.target, variable.target)) {
            found = true;
            break;
          }
        }
        if (!found) { return reject(BinderInvariantKind::InvalidBindingFact); }
      }
    }
    return true;
  }

  zc::Maybe<size_t> findTarget(const BindingTarget& target) const {
    for (size_t index = 0; index < targets.size(); ++index) {
      if (sameTarget(targets[index].target, target)) { return index; }
    }
    return zc::none;
  }

  zc::Maybe<size_t> closureIndexForScope(uint32_t scope) const {
    for (size_t index = 0; index < closures.size(); ++index) {
      if (closures[index].scope == scope) { return index; }
    }
    return zc::none;
  }

  zc::Maybe<size_t> closureIndexForKey(const AnonymousOwnerLocalKey& key) const {
    for (size_t index = 0; index < closures.size(); ++index) {
      if (closures[index].key == key) { return index; }
    }
    return zc::none;
  }

  zc::Maybe<size_t> bindingIndex(ast::NodeId node) const {
    for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
      if (candidate.nodeBindings[index].node == node) { return index; }
    }
    return zc::none;
  }

  zc::Maybe<size_t> freeRowIndex(const AnonymousOwnerLocalKey& closure) const {
    for (size_t index = 0; index < candidate.closureFreeVariables.size(); ++index) {
      if (candidate.closureFreeVariables[index].closure == closure) { return index; }
    }
    return zc::none;
  }

  zc::Maybe<size_t> explicitRowIndex(const AnonymousOwnerLocalKey& closure) const {
    for (size_t index = 0; index < candidate.explicitClosureCaptures.size(); ++index) {
      if (candidate.explicitClosureCaptures[index].closure == closure) { return index; }
    }
    return zc::none;
  }

  bool explicitClosureContains(const AnonymousOwnerLocalKey& closure,
                               const BindingTarget& target) const {
    auto rowIndex = explicitRowIndex(closure);
    if (rowIndex == zc::none) { return false; }
    for (const auto& capture :
         candidate.explicitClosureCaptures[ZC_ASSERT_NONNULL(rowIndex)].captures) {
      if (sameTarget(capture.target, target)) { return true; }
    }
    return false;
  }

  bool isAncestor(uint32_t ancestor, uint32_t descendant) const {
    if (ancestor >= candidate.scopes.size() || descendant >= candidate.scopes.size()) {
      return false;
    }
    uint32_t scope = descendant;
    for (size_t traversed = 0; traversed <= candidate.scopes.size(); ++traversed) {
      if (scope == ancestor) { return true; }
      const auto& record = candidate.scopes[scope];
      if (record.parent == zc::none) { return false; }
      ZC_IF_SOME(parent, record.parent) { scope = parent.index(); }
      if (scope >= candidate.scopes.size()) { return false; }
    }
    return false;
  }

  zc::Maybe<identity::SourceSpan> captureItemSource(ast::NodeId item) const {
    const auto& syntax = tree.node(item);
    const auto mode =
        static_cast<ast::CaptureMode>(syntax.payload.words[ast::kCaptureItemModeWord]);
    size_t tokenOrdinal = 0;
    ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
    if (mode == ast::CaptureMode::ByRef) {
      tokenOrdinal = 1;
    } else if (mode == ast::CaptureMode::This) {
      tokenKind = ast::SyntaxKind::ThisKeyword;
    } else if (mode != ast::CaptureMode::ByValue) {
      return zc::none;
    }
    return input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
  }

  bool siteLess(ast::NodeId left, ast::NodeId right) const {
    auto leftSource = input.parsedModule().spanFor(tree.node(left).range);
    auto rightSource = input.parsedModule().spanFor(tree.node(right).range);
    if (leftSource == zc::none || rightSource == zc::none) { return left.value < right.value; }
    const auto& leftSpan = ZC_ASSERT_NONNULL(leftSource);
    const auto& rightSpan = ZC_ASSERT_NONNULL(rightSource);
    if (leftSpan.byteStart() != rightSpan.byteStart()) {
      return leftSpan.byteStart() < rightSpan.byteStart();
    }
    if (leftSpan.byteEnd() != rightSpan.byteEnd()) {
      return leftSpan.byteEnd() < rightSpan.byteEnd();
    }
    return left.value < right.value;
  }

  void sortSites(zc::Vector<ast::NodeId>& sites) const {
    for (size_t index = 1; index < sites.size(); ++index) {
      size_t cursor = index;
      while (cursor > 0 && siteLess(sites[cursor], sites[cursor - 1])) {
        const auto displaced = sites[cursor - 1];
        sites[cursor - 1] = sites[cursor];
        sites[cursor] = displaced;
        --cursor;
      }
    }
  }

  const VerifiedBindingInput& input;
  const BindingMetadataCandidate& candidate;
  const ast::Tree& tree;
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<uint32_t> nearestCallable;
  zc::Vector<uint32_t> parentCallable;
  zc::Vector<TargetInfo> targets;
  zc::Vector<ClosureInfo> closures;
  zc::Vector<ExpectedVariable> expectedVariables;
  zc::Maybe<BinderInvariantKind> failure;
};

}  // namespace

zc::Maybe<BinderInvariantKind> verifyBindingCaptureSemantics(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  return CaptureSemanticValidator(input, candidate).verify();
}

}  // namespace zomlang::compiler::binder
