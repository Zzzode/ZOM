// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-graph-diagnostic-adapter.h"

#include <climits>

#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang::compiler::binder {
namespace {

struct VerifiedDiagnosticRange final {
  source::SourceLoc start;
  source::SourceLoc end;
};

zc::Maybe<VerifiedDiagnosticRange> resolveFailureRange(const VerifiedParsedModule& parsedModule,
                                                       const identity::SourceSpan& span) {
  const auto parsedSource = parsedModule.rootSpan();
  if (!span.belongsTo(parsedSource.source()) || span.byteStart() > span.byteEnd() ||
      span.byteEnd() > parsedModule.byteLength() || span.byteEnd() > UINT_MAX) {
    return zc::none;
  }
  auto start = parsedModule.sourceLocFor(span);
  if (start == zc::none) { return zc::none; }
  ZC_IF_SOME(startValue, start) {
    const auto length = static_cast<unsigned>(span.byteEnd() - span.byteStart());
    return VerifiedDiagnosticRange{startValue, startValue.getAdvancedLoc(length)};
  }
  ZC_UNREACHABLE;
}

zc::Maybe<VerifiedDiagnosticRange> resolveFailureRange(const VerifiedParsedModule& parsedModule,
                                                       const ModuleSyntaxDependencySite& site) {
  return resolveFailureRange(parsedModule, site.span);
}

bool isImportDependency(identity::ModuleDependencyKind kind) {
  return kind == identity::ModuleDependencyKind::Import ||
         kind == identity::ModuleDependencyKind::ModuleAlias;
}

zc::Maybe<zc::String> renderModulePath(const ModuleDependencyRequest& request) {
  if (request.normalizedPath().size() == 0) { return zc::none; }
  zc::String path;
  bool first = true;
  for (const auto& segment : request.normalizedPath()) {
    if (!first) { path = zc::str(path, "::"_zc); }
    path = zc::str(path, segment.text());
    first = false;
  }
  return zc::mv(path);
}

zc::Maybe<ast::NodeId> resolveSyntaxPath(const ast::Tree& tree, const LocalSyntaxPath& path) {
  if (!tree.contains(tree.root())) { return zc::none; }
  ast::NodeId current = tree.root();
  for (const uint32_t component : path.components()) {
    uint32_t childIndex = 0;
    zc::Maybe<ast::NodeId> selected;
    ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
      if (childIndex++ != component || selected != zc::none) { return; }
      selected = child;
    });
    if (selected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, selected) {
      if (!tree.contains(value)) { return zc::none; }
      current = value;
    }
  }
  return current;
}

zc::Maybe<uint32_t> schemaPreorderOrdinal(const ast::Tree& tree, ast::NodeId target) {
  uint32_t ordinal = 0;
  zc::Maybe<uint32_t> found;
  bool overflow = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (found != zc::none || overflow) { return; }
    if (node == target) {
      found = ordinal;
      return;
    }
    if (ordinal == UINT_MAX) {
      overflow = true;
      return;
    }
    ++ordinal;
  });
  return overflow ? zc::Maybe<uint32_t>() : found;
}

