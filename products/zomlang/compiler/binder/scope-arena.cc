// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/scope-arena.h"

#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

enum class OwnerKind : uint8_t { Module, Definition, Impl, Anonymous };

struct OwnerCursor final {
  OwnerKind kind;
  identity::ModuleId module;
  identity::DefId definition;
  ImplOccurrenceId implementation;
  ast::NodeId anonymous;
};

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::ModuleSkeleton,
                             ordinal};
}

ScopeOwner makeOwner(const VerifiedBindingInput& input, const OwnerCursor& owner) {
  switch (owner.kind) {
    case OwnerKind::Module:
      return ScopeOwner::module(owner.module);
    case OwnerKind::Definition:
      return ScopeOwner::definition(owner.definition);
    case OwnerKind::Impl:
      return ScopeOwner::implementation(owner.implementation);
    case OwnerKind::Anonymous:
      ZC_IF_SOME(value, input.definitions().anonymousEntityAt(owner.anonymous)) {
        return ScopeOwner::anonymous(value.key.clone());
      }
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

}  // namespace

zc::Maybe<ScopeKind> scopeKindForSyntax(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      return ScopeKind::Function;
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::LambdaExpression:
      return ScopeKind::Closure;
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::ErrorDecl:
      return ScopeKind::TypeBody;
    case ast::SyntaxKind::StandaloneImplDecl:
    case ast::SyntaxKind::MarkerImpl:
      return ScopeKind::ImplBody;
    case ast::SyntaxKind::BlockStmt:
      return ScopeKind::Block;
    case ast::SyntaxKind::WhileStmt:
    case ast::SyntaxKind::ForStmt:
    case ast::SyntaxKind::ForInStatement:
    case ast::SyntaxKind::DoWhileStatement:
      return ScopeKind::Loop;
    case ast::SyntaxKind::MatchStmt:
      return ScopeKind::Match;
    case ast::SyntaxKind::MatchArmStmt:
      return ScopeKind::MatchArm;
    case ast::SyntaxKind::UnsafeBlockExpr:
      return ScopeKind::UnsafeBlock;
    default:
      return zc::none;
  }
}

namespace {

bool spanContainedBy(const identity::SourceSpan& parent, const identity::SourceSpan& child) {
  return parent.byteStart() <= child.byteStart() && child.byteEnd() <= parent.byteEnd();
}

}  // namespace

zc::Maybe<uint32_t> checkedScopeIndex(uint64_t value) {
  if (value > static_cast<uint64_t>(UINT32_MAX)) { return zc::none; }
  return static_cast<uint32_t>(value);
}

