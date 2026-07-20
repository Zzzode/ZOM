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

namespace zomlang::compiler::binder {
namespace {

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

enum class DeferredMemberOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact
};

bool sameDeferredMemberFact(const DeferredMemberFact& left, const DeferredMemberFact& right) {
  if (left.node != right.node || left.base != right.base || left.member != right.member ||
      left.expectedNamespaces.size() != right.expectedNamespaces.size() ||
      left.genericArguments.size() != right.genericArguments.size() ||
      !sameSpan(left.source, right.source)) {
    return false;
  }
  for (size_t index = 0; index < left.expectedNamespaces.size(); ++index) {
    if (left.expectedNamespaces[index] != right.expectedNamespaces[index]) { return false; }
  }
  for (size_t index = 0; index < left.genericArguments.size(); ++index) {
    if (left.genericArguments[index] != right.genericArguments[index]) { return false; }
  }
  return true;
}

DeferredMemberOracleResult verifyDeferredMemberFacts(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<ast::NodeList> directCallTypeArguments;
  zc::Vector<bool> hasDirectCall;
  zc::Vector<size_t> factByNode;
  zc::Vector<size_t> resolutionByNode;
  directCallTypeArguments.resize(tree.nodeCount() + 1);
  hasDirectCall.resize(tree.nodeCount() + 1);
  factByNode.resize(tree.nodeCount() + 1);
  resolutionByNode.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < factByNode.size(); ++index) {
    hasDirectCall[index] = false;
    factByNode[index] = kMissing;
    resolutionByNode[index] = kMissing;
  }

