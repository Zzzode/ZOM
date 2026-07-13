// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-verifier.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"
#include "zomlang/compiler/binder/internal/body-binding.h"
#include "zomlang/compiler/binder/internal/control-transfer.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            BinderEmitterSite site, uint32_t ordinal = 0) {
  return BinderInvariantFact{kind, input.module(), zc::none, site, ordinal};
}

BinderInvariantFact builderFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                   uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::ModuleSkeleton, ordinal);
}

BinderInvariantFact bodyBuilderFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                       uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::BodyBinding, ordinal);
}

BinderInvariantFact verifierFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                    uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::BindingVerifier, ordinal);
}

enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, ControlTransfer };

struct PendingFailureRef final {
  PendingFailureKind kind;
  size_t index;
};

struct PendingFailureOrderKey final {
  uint64_t start;
  uint64_t end;
  uint16_t diagnostic;
  uint8_t emitterSite;
  uint32_t schemaPreorderOrdinal;
  size_t sequence;

  bool operator==(const PendingFailureOrderKey& other) const noexcept {
    return start == other.start && end == other.end && diagnostic == other.diagnostic &&
           emitterSite == other.emitterSite &&
           schemaPreorderOrdinal == other.schemaPreorderOrdinal && sequence == other.sequence;
  }
  bool operator<(const PendingFailureOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    if (diagnostic != other.diagnostic) { return diagnostic < other.diagnostic; }
    if (emitterSite != other.emitterSite) { return emitterSite < other.emitterSite; }
    if (schemaPreorderOrdinal != other.schemaPreorderOrdinal) {
      return schemaPreorderOrdinal < other.schemaPreorderOrdinal;
    }
    return sequence < other.sequence;
  }
};

BindingVerificationResult rejectBinderInvariant(BinderInvariantFact&& fact) {
  return InvariantRejected::single(
      BindingVerificationFailure(BindingVerificationFailureValue(zc::mv(fact))));
}

BindingVerificationResult rejectIdentityInvariant(const VerifiedBindingInput& input,
                                                  identity::IdentityInvariantKind kind,
                                                  identity::IdentityAllocationPhase phase,
                                                  uint32_t ordinal = 0) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto fact = identity::IdentityInvariant::from(kind, phase, zc::mv(noKey), zc::mv(noRange),
                                                identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, fact) {
    return InvariantRejected::single(
        BindingVerificationFailure(BindingVerificationFailureValue(zc::mv(value))));
  }
  return rejectBinderInvariant(
      verifierFailure(input, BinderInvariantKind::InvalidBindingFact, ordinal));
}

BindingVerificationResult rejectForeignContext(const VerifiedBindingInput& input,
                                               uint32_t ordinal = 0) {
  return rejectIdentityInvariant(input, identity::IdentityInvariantKind::ForeignContext,
                                 identity::IdentityAllocationPhase::Module, ordinal);
}

BindingVerificationResult rejectInvalidSourceRange(const VerifiedBindingInput& input,
                                                   uint32_t ordinal = 0) {
  return rejectIdentityInvariant(input, identity::IdentityInvariantKind::InvalidSourceRange,
                                 identity::IdentityAllocationPhase::Source, ordinal);
}

bool targetHasForeignContext(const VerifiedBindingInput& input, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    return !value.get<DefinitionBindingTarget>().definition.belongsTo(input.semanticContext());
  }
  return !value.get<ModuleBindingTarget>().module.belongsTo(input.semanticContext());
}

bool entryHasForeignContext(const VerifiedBindingInput& input, const ExportSurfaceEntry& entry) {
  if (targetHasForeignContext(input, entry.bindingIdentity) ||
      targetHasForeignContext(input, entry.canonicalTarget)) {
    return true;
  }
  const auto& visibility = entry.visibility.value();
  if (visibility.is<ModuleVisibility>() &&
      !visibility.get<ModuleVisibility>().module.belongsTo(input.semanticContext())) {
    return true;
  }
  for (const auto& step : entry.reexportChain) {
    if (!step.module.belongsTo(input.semanticContext()) ||
        !step.alias.belongsTo(input.semanticContext()) ||
        targetHasForeignContext(input, step.canonicalTarget)) {
      return true;
    }
  }
  return false;
}

bool hasForeignContext(const VerifiedBindingInput& input,
                       const BindingMetadataCandidate& candidate) {
  if (candidate.semanticContext != input.semanticContext() || candidate.module != input.module() ||
      !candidate.module.belongsTo(input.semanticContext()) ||
      candidate.currentSurface.sourceModule != input.module() ||
      candidate.currentSurface.sourcePackage != input.package() ||
      !candidate.currentSurface.sourceModule.belongsTo(input.semanticContext()) ||
      !candidate.currentSurface.sourcePackage.belongsTo(input.semanticContext())) {
    return true;
  }
  for (const auto& fact : candidate.nodeScopes) {
    if (fact.scope.module() != input.module() || !fact.scope.belongsTo(input.semanticContext())) {
      return true;
    }
  }
  for (const auto& fact : candidate.definitions) {
    if (!fact.identity.belongsTo(input.semanticContext()) ||
        fact.declaringScope.module() != input.module() ||
        !fact.declaringScope.belongsTo(input.semanticContext())) {
      return true;
    }
  }
  for (const auto& scope : candidate.scopes) {
    if (scope.id.module() != input.module() || !scope.id.belongsTo(input.semanticContext())) {
      return true;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || !parent.belongsTo(input.semanticContext())) {
        return true;
      }
    }
    const auto& owner = scope.owner.value();
    if ((owner.is<ModuleScopeOwner>() &&
         !owner.get<ModuleScopeOwner>().module.belongsTo(input.semanticContext())) ||
        (owner.is<DefinitionScopeOwner>() &&
         !owner.get<DefinitionScopeOwner>().definition.belongsTo(input.semanticContext())) ||
        (owner.is<ImplScopeOwner>() &&
         !owner.get<ImplScopeOwner>().implementation.belongsTo(input.semanticContext()))) {
      return true;
    }
    for (const auto& binding : scope.bindings) {
      if (targetHasForeignContext(input, binding.binding.bindingIdentity) ||
          targetHasForeignContext(input, binding.binding.canonicalTarget)) {
        return true;
      }
    }
  }
  for (const auto& resolution : candidate.nodeBindings) {
    const auto& value = resolution.value;
    if (value.is<BoundNameResolution>()) {
      const auto& bound = value.get<BoundNameResolution>();
      if (targetHasForeignContext(input, bound.bindingIdentity) ||
          targetHasForeignContext(input, bound.canonicalTarget)) {
        return true;
      }
    }
  }
  for (const auto& fact : candidate.controlTransfers) {
    const auto& target = fact.target;
    if (target.is<LoopControlTarget>()) {
      const auto scope = target.get<LoopControlTarget>().scope;
      if (scope.module() != input.module() || !scope.belongsTo(input.semanticContext())) {
        return true;
      }
    } else if (target.is<MatchControlTarget>()) {
      const auto scope = target.get<MatchControlTarget>().scope;
      if (scope.module() != input.module() || !scope.belongsTo(input.semanticContext())) {
        return true;
      }
    }
  }
  for (const auto& shadow : candidate.shadowTargets) {
    if (!shadow.definition.belongsTo(input.semanticContext()) ||
        targetHasForeignContext(input, shadow.target)) {
      return true;
    }
  }
  for (const auto& entry : candidate.currentSurface.visibleEntries) {
    if (entryHasForeignContext(input, entry)) { return true; }
  }
  for (const auto& entry : candidate.currentSurface.exports) {
    if (entryHasForeignContext(input, entry)) { return true; }
  }
  for (const auto& fact : candidate.impls) {
    if (!fact.identity.belongsTo(input.semanticContext()) ||
        fact.scope.module() != input.module() || !fact.scope.belongsTo(input.semanticContext())) {
      return true;
    }
    for (const auto member : fact.members) {
      if (!member.belongsTo(input.semanticContext())) { return true; }
    }
  }
  for (const auto& fact : candidate.moduleAliases) {
    if (!fact.alias.belongsTo(input.semanticContext()) ||
        !fact.canonicalTarget.belongsTo(input.semanticContext())) {
      return true;
    }
  }
  for (const auto& fact : candidate.imports) {
    if (!fact.alias.belongsTo(input.semanticContext()) ||
        !fact.sourceModule.belongsTo(input.semanticContext()) ||
        targetHasForeignContext(input, fact.canonicalTarget)) {
      return true;
    }
  }
  for (const auto& fact : candidate.localExports) {
    if (!fact.alias.belongsTo(input.semanticContext()) ||
        targetHasForeignContext(input, fact.sourceBinding) ||
        targetHasForeignContext(input, fact.canonicalTarget)) {
      return true;
    }
  }
  return false;
}

