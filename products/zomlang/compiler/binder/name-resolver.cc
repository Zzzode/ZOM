// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/name-resolver.h"

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"

namespace zomlang {
namespace compiler {
namespace binder {

using symbol::Scope;
using symbol::ScopeManager;
using symbol::Symbol;
using symbol::SymbolId;
using symbol::SymbolTable;

using ast::IdentId;
using ast::Node;
using ast::NodeId;
using ast::NodeList;
using ast::SyntaxKind;
using ast::Tree;

using diagnostics::DiagID;
using diagnostics::DiagnosticEngine;

namespace {

/// Mask used to extract the lower 32 bits of a Scope pointer as its ID.
constexpr uint32_t kScopeIdMask = 0xFFFFFFFF;

/// Sentinel value indicating that no scope is associated with a node.
constexpr uint32_t kInvalidScopeId = 0;

/// \brief Check if a name refers to a built-in primitive type.
///
/// Primitive type names are not user-defined symbols, so the NameResolver
/// must not report them as "undefined identifier" when they appear in
/// type position (NamedTypeExpr).
bool isPrimitiveTypeName(zc::StringPtr name) {
  // Integer types
  if (name == "i8"_zc || name == "i16"_zc || name == "i32"_zc || name == "i64"_zc) return true;
  if (name == "u8"_zc || name == "u16"_zc || name == "u32"_zc || name == "u64"_zc) return true;
  if (name == "isize"_zc || name == "usize"_zc) return true;
  // Float types
  if (name == "f32"_zc || name == "f64"_zc) return true;
  // Other primitives
  if (name == "bool"_zc || name == "str"_zc || name == "char"_zc) return true;
  if (name == "unit"_zc || name == "void"_zc || name == "never"_zc || name == "any"_zc) return true;
  if (name == "bigint"_zc) return true;
  return false;
}
}  // namespace

// ============================================================================
// Impl
// ============================================================================

struct NameResolver::Impl {
  Impl(SymbolTable& symbols, ScopeManager& scopes, const Tree& tree, ast::BindingMetadata& metadata,
       DiagnosticEngine& diags) noexcept
      : symbols(symbols), scopes(scopes), tree(tree), metadata(metadata), diags(diags) {
    metadata.resizeFor(tree);
    buildScopeIdMap();
  }

  SymbolTable& symbols;
  ScopeManager& scopes;
  const Tree& tree;
  ast::BindingMetadata& metadata;
  DiagnosticEngine& diags;

  /// Maps scope IDs (lower 32 bits of Scope*) back to the actual Scope.
  /// Populated from ScopeManager::getAllScopes() so we can reuse
  /// DeclCollector's scopes.
  zc::HashMap<uint32_t, const Scope*> scopeIdMap;  // non-owning

  /// Current scope stack.  Stores const pointers — we only read from
  /// scopes, never mutate.
  zc::Vector<const Scope*> scopeStack;  // non-owning

  // -----------------------------------------------------------------------
  // Scope ID map construction
  // -----------------------------------------------------------------------

  static uint32_t scopeIdOf(const Scope& s) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s) & kScopeIdMask);
  }

  void buildScopeIdMap() {
    auto allScopes = scopes.getAllScopes();
    for (size_t i = 0; i < allScopes.size(); ++i) {
      const auto& own = allScopes[i];
      if (own) {
        const Scope& s = *own;
        scopeIdMap.insert(scopeIdOf(s), &s);
      }
    }
  }

  // -----------------------------------------------------------------------
  // Scope stack helpers
  // -----------------------------------------------------------------------

  const Scope& currentScope() {
    if (!scopeStack.empty()) return *scopeStack.back();
    auto global = scopes.getGlobalScope();
    ZC_IF_SOME(g, global) { return g; }
    ZC_UNREACHABLE;
  }

  /// Try to push the scope associated with \p node (per BindingMetadata).
  /// Returns true if a scope was pushed (caller must pop later).
  bool pushNodeScope(NodeId node) {
    uint32_t sid = metadata.scope(node);
    if (sid == kInvalidScopeId) return false;

    auto it = scopeIdMap.find(sid);
    ZC_IF_SOME(scopePtr, it) {
      scopeStack.add(scopePtr);
      scopes.pushScope(*scopePtr);
      return true;
    }
    return false;
  }

  void popNodeScope() {
    ZC_REQUIRE(!scopeStack.empty(), "Scope stack underflow in NameResolver");
    scopeStack.removeLast();
    scopes.popScope();
  }

