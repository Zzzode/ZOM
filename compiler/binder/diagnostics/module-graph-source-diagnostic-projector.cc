// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/module-graph-source-diagnostic-projector.h"

#include <climits>

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"
#include "zc/core/string.h"

namespace zomlang::compiler::binder {
namespace {

zc::Vector<uint32_t> eventPath(uint32_t ordinal) {
  zc::Vector<uint32_t> path;
  path.add(ordinal);
  return path;
}

zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> highlight(const identity::SourceSpan& span) {
  zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
  related.add(
      diagnostics::SemanticDiagnosticRelatedSite{diagnostics::DiagnosticSecondaryRole::Highlight,
                                                 zc::none, span.clone(), zc::Vector<zc::String>()});
  return related;
}

zc::Maybe<zc::String> renderModulePath(zc::ArrayPtr<const identity::ModulePathSegment> segments) {
  if (segments.size() == 0) { return zc::none; }
  zc::String path;
  bool first = true;
  for (const auto& segment : segments) {
    if (!first) { path = zc::str(path, "::"_zc); }
    path = zc::str(path, segment.text());
    first = false;
  }
  return path;
}

bool appendProjected(diagnostics::SemanticDiagnosticFactBatch& batch,
                     diagnostics::SemanticDiagnosticProjectionResult&& projected) {
  if (!projected.is<diagnostics::SemanticDiagnosticFactProjection>()) { return false; }
  auto value = zc::mv(projected).get<diagnostics::SemanticDiagnosticFactProjection>();
  batch.facts.add(zc::mv(value.fact));
  for (auto& provenance : value.provenance) { batch.provenance.add(zc::mv(provenance)); }
  return true;
}

bool isImportDependency(identity::ModuleDependencyKind kind) {
  return kind == identity::ModuleDependencyKind::Import ||
         kind == identity::ModuleDependencyKind::ModuleAlias;
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

}  // namespace

diagnostics::SemanticDiagnosticBatchProjectionResult projectModuleGraphSourceFailure(
    const VerifiedParsedModule& parsed, const ModuleGraphSourceFailure& failure) {
  if (!parsed.source().sameAs(failure.source())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  }
  const auto& tree = parsed.tree();
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  auto declaration = resolveSyntaxPath(tree, failure.declaredNamePath());
  if (declaration == zc::none) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  const ast::NodeId declarationNode = ZC_ASSERT_NONNULL(declaration);
  const ast::NodeId rootDeclaration(
      tree.node(tree.root()).payload.words[ast::kSourceFileModuleWord]);
  if (declarationNode != rootDeclaration || !tree.contains(declarationNode) ||
      tree.node(declarationNode).kind != ast::SyntaxKind::ModuleDeclaration) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  const auto& syntax = tree.node(declarationNode);
  if (static_cast<ast::ModuleDeclarationForm>(
          syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
      ast::ModuleDeclarationForm::Alias) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  auto ordinal = schemaPreorderOrdinal(tree, declarationNode);
  if (ordinal == zc::none || ZC_ASSERT_NONNULL(ordinal) != failure.schemaPreorderOrdinal()) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  auto segment = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  if (segment == zc::none) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  zc::Vector<identity::ModulePathSegment> argumentPath;
  argumentPath.add(zc::mv(ZC_ASSERT_NONNULL(segment)));
  auto expectedArgument = diagnostics::ModuleRootArgument::fromCanonicalPath(zc::mv(argumentPath));
  if (expectedArgument == zc::none || ZC_ASSERT_NONNULL(expectedArgument) != failure.argument()) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  auto primary = parsed.retainedTokenSpan(declarationNode, 1, ast::SyntaxKind::Identifier);
  auto argument = renderModulePath(failure.argument().path());
  if (primary == zc::none || argument == zc::none ||
      !ZC_ASSERT_NONNULL(primary).belongsTo(failure.source())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  zc::Vector<zc::String> arguments;
  arguments.add(zc::mv(ZC_ASSERT_NONNULL(argument)));
  auto owner = failure.module().encode();
  auto path = eventPath(failure.schemaPreorderOrdinal());
  diagnostics::SemanticDiagnosticFactBatch batch;
  if (!appendProjected(
          batch,
          diagnostics::projectSemanticDiagnosticFact(
              failure.module(), diagnostics::SemanticDiagnosticDomain::ModuleGraph, owner.asPtr(),
              path.asPtr(), diagnostics::DiagID::ToolchainModuleRootReserved, zc::mv(arguments),
              ZC_ASSERT_NONNULL(primary), highlight(ZC_ASSERT_NONNULL(primary))))) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  return batch;
}

diagnostics::SemanticDiagnosticBatchProjectionResult projectModuleDeclarationNameMismatch(
    const VerifiedParsedModule& parsed, const identity::ModuleKey& selectedModule) {
  const auto& tree = parsed.tree();
  if (!parsed.source().belongsTo(selectedModule.crate())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  }
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile ||
      selectedModule.path().size() == 0) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  const ast::NodeId declaration(tree.node(tree.root()).payload.words[ast::kSourceFileModuleWord]);
  if (!tree.contains(declaration) ||
      tree.node(declaration).kind != ast::SyntaxKind::ModuleDeclaration) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  const auto& syntax = tree.node(declaration);
  auto declared = identity::ModulePathSegment::fromSource(
      tree.ident(ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord])));
  auto ordinal = schemaPreorderOrdinal(tree, declaration);
  auto primary = parsed.spanFor(syntax.range);
  if (declared == zc::none || ordinal == zc::none || primary == zc::none ||
      ZC_ASSERT_NONNULL(declared) == selectedModule.path().back()) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  zc::Vector<identity::ModulePathSegment> declaredPath;
  declaredPath.add(ZC_ASSERT_NONNULL(declared).clone());
  if (diagnostics::ModuleRootArgument::fromCanonicalPath(zc::mv(declaredPath)) != zc::none) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str(ZC_ASSERT_NONNULL(declared).text()));
  arguments.add(zc::str(selectedModule.path().back().text()));
  auto owner = selectedModule.encode();
  auto path = eventPath(ZC_ASSERT_NONNULL(ordinal));
  diagnostics::SemanticDiagnosticFactBatch batch;
  if (!appendProjected(
          batch,
          diagnostics::projectSemanticDiagnosticFact(
              selectedModule, diagnostics::SemanticDiagnosticDomain::ModuleGraph, owner.asPtr(),
              path.asPtr(), diagnostics::DiagID::ModuleDeclarationNameMismatch, zc::mv(arguments),
              ZC_ASSERT_NONNULL(primary), highlight(ZC_ASSERT_NONNULL(primary))))) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  return batch;
}