bool hasInvalidSourceRange(const VerifiedBindingInput& input,
                           const BindingMetadataCandidate& candidate) {
  const auto spanIsInvalid = [&](const identity::SourceSpan& span) {
    return !input.moduleKey().contains(span) || span.byteStart() > span.byteEnd() ||
           span.byteEnd() > input.parsedModule().byteLength();
  };
  for (const auto& failureFact : candidate.sourceFailures) {
    if (spanIsInvalid(failureFact.primary)) { return true; }
    for (const auto& note : failureFact.notes) {
      if (spanIsInvalid(note.source)) { return true; }
    }
  }
  for (const auto& fact : candidate.definitions) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.impls) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.controlTransfers) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& scope : candidate.scopes) {
    if (spanIsInvalid(scope.source)) { return true; }
    for (const auto& binding : scope.bindings) {
      if (spanIsInvalid(binding.binding.declarationSpan)) { return true; }
      ZC_IF_SOME(alias, binding.binding.aliasSpan) {
        if (spanIsInvalid(alias)) { return true; }
      }
    }
  }
  const auto entryIsInvalid = [&](const ExportSurfaceEntry& entry) {
    if (spanIsInvalid(entry.bindingSpan) || spanIsInvalid(entry.canonicalDeclarationSpan)) {
      return true;
    }
    ZC_IF_SOME(alias, entry.aliasSpan) {
      if (spanIsInvalid(alias)) { return true; }
    }
    ZC_IF_SOME(exportSpan, entry.exportSpan) {
      if (spanIsInvalid(exportSpan)) { return true; }
    }
    for (const auto& step : entry.reexportChain) {
      if (spanIsInvalid(step.exportSpan)) { return true; }
    }
    return false;
  };
  for (const auto& entry : candidate.currentSurface.visibleEntries) {
    if (entryIsInvalid(entry)) { return true; }
  }
  for (const auto& entry : candidate.currentSurface.exports) {
    if (entryIsInvalid(entry)) { return true; }
  }
  return false;
}

BindingTarget cloneTarget(const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    return BindingTarget::definition(value.get<DefinitionBindingTarget>().definition);
  }
  return BindingTarget::module(value.get<ModuleBindingTarget>().module);
}

VisibilityEnvelope cloneVisibility(const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    return VisibilityEnvelope::module(value.get<ModuleVisibility>().module);
  }
  return VisibilityEnvelope::external();
}

ExportSurfaceEntry cloneEntry(const ExportSurfaceEntry& entry) {
  zc::Maybe<identity::SourceSpan> aliasSpan;
  ZC_IF_SOME(value, entry.aliasSpan) { aliasSpan = value.clone(); }
  zc::Maybe<identity::SourceSpan> exportSpan;
  ZC_IF_SOME(value, entry.exportSpan) { exportSpan = value.clone(); }
  zc::Vector<ReexportProvenanceStep> chain;
  for (const auto& step : entry.reexportChain) {
    chain.add(ReexportProvenanceStep{step.module, step.alias, cloneTarget(step.canonicalTarget),
                                     step.exportSpan.clone()});
  }
  return ExportSurfaceEntry(
      entry.name.clone(), cloneTarget(entry.bindingIdentity), cloneTarget(entry.canonicalTarget),
      cloneVisibility(entry.visibility), entry.exported, entry.bindingSpan.clone(),
      entry.canonicalDeclarationSpan.clone(), zc::mv(aliasSpan), zc::mv(exportSpan), zc::mv(chain));
}

zc::Maybe<zc::Vector<uint32_t>> schemaPreorderOrdinals(const ast::Tree& tree) {
  zc::Vector<uint32_t> ordinals;
  ordinals.resize(tree.nodeCount() + 1);
  for (auto& value : ordinals) { value = UINT32_MAX; }
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node.value >= ordinals.size() || ordinals[node.value] != UINT32_MAX) { return; }
    ordinals[node.value] = ordinal++;
  });
  if (ordinal != tree.nodeCount()) { return zc::none; }
  return zc::mv(ordinals);
}

