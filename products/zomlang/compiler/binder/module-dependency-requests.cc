// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-dependency-requests.h"

#include <cstdint>

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

ModuleResolutionInvariantFact failure() {
  return ModuleResolutionInvariantFact{ModuleResolutionInvariantKind::InvalidRequest, 1};
}

zc::Maybe<zc::Vector<uint32_t>> schemaPreorderOrdinals(const ast::Tree& tree) {
  if (!tree.contains(tree.root()) || tree.nodeCount() > UINT32_MAX) { return zc::none; }

  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& ordinal : ordinals) { ordinal = UINT32_MAX; }

  size_t nextOrdinal = 0;
  bool valid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (!tree.contains(node) || node.value >= ordinals.size() ||
        ordinals[node.value] != UINT32_MAX || nextOrdinal >= tree.nodeCount() ||
        nextOrdinal > UINT32_MAX) {
      valid = false;
      return;
    }
    ordinals[node.value] = static_cast<uint32_t>(nextOrdinal++);
  });
  if (!valid || nextOrdinal != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> normalizedModulePath(const ast::Tree& tree,
                                                                        ast::NodeId path) {
  if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
    return zc::none;
  }
  const auto& syntax = tree.node(path);
  const ast::IdentList segments{syntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                syntax.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (segments.empty() || !tree.contains(segments)) { return zc::none; }

  zc::Vector<identity::ModulePathSegment> normalized(segments.size);
  for (const auto segment : tree.identList(segments)) {
    auto canonical = identity::ModulePathSegment::fromSource(tree.ident(segment));
    if (canonical == zc::none) { return zc::none; }
    ZC_IF_SOME(value, canonical) { normalized.add(zc::mv(value)); }
  }
  if (normalized.size() != segments.size) { return zc::none; }
  return zc::mv(normalized);
}

struct PendingRequest final {
  PendingRequest(identity::ModuleResolutionKey&& key,
                 ModuleSyntaxDependencySite&& syntaxSite) noexcept
      : key(zc::mv(key)) {
    syntaxSites.add(zc::mv(syntaxSite));
  }
  PendingRequest(PendingRequest&&) noexcept = default;
  PendingRequest& operator=(PendingRequest&&) noexcept = default;
  ZC_DISALLOW_COPY(PendingRequest);

  identity::ModuleResolutionKey key;
  zc::Vector<ModuleSyntaxDependencySite> syntaxSites;
};

ModuleDependencyRequestDerivationResult deriveForRequester(
    identity::ModuleId requester, const VerifiedParsedModule& parsedModule,
    const identity::Sha256Digest& environmentRevision, const StructuralModuleResolver& resolver) {
  const auto& tree = parsedModule.tree();
  if (!ast::verifySchema(tree) || !tree.contains(tree.root()) ||
      tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return failure();
  }
  const auto rootSpan = parsedModule.rootSpan();
  if (!rootSpan.belongsTo(parsedModule.source())) { return failure(); }

  auto ordinalResult = schemaPreorderOrdinals(tree);
  if (ordinalResult == zc::none) { return failure(); }

  zc::TreeMap<zc::String, PendingRequest> sortedRequests;
  bool valid = true;
  ZC_IF_SOME(ordinals, ordinalResult) {
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid) { return; }

      identity::ModuleDependencyKind kind = identity::ModuleDependencyKind::Import;
      ast::NodeId path;
      if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
        path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
      } else if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
        path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
        if (!tree.contains(path)) { return; }
        kind = identity::ModuleDependencyKind::ForeignReexport;
      } else if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
                 static_cast<ast::ModuleDeclarationForm>(
                     syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
                     ast::ModuleDeclarationForm::Alias) {
        path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
        kind = identity::ModuleDependencyKind::ModuleAlias;
      } else {
        return;
      }

      if (!tree.contains(node) || node.value >= ordinals.size() ||
          ordinals[node.value] == UINT32_MAX) {
        valid = false;
        return;
      }
      auto normalizedPath = normalizedModulePath(tree, path);
      auto span = parsedModule.spanFor(syntax.range);
      if (normalizedPath == zc::none || span == zc::none) {
        valid = false;
        return;
      }
      ZC_IF_SOME(spanValue, span) {
        if (spanValue.byteStart() > spanValue.byteEnd() ||
            spanValue.byteEnd() > parsedModule.byteLength() ||
            !spanValue.belongsTo(parsedModule.source())) {
          valid = false;
          return;
        }
        ZC_IF_SOME(pathValue, normalizedPath) {
          auto key = resolver.resolutionKey(requester, kind, zc::mv(pathValue));
          if (key == zc::none) {
            valid = false;
            return;
          }
          ZC_IF_SOME(keyValue, key) {
            const auto encoded = keyValue.encode();
            auto sortKey = zc::encodeHex(encoded.asPtr());
            ModuleSyntaxDependencySite site(node, zc::mv(spanValue), ordinals[node.value]);
            ZC_IF_SOME(existing, sortedRequests.find(sortKey)) {
              existing.syntaxSites.add(zc::mv(site));
            } else {
              sortedRequests.insert(zc::mv(sortKey),
                                    PendingRequest(zc::mv(keyValue), zc::mv(site)));
            }
          }
        }
      }
    });
  }
  if (!valid) { return failure(); }

  zc::Vector<ModuleDependencyRequest> requests(sortedRequests.size());
  for (auto& entry : sortedRequests) {
    auto request = ModuleDependencyRequest::source(
        requester, zc::mv(entry.value.key), environmentRevision, zc::mv(entry.value.syntaxSites));
    if (request == zc::none) { return failure(); }
    ZC_IF_SOME(value, request) { requests.add(zc::mv(value)); }
  }
  return zc::mv(requests);
}

}  // namespace

ModuleDependencyRequestDerivationResult ModuleDependencyRequestDeriver::derive(
    identity::ModuleId requester, const VerifiedParsedModule& parsedModule,
    const identity::Sha256Digest& environmentRevision, const StructuralModuleResolver& resolver) {
  if (!requester.isValid() || environmentRevision != resolver.environmentRevision()) {
    return failure();
  }
  return deriveForRequester(requester, parsedModule, environmentRevision, resolver);
}

}  // namespace zomlang::compiler::binder