diagnostics::SemanticDiagnosticBatchProjectionResult projectModuleDependencyResolutionFailure(
    const VerifiedParsedModule& parsed, const ModuleDependencyRequest& request, bool ambiguous) {
  const bool importDependency = isImportDependency(request.kind());
  const bool reexportDependency = request.kind() == identity::ModuleDependencyKind::ForeignReexport;
  if (request.isPrelude() || request.syntaxSites().size() == 0 ||
      (!importDependency && !reexportDependency) ||
      !parsed.source().belongsTo(request.key().requester().crate())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  }
  const diagnostics::DiagID code =
      ambiguous ? (importDependency ? diagnostics::DiagID::ImportModuleAmbiguous
                                    : diagnostics::DiagID::ReexportModuleAmbiguous)
                : (importDependency ? diagnostics::DiagID::ImportModuleNotFound
                                    : diagnostics::DiagID::ReexportModuleNotFound);
  auto owner = request.key().encode();
  diagnostics::SemanticDiagnosticFactBatch batch;
  batch.facts.reserve(request.syntaxSites().size());
  batch.provenance.reserve(request.syntaxSites().size() * 2);
  for (const auto& site : request.syntaxSites()) {
    if (!site.span.belongsTo(parsed.source()) || site.span.byteEnd() > parsed.byteLength()) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
    zc::Vector<zc::String> arguments;
    if (!ambiguous) {
      auto pathArgument = renderModulePath(request.normalizedPath());
      if (pathArgument == zc::none) {
        return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
      arguments.add(zc::mv(ZC_ASSERT_NONNULL(pathArgument)));
    }
    auto path = eventPath(site.schemaPreorderOrdinal);
    if (!appendProjected(
            batch, diagnostics::projectSemanticDiagnosticFact(
                       request.key().requester(),
                       diagnostics::SemanticDiagnosticDomain::ModuleGraph, owner.asPtr(),
                       path.asPtr(), code, zc::mv(arguments), site.span, highlight(site.span)))) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
  }
  return batch;
}

}  // namespace zomlang::compiler::binder