bool hasCompleteLexicalBindingSites(const ast::Tree& tree,
                                    zc::ArrayPtr<const BindingResolution> bindings) {
  zc::Vector<uint8_t> requiredNamespaces;
  zc::Vector<bool> published;
  requiredNamespaces.resize(tree.nodeCount() + 1);
  published.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < requiredNamespaces.size(); ++index) {
    requiredNamespaces[index] = 0;
    published[index] = false;
  }
  size_t requiredCount = 0;
  bool valid = true;
  const auto requireSite = [&](ast::NodeId node, ast::SyntaxKind kind, Namespace nameSpace) {
    if (!tree.contains(node) || node.value >= requiredNamespaces.size() ||
        tree.node(node).kind != kind ||
        (nameSpace != Namespace::Value && nameSpace != Namespace::Type)) {
      valid = false;
      return;
    }
    const auto encodedNamespace = static_cast<uint8_t>(nameSpace);
    if (requiredNamespaces[node.value] != 0) {
      if (requiredNamespaces[node.value] != encodedNamespace) { valid = false; }
      return;
    }
    requiredNamespaces[node.value] = encodedNamespace;
    ++requiredCount;
  };
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    switch (syntax.kind) {
      case ast::SyntaxKind::IdentExpr:
        requireSite(node, ast::SyntaxKind::IdentExpr, Namespace::Value);
        return;
      case ast::SyntaxKind::NamedTypeExpr:
        requireSite(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]),
                    ast::SyntaxKind::ModulePath, Namespace::Type);
        return;
      case ast::SyntaxKind::TypeQueryExpr:
        requireSite(ast::NodeId(syntax.payload.words[ast::kTypeQueryExprPathWord]),
                    ast::SyntaxKind::ModulePath, Namespace::Value);
        return;
      case ast::SyntaxKind::EnumPattern:
        requireSite(ast::NodeId(syntax.payload.words[ast::kEnumPatternPathWord]),
                    ast::SyntaxKind::ModulePath, Namespace::Value);
        return;
      case ast::SyntaxKind::StructPattern: {
        const ast::NodeId typePath(syntax.payload.words[ast::kStructPatternTyPathWord]);
        if (tree.contains(typePath)) {
          requireSite(typePath, ast::SyntaxKind::ModulePath, Namespace::Type);
        }
        return;
      }
      case ast::SyntaxKind::MarkerImpl:
        requireSite(ast::NodeId(syntax.payload.words[ast::kMarkerImplMarkerPathWord]),
                    ast::SyntaxKind::AttributePath, Namespace::Type);
        return;
      case ast::SyntaxKind::DynTypeMarkerList: {
        const ast::NodeList markers{syntax.payload.words[ast::kDynTypeMarkerListMarkersFirstWord],
                                    syntax.payload.words[ast::kDynTypeMarkerListMarkersSizeWord]};
        if (!tree.contains(markers) ||
            syntax.payload.words[ast::kDynTypeMarkerListNMarkersWord] != markers.size) {
          valid = false;
          return;
        }
        for (const ast::NodeId marker : tree.list(markers)) {
          requireSite(marker, ast::SyntaxKind::AttributePath, Namespace::Type);
        }
        return;
      }
      case ast::SyntaxKind::ObjectProperty:
        if (syntax.payload.words[ast::kObjectPropertyShortFormWord] != 0) {
          requireSite(node, ast::SyntaxKind::ObjectProperty, Namespace::Value);
        }
        return;
      default:
        return;
    }
  });
  if (!valid) { return false; }
  size_t publishedCount = 0;
  for (const auto& binding : bindings) {
    if (!tree.contains(binding.node) || binding.node.value >= requiredNamespaces.size() ||
        published[binding.node.value]) {
      return false;
    }
    const auto nodeKind = tree.node(binding.node).kind;
    if (requiredNamespaces[binding.node.value] == 0) {
      if ((nodeKind != ast::SyntaxKind::BreakStmt &&
           nodeKind != ast::SyntaxKind::ContinueStatement) ||
          !binding.value.is<FailedBindingResolution>()) {
        return false;
      }
      published[binding.node.value] = true;
      continue;
    }
    if (binding.value.is<BoundNameResolution>() &&
        static_cast<uint8_t>(binding.value.get<BoundNameResolution>().nameSpace) !=
            requiredNamespaces[binding.node.value]) {
      return false;
    }
    if (!binding.value.is<BoundNameResolution>() && !binding.value.is<FailedBindingResolution>()) {
      return false;
    }
    published[binding.node.value] = true;
    ++publishedCount;
  }
  return publishedCount == requiredCount;
}

enum class ControlOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

ControlOracleResult verifyControlTransferFacts(const VerifiedBindingInput& input,
                                               const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) { return ControlOracleResult::MalformedScopeGraph; }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.nodeScopes.size() != tree.nodeCount()) {
    return ControlOracleResult::MalformedScopeGraph;
  }

  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<size_t> factByNode;
  zc::Vector<size_t> resolutionByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  factByNode.resize(tree.nodeCount() + 1);
  resolutionByNode.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < scopeByNode.size(); ++index) {
    scopeByNode[index] = UINT32_MAX;
    factByNode[index] = kMissing;
    resolutionByNode[index] = kMissing;
  }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext())) {
      return ControlOracleResult::MalformedScopeGraph;
    }
    if ((index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
      return ControlOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return ControlOracleResult::MalformedScopeGraph;
      }
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return ControlOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }

  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.controlTransfers.size(); ++index) {
    const auto& fact = candidate.controlTransfers[index];
    if (!tree.contains(fact.node) || fact.node.value >= factByNode.size() ||
        factByNode[fact.node.value] != kMissing ||
        (index != 0 && fact.node.value <= previousNode)) {
      return ControlOracleResult::InvalidBindingFact;
    }
    const auto kind = tree.node(fact.node).kind;
    if (kind != ast::SyntaxKind::BreakStmt && kind != ast::SyntaxKind::ContinueStatement) {
      return ControlOracleResult::InvalidBindingFact;
    }
    factByNode[fact.node.value] = index;
    previousNode = fact.node.value;
  }
  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto& resolution = candidate.nodeBindings[index];
    if (!tree.contains(resolution.node)) { return ControlOracleResult::InvalidBindingFact; }
    const auto kind = tree.node(resolution.node).kind;
    if (kind != ast::SyntaxKind::BreakStmt && kind != ast::SyntaxKind::ContinueStatement) {
      continue;
    }
    if (resolutionByNode[resolution.node.value] != kMissing) {
      return ControlOracleResult::InvalidBindingFact;
    }
    resolutionByNode[resolution.node.value] = index;
  }

  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) { return ControlOracleResult::MalformedScopeGraph; }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));
  zc::Vector<bool> consumedFacts;
  zc::Vector<bool> consumedFailures;
  consumedFacts.resize(candidate.controlTransfers.size());
  consumedFailures.resize(candidate.sourceFailures.size());
  for (auto& consumed : consumedFacts) { consumed = false; }
  for (auto& consumed : consumedFailures) { consumed = false; }

  ControlOracleResult result = ControlOracleResult::Valid;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != ControlOracleResult::Valid ||
        (syntax.kind != ast::SyntaxKind::BreakStmt &&
         syntax.kind != ast::SyntaxKind::ContinueStatement)) {
      return;
    }
    const bool isBreak = syntax.kind == ast::SyntaxKind::BreakStmt;
    const uint32_t label =
        syntax.payload.words[isBreak ? ast::kBreakStmtLabelWord : ast::kContinueStatementLabelWord];
    if (label != 0 || node.value >= scopeByNode.size() || scopeByNode[node.value] == UINT32_MAX) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }

    enum class ExpectedTargetKind : uint8_t { None, Loop, Match };
    ExpectedTargetKind targetKind = ExpectedTargetKind::None;
    zc::Maybe<ScopeId> targetScope;
    uint32_t scopeIndex = scopeByNode[node.value];
    bool terminated = false;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        result = ControlOracleResult::MalformedScopeGraph;
        return;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Loop) {
        targetKind = ExpectedTargetKind::Loop;
        targetScope = scope.id;
        terminated = true;
        break;
      }
      if (scope.kind == ScopeKind::Match && isBreak) {
        targetKind = ExpectedTargetKind::Match;
        targetScope = scope.id;
        terminated = true;
        break;
      }
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||
          scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        terminated = true;
        break;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    if (!terminated) {
      result = ControlOracleResult::MalformedScopeGraph;
      return;
    }

    const size_t factIndex = factByNode[node.value];
    const size_t resolutionIndex = resolutionByNode[node.value];
    if (targetKind != ExpectedTargetKind::None) {
      if (factIndex == kMissing) {
        result = resolutionIndex == kMissing ? ControlOracleResult::MissingRequiredResolution
                                             : ControlOracleResult::InvalidBindingFact;
        return;
      }
      if (resolutionIndex != kMissing || targetScope == zc::none) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      const auto& fact = candidate.controlTransfers[factIndex];
      const auto expectedKind =
          isBreak ? ControlTransferKind::Break : ControlTransferKind::Continue;
      auto expectedSource = input.parsedModule().spanFor(syntax.range);
      if (fact.kind != expectedKind || expectedSource == zc::none) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      ZC_IF_SOME(source, expectedSource) {
        if (!sameSpan(fact.source, source)) {
          result = ControlOracleResult::InvalidBindingFact;
          return;
        }
      }
      const auto expectedScope = ZC_ASSERT_NONNULL(targetScope);
      if ((targetKind == ExpectedTargetKind::Loop &&
           (!fact.target.is<LoopControlTarget>() ||
            fact.target.get<LoopControlTarget>().scope != expectedScope)) ||
          (targetKind == ExpectedTargetKind::Match &&
           (!fact.target.is<MatchControlTarget>() ||
            fact.target.get<MatchControlTarget>().scope != expectedScope))) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      consumedFacts[factIndex] = true;
      return;
    }

    if (factIndex != kMissing) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    if (resolutionIndex == kMissing) {
      result = ControlOracleResult::MissingRequiredResolution;
      return;
    }
    const auto& resolution = candidate.nodeBindings[resolutionIndex];
    if (!resolution.value.is<FailedBindingResolution>()) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    const size_t failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
    if (failureIndex >= candidate.sourceFailures.size() || consumedFailures[failureIndex]) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    const auto& failureFact = candidate.sourceFailures[failureIndex];
    const auto expectedDiagnostic = isBreak ? BinderDiagnosticCode::BreakTargetNotFound
                                            : BinderDiagnosticCode::ContinueTargetNotFound;
    auto expectedPrimary = input.parsedModule().leadingTokenSpan(
        node, isBreak ? ast::SyntaxKind::BreakKeyword : ast::SyntaxKind::ContinueKeyword);
    const uint8_t emitterSite = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
    const uint32_t schemaOrdinal =
        static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
    const uint16_t localOrdinal = static_cast<uint16_t>(failureFact.emitterOrdinal);
    if (failureFact.diagnostic != expectedDiagnostic || expectedPrimary == zc::none ||
        !failureFact.notes.empty() ||
        emitterSite != static_cast<uint8_t>(BinderEmitterSite::BodyBinding) ||
        schemaOrdinal != schemaOrdinals[node.value] || localOrdinal != 0) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    ZC_IF_SOME(primary, expectedPrimary) {
      if (!sameSpan(failureFact.primary, primary)) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
    }
    consumedFailures[failureIndex] = true;
  });
  if (result != ControlOracleResult::Valid) { return result; }
  for (const auto consumed : consumedFacts) {
    if (!consumed) { return ControlOracleResult::InvalidBindingFact; }
  }
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const auto diagnostic = candidate.sourceFailures[index].diagnostic;
    if ((diagnostic == BinderDiagnosticCode::BreakTargetNotFound ||
         diagnostic == BinderDiagnosticCode::ContinueTargetNotFound) &&
        !consumedFailures[index]) {
      return ControlOracleResult::InvalidBindingFact;
    }
  }
  return ControlOracleResult::Valid;
}