  // -----------------------------------------------------------------------
  // Name extraction helpers
  // -----------------------------------------------------------------------

  zc::StringPtr identName(NodeId node) const {
    const Node& n = tree.node(node);
    ZC_IREQUIRE(n.kind == SyntaxKind::IdentExpr, "expected IdentExpr for name extraction");
    return tree.ident(IdentId(n.payload.words[ast::kIdentExprNameWord]));
  }

  zc::StringPtr memberProperty(NodeId node) const {
    const Node& n = tree.node(node);
    ZC_IREQUIRE(n.kind == SyntaxKind::MemberExpression, "expected MemberExpression");
    return tree.ident(IdentId(n.payload.words[ast::kMemberExpressionPropertyWord]));
  }

  // -----------------------------------------------------------------------
  // Metadata helpers
  // -----------------------------------------------------------------------

  void bindSymbol(NodeId node, const Symbol& sym) {
    metadata.setSymbol(node, sym.getId());
    metadata.setIsUnresolved(node, false);
  }

  void markUnresolved(NodeId node) {
    metadata.setIsUnresolved(node, true);

    const Node& n = tree.node(node);
    zc::StringPtr name = extractNameForDiag(node);
    diags.diagnose<DiagID::UndefinedIdentifier>(n.range.getStart(), name);
  }

  zc::StringPtr extractNameForDiag(NodeId node) const {
    const Node& n = tree.node(node);
    switch (n.kind) {
      case SyntaxKind::IdentExpr:
        return tree.ident(IdentId(n.payload.words[ast::kIdentExprNameWord]));
      case SyntaxKind::NamedTypeExpr: {
        NodeId pathId(n.payload.words[ast::kNamedTypeExprPathWord]);
        if (tree.contains(pathId)) {
          const Node& path = tree.node(pathId);
          if (path.kind == SyntaxKind::IdentExpr)
            return tree.ident(IdentId(path.payload.words[ast::kIdentExprNameWord]));
        }
        break;
      }
      case SyntaxKind::MemberExpression:
        return tree.ident(IdentId(n.payload.words[ast::kMemberExpressionPropertyWord]));
      default:
        break;
    }
    return "<unknown>"_zc;
  }

  void reportTypeNamespaceMismatch(NodeId node, zc::StringPtr expectedCtx) {
    const Node& n = tree.node(node);
    zc::StringPtr name = extractNameForDiag(node);
    diags.diagnose<DiagID::SemanticError>(
        n.range.getStart(),
        zc::str("Symbol '"_zc, name, "' cannot be used in "_zc, expectedCtx, " context"_zc));
  }

  // -----------------------------------------------------------------------
  // Name lookup
  // -----------------------------------------------------------------------

  /// Look up a plain name from the current scope chain (lexical scoping).
  zc::Maybe<const Symbol&> lookupName(zc::StringPtr name) {
    const Scope& scope = currentScope();
    return symbols.lookupRecursive(name, scope);
  }

  /// Look up a member of a base symbol (field / method / nested type).
  zc::Maybe<const Symbol&> lookupMember(const Symbol& base, zc::StringPtr name) {
    // 1. Try the base symbol's own scope (class / interface / namespace).
    auto baseScope = base.getScope();
    ZC_IF_SOME(scope, baseScope) {
      auto member = scope.lookupSymbolLocally(name);
      ZC_IF_SOME(m, member) { return m; }
    }

    // 2. Walk the type hierarchy for inherited members.
    auto baseType = base.getType();
    ZC_IF_SOME(typeSym, baseType) {
      auto supertypes = typeSym.getSupertypes();
      for (size_t i = 0; i < supertypes.size(); ++i) {
        auto super = supertypes[i];
        ZC_IF_SOME(superType, super) {
          auto superScope = superType.getScope();
          ZC_IF_SOME(sScope, superScope) {
            auto member = sScope.lookupSymbolLocally(name);
            ZC_IF_SOME(m, member) { return m; }
          }
        }
      }
    }

    // 3. If the base itself is a type symbol, try its scope recursively.
    if (base.isTypeSymbol()) {
      ZC_IF_SOME(scope, baseScope) {
        auto member = scope.lookupSymbolRecursively(name);
        ZC_IF_SOME(m, member) { return m; }
      }
    }

    return zc::none;
  }

  // -----------------------------------------------------------------------
  // Symbol-by-ID resolution (used after child node resolution).
  // -----------------------------------------------------------------------