zc::Maybe<VerifiedDiagnosticRange> resolveToolchainModuleRootRange(
    const VerifiedParsedModule& parsedModule, const ModuleGraphSourceFailure& failure) {
  if (!parsedModule.source().sameAs(failure.source())) { return zc::none; }
  const auto& tree = parsedModule.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  auto declaration = resolveSyntaxPath(tree, failure.declaredNamePath());
  if (declaration == zc::none) { return zc::none; }
  ZC_IF_SOME(declarationNode, declaration) {
    const ast::NodeId rootDeclaration(
        tree.node(tree.root()).payload.words[ast::kSourceFileModuleWord]);
    if (declarationNode != rootDeclaration || !tree.contains(declarationNode) ||
        tree.node(declarationNode).kind != ast::SyntaxKind::ModuleDeclaration) {
      return zc::none;
    }
    const auto& syntax = tree.node(declarationNode);
    if (static_cast<ast::ModuleDeclarationForm>(
            syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
        ast::ModuleDeclarationForm::Alias) {
      return zc::none;
    }
    auto ordinal = schemaPreorderOrdinal(tree, declarationNode);
    if (ordinal == zc::none || ZC_ASSERT_NONNULL(ordinal) != failure.schemaPreorderOrdinal()) {
      return zc::none;
    }
    auto segment = identity::ModulePathSegment::fromSource(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
    if (segment == zc::none) { return zc::none; }
    zc::Vector<identity::ModulePathSegment> argumentPath;
    ZC_IF_SOME(value, segment) { argumentPath.add(zc::mv(value)); }
    auto argument =
        diagnostics::ToolchainModuleRootArgument::fromCanonicalPath(zc::mv(argumentPath));
    if (argument == zc::none || ZC_ASSERT_NONNULL(argument) != failure.argument()) {
      return zc::none;
    }
    auto declaredName =
        parsedModule.retainedTokenSpan(declarationNode, 1, ast::SyntaxKind::Identifier);
    if (declaredName == zc::none || !ZC_ASSERT_NONNULL(declaredName).belongsTo(failure.source())) {
      return zc::none;
    }
    return resolveFailureRange(parsedModule, ZC_ASSERT_NONNULL(declaredName));
  }
  ZC_UNREACHABLE;
}

zc::Maybe<zc::String> renderToolchainModuleRootArgument(
    const diagnostics::ToolchainModuleRootArgument& argument) {
  if (argument.path().size() == 0) { return zc::none; }
  zc::String path;
  bool first = true;
  for (const auto& segment : argument.path()) {
    if (!first) { path = zc::str(path, "::"_zc); }
    path = zc::str(path, segment.text());
    first = false;
  }
  return zc::mv(path);
}

}  // namespace

bool canEmitModuleGraphSourceFailure(const VerifiedParsedModule& parsedModule,
                                     const ModuleGraphSourceFailure& failure) {
  return resolveToolchainModuleRootRange(parsedModule, failure) != zc::none &&
         renderToolchainModuleRootArgument(failure.argument()) != zc::none;
}

bool emitModuleGraphSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                  const VerifiedParsedModule& parsedModule,
                                  const ModuleGraphSourceFailure& failure) {
  if (!canEmitModuleGraphSourceFailure(parsedModule, failure)) { return false; }
  auto range = resolveToolchainModuleRootRange(parsedModule, failure);
  auto path = renderToolchainModuleRootArgument(failure.argument());
  if (range == zc::none || path == zc::none) { return false; }
  ZC_IF_SOME(rangeValue, range) {
    ZC_IF_SOME(pathValue, path) {
      auto diagnostic = diagnostics.diagnose<diagnostics::DiagID::ToolchainModuleRootReserved>(
          rangeValue.start, zc::mv(pathValue));
      diagnostic.addRange(source::CharSourceRange::getCharRange(rangeValue.start, rangeValue.end));
      diagnostic.emit();
      return true;
    }
  }
  ZC_UNREACHABLE;
}

bool emitModuleDependencyResolutionFailure(diagnostics::DiagnosticEngine& diagnostics,
                                           const VerifiedParsedModule& parsedModule,
                                           const ModuleDependencyRequest& request, bool ambiguous) {
  if (request.isPrelude() || request.syntaxSites().size() == 0) { return false; }
  const bool importDependency = isImportDependency(request.kind());
  const bool reexportDependency = request.kind() == identity::ModuleDependencyKind::ForeignReexport;
  if (!importDependency && !reexportDependency) { return false; }
  for (const auto& site : request.syntaxSites()) {
    auto range = resolveFailureRange(parsedModule, site);
    if (range == zc::none) { return false; }
  }
  for (const auto& site : request.syntaxSites()) {
    auto range = resolveFailureRange(parsedModule, site);
    ZC_IF_SOME(rangeValue, range) {
      const auto sourceRange =
          source::CharSourceRange::getCharRange(rangeValue.start, rangeValue.end);
      using diagnostics::DiagID;
      if (ambiguous) {
        if (importDependency) {
          auto diagnostic = diagnostics.diagnose<DiagID::ImportModuleAmbiguous>(rangeValue.start);
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
        } else {
          auto diagnostic = diagnostics.diagnose<DiagID::ReexportModuleAmbiguous>(rangeValue.start);
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
        }
        continue;
      }
      auto path = renderModulePath(request);
      if (path == zc::none) { return false; }
      ZC_IF_SOME(pathValue, path) {
        if (importDependency) {
          auto diagnostic = diagnostics.diagnose<DiagID::ImportModuleNotFound>(rangeValue.start,
                                                                               zc::mv(pathValue));
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
        } else {
          auto diagnostic = diagnostics.diagnose<DiagID::ReexportModuleNotFound>(rangeValue.start,
                                                                                 zc::mv(pathValue));
          diagnostic.addRange(sourceRange);
          diagnostic.emit();
        }
      }
    }
  }
  return true;
}

}  // namespace zomlang::compiler::binder
