// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/scope-arena.h"

#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

enum class OwnerKind : uint8_t { Module, Definition, Impl };

struct OwnerCursor final {
  OwnerKind kind;
  identity::ModuleId module;
  identity::DefId definition;
  identity::ImplId implementation;
};

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::ModuleSkeleton,
                             ordinal};
}

ScopeOwner makeOwner(const OwnerCursor& owner) {
  switch (owner.kind) {
    case OwnerKind::Module:
      return ScopeOwner::module(owner.module);
    case OwnerKind::Definition:
      return ScopeOwner::definition(owner.definition);
    case OwnerKind::Impl:
      return ScopeOwner::implementation(owner.implementation);
  }
  ZC_UNREACHABLE;
}

zc::Maybe<ScopeKind> scopeKind(ast::SyntaxKind kind) {
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

bool spanContainedBy(const identity::SourceSpan& parent, const identity::SourceSpan& child) {
  return parent.byteStart() <= child.byteStart() && child.byteEnd() <= parent.byteEnd();
}

void sortNodeScopes(zc::Vector<NodeScopeFact>& facts) {
  for (size_t index = 1; index < facts.size(); ++index) {
    auto current = facts[index];
    size_t insertion = index;
    while (insertion > 0 && current.node.value < facts[insertion - 1].node.value) {
      facts[insertion] = facts[insertion - 1];
      --insertion;
    }
    facts[insertion] = current;
  }
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

  zc::Vector<bool> visited;
  visited.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < visited.size(); ++index) { visited[index] = false; }
  uint64_t nextScopeIndex = 1;
  uint32_t preorderOrdinal = 0;
  zc::Maybe<BinderInvariantFact> rejected;
  const OwnerCursor moduleOwner{OwnerKind::Module, input.module(), identity::DefId(),
                                identity::ImplId()};

  auto visit = [&](auto&& self, ast::NodeId node, ScopeId enclosingScope,
                   OwnerCursor owner) -> void {
    if (rejected != zc::none) { return; }
    const uint32_t ordinal = preorderOrdinal++;
    if (!node || node.value > tree.nodeCount() || visited[node.value] || !tree.contains(node)) {
      rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
      return;
    }
    visited[node.value] = true;

    ScopeId currentScope = enclosingScope;
    OwnerCursor currentOwner = owner;
    ZC_IF_SOME(kind, scopeKind(tree.node(node).kind)) {
      auto index = checkedScopeIndex(nextScopeIndex);
      auto span = input.parsedModule().spanFor(tree.node(node).range);
      if (index == zc::none || span == zc::none) {
        rejected = failure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
        return;
      }
      if (kind == ScopeKind::Function || kind == ScopeKind::Closure ||
          kind == ScopeKind::TypeBody) {
        auto definition = input.definitions().definitionAt(node);
        if (definition == zc::none) {
          rejected = failure(input, BinderInvariantKind::MissingRequiredResolution, ordinal);
          return;
        }
        ZC_IF_SOME(value, definition) {
          currentOwner =
              OwnerCursor{OwnerKind::Definition, input.module(), value, identity::ImplId()};
        }
      } else if (kind == ScopeKind::ImplBody) {
        auto implementation = input.definitions().implAt(node);
        if (implementation == zc::none) {
          rejected = failure(input, BinderInvariantKind::MissingRequiredResolution, ordinal);
          return;
        }
        ZC_IF_SOME(value, implementation) {
          currentOwner = OwnerCursor{OwnerKind::Impl, input.module(), identity::DefId(), value};
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
          candidate.scopes.add(ScopeRecord(currentScope, zc::mv(parent), makeOwner(currentOwner),
                                           kind, zc::mv(bindings), zc::mv(spanValue)));
          ++nextScopeIndex;
        }
      }
    }

    candidate.nodeScopes.add(NodeScopeFact{node, currentScope});
    ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) {
      self(self, child, currentScope, currentOwner);
    });
  };
  visit(visit, tree.root(), moduleScope, moduleOwner);

  ZC_IF_SOME(fact, rejected) { return zc::mv(fact); }
  if (candidate.nodeScopes.size() != tree.nodeCount()) {
    return failure(input, BinderInvariantKind::MissingRequiredResolution, preorderOrdinal);
  }
  for (size_t index = 1; index < visited.size(); ++index) {
    if (!visited[index]) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, preorderOrdinal);
    }
  }
  sortNodeScopes(candidate.nodeScopes);
  return candidate;
}

}  // namespace zomlang::compiler::binder