  zc::Maybe<const Symbol&> findSymbolById(SymbolId id) {
    // Walk the scope stack from innermost outward.
    for (size_t idx = scopeStack.size(); idx > 0; --idx) {
      const Scope& s = *scopeStack[idx - 1];
      auto allSyms = symbols.getSymbolsInScope(s);
      for (size_t i = 0; i < allSyms.size(); ++i) {
        auto sym = allSyms[i];
        ZC_IF_SOME(s, sym) {
          if (s.getId() == id) { return s; }
        }
      }
    }
    return zc::none;
  }

  // -----------------------------------------------------------------------
  // Per-node resolution — one method per name-referencing AST kind
  // -----------------------------------------------------------------------

  void resolveIdentExpr(NodeId node) {
    zc::StringPtr name = identName(node);
    ZC_IF_SOME(sym, lookupName(name)) {
      // Note: We do NOT report a namespace mismatch here. Type symbols can
      // legitimately appear in value position (e.g., `new MyClass()`), and
      // the BodyChecker performs more precise context-dependent validation.

      bindSymbol(node, sym);

      // Shadowing detection.
      const Scope& cur = currentScope();
      auto parent = cur.getParent();
      ZC_IF_SOME(p, parent) {
        auto outer = p.lookupSymbolLocally(name);
        ZC_IF_SOME(outerSym, outer) {
          if (&outerSym != &sym) { metadata.setShadowOf(node, ast::NodeId()); }
        }
      }
      return;
    }

    markUnresolved(node);
  }

  void resolveMemberExpression(NodeId node) {
    const Node& n = tree.node(node);
    NodeId objectId(n.payload.words[ast::kMemberExpressionObjectWord]);

    // Resolve the base object first.
    if (tree.contains(objectId)) resolveNode(objectId);

    // Find the base symbol from the resolved object.
    zc::Maybe<const Symbol&> baseSym = zc::none;
    if (tree.contains(objectId)) {
      SymbolId objSymId = metadata.symbol(objectId);
      if (objSymId.isValid()) baseSym = findSymbolById(objSymId);
    }

    ZC_IF_SOME(base, baseSym) {
      zc::StringPtr propName = memberProperty(node);
      ZC_IF_SOME(member, lookupMember(base, propName)) {
        bindSymbol(node, member);
        return;
      }
      metadata.setIsDeferredMember(node, true);
      metadata.setIsUnresolved(node, false);
      return;
    }

    metadata.setIsDeferredMember(node, true);
    metadata.setIsUnresolved(node, false);
  }

  void resolveCallExpression(NodeId node) {
    const Node& n = tree.node(node);

    // Resolve callee.
    NodeId calleeId(n.payload.words[ast::kCallExpressionCalleeWord]);
    if (tree.contains(calleeId)) resolveNode(calleeId);

    // Resolve type arguments.
    NodeList typeArgs;
    typeArgs.first = n.payload.words[ast::kCallExpressionTypeArgsFirstWord];
    typeArgs.size = n.payload.words[ast::kCallExpressionTypeArgsSizeWord];
    for (NodeId arg : tree.list(typeArgs)) resolveNode(arg);

    // Resolve regular arguments.
    NodeList args;
    args.first = n.payload.words[ast::kCallExpressionArgsFirstWord];
    args.size = n.payload.words[ast::kCallExpressionArgsSizeWord];
    for (NodeId arg : tree.list(args)) resolveNode(arg);
  }

