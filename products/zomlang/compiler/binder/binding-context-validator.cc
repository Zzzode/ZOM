// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-context-validator.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr size_t kMissingIndex = static_cast<size_t>(-1);
constexpr uint32_t kMissingScope = UINT32_MAX;

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

bool sameSelfOwner(const SelfOwner& left, const SelfOwner& right) {
  if (left.is<NominalSelfOwner>()) {
    return right.is<NominalSelfOwner>() &&
           left.get<NominalSelfOwner>().definition == right.get<NominalSelfOwner>().definition;
  }
  if (left.is<InterfaceSelfOwner>()) {
    return right.is<InterfaceSelfOwner>() &&
           left.get<InterfaceSelfOwner>().definition == right.get<InterfaceSelfOwner>().definition;
  }
  return right.is<ImplSelfOwner>() &&
         left.get<ImplSelfOwner>().occurrence == right.get<ImplSelfOwner>().occurrence;
}

class BindingContextSemanticValidator final {
public:
  BindingContextSemanticValidator(const VerifiedBindingInput& input,
                                  const BindingMetadataCandidate& candidate) noexcept
      : input(input), candidate(candidate), tree(input.tree()) {}

  zc::Maybe<BinderInvariantKind> verify() {
    if (!buildAstIndex() || !buildCandidateIndex() || !buildReceiverIndex()) { return failure; }
    if (!verifySelfTypes() || !verifyThisBindings()) { return failure; }
    return zc::none;
  }

private:
  bool reject(BinderInvariantKind kind) {
    if (failure == zc::none) { failure = kind; }
    return false;
  }