  size_t memberCount = 0;
  bool treeIsValid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (!treeIsValid) { return; }
    if (syntax.kind == ast::SyntaxKind::MemberExpression) {
      const auto access = static_cast<ast::MemberAccessKind>(
          syntax.payload.words[ast::kMemberExpressionAccessWord]);
      if (access != ast::MemberAccessKind::Dot && access != ast::MemberAccessKind::Optional) {
        treeIsValid = false;
        return;
      }
      ++memberCount;
      return;
    }
    if (syntax.kind != ast::SyntaxKind::CallExpression) { return; }
    const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
    const ast::NodeList typeArguments{syntax.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                      syntax.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
    if (!tree.contains(callee) || !tree.contains(typeArguments)) {
      treeIsValid = false;
      return;
    }
    for (const ast::NodeId argument : tree.list(typeArguments)) {
      if (!tree.contains(argument)) {
        treeIsValid = false;
        return;
      }
    }
    if (tree.node(callee).kind != ast::SyntaxKind::MemberExpression) { return; }
    if (callee.value >= hasDirectCall.size() || hasDirectCall[callee.value]) {
      treeIsValid = false;
      return;
    }
    hasDirectCall[callee.value] = true;
    directCallTypeArguments[callee.value] = typeArguments;
  });
  if (!treeIsValid) { return DeferredMemberOracleResult::InvalidBindingFact; }

  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.deferredMembers.size(); ++index) {
    const auto& fact = candidate.deferredMembers[index];
    if (!tree.contains(fact.node) || fact.node.value >= factByNode.size() ||
        tree.node(fact.node).kind != ast::SyntaxKind::MemberExpression ||
        factByNode[fact.node.value] != kMissing ||
        (index != 0 && fact.node.value <= previousNode)) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    factByNode[fact.node.value] = index;
    previousNode = fact.node.value;
  }
  if (candidate.deferredMembers.size() > memberCount) {
    return DeferredMemberOracleResult::InvalidBindingFact;
  }

  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto& resolution = candidate.nodeBindings[index];
    if (!tree.contains(resolution.node)) { return DeferredMemberOracleResult::InvalidBindingFact; }
    const bool isMember = tree.node(resolution.node).kind == ast::SyntaxKind::MemberExpression;
    if (resolution.value.is<DeferredMemberFact>() && !isMember) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    if (!isMember) { continue; }
    if (resolution.node.value >= resolutionByNode.size() ||
        resolutionByNode[resolution.node.value] != kMissing) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    resolutionByNode[resolution.node.value] = index;
  }

  DeferredMemberOracleResult result = DeferredMemberOracleResult::Valid;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != DeferredMemberOracleResult::Valid ||
        syntax.kind != ast::SyntaxKind::MemberExpression) {
      return;
    }
    const size_t factIndex = factByNode[node.value];
    const size_t resolutionIndex = resolutionByNode[node.value];
    if (factIndex == kMissing || resolutionIndex == kMissing) {
      result = DeferredMemberOracleResult::MissingRequiredResolution;
      return;
    }
    const auto& resolution = candidate.nodeBindings[resolutionIndex];
    if (!resolution.value.is<DeferredMemberFact>()) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    const auto& fact = candidate.deferredMembers[factIndex];
    const auto& inlineFact = resolution.value.get<DeferredMemberFact>();
    if (!sameDeferredMemberFact(fact, inlineFact)) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }

    const ast::NodeId base(syntax.payload.words[ast::kMemberExpressionObjectWord]);
    const auto access =
        static_cast<ast::MemberAccessKind>(syntax.payload.words[ast::kMemberExpressionAccessWord]);
    auto name = identity::DeclaredDefinitionName::fromSource(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kMemberExpressionPropertyWord])));
    auto source = input.parsedModule().spanFor(syntax.range);
    if (!tree.contains(base) || name == zc::none || source == zc::none || fact.node != node ||
        fact.base != base || fact.expectedNamespaces.size() != 1 ||
        fact.expectedNamespaces[0] != Namespace::Value ||
        (access != ast::MemberAccessKind::Dot && access != ast::MemberAccessKind::Optional)) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    ZC_IF_SOME(nameValue, name) {
      if (fact.member != nameValue) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }
    ZC_IF_SOME(sourceValue, source) {
      if (!sameSpan(fact.source, sourceValue)) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }

    const auto expectedArguments = hasDirectCall[node.value]
                                       ? tree.list(directCallTypeArguments[node.value])
                                       : zc::ArrayPtr<const ast::NodeId>();
    if (fact.genericArguments.size() != expectedArguments.size()) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    for (size_t index = 0; index < expectedArguments.size(); ++index) {
      if (fact.genericArguments[index] != expectedArguments[index]) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }
  });
  if (result != DeferredMemberOracleResult::Valid) { return result; }
  return candidate.deferredMembers.size() == memberCount
             ? DeferredMemberOracleResult::Valid
             : DeferredMemberOracleResult::MissingRequiredResolution;
}