  void resolveNamedTypeExpr(NodeId node) {
    const Node& n = tree.node(node);
    NodeId pathId(n.payload.words[ast::kNamedTypeExprPathWord]);

    // Resolve type arguments first.
    NodeList typeArgs;
    typeArgs.first = n.payload.words[ast::kNamedTypeExprArgsFirstWord];
    typeArgs.size = n.payload.words[ast::kNamedTypeExprArgsSizeWord];
    for (NodeId arg : tree.list(typeArgs)) resolveNode(arg);

    if (!tree.contains(pathId)) {
      markUnresolved(node);
      return;
    }

    const Node& pathNode = tree.node(pathId);

    if (pathNode.kind == SyntaxKind::IdentExpr) {
      // Simple type name: e.g. `MyClass` or `i32`
      zc::StringPtr name = identName(pathId);

      // Primitive type names (i32, bool, str, etc.) are built-in, not symbols.
      // Don't mark them as unresolved.
      if (isPrimitiveTypeName(name)) { return; }

      // Type modifier wrappers such as `mut T` and `const T` are represented
      // as synthetic NamedTypeExpr nodes by the recursive-descent parser and
      // unit-test builders. Their type arguments were resolved above; the
      // wrapper name itself is not a user-defined type symbol.
      if ((name == "mut"_zc || name == "const"_zc) && typeArgs.size > 0) { return; }

      ZC_IF_SOME(sym, lookupName(name)) {
        // Note: We do NOT report a namespace mismatch here. Value symbols can
        // legitimately appear in type position in some contexts, and the
        // BodyChecker performs more precise context-dependent validation.

        bindSymbol(node, sym);
        bindSymbol(pathId, sym);
        return;
      }

      markUnresolved(node);
      return;
    }

    if (pathNode.kind == SyntaxKind::MemberExpression) {
      // Qualified type name: e.g. `std::vector`
      resolveNode(pathId);
      SymbolId memberSymId = metadata.symbol(pathId);
      if (memberSymId.isValid()) {
        metadata.setSymbol(node, memberSymId);
        metadata.setIsUnresolved(node, metadata.isUnresolved(pathId));
      } else {
        markUnresolved(node);
      }
      return;
    }

    // Fallback.
    resolveNode(pathId);
  }

  void resolveNewExpression(NodeId node) {
    const Node& n = tree.node(node);

    NodeId calleeId(n.payload.words[ast::kNewExpressionCalleeWord]);
    if (tree.contains(calleeId)) resolveNode(calleeId);

    NodeList typeArgs;
    typeArgs.first = n.payload.words[ast::kNewExpressionTypeArgsFirstWord];
    typeArgs.size = n.payload.words[ast::kNewExpressionTypeArgsSizeWord];
    for (NodeId arg : tree.list(typeArgs)) resolveNode(arg);

    NodeList args;
    args.first = n.payload.words[ast::kNewExpressionArgsFirstWord];
    args.size = n.payload.words[ast::kNewExpressionArgsSizeWord];
    for (NodeId arg : tree.list(args)) resolveNode(arg);
  }

  void resolveThisExpr(NodeId node) {
    ZC_IF_SOME(sym, lookupName(symbol::INTERNAL_SYMBOL_NAME_THIS)) {
      bindSymbol(node, sym);
      return;
    }
    markUnresolved(node);
  }

  void resolveSuperExpr(NodeId node) {
    // Walk up scope stack to find enclosing class scope.
    for (size_t idx = scopeStack.size(); idx > 0; --idx) {
      const Scope& s = *scopeStack[idx - 1];
      if (s.getKind() == Scope::Kind::Class) {
        auto thisSym = s.lookupSymbolLocally(symbol::INTERNAL_SYMBOL_NAME_THIS);
        ZC_IF_SOME(ts, thisSym) {
          bindSymbol(node, ts);
          return;
        }
        break;
      }
    }
    markUnresolved(node);
  }

  void resolveIndexExpression(NodeId node) {
    const Node& n = tree.node(node);
    NodeId objectId(n.payload.words[ast::kIndexExpressionObjectWord]);
    NodeId indexId(n.payload.words[ast::kIndexExpressionIndexWord]);
    if (tree.contains(objectId)) resolveNode(objectId);
    if (tree.contains(indexId)) resolveNode(indexId);
  }

  void resolveCastExpression(NodeId node) {
    const Node& n = tree.node(node);
    NodeId exprId(n.payload.words[ast::kCastExpressionExprWord]);
    NodeId tyId(n.payload.words[ast::kCastExpressionTyWord]);
    if (tree.contains(exprId)) resolveNode(exprId);
    if (tree.contains(tyId)) resolveNode(tyId);
  }

  void resolveIsExpression(NodeId node) {
    const Node& n = tree.node(node);
    NodeId exprId(n.payload.words[ast::kIsExpressionExprWord]);
    NodeId tyId(n.payload.words[ast::kIsExpressionTyWord]);
    if (tree.contains(exprId)) resolveNode(exprId);
    if (tree.contains(tyId)) resolveNode(tyId);
  }

  void resolveAssignmentExpr(NodeId node) {
    const Node& n = tree.node(node);
    NodeId lhsId(n.payload.words[ast::kAssignmentExprLhsWord]);
    NodeId rhsId(n.payload.words[ast::kAssignmentExprRhsWord]);
    if (tree.contains(lhsId)) resolveNode(lhsId);
    if (tree.contains(rhsId)) resolveNode(rhsId);
  }