BinderInvariantKind controlOracleInvariant(ControlOracleResult result) {
  switch (result) {
    case ControlOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ControlOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ControlOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ControlOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

bool encodeScopeId(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                   ScopeId id) {
  if (id.module() != input.module() || !id.belongsTo(input.semanticContext())) { return false; }
  input.moduleKey().encode(encoder);
  encoder.encodeUint32(id.index());
  return true;
}

bool encodeControlTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                         const ControlTarget& target) {
  if (target.is<LoopControlTarget>()) {
    encoder.encodeUint8(0x02);
    return encodeScopeId(encoder, input, target.get<LoopControlTarget>().scope);
  }
  if (target.is<MatchControlTarget>()) {
    encoder.encodeUint8(0x03);
    return encodeScopeId(encoder, input, target.get<MatchControlTarget>().scope);
  }
  return false;
}

bool encodeDefinition(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      identity::DefId definition) {
  ZC_IF_SOME(key, input.definitions().definitionKey(definition)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeImplementation(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                          identity::ImplId implementation) {
  ZC_IF_SOME(key, input.definitions().implKey(implementation)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                  const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeDefinition(encoder, input, value.get<DefinitionBindingTarget>().definition);
  }
  const auto module = value.get<ModuleBindingTarget>().module;
  if (module != input.module()) { return false; }
  encoder.encodeUint8(0x02);
  input.moduleKey().encode(encoder);
  return true;
}

void encodeName(identity::CanonicalEncoder& encoder, const BindingNameKey& name) {
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
}

bool encodeVisibility(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const VisibilityEnvelope& visibility) {
  const auto& value = visibility.value();
  if (value.is<ModuleVisibility>()) {
    if (value.get<ModuleVisibility>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  encoder.encodeUint8(0x02);
  return true;
}

void encodeMaybeSpan(identity::CanonicalEncoder& encoder,
                     const zc::Maybe<identity::SourceSpan>& span) {
  ZC_IF_SOME(value, span) {
    encoder.encodeSome();
    value.encode(encoder);
    return;
  }
  encoder.encodeNone();
}

bool encodeEntry(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                 const ExportSurfaceEntry& entry) {
  encodeName(encoder, entry.name);
  if (!encodeTarget(encoder, input, entry.bindingIdentity) ||
      !encodeTarget(encoder, input, entry.canonicalTarget) ||
      !encodeVisibility(encoder, input, entry.visibility)) {
    return false;
  }
  encoder.encodeBool(entry.exported);
  entry.bindingSpan.encode(encoder);
  entry.canonicalDeclarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, entry.aliasSpan);
  encodeMaybeSpan(encoder, entry.exportSpan);
  encoder.encodeSequenceSize(entry.reexportChain.size());
  for (const auto& step : entry.reexportChain) {
    if (step.module != input.module() || !encodeDefinition(encoder, input, step.alias)) {
      return false;
    }
    input.moduleKey().encode(encoder);
    if (!encodeTarget(encoder, input, step.canonicalTarget)) { return false; }
    step.exportSpan.encode(encoder);
  }
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeSurfaceMap(const VerifiedBindingInput& input,
                                               zc::ArrayPtr<const ExportSurfaceEntry> entries) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(entries.size());
  for (const auto& entry : entries) {
    encodeName(encoder, entry.name);
    if (!encodeEntry(encoder, input, entry)) { return zc::none; }
  }
  return encoder.finish();
}

bool encodeScopeOwner(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const ScopeOwner& owner) {
  const auto& value = owner.value();
  if (value.is<ModuleScopeOwner>()) {
    if (value.get<ModuleScopeOwner>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  if (value.is<DefinitionScopeOwner>()) {
    encoder.encodeUint8(0x02);
    return encodeDefinition(encoder, input, value.get<DefinitionScopeOwner>().definition);
  }
  encoder.encodeUint8(0x03);
  return encodeImplementation(encoder, input, value.get<ImplScopeOwner>().implementation);
}

zc::Maybe<zc::Array<uint8_t>> encodeAllocationScopeRecord(const VerifiedBindingInput& input,
                                                          const ScopeRecord& scope) {
  if (scope.id.module() != input.module() || !scope.id.belongsTo(input.semanticContext())) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  input.moduleKey().encode(encoder);
  encoder.encodeUint32(scope.id.index());
  ZC_IF_SOME(parent, scope.parent) {
    if (parent.module() != input.module() || !parent.belongsTo(input.semanticContext())) {
      return zc::none;
    }
    encoder.encodeSome();
    encoder.encodeUint32(parent.index());
  }
  else { encoder.encodeNone(); }
  if (!encodeScopeOwner(encoder, input, scope.owner)) { return zc::none; }
  encoder.encodeUint8(static_cast<uint8_t>(scope.kind));
  scope.source.encode(encoder);
  return encoder.finish();
}

bool encodeNameBinding(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                       const NameBinding& binding) {
  if (!encodeTarget(encoder, input, binding.bindingIdentity) ||
      !encodeTarget(encoder, input, binding.canonicalTarget)) {
    return false;
  }
  encoder.encodeUint8(static_cast<uint8_t>(binding.nameSpace));
  encoder.encodeUint8(static_cast<uint8_t>(binding.origin));
  binding.declarationSpan.encode(encoder);
  encodeMaybeSpan(encoder, binding.aliasSpan);
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeCandidate(const VerifiedBindingInput& input,
                                              const BindingMetadataCandidate& candidate) {
  identity::CanonicalEncoder encoder;
  input.moduleKey().encode(encoder);
  encoder.encodeSequenceSize(candidate.sourceFailures.size());
  for (const auto& failureFact : candidate.sourceFailures) {
    encoder.encodeUint32(static_cast<uint32_t>(failureFact.diagnostic));
    failureFact.primary.encode(encoder);
    encoder.encodeUint64(failureFact.emitterOrdinal);
    encoder.encodeSequenceSize(failureFact.notes.size());
    for (const auto& note : failureFact.notes) {
      encoder.encodeUint32(static_cast<uint32_t>(note.diagnostic));
      note.source.encode(encoder);
    }
  }
  encoder.encodeSequenceSize(candidate.nodeScopes.size());
  for (const auto& nodeScope : candidate.nodeScopes) {
    encoder.encodeUint32(nodeScope.node.value);
    if (!encodeScopeId(encoder, input, nodeScope.scope)) { return zc::none; }
  }
  encoder.encodeSequenceSize(candidate.nodeBindings.size());
  for (const auto& binding : candidate.nodeBindings) {
    encoder.encodeUint32(binding.node.value);
    const auto& value = binding.value;
    if (value.is<BoundNameResolution>()) {
      const auto& bound = value.get<BoundNameResolution>();
      encoder.encodeUint8(0x01);
      if (!encodeTarget(encoder, input, bound.bindingIdentity) ||
          !encodeTarget(encoder, input, bound.canonicalTarget)) {
        return zc::none;
      }
      encoder.encodeUint8(static_cast<uint8_t>(bound.nameSpace));
      encoder.encodeUint8(static_cast<uint8_t>(bound.origin));
    } else if (value.is<FailedBindingResolution>()) {
      encoder.encodeUint8(0x04);
      encoder.encodeUint64(value.get<FailedBindingResolution>().failureIndex);
    } else {
      return zc::none;
    }
  }
  encoder.encodeSequenceSize(candidate.definitions.size());
  for (const auto& fact : candidate.definitions) {
    if (!encodeDefinition(encoder, input, fact.identity)) { return zc::none; }
    const auto& site = fact.site.value();
    if (site.is<DeclarationDefinitionSite>()) {
      encoder.encodeUint8(0x01);
      encoder.encodeUint32(site.get<DeclarationDefinitionSite>().node.value);
    } else {
      const auto& pattern = site.get<PatternBindingSite>();
      encoder.encodeUint8(0x02);
      encoder.encodeUint32(pattern.introducer.value);
      encoder.encodeSequenceSize(pattern.patternPath.size());
      for (const auto component : pattern.patternPath) { encoder.encodeUint32(component); }
    }
    encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
    fact.name.encode(encoder);
    encoder.encodeUint8(static_cast<uint8_t>(fact.nameSpace));
    if (!encodeScopeId(encoder, input, fact.declaringScope)) { return zc::none; }
    fact.source.encode(encoder);
    encoder.encodeUint8(static_cast<uint8_t>(fact.activation));
  }
  encoder.encodeSequenceSize(candidate.impls.size());
  for (const auto& fact : candidate.impls) {
    if (!encodeImplementation(encoder, input, fact.identity)) { return zc::none; }
    encoder.encodeUint32(fact.node.value);
    if (!encodeScopeId(encoder, input, fact.scope)) { return zc::none; }
    encoder.encodeSequenceSize(fact.members.size());
    for (const auto member : fact.members) {
      if (!encodeDefinition(encoder, input, member)) { return zc::none; }
    }
    fact.source.encode(encoder);
  }
  encoder.encodeSequenceSize(candidate.scopes.size());
  for (const auto& scope : candidate.scopes) {
    if (!encodeScopeId(encoder, input, scope.id)) { return zc::none; }
    ZC_IF_SOME(parent, scope.parent) {
      encoder.encodeSome();
      if (!encodeScopeId(encoder, input, parent)) { return zc::none; }
    }
    else { encoder.encodeNone(); }
    if (!encodeScopeOwner(encoder, input, scope.owner)) { return zc::none; }
    encoder.encodeUint8(static_cast<uint8_t>(scope.kind));
    encoder.encodeSequenceSize(scope.bindings.size());
    for (const auto& binding : scope.bindings) {
      encodeName(encoder, binding.name);
      if (!encodeNameBinding(encoder, input, binding.binding)) { return zc::none; }
    }
    scope.source.encode(encoder);
  }
  encoder.encodeSequenceSize(candidate.moduleAliases.size());
  encoder.encodeSequenceSize(candidate.imports.size());
  encoder.encodeSequenceSize(candidate.localExports.size());
  encoder.encodeSequenceSize(candidate.deferredMembers.size());
  encoder.encodeSequenceSize(candidate.labels.size());
  encoder.encodeSequenceSize(candidate.controlTransfers.size());
  for (const auto& fact : candidate.controlTransfers) {
    encoder.encodeUint32(fact.node.value);
    encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
    if (!encodeControlTarget(encoder, input, fact.target)) { return zc::none; }
    fact.source.encode(encoder);
  }
  encoder.encodeSequenceSize(candidate.shadowTargets.size());
  for (const auto& shadow : candidate.shadowTargets) {
    if (!encodeDefinition(encoder, input, shadow.definition) ||
        !encodeTarget(encoder, input, shadow.target)) {
      return zc::none;
    }
  }
  encoder.encodeSequenceSize(candidate.closureFreeVariables.size());
  if (candidate.currentSurface.sourceModule != input.module() ||
      candidate.currentSurface.sourcePackage != input.package()) {
    return zc::none;
  }
  input.moduleKey().encode(encoder);
  input.packageKey().encode(encoder);
  encoder.encodeDigest(candidate.currentSurface.revision.digest());
  ZC_IF_SOME(visible, encodeSurfaceMap(input, candidate.currentSurface.visibleEntries.asPtr())) {
    encoder.encodeByteString(visible.asPtr());
  }
  else { return zc::none; }
  ZC_IF_SOME(exports, encodeSurfaceMap(input, candidate.currentSurface.exports.asPtr())) {
    encoder.encodeByteString(exports.asPtr());
  }
  else { return zc::none; }
  return encoder.finish();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> encodeBindingAllocationDump(const VerifiedBindingInput& input,
                                                          zc::ArrayPtr<const ScopeRecord> scopes) {
  zc::Vector<zc::Array<uint8_t>> storage;
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].id.index() != index) { return zc::none; }
    auto encoded = encodeAllocationScopeRecord(input, scopes[index]);
    ZC_IF_SOME(value, encoded) { storage.add(zc::mv(value)); }
    else { return zc::none; }
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> records;
  for (const auto& value : storage) { records.add(value.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> labels;
  return frameBindingAllocationDump(records.asPtr(), labels.asPtr());
}

ExportSurfaceCandidate::ExportSurfaceCandidate(identity::ModuleId sourceModule,
                                               identity::PackageId sourcePackage,
                                               ExportSurfaceRevision revision,
                                               zc::Vector<ExportSurfaceEntry>&& visibleEntries,
                                               zc::Vector<ExportSurfaceEntry>&& exports) noexcept
    : sourceModule(sourceModule),
      sourcePackage(sourcePackage),
      revision(revision),
      visibleEntries(zc::mv(visibleEntries)),
      exports(zc::mv(exports)) {}

ExportSurfaceCandidate ExportSurfaceCandidate::clone() const {
  zc::Vector<ExportSurfaceEntry> visible;
  for (const auto& entry : visibleEntries) { visible.add(cloneEntry(entry)); }
  zc::Vector<ExportSurfaceEntry> external;
  for (const auto& entry : exports) { external.add(cloneEntry(entry)); }
  return ExportSurfaceCandidate(sourceModule, sourcePackage, revision, zc::mv(visible),
                                zc::mv(external));
}

BindingMetadataCandidate::BindingMetadataCandidate(identity::SemanticContextBrand semanticContext,
                                                   identity::ModuleId module,
                                                   zc::Vector<NodeScopeFact>&& nodeScopes,
                                                   zc::Vector<DefinitionFact>&& definitions,
                                                   zc::Vector<ImplBindingFact>&& impls,
                                                   zc::Vector<ScopeRecord>&& scopes,
                                                   ExportSurfaceCandidate&& currentSurface) noexcept
    : semanticContext(semanticContext),
      module(module),
      nodeScopes(zc::mv(nodeScopes)),
      definitions(zc::mv(definitions)),
      impls(zc::mv(impls)),
      scopes(zc::mv(scopes)),
      currentSurface(zc::mv(currentSurface)) {}

VerifiedBindingOutput::VerifiedBindingOutput(VerifiedBindingMetadata&& metadata,
                                             VerifiedExportSurface&& surface) noexcept
    : metadata(zc::mv(metadata)), surface(zc::mv(surface)) {}

SourceRejected::SourceRejected(zc::Vector<BindingFailureRef>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
zc::ArrayPtr<const BindingFailureRef> SourceRejected::failures() const noexcept {
  return failureValues.asPtr();
}

BindingVerificationFailure::BindingVerificationFailure(
    BindingVerificationFailureValue&& value) noexcept
    : value(zc::mv(value)) {}

InvariantRejected::InvariantRejected(zc::Vector<BindingVerificationFailure>&& failures) noexcept
    : failureValues(zc::mv(failures)) {}
InvariantRejected InvariantRejected::single(BindingVerificationFailure&& failure) {
  zc::Vector<BindingVerificationFailure> failures;
  failures.add(zc::mv(failure));
  return InvariantRejected(zc::mv(failures));
}
zc::ArrayPtr<const BindingVerificationFailure> InvariantRejected::failures() const noexcept {
  return failureValues.asPtr();
}

struct VerifiedBindingMetadata::Impl final {
  explicit Impl(BindingMetadataCandidate&& candidate) : candidate(zc::mv(candidate)) {}
  BindingMetadataCandidate candidate;
};

VerifiedBindingMetadata::VerifiedBindingMetadata(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBindingMetadata::~VerifiedBindingMetadata() noexcept(false) = default;
VerifiedBindingMetadata::VerifiedBindingMetadata(VerifiedBindingMetadata&&) noexcept = default;
VerifiedBindingMetadata& VerifiedBindingMetadata::operator=(VerifiedBindingMetadata&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedBindingMetadata::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
identity::ModuleId VerifiedBindingMetadata::module() const noexcept {
  return impl->candidate.module;
}
zc::ArrayPtr<const NodeScopeFact> VerifiedBindingMetadata::nodeScopes() const {
  return impl->candidate.nodeScopes.asPtr();
}
zc::ArrayPtr<const BindingResolution> VerifiedBindingMetadata::nodeBindings() const {
  return impl->candidate.nodeBindings.asPtr();
}
zc::ArrayPtr<const ScopeRecord> VerifiedBindingMetadata::scopes() const {
  return impl->candidate.scopes.asPtr();
}
zc::ArrayPtr<const DefinitionFact> VerifiedBindingMetadata::definitions() const {
  return impl->candidate.definitions.asPtr();
}
zc::ArrayPtr<const ImplBindingFact> VerifiedBindingMetadata::impls() const {
  return impl->candidate.impls.asPtr();
}
zc::ArrayPtr<const ModuleAliasBindingFact> VerifiedBindingMetadata::moduleAliases() const {
  return impl->candidate.moduleAliases.asPtr();
}
zc::ArrayPtr<const ImportBindingFact> VerifiedBindingMetadata::imports() const {
  return impl->candidate.imports.asPtr();
}
zc::ArrayPtr<const LocalExportFact> VerifiedBindingMetadata::localExports() const {
  return impl->candidate.localExports.asPtr();
}
zc::ArrayPtr<const DeferredMemberFact> VerifiedBindingMetadata::deferredMembers() const {
  return impl->candidate.deferredMembers.asPtr();
}
zc::ArrayPtr<const LabelFact> VerifiedBindingMetadata::labels() const {
  return impl->candidate.labels.asPtr();
}
zc::ArrayPtr<const ControlTransferFact> VerifiedBindingMetadata::controlTransfers() const {
  return impl->candidate.controlTransfers.asPtr();
}
zc::ArrayPtr<const ShadowTargetFact> VerifiedBindingMetadata::shadowTargets() const {
  return impl->candidate.shadowTargets.asPtr();
}
zc::ArrayPtr<const ClosureFreeVariableFact> VerifiedBindingMetadata::closureFreeVariables() const {
  return impl->candidate.closureFreeVariables.asPtr();
}

struct VerifiedExportSurface::Impl final {
  explicit Impl(ExportSurfaceCandidate&& candidate) : candidate(zc::mv(candidate)) {}
  ExportSurfaceCandidate candidate;
};

VerifiedExportSurface::VerifiedExportSurface(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedExportSurface::~VerifiedExportSurface() noexcept(false) = default;
VerifiedExportSurface::VerifiedExportSurface(VerifiedExportSurface&&) noexcept = default;
VerifiedExportSurface& VerifiedExportSurface::operator=(VerifiedExportSurface&&) noexcept = default;
identity::ModuleId VerifiedExportSurface::sourceModule() const noexcept {
  return impl->candidate.sourceModule;
}
identity::PackageId VerifiedExportSurface::sourcePackage() const noexcept {
  return impl->candidate.sourcePackage;
}
const ExportSurfaceRevision& VerifiedExportSurface::revision() const noexcept {
  return impl->candidate.revision;
}
zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::visibleEntries() const {
  return impl->candidate.visibleEntries.asPtr();
}
zc::ArrayPtr<const ExportSurfaceEntry> VerifiedExportSurface::exports() const {
  return impl->candidate.exports.asPtr();
}

BindingCandidateResult BindingBuilder::build(const VerifiedBindingInput& input,
                                             diagnostics::DiagnosticEngine& diagnostics) {
  return buildCandidate(input, diagnostics);
}

BindingCandidateResult BindingBuilder::buildCandidate(
    const VerifiedBindingInput& input, zc::Maybe<diagnostics::DiagnosticEngine&> diagnostics) {
  const auto& tree = input.tree();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) {
    return zc::mv(arenaResult.get<BinderInvariantFact>());
  }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module) {
    return builderFailure(input, BinderInvariantKind::MalformedScopeGraph);
  }
  auto skeletonResult = BindingSkeletonBuilder::build(input, arena);
  if (!skeletonResult.is<DefinitionSkeletonCandidate>()) {
    return zc::mv(skeletonResult.get<BinderInvariantFact>());
  }
  auto skeleton = zc::mv(skeletonResult.get<DefinitionSkeletonCandidate>());
  auto bodyResult = BodyBindingBuilder::build(input, arena, skeleton);
  if (!bodyResult.is<BodyBindingCandidate>()) {
    return zc::mv(bodyResult.get<BinderInvariantFact>());
  }
  auto body = zc::mv(bodyResult.get<BodyBindingCandidate>());
  auto controlResult = ControlTransferBuilder::build(input, arena);
  if (!controlResult.is<ControlTransferCandidate>()) {
    return zc::mv(controlResult.get<BinderInvariantFact>());
  }
  auto control = zc::mv(controlResult.get<ControlTransferCandidate>());

  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) {
    return bodyBuilderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal);
  }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));
  zc::TreeMap<PendingFailureOrderKey, PendingFailureRef> failureOrder;
  size_t sequence = 0;
  for (size_t index = 0; index < skeleton.duplicates.size(); ++index) {
    const auto& duplicate = skeleton.duplicates[index];
    if (!tree.contains(duplicate.primaryNode) ||
        schemaOrdinals[duplicate.primaryNode.value] == UINT32_MAX) {
      return builderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal);
    }
    const auto ordinal = schemaOrdinals[duplicate.primaryNode.value];
    failureOrder.insert(
        PendingFailureOrderKey{duplicate.primary.byteStart(), duplicate.primary.byteEnd(),
                               static_cast<uint16_t>(duplicate.diagnostic),
                               static_cast<uint8_t>(duplicate.emitterSite), ordinal, sequence++},
        PendingFailureRef{PendingFailureKind::Duplicate, index});
  }
  for (size_t index = 0; index < body.failures.size(); ++index) {
    const auto& bodyFailure = body.failures[index];
    failureOrder.insert(
        PendingFailureOrderKey{bodyFailure.source.byteStart(), bodyFailure.source.byteEnd(),
                               static_cast<uint16_t>(bodyFailure.diagnostic),
                               static_cast<uint8_t>(BinderEmitterSite::BodyBinding),
                               bodyFailure.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::BodyLookup, index});
  }
  for (size_t index = 0; index < control.failures.size(); ++index) {
    const auto& controlFailure = control.failures[index];
    failureOrder.insert(
        PendingFailureOrderKey{controlFailure.source.byteStart(), controlFailure.source.byteEnd(),
                               static_cast<uint16_t>(controlFailure.diagnostic),
                               static_cast<uint8_t>(BinderEmitterSite::BodyBinding),
                               controlFailure.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::ControlTransfer, index});
  }

  zc::Vector<BindingFailureRef> sourceFailures;
  uint8_t previousSite = 0;
  uint32_t previousOrdinal = 0;
  uint16_t localOrdinal = 0;
  bool hasPrevious = false;
  for (const auto& ordered : failureOrder) {
    const auto site = ordered.key.emitterSite;
    const auto schemaOrdinal = ordered.key.schemaPreorderOrdinal;
    if (hasPrevious && site == previousSite && schemaOrdinal == previousOrdinal) {
      if (localOrdinal == UINT16_MAX) {
        return builderFailure(input, BinderInvariantKind::InvalidEmitterOrdinal, schemaOrdinal);
      }
      ++localOrdinal;
    } else {
      localOrdinal = 0;
    }
    hasPrevious = true;
    previousSite = site;
    previousOrdinal = schemaOrdinal;
    const uint64_t emitterOrdinal =
        (uint64_t(site) << 56) | (uint64_t(schemaOrdinal) << 16) | localOrdinal;

    if (ordered.value.kind == PendingFailureKind::Duplicate) {
      const auto& duplicate = skeleton.duplicates[ordered.value.index];
      ZC_IF_SOME(engine, diagnostics) {
        if (!BindingDiagnosticAdapter::emitRedeclaration(
                engine, duplicate.diagnostic, tree.node(duplicate.primaryNode).range.getStart(),
                tree.node(duplicate.previousNode).range.getStart(),
                VerifiedIdentifierArgument::from(duplicate.name))) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, duplicate.emitterSite,
                         schemaOrdinal);
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> notes;
      notes.add(BindingDiagnosticNoteRef{BinderDiagnosticCode::PreviousDeclarationHere,
                                         duplicate.previous.clone()});
      sourceFailures.add(BindingFailureRef{duplicate.diagnostic, duplicate.primary.clone(),
                                           emitterOrdinal, zc::mv(notes)});
      continue;
    }

    if (ordered.value.kind == PendingFailureKind::ControlTransfer) {
      const auto& controlFailure = control.failures[ordered.value.index];
      ZC_IF_SOME(engine, diagnostics) {
        if (!BindingDiagnosticAdapter::emitControlTransferFailure(
                engine, controlFailure.diagnostic,
                tree.node(controlFailure.node).range.getStart())) {
          return bodyBuilderFailure(input, BinderInvariantKind::InvalidBindingFact, schemaOrdinal);
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> noNotes;
      const size_t failureIndex = sourceFailures.size();
      sourceFailures.add(BindingFailureRef{controlFailure.diagnostic, controlFailure.source.clone(),
                                           emitterOrdinal, zc::mv(noNotes)});
      body.nodeBindings.add(BindingResolution{
          controlFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})});
      continue;
    }

    const auto& bodyFailure = body.failures[ordered.value.index];
    ZC_IF_SOME(engine, diagnostics) {
      if (!BindingDiagnosticAdapter::emitLookupFailure(
              engine, bodyFailure.diagnostic, tree.node(bodyFailure.node).range.getStart(),
              VerifiedIdentifierArgument::from(bodyFailure.name), bodyFailure.expectedNamespace)) {
        return bodyBuilderFailure(input, BinderInvariantKind::InvalidBindingFact, schemaOrdinal);
      }
    }
    zc::Vector<BindingDiagnosticNoteRef> noNotes;
    const size_t failureIndex = sourceFailures.size();
    sourceFailures.add(BindingFailureRef{bodyFailure.diagnostic, bodyFailure.source.clone(),
                                         emitterOrdinal, zc::mv(noNotes)});
    body.nodeBindings.add(BindingResolution{
        bodyFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})});
  }

  zc::TreeMap<uint32_t, size_t> bindingOrder;
  for (size_t index = 0; index < body.nodeBindings.size(); ++index) {
    const auto node = body.nodeBindings[index].node;
    if (!tree.contains(node) || bindingOrder.find(node.value) != zc::none) {
      const uint32_t ordinal =
          tree.contains(node) ? schemaOrdinals[node.value] : static_cast<uint32_t>(0);
      return bodyBuilderFailure(input, BinderInvariantKind::InvalidBindingFact, ordinal);
    }
    bindingOrder.insert(node.value, index);
  }
  zc::Vector<BindingResolution> nodeBindings;
  for (const auto& ordered : bindingOrder) {
    nodeBindings.add(zc::mv(body.nodeBindings[ordered.value]));
  }

  zc::Vector<ExportSurfaceEntry> visibleEntries;
  zc::Vector<ExportSurfaceEntry> exports;
  for (auto& seed : skeleton.moduleSurfaceSeeds) {
    zc::Maybe<identity::SourceSpan> noSurfaceAlias;
    zc::Vector<ReexportProvenanceStep> noChain;
    auto entry = ExportSurfaceEntry(
        zc::mv(seed.name), BindingTarget::definition(seed.identity),
        BindingTarget::definition(seed.identity),
        seed.exported ? VisibilityEnvelope::external() : VisibilityEnvelope::module(input.module()),
        seed.exported, seed.source.clone(), seed.source.clone(), zc::mv(noSurfaceAlias),
        zc::mv(seed.exportSpan), zc::mv(noChain));
    if (entry.exported) { exports.add(cloneEntry(entry)); }
    visibleEntries.add(zc::mv(entry));
  }
  auto encodedVisible = encodeSurfaceMap(input, visibleEntries.asPtr());
  auto encodedExports = encodeSurfaceMap(input, exports.asPtr());
  if (encodedVisible == zc::none || encodedExports == zc::none) {
    return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
  }
  ZC_IF_SOME(visible, encodedVisible) {
    ZC_IF_SOME(external, encodedExports) {
      const auto moduleBytes = input.moduleKey().encode();
      const auto packageBytes = input.packageKey().encode();
      auto revision = ExportSurfaceRevision::computeFramed(
          input.semanticFingerprint().digest(), moduleBytes.asPtr(), packageBytes.asPtr(),
          visible.asPtr(), external.asPtr());
      ZC_IF_SOME(revisionValue, revision) {
        ExportSurfaceCandidate surface(input.module(), input.package(), revisionValue,
                                       zc::mv(visibleEntries), zc::mv(exports));
        BindingMetadataCandidate candidate(input.semanticContext(), input.module(),
                                           zc::mv(arena.nodeScopes), zc::mv(skeleton.definitions),
                                           zc::mv(skeleton.impls), zc::mv(arena.scopes),
                                           zc::mv(surface));
        candidate.sourceFailures = zc::mv(sourceFailures);
        candidate.nodeBindings = zc::mv(nodeBindings);
        candidate.controlTransfers = zc::mv(control.controlTransfers);
        candidate.shadowTargets = zc::mv(body.shadowTargets);
        return candidate;
      }
    }
  }
  return builderFailure(input, BinderInvariantKind::InvalidBindingFact);
}

BindingVerificationResult BindingVerifier::verify(const VerifiedBindingInput& input,
                                                  BindingMetadataCandidate&& candidate) {
  auto expectedResult = BindingBuilder::buildCandidate(input, zc::none);
  if (!expectedResult.is<BindingMetadataCandidate>()) {
    return rejectBinderInvariant(zc::mv(expectedResult.get<BinderInvariantFact>()));
  }
  if (hasForeignContext(input, candidate)) { return rejectForeignContext(input); }
  if (hasInvalidSourceRange(input, candidate)) { return rejectInvalidSourceRange(input); }
  const auto& expected = expectedResult.get<BindingMetadataCandidate>();
  const auto expectedControl = verifyControlTransferFacts(input, expected);
  if (expectedControl != ControlOracleResult::Valid) {
    return rejectBinderInvariant(verifierFailure(input, controlOracleInvariant(expectedControl)));
  }
  const auto candidateControl = verifyControlTransferFacts(input, candidate);
  if (candidateControl != ControlOracleResult::Valid) {
    return rejectBinderInvariant(verifierFailure(input, controlOracleInvariant(candidateControl)));
  }
  if (!hasCompleteLexicalBindingSites(input.tree(), expected.nodeBindings.asPtr())) {
    return rejectBinderInvariant(
        bodyBuilderFailure(input, BinderInvariantKind::MissingRequiredResolution));
  }
  if (candidate.scopes.size() < expected.scopes.size() ||
      candidate.definitions.size() < expected.definitions.size() ||
      candidate.impls.size() < expected.impls.size() ||
      candidate.nodeScopes.size() < expected.nodeScopes.size() ||
      candidate.sourceFailures.size() < expected.sourceFailures.size() ||
      candidate.nodeBindings.size() < expected.nodeBindings.size() ||
      candidate.controlTransfers.size() < expected.controlTransfers.size() ||
      candidate.shadowTargets.size() < expected.shadowTargets.size()) {
    return rejectBinderInvariant(
        verifierFailure(input, BinderInvariantKind::MissingRequiredResolution));
  }
  if (candidate.scopes.size() > expected.scopes.size() ||
      candidate.definitions.size() > expected.definitions.size() ||
      candidate.impls.size() > expected.impls.size() ||
      candidate.nodeScopes.size() > expected.nodeScopes.size() ||
      candidate.sourceFailures.size() > expected.sourceFailures.size() ||
      candidate.nodeBindings.size() > expected.nodeBindings.size() ||
      candidate.controlTransfers.size() > expected.controlTransfers.size() ||
      candidate.shadowTargets.size() > expected.shadowTargets.size()) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  if (!candidate.moduleAliases.empty() || !candidate.imports.empty() ||
      !candidate.localExports.empty() || !candidate.deferredMembers.empty() ||
      !candidate.labels.empty() || !candidate.closureFreeVariables.empty()) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  auto candidateAllocation = encodeBindingAllocationDump(input, candidate.scopes.asPtr());
  auto expectedAllocation = encodeBindingAllocationDump(input, expected.scopes.asPtr());
  if (candidateAllocation == zc::none || expectedAllocation == zc::none) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::MalformedScopeGraph));
  }
  ZC_IF_SOME(candidateDump, candidateAllocation) {
    ZC_IF_SOME(expectedDump, expectedAllocation) {
      if (!sameBytes(candidateDump.asPtr(), expectedDump.asPtr())) {
        return rejectBinderInvariant(
            verifierFailure(input, BinderInvariantKind::MalformedScopeGraph));
      }
    }
  }
  auto candidateBytes = encodeCandidate(input, candidate);
  auto expectedBytes = encodeCandidate(input, expected);
  if (candidateBytes == zc::none || expectedBytes == zc::none) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  ZC_IF_SOME(candidateValue, candidateBytes) {
    ZC_IF_SOME(expectedValue, expectedBytes) {
      if (!sameBytes(candidateValue.asPtr(), expectedValue.asPtr())) {
        return rejectBinderInvariant(
            verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
      }
    }
  }
  if (!candidate.sourceFailures.empty()) {
    return SourceRejected(zc::mv(candidate.sourceFailures));
  }
  auto surface = candidate.currentSurface.clone();
  return VerifiedBindingOutput(
      VerifiedBindingMetadata(zc::heap<VerifiedBindingMetadata::Impl>(zc::mv(candidate))),
      VerifiedExportSurface(zc::heap<VerifiedExportSurface::Impl>(zc::mv(surface))));
}

}  // namespace zomlang::compiler::binder