BinderInvariantKind deferredMemberOracleInvariant(DeferredMemberOracleResult result) {
  switch (result) {
    case DeferredMemberOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case DeferredMemberOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case DeferredMemberOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

enum class ContextualSelfOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

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

zc::Maybe<SelfOwner> reconstructContextualSelfOwner(const VerifiedBindingInput& input,
                                                    zc::ArrayPtr<const ast::NodeId> parentNodes,
                                                    ast::NodeId node, bool& malformed) {
  const auto& tree = input.tree();
  ast::NodeId child = node;
  size_t remaining = parentNodes.size();
  while (tree.contains(child) && child.value < parentNodes.size() && remaining != 0) {
    --remaining;
    const ast::NodeId parent = parentNodes[child.value];
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

ContextualSelfOracleResult verifyContextualSelfFacts(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<ast::NodeId> parentNodes;
  zc::Vector<bool> visited;
  zc::Vector<size_t> factByNode;
  parentNodes.resize(tree.nodeCount() + 1);
  visited.resize(tree.nodeCount() + 1);
  factByNode.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < parentNodes.size(); ++index) {
    parentNodes[index] = ast::NodeId();
    visited[index] = false;
    factByNode[index] = kMissing;
  }

  size_t visitedCount = 0;
  bool treeIsValid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (!treeIsValid || !tree.contains(node) || node.value >= visited.size() ||
        visited[node.value]) {
      treeIsValid = false;
      return;
    }
    visited[node.value] = true;
    ++visitedCount;
    ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) {
      if (!tree.contains(child) || child.value >= parentNodes.size() || parentNodes[child.value]) {
        treeIsValid = false;
        return;
      }
      parentNodes[child.value] = node;
    });
  });
  if (!treeIsValid || visitedCount != tree.nodeCount()) {
    return ContextualSelfOracleResult::MalformedScopeGraph;
  }

  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.selfTypes.size(); ++index) {
    const auto& fact = candidate.selfTypes[index];
    if (!tree.contains(fact.syntax) || fact.syntax.value >= factByNode.size() ||
        tree.node(fact.syntax).kind != ast::SyntaxKind::NamedTypeExpr ||
        factByNode[fact.syntax.value] != kMissing ||
        (index != 0 && fact.syntax.value <= previousNode)) {
      return ContextualSelfOracleResult::InvalidBindingFact;
    }
    factByNode[fact.syntax.value] = index;
    previousNode = fact.syntax.value;
  }

  size_t expectedCount = 0;
  ContextualSelfOracleResult result = ContextualSelfOracleResult::Valid;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != ContextualSelfOracleResult::Valid ||
        syntax.kind != ast::SyntaxKind::NamedTypeExpr) {
      return;
    }
    const ast::NodeId path(syntax.payload.words[ast::kNamedTypeExprPathWord]);
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& pathSyntax = tree.node(path);
    const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (!tree.contains(segments)) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto names = tree.identList(segments);
    if (names.size() == 0 || tree.ident(names[0]) != "Self"_zc) { return; }

    const ast::NodeId parent = parentNodes[node.value];
    if (tree.contains(parent) && tree.node(parent).kind == ast::SyntaxKind::FunctionParameterDecl) {
      const auto& parameter = tree.node(parent);
      const ast::NodeId type(parameter.payload.words[ast::kFunctionParameterDeclTyWord]);
      if (type == node && input.parsedModule().functionParameterHasImplicitSelfType(parent)) {
        return;
      }
    }

    bool malformedOwner = false;
    auto owner = reconstructContextualSelfOwner(input, parentNodes.asPtr(), node, malformedOwner);
    if (malformedOwner) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    if (owner == zc::none) { return; }
    ++expectedCount;

    const size_t factIndex = factByNode[node.value];
    if (factIndex == kMissing) {
      result = ContextualSelfOracleResult::MissingRequiredResolution;
      return;
    }
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
    if (source == zc::none) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& fact = candidate.selfTypes[factIndex];
    if (!sameSelfOwner(fact.owner, ZC_ASSERT_NONNULL(owner)) ||
        !sameSpan(fact.source, ZC_ASSERT_NONNULL(source))) {
      result = ContextualSelfOracleResult::InvalidBindingFact;
    }
  });
  if (result != ContextualSelfOracleResult::Valid) { return result; }
  if (candidate.selfTypes.size() < expectedCount) {
    return ContextualSelfOracleResult::MissingRequiredResolution;
  }
  return candidate.selfTypes.size() == expectedCount
             ? ContextualSelfOracleResult::Valid
             : ContextualSelfOracleResult::InvalidBindingFact;
}

BinderInvariantKind contextualSelfOracleInvariant(ContextualSelfOracleResult result) {
  switch (result) {
    case ContextualSelfOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ContextualSelfOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ContextualSelfOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ContextualSelfOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

}  // namespace

zc::Maybe<BinderInvariantKind> verifyDeferredMemberOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyDeferredMemberFacts(input, candidate);
  if (result == DeferredMemberOracleResult::Valid) { return zc::none; }
  return deferredMemberOracleInvariant(result);
}

zc::Maybe<BinderInvariantKind> verifyContextualSelfOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyContextualSelfFacts(input, candidate);
  if (result == ContextualSelfOracleResult::Valid) { return zc::none; }
  return contextualSelfOracleInvariant(result);
}

}  // namespace zomlang::compiler::binder
