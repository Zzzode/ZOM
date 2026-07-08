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

#include "zomlang/compiler/binder/import-resolver.h"

#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-flags.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace binder {

using symbol::Scope;
using symbol::ScopeManager;
using symbol::Symbol;
using symbol::SymbolFlags;
using symbol::SymbolTable;

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct ImportResolver::Impl {
  Impl(SymbolTable& symbols, ScopeManager& scopes, const ast::Tree& tree,
       ast::BindingMetadata& metadata, diagnostics::DiagnosticEngine& diags) noexcept
      : symbols(symbols), scopes(scopes), tree(tree), metadata(metadata), diags(diags) {}

  SymbolTable& symbols;
  ScopeManager& scopes;
  const ast::Tree& tree;
  ast::BindingMetadata& metadata;
  diagnostics::DiagnosticEngine& diags;

  /// Module path (joined by "::") -> module scope (non-owning pointer).
  zc::HashMap<zc::StringPtr, Scope*> moduleScopes;  // non-owning

  /// Modules currently being resolved (for circular import detection).
  zc::HashSet<zc::StringPtr> resolvingModules;

  // ------------------------------------------------------------------
  // Helpers
  // ------------------------------------------------------------------

  /// Build a "::"-joined module path string from a ModulePath node's segments.
  zc::String buildModulePathString(ast::NodeId pathNode) const {
    const ast::Node& pathNodeRef = tree.node(pathNode);
    ZC_IREQUIRE(pathNodeRef.kind == ast::SyntaxKind::ModulePath, "expected ModulePath node");

    ast::IdentList segments;
    segments.first = pathNodeRef.payload.words[ast::kModulePathSegmentsFirstWord];
    segments.size = pathNodeRef.payload.words[ast::kModulePathSegmentsSizeWord];

    zc::String result;
    auto segmentIds = tree.identList(segments);
    bool first = true;
    for (ast::IdentId segId : segmentIds) {
      if (!first) { result = zc::str(result, "::"_zc); }
      zc::StringPtr seg = tree.ident(segId);
      result = zc::str(result, seg);
      first = false;
    }
    return result;
  }

  /// Return the last segment name from a ModulePath node.
  zc::StringPtr lastSegmentName(ast::NodeId pathNode) const {
    const ast::Node& pathNodeRef = tree.node(pathNode);
    ast::IdentList segments;
    segments.first = pathNodeRef.payload.words[ast::kModulePathSegmentsFirstWord];
    segments.size = pathNodeRef.payload.words[ast::kModulePathSegmentsSizeWord];

    auto segmentIds = tree.identList(segments);
    if (segmentIds.size() == 0) return zc::StringPtr();
    return tree.ident(segmentIds[segmentIds.size() - 1]);
  }

  /// Return the Scope that should receive newly imported symbols.
  Scope& getTargetScope() {
    auto maybeScope = scopes.getCurrentScope();
    ZC_IF_SOME(s, maybeScope) { return const_cast<Scope&>(s); }
    auto global = scopes.getGlobalScopeMutable();
    ZC_IF_SOME(g, global) { return g; }
    ZC_UNREACHABLE;
  }

  /// Look up (or lazily create) the module scope for the given path string.
  zc::Maybe<Scope&> findOrCreateModuleScope(zc::StringPtr modulePath) {
    auto existing = moduleScopes.find(modulePath);
    ZC_IF_SOME(scopePtr, existing) { return *scopePtr; }

    // Lazily create a module scope under the global scope.
    auto global = scopes.getGlobalScopeMutable();
    ZC_IF_SOME(g, global) {
      Scope& modScope = scopes.createScope(Scope::Kind::Module, modulePath, g);
      moduleScopes.insert(modulePath, &modScope);
      return modScope;
    }
    return zc::none;
  }

  // ------------------------------------------------------------------
  // Import resolution
  // ------------------------------------------------------------------

  void resolveImportDecl(ast::NodeId importNode) {
    const ast::Node& importRef = tree.node(importNode);
    ZC_IREQUIRE(importRef.kind == ast::SyntaxKind::ImportDeclaration, "expected ImportDeclaration");

    // Extract the module path node.
    ast::NodeId pathId(importRef.payload.words[ast::kImportDeclarationPathWord]);
    if (!pathId) return;

    zc::String modulePathStr = buildModulePathString(pathId);
    zc::StringPtr modulePathPtr = modulePathStr.asPtr();

    // Circular import detection.
    if (resolvingModules.contains(modulePathPtr)) {
      diags.diagnose<diagnostics::DiagID::CircularImport>(importRef.range.getStart(),
                                                          modulePathPtr);
      return;
    }

    auto& insertedMod = resolvingModules.insert(modulePathPtr);

    // Resolve (or create) the module scope.
    auto moduleScope = findOrCreateModuleScope(modulePathPtr);
    ZC_IF_SOME(modScope, moduleScope) {
      Scope& target = getTargetScope();

      // Extract optional alias (for bare import with alias: `import foo as bar`).
      ast::IdentId aliasId(importRef.payload.words[ast::kImportDeclarationAliasWord]);

      // Extract specifier list.
      ast::NodeList specList;
      specList.first = importRef.payload.words[ast::kImportDeclarationSpecifiersFirstWord];
      specList.size = importRef.payload.words[ast::kImportDeclarationSpecifiersSizeWord];

      if (specList.size == 0) {
        // Bare import: `import foo` or `import foo as bar`.
        zc::StringPtr localName = aliasId ? tree.ident(aliasId) : lastSegmentName(pathId);

        bindModuleImport(localName, modScope, target, importNode);
      } else {
        // Named imports: `import { bar, baz } from foo`.
        auto specNodes = tree.list(specList);
        for (ast::NodeId specId : specNodes) {
          resolveImportSpecifier(specId, modScope, target, importNode);
        }
      }
    }
    else {
      diags.diagnose<diagnostics::DiagID::ImportModuleNotFound>(importRef.range.getStart(),
                                                                modulePathPtr);
    }

    resolvingModules.erase(insertedMod);
  }

  void resolveImportSpecifier(ast::NodeId specNode, Scope& moduleScope, Scope& targetScope,
                              ast::NodeId importNode) {
    const ast::Node& specRef = tree.node(specNode);
    ZC_IREQUIRE(specRef.kind == ast::SyntaxKind::ImportSpecifier, "expected ImportSpecifier");

    ast::IdentId nameId(specRef.payload.words[ast::kImportSpecifierNameWord]);
    ast::IdentId aliasId(specRef.payload.words[ast::kImportSpecifierAliasWord]);

    zc::StringPtr originalName = tree.ident(nameId);
    zc::StringPtr localName = aliasId ? tree.ident(aliasId) : originalName;

    bindImportedSymbol(originalName, localName, moduleScope, targetScope, specNode);
  }

  void bindModuleImport(zc::StringPtr localName, Scope& moduleScope, Scope& targetScope,
                        ast::NodeId importNode) {
    // Check for duplicate import.
    if (targetScope.hasSymbol(localName)) {
      diags.diagnose<diagnostics::DiagID::DuplicateIdentifier>(
          tree.node(importNode).range.getStart(), localName);
      return;
    }

    // Create a variable symbol that refers to the module scope.
    auto& modSymbol = symbols.createVariable(localName, targetScope);
    modSymbol.addFlag(SymbolFlags::Module | SymbolFlags::Export | SymbolFlags::Public);
    modSymbol.setScope(moduleScope);

    // Record the symbol association in metadata.
    metadata.setSymbol(importNode, modSymbol.getId());
  }

  void bindImportedSymbol(zc::StringPtr originalName, zc::StringPtr localName, Scope& moduleScope,
                          Scope& targetScope, ast::NodeId specNode) {
    // Check for duplicate import in target scope.
    if (targetScope.hasSymbol(localName)) {
      diags.diagnose<diagnostics::DiagID::DuplicateIdentifier>(tree.node(specNode).range.getStart(),
                                                               localName);
      return;
    }

    // Look up the symbol in the module scope (locally first, then recursively).
    auto found = moduleScope.lookupSymbolLocally(originalName);
    ZC_IF_SOME(sym, found) {
      bindAliasSymbol(sym, localName, targetScope, specNode);
      return;
    }

    found = moduleScope.lookupSymbolRecursively(originalName);
    ZC_IF_SOME(sym, found) {
      bindAliasSymbol(sym, localName, targetScope, specNode);
      return;
    }

    // Symbol not found in the module.
    diags.diagnose<diagnostics::DiagID::ImportMemberNotFound>(tree.node(specNode).range.getStart(),
                                                              moduleScope.getName(), originalName);
    metadata.setIsUnresolved(specNode, true);
  }

  void bindAliasSymbol(const Symbol& sourceSymbol, zc::StringPtr localName, Scope& targetScope,
                       ast::NodeId specNode) {
    // Create an alias variable symbol in the target scope.
    // Note: we do NOT copy the type here because VariableSymbol::setType takes
    // ownership of the TypeSymbol, and we cannot share ownership with the source.
    // The type checker will resolve the type through the symbol's metadata linkage.
    auto& importSym = symbols.createVariable(localName, targetScope);
    importSym.addFlag(SymbolFlags::Export | SymbolFlags::Public);

    metadata.setSymbol(specNode, importSym.getId());
  }

  // ------------------------------------------------------------------
  // Export / re-export resolution
  // ------------------------------------------------------------------

  void resolveExportDecl(ast::NodeId exportNode) {
    const ast::Node& exportRef = tree.node(exportNode);
    ZC_IREQUIRE(exportRef.kind == ast::SyntaxKind::ExportDeclaration, "expected ExportDeclaration");

    ast::NodeId pathId(exportRef.payload.words[ast::kExportDeclarationPathWord]);

    ast::NodeList specList;
    specList.first = exportRef.payload.words[ast::kExportDeclarationSpecifiersFirstWord];
    specList.size = exportRef.payload.words[ast::kExportDeclarationSpecifiersSizeWord];

    Scope& target = getTargetScope();

    if (pathId) {
      // Re-export from another module: `export { bar } from foo`.
      zc::String modulePathStr = buildModulePathString(pathId);
      zc::StringPtr modulePathPtr = modulePathStr.asPtr();

      if (resolvingModules.contains(modulePathPtr)) {
        diags.diagnose<diagnostics::DiagID::CircularReexport>(exportRef.range.getStart(),
                                                              modulePathPtr);
        return;
      }

      auto& insertedMod2 = resolvingModules.insert(modulePathPtr);

      auto moduleScope = findOrCreateModuleScope(modulePathPtr);
      ZC_IF_SOME(modScope, moduleScope) {
        auto specNodes = tree.list(specList);
        for (ast::NodeId specId : specNodes) {
          resolveReexportSpecifier(specId, modScope, target, exportNode);
        }
      }
      else {
        diags.diagnose<diagnostics::DiagID::ReexportModuleNotFound>(exportRef.range.getStart(),
                                                                    modulePathPtr);
      }

      resolvingModules.erase(insertedMod2);
    } else {
      // Local re-export: `export { bar }`.
      auto specNodes = tree.list(specList);
      for (ast::NodeId specId : specNodes) {
        resolveLocalExportSpecifier(specId, target, exportNode);
      }
    }
  }

  void resolveReexportSpecifier(ast::NodeId specNode, Scope& moduleScope, Scope& targetScope,
                                ast::NodeId exportNode) {
    const ast::Node& specRef = tree.node(specNode);
    ZC_IREQUIRE(specRef.kind == ast::SyntaxKind::ExportSpecifier, "expected ExportSpecifier");

    ast::IdentId nameId(specRef.payload.words[ast::kExportSpecifierNameWord]);
    ast::IdentId aliasId(specRef.payload.words[ast::kExportSpecifierAliasWord]);

    zc::StringPtr originalName = tree.ident(nameId);
    zc::StringPtr exportName = aliasId ? tree.ident(aliasId) : originalName;

    // Look up in the source module scope.
    auto found = moduleScope.lookupSymbolLocally(originalName);
    ZC_IF_SOME(sym, found) {
      bindReexportSymbol(sym, exportName, targetScope, specNode);
      return;
    }

    found = moduleScope.lookupSymbolRecursively(originalName);
    ZC_IF_SOME(sym, found) {
      bindReexportSymbol(sym, exportName, targetScope, specNode);
      return;
    }

    diags.diagnose<diagnostics::DiagID::ReexportMemberNotFound>(
        specRef.range.getStart(), moduleScope.getName(), originalName);
    metadata.setIsUnresolved(specNode, true);
  }

  void bindReexportSymbol(const Symbol& sourceSymbol, zc::StringPtr exportName, Scope& targetScope,
                          ast::NodeId specNode) {
    metadata.setIsReexport(specNode, true);

    if (targetScope.hasSymbol(exportName)) {
      diags.diagnose<diagnostics::DiagID::DuplicateIdentifier>(tree.node(specNode).range.getStart(),
                                                               exportName);
      return;
    }

    // Create a re-export alias symbol. Type is not copied (see bindAliasSymbol note).
    auto& reexportSym = symbols.createVariable(exportName, targetScope);
    reexportSym.addFlag(SymbolFlags::Export | SymbolFlags::Public);

    metadata.setSymbol(specNode, reexportSym.getId());
  }

  void resolveLocalExportSpecifier(ast::NodeId specNode, Scope& targetScope,
                                   ast::NodeId exportNode) {
    const ast::Node& specRef = tree.node(specNode);
    ZC_IREQUIRE(specRef.kind == ast::SyntaxKind::ExportSpecifier, "expected ExportSpecifier");

    ast::IdentId nameId(specRef.payload.words[ast::kExportSpecifierNameWord]);
    ast::IdentId aliasId(specRef.payload.words[ast::kExportSpecifierAliasWord]);

    zc::StringPtr originalName = tree.ident(nameId);
    zc::StringPtr exportName = aliasId ? tree.ident(aliasId) : originalName;

    // Look up locally via SymbolTable (returns non-const reference for modification).
    auto found = symbols.lookup(originalName, targetScope);
    ZC_IF_SOME(sym, found) {
      // Mark the source symbol as exported.
      sym.addFlag(SymbolFlags::Export);

      if (exportName != originalName) {
        // Create an alias symbol under the export name.
        if (targetScope.hasSymbol(exportName)) {
          diags.diagnose<diagnostics::DiagID::DuplicateIdentifier>(specRef.range.getStart(),
                                                                   exportName);
          return;
        }
        auto& aliasSym = symbols.createVariable(exportName, targetScope);
        aliasSym.addFlag(SymbolFlags::Export | SymbolFlags::Public);
        metadata.setSymbol(specNode, aliasSym.getId());
      } else {
        metadata.setSymbol(specNode, sym.getId());
      }
      metadata.setIsReexport(specNode, true);
      return;
    }

    // Try recursive lookup via SymbolTable.
    found = symbols.lookupRecursive(originalName, targetScope);
    ZC_IF_SOME(sym, found) {
      sym.addFlag(SymbolFlags::Export);
      metadata.setSymbol(specNode, sym.getId());
      metadata.setIsReexport(specNode, true);
      return;
    }

    diags.diagnose<diagnostics::DiagID::UndefinedIdentifier>(specRef.range.getStart(),
                                                             originalName);
    metadata.setIsUnresolved(specNode, true);
  }

  // ------------------------------------------------------------------
  // Tree traversal
  // ------------------------------------------------------------------

  bool resolveAll() {
    metadata.resizeFor(tree);

    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId nodeId, const ast::Node& node) {
      switch (node.kind) {
        case ast::SyntaxKind::ImportDeclaration:
          resolveImportDecl(nodeId);
          break;
        case ast::SyntaxKind::ExportDeclaration:
          resolveExportDecl(nodeId);
          break;
        default:
          break;
      }
    });

    return !diags.hasErrors();
  }
};

// ---------------------------------------------------------------------------
// ImportResolver public interface
// ---------------------------------------------------------------------------

ImportResolver::ImportResolver(SymbolTable& symbols, ScopeManager& scopes, const ast::Tree& tree,
                               ast::BindingMetadata& metadata,
                               diagnostics::DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(symbols, scopes, tree, metadata, diags)) {}

ImportResolver::~ImportResolver() noexcept(false) = default;

bool ImportResolver::resolveImports() { return impl->resolveAll(); }

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