ScopeArenaBuildResult ScopeArenaBuilder::build(const VerifiedBindingInput& input) {
  const auto& tree = input.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return failure(input, BinderInvariantKind::MissingRequiredResolution, 0);
  }

  ScopeArenaCandidate candidate;
  const ScopeId moduleScope(input.module(), 0);
  zc::Maybe<ScopeId> noParent;
  zc::Vector<ScopeBindingEntry> moduleBindings;
  candidate.scopes.add(ScopeRecord(moduleScope, zc::mv(noParent),
                                   ScopeOwner::module(input.module()), ScopeKind::Module,
                                   zc::mv(moduleBindings), input.parsedModule().rootSpan()));

  zc::Vector<zc::Maybe<NodeScopeFact>> nodeScopeSlots;
  nodeScopeSlots.resize(tree.nodeCount());
  uint64_t nextScopeIndex = 1;
  uint64_t scopeNodeCount = 0;
  uint32_t preorderOrdinal = 0;
  zc::Maybe<BinderInvariantFact> rejected;
  const OwnerCursor moduleOwner{OwnerKind::Module, input.module(), identity::DefId(),
                                ImplOccurrenceId(), ast::NodeId()};

  auto visit = [&](auto&& self, ast::NodeId node, ScopeId enclosingScope,
                   OwnerCursor owner) -> void {
    if (rejected != zc::none) { return; }
    const uint32_t ordinal = preorderOrdinal++;
    if (!node || node.value > tree.nodeCount() || !tree.contains(node)) {
      rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
      return;
    }
    const size_t nodeSlot = static_cast<size_t>(node.value - 1);
    if (nodeScopeSlots[nodeSlot] != zc::none) {
      rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
      return;
    }

    ScopeId currentScope = enclosingScope;
    OwnerCursor currentOwner = owner;
    ZC_IF_SOME(kind, scopeKindForSyntax(tree.node(node).kind)) {
      ++scopeNodeCount;
      auto index = checkedScopeIndex(nextScopeIndex);
      auto span = input.parsedModule().spanFor(tree.node(node).range);
      if (index == zc::none || span == zc::none) {
        rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
        return;
      }
      if (kind == ScopeKind::Function || kind == ScopeKind::TypeBody) {
        auto definition = input.definitions().definitionAt(node);
        if (definition == zc::none) {
          rejected = failure(input, BinderInvariantKind::MissingRequiredResolution, ordinal);
          return;
        }
        ZC_IF_SOME(value, definition) {
          currentOwner = OwnerCursor{OwnerKind::Definition, input.module(), value,
                                     ImplOccurrenceId(), ast::NodeId()};
        }
      } else if (kind == ScopeKind::Closure) {
        auto anonymous = input.definitions().anonymousEntityAt(node);
        if (anonymous == zc::none) {
          rejected = failure(input, BinderInvariantKind::MissingRequiredResolution, ordinal);
          return;
        }
        ZC_IF_SOME(value, anonymous) {
          currentOwner = OwnerCursor{OwnerKind::Anonymous, input.module(), identity::DefId(),
                                     ImplOccurrenceId(), value.node};
        }
      } else if (kind == ScopeKind::ImplBody) {
        auto implementation = input.definitions().implAt(node);
        if (implementation == zc::none) {
          rejected = failure(input, BinderInvariantKind::MissingRequiredResolution, ordinal);
          return;
        }
        ZC_IF_SOME(value, implementation) {
          currentOwner =
              OwnerCursor{OwnerKind::Impl, input.module(), identity::DefId(), value, ast::NodeId()};
        }
      }
      ZC_IF_SOME(indexValue, index) {
        ZC_IF_SOME(spanValue, span) {
          if (!spanContainedBy(candidate.scopes[enclosingScope.index()].source, spanValue)) {
            rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
            return;
          }
          currentScope = ScopeId(input.module(), indexValue);
          zc::Maybe<ScopeId> parent = enclosingScope;
          zc::Vector<ScopeBindingEntry> bindings;
          candidate.scopes.add(ScopeRecord(currentScope, zc::mv(parent),
                                           makeOwner(input, currentOwner), kind, zc::mv(bindings),
                                           zc::mv(spanValue)));
          ++nextScopeIndex;
        }
      }
    }

    nodeScopeSlots[nodeSlot] = NodeScopeFact{node, currentScope};
    ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) {
      self(self, child, currentScope, currentOwner);
    });
  };
  visit(visit, tree.root(), moduleScope, moduleOwner);

  ZC_IF_SOME(fact, rejected) { return zc::mv(fact); }
  const uint64_t expectedScopeCount = scopeNodeCount + 1;
  if (static_cast<uint64_t>(candidate.scopes.size()) != expectedScopeCount ||
      nextScopeIndex != expectedScopeCount) {
    return failure(input, BinderInvariantKind::MissingRequiredResolution, preorderOrdinal);
  }
  candidate.nodeScopes.reserve(tree.nodeCount());
  for (size_t index = 0; index < nodeScopeSlots.size(); ++index) {
    if (nodeScopeSlots[index] == zc::none) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, preorderOrdinal);
    }
    ZC_IF_SOME(fact, zc::mv(nodeScopeSlots[index])) {
      if (fact.node.value != index + 1 || !tree.contains(fact.node) ||
          fact.scope.module() != input.module() || fact.scope.index() >= candidate.scopes.size() ||
          candidate.scopes[fact.scope.index()].id != fact.scope) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, preorderOrdinal);
      }
      candidate.nodeScopes.add(zc::mv(fact));
    }
  }
  return candidate;
}

}  // namespace zomlang::compiler::binder