  bool buildAstIndex() {
    const size_t size = tree.nodeCount() + 1;
    parentByNode.resize(size);
    visited.resize(size);
    schemaOrdinalByNode.resize(size);
    for (size_t index = 0; index < size; ++index) {
      parentByNode[index] = ast::NodeId();
      visited[index] = false;
      schemaOrdinalByNode[index] = UINT32_MAX;
    }

    uint32_t ordinal = 0;
    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid || !tree.contains(node) || node.value >= size || visited[node.value]) {
        valid = false;
        return;
      }
      visited[node.value] = true;
      schemaOrdinalByNode[node.value] = ordinal++;
      ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) {
        if (!valid || !tree.contains(child) || child.value >= size || parentByNode[child.value]) {
          valid = false;
          return;
        }
        parentByNode[child.value] = node;
      });
    });
    if (!valid || ordinal != tree.nodeCount()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }
    return true;
  }

  bool buildCandidateIndex() {
    const size_t size = tree.nodeCount() + 1;
    scopeByNode.resize(size);
    selfFactByNode.resize(size);
    thisFactByNode.resize(size);
    resolutionByNode.resize(size);
    for (size_t index = 0; index < size; ++index) {
      scopeByNode[index] = kMissingScope;
      selfFactByNode[index] = kMissingIndex;
      thisFactByNode[index] = kMissingIndex;
      resolutionByNode[index] = kMissingIndex;
    }
    if (candidate.nodeScopes.size() != tree.nodeCount() || candidate.scopes.empty()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }
    for (const auto& fact : candidate.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= size ||
          scopeByNode[fact.node.value] != kMissingScope ||
          fact.scope.index() >= candidate.scopes.size() ||
          candidate.scopes[fact.scope.index()].id != fact.scope) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      scopeByNode[fact.node.value] = fact.scope.index();
    }
    for (ast::NodeId node(1); node.value < size; ++node.value) {
      if (tree.contains(node) && scopeByNode[node.value] == kMissingScope) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
    }

    uint32_t previous = 0;
    for (size_t index = 0; index < candidate.selfTypes.size(); ++index) {
      const auto node = candidate.selfTypes[index].syntax;
      if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::NamedTypeExpr ||
          selfFactByNode[node.value] != kMissingIndex || (index != 0 && node.value <= previous)) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      selfFactByNode[node.value] = index;
      previous = node.value;
    }
    previous = 0;
    for (size_t index = 0; index < candidate.thisBindings.size(); ++index) {
      const auto node = candidate.thisBindings[index].expression;
      if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::ThisExpr ||
          thisFactByNode[node.value] != kMissingIndex || (index != 0 && node.value <= previous)) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      thisFactByNode[node.value] = index;
      previous = node.value;
    }
    for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
      const auto node = candidate.nodeBindings[index].node;
      if (!tree.contains(node) || resolutionByNode[node.value] != kMissingIndex) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      resolutionByNode[node.value] = index;
    }
    return true;
  }

  bool buildReceiverIndex() {
    receiverByScope.resize(candidate.scopes.size());
    for (auto& value : receiverByScope) { value = kMissingIndex; }
    const auto parameters = input.definitions().callableParameters();
    for (size_t index = 0; index < parameters.size(); ++index) {
      const auto& parameter = parameters[index];
      const bool receiver =
          parameter.record.position().kind() == identity::CallableParameterPositionKind::Receiver;
      auto source = input.parsedModule().functionParameterNameSpan(parameter.node,
                                                                   ast::SyntaxKind::ThisKeyword);
      if (!receiver) {
        if (source != zc::none) { return reject(BinderInvariantKind::InvalidBindingFact); }
        continue;
      }
      if (!tree.contains(parameter.node) ||
          tree.node(parameter.node).kind != ast::SyntaxKind::FunctionParameterDecl ||
          parameter.node.value >= scopeByNode.size() || source == zc::none) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      const uint32_t scope = scopeByNode[parameter.node.value];
      if (scope >= candidate.scopes.size() || candidate.scopes[scope].kind != ScopeKind::Function) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      const size_t current = receiverByScope[scope];
      if (current == kMissingIndex ||
          parameters[index].source.byteStart() < parameters[current].source.byteStart() ||
          (parameters[index].source.byteStart() == parameters[current].source.byteStart() &&
           parameters[index].source.byteEnd() < parameters[current].source.byteEnd())) {
        receiverByScope[scope] = index;
      }
    }
    return true;
  }

  bool isContextualSelfRoot(ast::NodeId node) const {
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::NamedTypeExpr) {
      return false;
    }
    const auto& syntax = tree.node(node);
    const ast::NodeId path(syntax.payload.words[ast::kNamedTypeExprPathWord]);
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      return false;
    }
    const auto& pathSyntax = tree.node(path);
    const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (!tree.contains(segments)) { return false; }
    const auto names = tree.identList(segments);
    return names.size() != 0 && tree.ident(names[0]) == "Self"_zc;
  }

  bool isImplicitReceiverType(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= parentByNode.size()) { return false; }
    const ast::NodeId parent = parentByNode[node.value];
    if (!tree.contains(parent) ||
        tree.node(parent).kind != ast::SyntaxKind::FunctionParameterDecl) {
      return false;
    }
    const ast::NodeId type(tree.node(parent).payload.words[ast::kFunctionParameterDeclTyWord]);
    return type == node && input.parsedModule().functionParameterHasImplicitSelfType(parent);
  }

  zc::Maybe<SelfOwner> contextualSelfOwner(ast::NodeId node, bool& malformed) const {
    ast::NodeId child = node;
    size_t remaining = parentByNode.size();
    while (tree.contains(child) && child.value < parentByNode.size() && remaining != 0) {
      --remaining;
      const ast::NodeId parent = parentByNode[child.value];
      if (!parent) { return zc::none; }
      if (!tree.contains(parent)) {
        malformed = true;
        return zc::none;
      }
      const auto& syntax = tree.node(parent);
      ast::NodeId body;
      bool nominal = false;
      bool interface = false;
      bool implementation = false;
      switch (syntax.kind) {
        case ast::SyntaxKind::ClassDecl:
          body = ast::NodeId(syntax.payload.words[ast::kClassDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::StructDecl:
          body = ast::NodeId(syntax.payload.words[ast::kStructDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::EnumDeclaration:
          body = ast::NodeId(syntax.payload.words[ast::kEnumDeclarationVariantsIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::ErrorDecl:
          body = ast::NodeId(syntax.payload.words[ast::kErrorDeclMembersIdWord]);
          nominal = true;
          break;
        case ast::SyntaxKind::InterfaceDecl:
          body = ast::NodeId(syntax.payload.words[ast::kInterfaceDeclMembersIdWord]);
          interface = true;
          break;
        case ast::SyntaxKind::StandaloneImplDecl:
          body = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclMembersIdWord]);
          implementation = true;
          break;
        default:
          break;
      }
      if (body && !tree.contains(body)) {
        malformed = true;
        return zc::none;
      }
      if (body && child == body) {
        if (implementation) {
          auto owner = input.definitions().implAt(parent);
          if (owner == zc::none) {
            malformed = true;
            return zc::none;
          }
          return SelfOwner(ImplSelfOwner{ZC_ASSERT_NONNULL(owner)});
        }
        auto owner = input.definitions().definitionAt(parent);
        if (owner == zc::none || (!nominal && !interface)) {
          malformed = true;
          return zc::none;
        }
        if (interface) { return SelfOwner(InterfaceSelfOwner{ZC_ASSERT_NONNULL(owner)}); }
        return SelfOwner(NominalSelfOwner{ZC_ASSERT_NONNULL(owner)});
      }
      child = parent;
    }
    if (remaining == 0) { malformed = true; }
    return zc::none;
  }

  bool verifyFailure(ast::NodeId node, BinderDiagnosticCode diagnostic,
                     const identity::SourceSpan& source) {
    if (!tree.contains(node) || node.value >= resolutionByNode.size()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }
    const size_t resolutionIndex = resolutionByNode[node.value];
    if (resolutionIndex == kMissingIndex) {
      return reject(BinderInvariantKind::MissingRequiredResolution);
    }
    const auto& value = candidate.nodeBindings[resolutionIndex].value;
    if (!value.is<FailedBindingResolution>()) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    const size_t failureIndex = value.get<FailedBindingResolution>().failureIndex;
    if (failureIndex >= candidate.sourceFailures.size()) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    const auto& fact = candidate.sourceFailures[failureIndex];
    const uint8_t site = static_cast<uint8_t>(fact.emitterOrdinal >> 56);
    const uint32_t ordinal = static_cast<uint32_t>((fact.emitterOrdinal >> 16) & UINT32_MAX);
    const uint16_t sequence = static_cast<uint16_t>(fact.emitterOrdinal);
    if (fact.diagnostic != diagnostic || !sameSpan(fact.primary, source) ||
        site != static_cast<uint8_t>(BinderEmitterSite::BodyBinding) ||
        ordinal != schemaOrdinalByNode[node.value] || sequence != 0 || !fact.notes.empty()) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    return true;
  }

  bool verifySelfTypes() {
    size_t expectedCount = 0;
    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid || syntax.kind != ast::SyntaxKind::NamedTypeExpr || !isContextualSelfRoot(node) ||
          isImplicitReceiverType(node)) {
        return;
      }
      bool malformed = false;
      auto owner = contextualSelfOwner(node, malformed);
      if (malformed) {
        valid = reject(BinderInvariantKind::MalformedScopeGraph);
        return;
      }
      auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
      if (source == zc::none) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }
      const size_t factIndex = selfFactByNode[node.value];
      if (owner == zc::none) {
        if (factIndex != kMissingIndex) {
          valid = reject(BinderInvariantKind::InvalidBindingFact);
          return;
        }
        valid = verifyFailure(node, BinderDiagnosticCode::ContextualSelfOutsideType,
                              ZC_ASSERT_NONNULL(source));
        return;
      }
      ++expectedCount;
      if (factIndex == kMissingIndex) {
        valid = reject(BinderInvariantKind::MissingRequiredResolution);
        return;
      }
      const auto& fact = candidate.selfTypes[factIndex];
      if (!sameSelfOwner(fact.owner, ZC_ASSERT_NONNULL(owner)) ||
          !sameSpan(fact.source, ZC_ASSERT_NONNULL(source)) ||
          resolutionByNode[node.value] != kMissingIndex) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
      }
    });
    if (!valid) { return false; }
    if (candidate.selfTypes.size() < expectedCount) {
      return reject(BinderInvariantKind::MissingRequiredResolution);
    }
    if (candidate.selfTypes.size() != expectedCount) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    return true;
  }

  zc::Maybe<size_t> activeReceiver(uint32_t referenceScope) const {
    uint32_t scope = referenceScope;
    for (size_t traversed = 0; traversed <= candidate.scopes.size(); ++traversed) {
      if (scope >= candidate.scopes.size()) { return zc::none; }
      if (receiverByScope[scope] != kMissingIndex) { return receiverByScope[scope]; }
      const auto& record = candidate.scopes[scope];
      if (record.kind == ScopeKind::Function || record.kind == ScopeKind::Module ||
          record.parent == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(parent, record.parent) { scope = parent.index(); }
    }
    return zc::none;
  }

  bool closureIsExplicit(uint32_t scope, bool& explicitCapture,
                         zc::Maybe<AnonymousOwnerLocalKey>& key) const {
    if (scope >= candidate.scopes.size() || candidate.scopes[scope].kind != ScopeKind::Closure) {
      return false;
    }
    const auto& owner = candidate.scopes[scope].owner.value();
    if (!owner.is<AnonymousScopeOwner>()) { return false; }
    const auto& anonymous = owner.get<AnonymousScopeOwner>().anonymous;
    zc::Maybe<ast::NodeId> closureNode;
    for (const auto& entry : input.definitions().anonymousEntities()) {
      if (entry.key == anonymous) {
        if (closureNode != zc::none) { return false; }
        closureNode = entry.node;
      }
    }
    if (closureNode == zc::none || !tree.contains(ZC_ASSERT_NONNULL(closureNode))) { return false; }
    const auto& syntax = tree.node(ZC_ASSERT_NONNULL(closureNode));
    if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
      const ast::NodeId captureList(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
      explicitCapture = static_cast<bool>(captureList);
      if (explicitCapture && (!tree.contains(captureList) ||
                              tree.node(captureList).kind != ast::SyntaxKind::CaptureList)) {
        return false;
      }
    } else if (syntax.kind == ast::SyntaxKind::LambdaExpression) {
      explicitCapture = false;
    } else {
      return false;
    }
    key = anonymous.clone();
    return true;
  }

  bool explicitCaptureContains(const AnonymousOwnerLocalKey& closure,
                               identity::CallableParameterId receiver) const {
    size_t rowCount = 0;
    bool found = false;
    for (const auto& row : candidate.explicitClosureCaptures) {
      if (row.closure != closure) { continue; }
      ++rowCount;
      for (const auto& capture : row.captures) {
        const auto& target = capture.target.value();
        if (target.is<CallableParameterBindingTarget>() &&
            target.get<CallableParameterBindingTarget>().parameter == receiver) {
          found = true;
        }
      }
    }
    return rowCount == 1 && found;
  }

  bool receiverIsAccessible(uint32_t referenceScope, size_t receiverIndex) const {
    const auto parameters = input.definitions().callableParameters();
    if (receiverIndex >= parameters.size()) { return false; }
    const uint32_t targetScope = scopeByNode[parameters[receiverIndex].node.value];
    uint32_t scope = referenceScope;
    for (size_t traversed = 0; traversed <= candidate.scopes.size(); ++traversed) {
      if (scope >= candidate.scopes.size()) { return false; }
      if (scope == targetScope) { return true; }
      const auto& record = candidate.scopes[scope];
      if (record.kind == ScopeKind::Function) { return false; }
      if (record.kind == ScopeKind::Closure) {
        bool explicitCapture = false;
        zc::Maybe<AnonymousOwnerLocalKey> closure;
        if (!closureIsExplicit(scope, explicitCapture, closure)) { return false; }
        if (explicitCapture && !explicitCaptureContains(ZC_ASSERT_NONNULL(closure),
                                                        parameters[receiverIndex].parameter)) {
          return false;
        }
      }
      if (record.kind == ScopeKind::Module || record.parent == zc::none) { return false; }
      ZC_IF_SOME(parent, record.parent) { scope = parent.index(); }
    }
    return false;
  }

  bool verifyThisBindings() {
    size_t expectedCount = 0;
    bool valid = true;
    const auto parameters = input.definitions().callableParameters();
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid || syntax.kind != ast::SyntaxKind::ThisExpr) { return; }
      if (node.value >= scopeByNode.size() || scopeByNode[node.value] == kMissingScope) {
        valid = reject(BinderInvariantKind::MalformedScopeGraph);
        return;
      }
      auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::ThisKeyword);
      if (source == zc::none) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }
      auto receiver = activeReceiver(scopeByNode[node.value]);
      const bool accessible =
          receiver != zc::none &&
          receiverIsAccessible(scopeByNode[node.value], ZC_ASSERT_NONNULL(receiver));
      const size_t factIndex = thisFactByNode[node.value];
      if (!accessible) {
        if (factIndex != kMissingIndex) {
          valid = reject(BinderInvariantKind::InvalidBindingFact);
          return;
        }
        valid = verifyFailure(node, BinderDiagnosticCode::UndefinedIdentifier,
                              ZC_ASSERT_NONNULL(source));
        return;
      }
      ++expectedCount;
      if (factIndex == kMissingIndex) {
        valid = reject(BinderInvariantKind::MissingRequiredResolution);
        return;
      }
      const auto& fact = candidate.thisBindings[factIndex];
      if (fact.binding.receiverParameter != parameters[ZC_ASSERT_NONNULL(receiver)].parameter ||
          !sameSpan(fact.source, ZC_ASSERT_NONNULL(source)) ||
          resolutionByNode[node.value] != kMissingIndex) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
      }
    });
    if (!valid) { return false; }
    if (candidate.thisBindings.size() < expectedCount) {
      return reject(BinderInvariantKind::MissingRequiredResolution);
    }
    if (candidate.thisBindings.size() != expectedCount) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    return true;
  }

  const VerifiedBindingInput& input;
  const BindingMetadataCandidate& candidate;
  const ast::Tree& tree;
  zc::Vector<ast::NodeId> parentByNode;
  zc::Vector<bool> visited;
  zc::Vector<uint32_t> schemaOrdinalByNode;
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<size_t> selfFactByNode;
  zc::Vector<size_t> thisFactByNode;
  zc::Vector<size_t> resolutionByNode;
  zc::Vector<size_t> receiverByScope;
  zc::Maybe<BinderInvariantKind> failure;
};

}  // namespace

zc::Maybe<BinderInvariantKind> verifyBindingContextSemantics(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  return BindingContextSemanticValidator(input, candidate).verify();
}

}  // namespace zomlang::compiler::binder
