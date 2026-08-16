// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/driver/module-discovery.h"

#include <cstdint>

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver {
namespace {

zc::Array<uint8_t> encodePath(const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder encoder;
  path.encode(encoder);
  return encoder.finish();
}

identity::CanonicalPathSegment requirePathSegment(zc::StringPtr text) {
  auto result = identity::CanonicalPathSegment::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

zc::Vector<identity::CanonicalPathSegment> cloneRoot(
    const identity::CanonicalRelativePath& searchRoot, size_t additionalCapacity) {
  zc::Vector<identity::CanonicalPathSegment> result(searchRoot.segments().size() +
                                                    additionalCapacity);
  for (const auto& segment : searchRoot.segments()) { result.add(segment.clone()); }
  return result;
}

identity::CanonicalRelativePath directCandidate(
    const identity::CanonicalRelativePath& searchRoot,
    zc::ArrayPtr<const identity::ModulePathSegment> modulePath) {
  auto segments = cloneRoot(searchRoot, modulePath.size());
  for (size_t index = 0; index + 1 < modulePath.size(); ++index) {
    segments.add(requirePathSegment(modulePath[index].text()));
  }
  segments.add(requirePathSegment(zc::str(modulePath.back().text(), ".zom")));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalRelativePath nestedCandidate(
    const identity::CanonicalRelativePath& searchRoot,
    zc::ArrayPtr<const identity::ModulePathSegment> modulePath) {
  auto segments = cloneRoot(searchRoot, modulePath.size() + 1);
  for (const auto& segment : modulePath) { segments.add(requirePathSegment(segment.text())); }
  segments.add(requirePathSegment("mod.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

bool containsPath(const package::SourceTreeRecord& sourceTree,
                  const identity::CanonicalRelativePath& path) {
  const auto expected = encodePath(path);
  for (const auto& file : sourceTree.files()) {
    const auto current = encodePath(file.path());
    if (current.asPtr() == expected.asPtr()) { return true; }
    if (expected.asPtr() < current.asPtr()) { return false; }
  }
  return false;
}

zc::Maybe<zc::Vector<ast::NodeId>> rootedSchemaPreorder(const ast::Tree& tree) {
  if (!ast::verifySchema(tree) || tree.nodeCount() >= UINT32_MAX || !tree.contains(tree.root())) {
    return zc::none;
  }

  enum class TraversalState : uint8_t { Unseen, Active, Complete };
  struct TraversalEvent final {
    ast::NodeId node;
    bool exit;
  };

  zc::Vector<TraversalState> states;
  states.resize(tree.nodeCount() + 1);
  for (auto& state : states) { state = TraversalState::Unseen; }
  zc::Vector<uint32_t> dependencySiteReferences;
  dependencySiteReferences.resize(tree.nodeCount() + 1);
  for (auto& count : dependencySiteReferences) { count = 0; }

  zc::Vector<TraversalEvent> pending(tree.nodeCount());
  pending.add(TraversalEvent{tree.root(), false});
  zc::Vector<ast::NodeId> preorder(tree.nodeCount());
  while (!pending.empty()) {
    const auto event = pending.back();
    pending.removeLast();
    const ast::NodeId node = event.node;
    if (!tree.contains(node) || node.value >= states.size()) { return zc::none; }
    if (event.exit) {
      if (states[node.value] != TraversalState::Active) { return zc::none; }
      states[node.value] = TraversalState::Complete;
      continue;
    }
    if (states[node.value] == TraversalState::Complete) { continue; }
    if (states[node.value] == TraversalState::Active) { return zc::none; }
    states[node.value] = TraversalState::Active;
    preorder.add(node);
    pending.add(TraversalEvent{node, true});

    zc::Vector<ast::NodeId> children;
    ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) { children.add(child); });
    for (const auto child : children) {
      if (!tree.contains(child) || child.value >= dependencySiteReferences.size()) {
        return zc::none;
      }
      const auto kind = tree.node(child).kind;
      if (kind == ast::SyntaxKind::ImportDeclaration ||
          kind == ast::SyntaxKind::ExportDeclaration ||
          kind == ast::SyntaxKind::ModuleDeclaration) {
        auto& references = dependencySiteReferences[child.value];
        ++references;
        if (references != 1) { return zc::none; }
      }
    }
    for (size_t index = children.size(); index > 0; --index) {
      pending.add(TraversalEvent{children[index - 1], false});
    }
  }

  return zc::mv(preorder);
}

zc::Maybe<zc::Vector<identity::ModulePathSegment>> normalizeModulePath(const ast::Tree& tree,
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
    auto result = identity::ModulePathSegment::fromSource(tree.ident(segment));
    if (result == zc::none) { return zc::none; }
    ZC_IF_SOME(value, result) { normalized.add(zc::mv(value)); }
  }
  if (normalized.size() != segments.size) { return zc::none; }
  return zc::mv(normalized);
}

zc::String encodeStructuralRequestKey(const StructuralModuleDependencyRequest& request) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(zc::StringPtr("zom.structural-module-dependency-request").asBytes());
  encoder.encodeUint8(static_cast<uint8_t>(request.kind()));
  encoder.encodeSequenceSize(request.normalizedPath().size());
  for (const auto& segment : request.normalizedPath()) { segment.encode(encoder); }
  encoder.encodeUint32(request.schemaPreorderOrdinal());
  return zc::encodeHex(encoder.finish().asPtr());
}

}  // namespace

StructuralModuleDependencyRequest::StructuralModuleDependencyRequest(
    StructuralModuleDependencyKind kind, zc::Vector<identity::ModulePathSegment>&& path,
    ast::NodeId syntaxNode, source::SourceRange syntaxRange,
    uint32_t schemaPreorderOrdinal) noexcept
    : kindValue(kind),
      pathValue(zc::mv(path)),
      syntaxNodeValue(syntaxNode),
      syntaxRangeValue(syntaxRange),
      schemaPreorderOrdinalValue(schemaPreorderOrdinal) {}

zc::Maybe<StructuralModuleDependencyRequest> StructuralModuleDependencyRequest::from(
    StructuralModuleDependencyKind kind, zc::Vector<identity::ModulePathSegment>&& path,
    ast::NodeId syntaxNode, source::SourceRange syntaxRange,
    uint32_t schemaPreorderOrdinal) noexcept {
  if (path.empty() || !syntaxNode) { return zc::none; }
  switch (kind) {
    case StructuralModuleDependencyKind::Import:
    case StructuralModuleDependencyKind::ForeignReexport:
    case StructuralModuleDependencyKind::ModuleAlias:
      break;
    default:
      return zc::none;
  }
  return StructuralModuleDependencyRequest(kind, zc::mv(path), syntaxNode, syntaxRange,
                                           schemaPreorderOrdinal);
}

StructuralModuleDependencyKind StructuralModuleDependencyRequest::kind() const noexcept {
  return kindValue;
}

zc::ArrayPtr<const identity::ModulePathSegment> StructuralModuleDependencyRequest::normalizedPath()
    const noexcept {
  return pathValue.asPtr();
}

ast::NodeId StructuralModuleDependencyRequest::syntaxNode() const noexcept {
  return syntaxNodeValue;
}

source::SourceRange StructuralModuleDependencyRequest::syntaxRange() const noexcept {
  return syntaxRangeValue;
}

uint32_t StructuralModuleDependencyRequest::schemaPreorderOrdinal() const noexcept {
  return schemaPreorderOrdinalValue;
}

ResolvedModuleSource::ResolvedModuleSource(identity::CanonicalRelativePath&& path) noexcept
    : pathValue(zc::mv(path)) {}

ResolvedModuleSource ResolvedModuleSource::from(identity::CanonicalRelativePath&& path) {
  return ResolvedModuleSource(zc::mv(path));
}

const identity::CanonicalRelativePath& ResolvedModuleSource::path() const noexcept {
  return pathValue;
}

AmbiguousModuleSource::AmbiguousModuleSource(
    zc::Vector<identity::CanonicalRelativePath>&& paths) noexcept
    : pathValues(zc::mv(paths)) {}

AmbiguousModuleSource AmbiguousModuleSource::from(identity::CanonicalRelativePath&& first,
                                                  identity::CanonicalRelativePath&& second) {
  const auto firstBytes = encodePath(first);
  const auto secondBytes = encodePath(second);
  if (firstBytes.asPtr() == secondBytes.asPtr()) { ZC_UNREACHABLE; }
  zc::Vector<identity::CanonicalRelativePath> paths(2);
  if (secondBytes.asPtr() < firstBytes.asPtr()) {
    paths.add(zc::mv(second));
    paths.add(zc::mv(first));
  } else {
    paths.add(zc::mv(first));
    paths.add(zc::mv(second));
  }
  return AmbiguousModuleSource(zc::mv(paths));
}

zc::ArrayPtr<const identity::CanonicalRelativePath> AmbiguousModuleSource::paths() const noexcept {
  return pathValues.asPtr();
}

ResolvedCoreModuleSource::ResolvedCoreModuleSource(
    identity::SourceFileKey&& source, const identity::Sha256Digest& contentDigest) noexcept
    : sourceValue(zc::mv(source)), contentDigestValue(contentDigest) {}

ResolvedCoreModuleSource ResolvedCoreModuleSource::from(
    identity::SourceFileKey&& source, const identity::Sha256Digest& contentDigest) {
  return ResolvedCoreModuleSource(zc::mv(source), contentDigest);
}

const identity::SourceFileKey& ResolvedCoreModuleSource::source() const noexcept {
  return sourceValue;
}

const identity::Sha256Digest& ResolvedCoreModuleSource::contentDigest() const noexcept {
  return contentDigestValue;
}

ModuleSourceDiscoveryResult discoverModuleSource(
    const package::SourceTreeRecord& sourceTree, const identity::CanonicalRelativePath& searchRoot,
    zc::ArrayPtr<const identity::ModulePathSegment> modulePath) {
  if (modulePath.size() == 0) { return InvalidModuleSourceRequest(); }

  auto direct = directCandidate(searchRoot, modulePath);
  auto nested = nestedCandidate(searchRoot, modulePath);
  const bool hasDirect = containsPath(sourceTree, direct);
  const bool hasNested = containsPath(sourceTree, nested);
  if (hasDirect && hasNested) {
    return AmbiguousModuleSource::from(zc::mv(direct), zc::mv(nested));
  }
  if (hasDirect) { return ResolvedModuleSource::from(zc::mv(direct)); }
  if (hasNested) { return ResolvedModuleSource::from(zc::mv(nested)); }
  return MissingModuleSource();
}

CoreModuleSourceDiscoveryResult discoverCoreModuleSource(
    const source::core::AdmittedCoreSourceCatalog& catalog,
    zc::ArrayPtr<const identity::ModulePathSegment> modulePath) {
  if (modulePath.size() == 0) { return InvalidModuleSourceRequest(); }
  ZC_IF_SOME(entry, catalog.find(modulePath)) {
    return ResolvedCoreModuleSource::from(entry.source().clone(), entry.contentDigest());
  }
  return MissingModuleSource();
}

StructuralModuleDependencyRequestResult extractStructuralModuleDependencyRequests(
    const ast::Tree& tree) {
  auto preorderResult = rootedSchemaPreorder(tree);
  if (preorderResult == zc::none) { return InvalidStructuralModuleDependencyRequests(); }

  zc::TreeMap<zc::String, StructuralModuleDependencyRequest> sortedRequests;
  ZC_IF_SOME(preorder, preorderResult) {
    for (size_t ordinal = 0; ordinal < preorder.size(); ++ordinal) {
      const ast::NodeId node = preorder[ordinal];
      const auto& syntax = tree.node(node);

      StructuralModuleDependencyKind kind = StructuralModuleDependencyKind::Import;
      ast::NodeId path;
      if (syntax.kind == ast::SyntaxKind::ImportDeclaration) {
        path = ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]);
      } else if (syntax.kind == ast::SyntaxKind::ExportDeclaration) {
        path = ast::NodeId(syntax.payload.words[ast::kExportDeclarationPathWord]);
        if (!path) { continue; }
        kind = StructuralModuleDependencyKind::ForeignReexport;
      } else if (syntax.kind == ast::SyntaxKind::ModuleDeclaration &&
                 static_cast<ast::ModuleDeclarationForm>(
                     syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
                     ast::ModuleDeclarationForm::Alias) {
        path = ast::NodeId(syntax.payload.words[ast::kModuleDeclarationAliasTargetWord]);
        kind = StructuralModuleDependencyKind::ModuleAlias;
      } else {
        continue;
      }

      auto normalizedPath = normalizeModulePath(tree, path);
      if (normalizedPath == zc::none) { return InvalidStructuralModuleDependencyRequests(); }
      ZC_IF_SOME(pathValue, normalizedPath) {
        auto request = StructuralModuleDependencyRequest::from(
            kind, zc::mv(pathValue), node, syntax.range, static_cast<uint32_t>(ordinal));
        if (request == zc::none) { return InvalidStructuralModuleDependencyRequests(); }
        ZC_IF_SOME(requestValue, request) {
          auto key = encodeStructuralRequestKey(requestValue);
          if (sortedRequests.find(key) != zc::none) {
            return InvalidStructuralModuleDependencyRequests();
          }
          sortedRequests.insert(zc::mv(key), zc::mv(requestValue));
        }
      }
    }
  }

  zc::Vector<StructuralModuleDependencyRequest> requests(sortedRequests.size());
  for (auto& entry : sortedRequests) { requests.add(zc::mv(entry.value)); }
  return zc::mv(requests);
}

}  // namespace zomlang::compiler::driver