  void resolvePositionalStructCtorExpr(NodeId node) {
    const Node& n = tree.node(node);
    NodeId structPathId(n.payload.words[ast::kPositionalStructCtorExprStructPathWord]);
    if (tree.contains(structPathId)) resolveNode(structPathId);

    NodeList args;
    args.first = n.payload.words[ast::kPositionalStructCtorExprArgsFirstWord];
    args.size = n.payload.words[ast::kPositionalStructCtorExprArgsSizeWord];
    for (NodeId arg : tree.list(args)) resolveNode(arg);
  }

  void resolveStructLiteralExpr(NodeId node) {
    const Node& n = tree.node(node);
    NodeId tyId(n.payload.words[ast::kStructLiteralExprTyWord]);
    if (tree.contains(tyId)) resolveNode(tyId);

    NodeList props;
    props.first = n.payload.words[ast::kStructLiteralExprPropertiesFirstWord];
    props.size = n.payload.words[ast::kStructLiteralExprPropertiesSizeWord];
    for (NodeId prop : tree.list(props)) resolveNode(prop);
  }

  // -----------------------------------------------------------------------
  // Generic child traversal
  // -----------------------------------------------------------------------

  void resolveChildren(NodeId node) {
    if (!node || !tree.contains(node)) return;
    const Node& n = tree.node(node);
    ast::visitChildNodeIds(tree, n, [this](NodeId child) { resolveNode(child); });
  }

  // -----------------------------------------------------------------------
  // Main dispatch with automatic scope management
  // -----------------------------------------------------------------------

  void resolveNode(NodeId node) {
    if (!node || !tree.contains(node)) return;

    const Node& n = tree.node(node);

    // Automatically push the scope that DeclCollector associated with
    // this node (if any).  This ensures we search the correct scope
    // chain when resolving names inside the node.
    bool pushedScope = pushNodeScope(node);

    switch (n.kind) {
      // --- Name-referencing nodes ---
      case SyntaxKind::IdentExpr:
        resolveIdentExpr(node);
        break;
      case SyntaxKind::MemberExpression:
        resolveMemberExpression(node);
        break;
      case SyntaxKind::CallExpression:
        resolveCallExpression(node);
        break;
      case SyntaxKind::NamedTypeExpr:
        resolveNamedTypeExpr(node);
        break;
      case SyntaxKind::NewExpression:
        resolveNewExpression(node);
        break;
      case SyntaxKind::ThisExpr:
        resolveThisExpr(node);
        break;
      case SyntaxKind::SuperExpr:
        resolveSuperExpr(node);
        break;
      case SyntaxKind::IndexExpression:
        resolveIndexExpression(node);
        break;
      case SyntaxKind::CastExpression:
        resolveCastExpression(node);
        break;
      case SyntaxKind::IsExpression:
        resolveIsExpression(node);
        break;
      case SyntaxKind::AssignmentExpr:
        resolveAssignmentExpr(node);
        break;
      case SyntaxKind::PositionalStructCtorExpr:
        resolvePositionalStructCtorExpr(node);
        break;
      case SyntaxKind::StructLiteralExpr:
        resolveStructLiteralExpr(node);
        break;

      // --- Everything else: just recurse into children ---
      default:
        resolveChildren(node);
        break;
    }

    // Pop scope if we pushed one.
    if (pushedScope) popNodeScope();
  }

  // -----------------------------------------------------------------------
  // Entry point
  // -----------------------------------------------------------------------

  bool resolve() {
    ZC_IREQUIRE(tree.contains(tree.root()), "cannot resolve names in tree without root");

    // Push global scope.
    auto global = scopes.getGlobalScope();
    ZC_IF_SOME(g, global) {
      scopeStack.add(&g);
      scopes.pushScope(g);
    }

    resolveNode(tree.root());

    // Pop global scope.
    if (!scopeStack.empty()) {
      scopeStack.removeLast();
      scopes.popScope();
    }

    return !diags.hasErrors();
  }
};

// ============================================================================
// NameResolver public API
// ============================================================================

NameResolver::NameResolver(SymbolTable& symbols, ScopeManager& scopes, const Tree& tree,
                           ast::BindingMetadata& metadata, DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(symbols, scopes, tree, metadata, diags)) {}

NameResolver::~NameResolver() noexcept(false) = default;

bool NameResolver::resolve() { return impl->resolve(); }

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
