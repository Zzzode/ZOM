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
#include "zomlang/compiler/binder/internal/label-facts.h"
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

enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, LabelDuplicate, ControlTransfer };

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

bool labelIdHasForeignContext(const VerifiedBindingInput& input, const LabelId& label) {
  if (!label.belongsTo(input.semanticContext())) { return true; }
  const auto& owner = label.owner().value();
  if (owner.is<ModuleLabelOwner>()) {
    return owner.get<ModuleLabelOwner>().module != input.module();
  }
  return input.definitions().definitionKey(owner.get<CallableLabelOwner>().callable) == zc::none;
}

bool labelTargetHasForeignContext(const VerifiedBindingInput& input, const LabelTarget& target) {
  if (!target.belongsTo(input.semanticContext())) { return true; }
  const auto& value = target.value();
  const auto scope = value.is<BlockLabelTarget>() ? value.get<BlockLabelTarget>().scope
                                                  : value.get<LoopLabelTarget>().scope;
  return scope.module() != input.module() || !scope.belongsTo(input.semanticContext());
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
    } else if (value.is<BoundLabelResolution>()) {
      const auto& bound = value.get<BoundLabelResolution>();
      if (labelIdHasForeignContext(input, bound.label) ||
          labelTargetHasForeignContext(input, bound.target)) {
        return true;
      }
    }
  }
  for (const auto& fact : candidate.labels) {
    if (!fact.owner.belongsTo(input.semanticContext()) ||
        labelIdHasForeignContext(input, fact.identity) ||
        labelTargetHasForeignContext(input, fact.target)) {
      return true;
    }
    const auto& owner = fact.owner.value();
    if (owner.is<ModuleLabelOwner>()) {
      if (owner.get<ModuleLabelOwner>().module != input.module()) { return true; }
    } else {
      const auto callable = owner.get<CallableLabelOwner>().callable;
      if (input.definitions().definitionKey(callable) == zc::none) { return true; }
    }
  }
  for (const auto& fact : candidate.controlTransfers) {
    const auto& target = fact.target;
    if (target.is<ExplicitLabelControlTarget>()) {
      if (labelIdHasForeignContext(input, target.get<ExplicitLabelControlTarget>().label)) {
        return true;
      }
    } else if (target.is<LoopControlTarget>()) {
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
  for (const auto& fact : candidate.labels) {
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
          (!binding.value.is<BoundLabelResolution>() &&
           !binding.value.is<FailedBindingResolution>())) {
        return false;
      }
      if (binding.value.is<BoundLabelResolution>()) {
        const auto& syntax = tree.node(binding.node);
        const uint32_t label =
            syntax.payload
                .words[nodeKind == ast::SyntaxKind::BreakStmt ? ast::kBreakStmtLabelWord
                                                              : ast::kContinueStatementLabelWord];
        if (label == 0) { return false; }
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

enum class LabelOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

struct OracleLabelOwner final {
  bool callable;
  identity::DefId definition;
};

struct OracleLabelCounter final {
  OracleLabelOwner owner;
  uint64_t nextIndex;
};

struct OracleLabelTarget final {
  bool loop;
  ScopeId scope;
};

struct OracleLabelRecord final {
  ast::NodeId node;
  ast::NodeId statement;
  OracleLabelOwner owner;
  uint32_t index;
  identity::SemanticIdentifier name;
  OracleLabelTarget target;
  identity::SourceSpan source;
  zc::Maybe<identity::SourceSpan> previous;
  uint32_t schemaPreorderOrdinal;
  zc::Array<uint8_t> ownerKey;
};

bool sameOracleOwner(const OracleLabelOwner& left, const OracleLabelOwner& right) {
  return left.callable == right.callable && (!left.callable || left.definition == right.definition);
}

int compareCanonicalBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool oracleLabelLess(const OracleLabelRecord& left, const OracleLabelRecord& right) {
  if (left.owner.callable != right.owner.callable) { return !left.owner.callable; }
  const int keyOrder = compareCanonicalBytes(left.ownerKey.asPtr(), right.ownerKey.asPtr());
  if (keyOrder != 0) { return keyOrder < 0; }
  return left.index < right.index;
}

bool oracleLoopKind(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::WhileStmt || kind == ast::SyntaxKind::ForStmt ||
         kind == ast::SyntaxKind::ForInStatement || kind == ast::SyntaxKind::DoWhileStatement;
}

LabelOracleResult verifyLabelFacts(const VerifiedBindingInput& input,
                                   const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) { return LabelOracleResult::MalformedScopeGraph; }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module ||
      arena.nodeScopes.size() != tree.nodeCount()) {
    return LabelOracleResult::MalformedScopeGraph;
  }

  zc::Vector<uint32_t> scopeByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  for (auto& value : scopeByNode) { value = UINT32_MAX; }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return LabelOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.index() != index || scope.id.module() != input.module() ||
        (index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
      return LabelOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index) {
        return LabelOracleResult::MalformedScopeGraph;
      }
    }
  }

  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) { return LabelOracleResult::MalformedScopeGraph; }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));
  zc::Vector<OracleLabelCounter> counters;
  zc::Vector<OracleLabelRecord> expected;
  LabelOracleResult result = LabelOracleResult::Valid;

  const auto ownerFor = [&](ast::NodeId node) -> zc::Maybe<OracleLabelOwner> {
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      return zc::none;
    }
    uint32_t scopeIndex = scopeByNode[node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return zc::none; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        const auto& owner = scope.owner.value();
        if (!owner.is<DefinitionScopeOwner>()) { return zc::none; }
        return OracleLabelOwner{true, owner.get<DefinitionScopeOwner>().definition};
      }
      if (scope.kind == ScopeKind::Module) {
        const auto& owner = scope.owner.value();
        if (!owner.is<ModuleScopeOwner>() ||
            owner.get<ModuleScopeOwner>().module != input.module()) {
          return zc::none;
        }
        return OracleLabelOwner{false, identity::DefId()};
      }
      if (scope.parent == zc::none) { return zc::none; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return zc::none;
  };

  const auto targetFor = [&](ast::NodeId statement) -> zc::Maybe<OracleLabelTarget> {
    ast::NodeId target = statement;
    for (size_t traversed = 0; traversed <= tree.nodeCount(); ++traversed) {
      if (!tree.contains(target)) { return zc::none; }
      const auto& syntax = tree.node(target);
      if (syntax.kind != ast::SyntaxKind::LabeledStatement) { break; }
      target = ast::NodeId(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      if (traversed == tree.nodeCount()) { return zc::none; }
    }
    if (!tree.contains(target) || target.value >= scopeByNode.size() ||
        scopeByNode[target.value] == UINT32_MAX) {
      return zc::none;
    }
    const auto scopeIndex = scopeByNode[target.value];
    if (scopeIndex >= arena.scopes.size()) { return zc::none; }
    const auto& scope = arena.scopes[scopeIndex];
    const auto kind = tree.node(target).kind;
    if (kind == ast::SyntaxKind::BlockStmt && scope.kind == ScopeKind::Block) {
      return OracleLabelTarget{false, scope.id};
    }
    if (oracleLoopKind(kind) && scope.kind == ScopeKind::Loop) {
      return OracleLabelTarget{true, scope.id};
    }
    return zc::none;
  };

  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != LabelOracleResult::Valid || syntax.kind != ast::SyntaxKind::LabeledStatement) {
      return;
    }
    const ast::NodeId statement(syntax.payload.words[ast::kLabeledStatementStatementWord]);
    if (!tree.contains(statement)) {
      result = LabelOracleResult::MissingRequiredResolution;
      return;
    }
    auto owner = ownerFor(node);
    auto target = targetFor(statement);
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
    auto name = identity::SemanticIdentifier::fromSource(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kLabeledStatementLabelWord])));
    if (owner == zc::none || target == zc::none || source == zc::none || name == zc::none) {
      result = LabelOracleResult::InvalidBindingFact;
      return;
    }
    ZC_IF_SOME(ownerValue, owner) {
      size_t counterIndex = counters.size();
      for (size_t index = 0; index < counters.size(); ++index) {
        if (sameOracleOwner(counters[index].owner, ownerValue)) {
          counterIndex = index;
          break;
        }
      }
      if (counterIndex == counters.size()) { counters.add(OracleLabelCounter{ownerValue, 0}); }
      if (counters[counterIndex].nextIndex > static_cast<uint64_t>(UINT32_MAX)) {
        result = LabelOracleResult::InvalidBindingFact;
        return;
      }
      zc::Maybe<identity::SourceSpan> previous;
      ZC_IF_SOME(nameValue, name) {
        for (const auto& prior : expected) {
          if (sameOracleOwner(prior.owner, ownerValue) && prior.name == nameValue) {
            previous = prior.source.clone();
            break;
          }
        }
        zc::Array<uint8_t> ownerKey;
        if (ownerValue.callable) {
          auto key = input.definitions().definitionKey(ownerValue.definition);
          if (key == zc::none) {
            result = LabelOracleResult::MissingRequiredResolution;
            return;
          }
          ZC_IF_SOME(value, key) { ownerKey = value.encode(); }
        } else {
          ownerKey = input.moduleKey().encode();
        }
        ZC_IF_SOME(targetValue, target) {
          ZC_IF_SOME(sourceValue, source) {
            expected.add(OracleLabelRecord{node, statement, ownerValue,
                                           static_cast<uint32_t>(counters[counterIndex].nextIndex),
                                           zc::mv(nameValue), targetValue, zc::mv(sourceValue),
                                           zc::mv(previous), schemaOrdinals[node.value],
                                           zc::mv(ownerKey)});
            ++counters[counterIndex].nextIndex;
          }
        }
      }
    }
  });
  if (result != LabelOracleResult::Valid) { return result; }

  for (size_t index = 1; index < expected.size(); ++index) {
    auto current = zc::mv(expected[index]);
    size_t insertion = index;
    while (insertion > 0 && oracleLabelLess(current, expected[insertion - 1])) {
      expected[insertion] = zc::mv(expected[insertion - 1]);
      --insertion;
    }
    expected[insertion] = zc::mv(current);
  }

  if (candidate.labels.size() < expected.size()) {
    return LabelOracleResult::MissingRequiredResolution;
  }
  if (candidate.labels.size() > expected.size()) { return LabelOracleResult::InvalidBindingFact; }
  for (size_t index = 0; index < expected.size(); ++index) {
    const auto& actual = candidate.labels[index];
    const auto& wanted = expected[index];
    const auto& actualOwner = actual.owner.value();
    const bool ownerMatches =
        wanted.owner.callable
            ? actualOwner.is<CallableLabelOwner>() &&
                  actualOwner.get<CallableLabelOwner>().callable == wanted.owner.definition
            : actualOwner.is<ModuleLabelOwner>() &&
                  actualOwner.get<ModuleLabelOwner>().module == input.module();
    const auto& actualTarget = actual.target.value();
    const bool targetMatches =
        wanted.target.loop ? actualTarget.is<LoopLabelTarget>() &&
                                 actualTarget.get<LoopLabelTarget>().scope == wanted.target.scope
                           : actualTarget.is<BlockLabelTarget>() &&
                                 actualTarget.get<BlockLabelTarget>().scope == wanted.target.scope;
    if (!ownerMatches || actual.identity.owner() != actual.owner ||
        actual.identity.index() != wanted.index || actual.name != wanted.name ||
        actual.statement != wanted.statement || !targetMatches ||
        !sameSpan(actual.source, wanted.source)) {
      return LabelOracleResult::InvalidBindingFact;
    }
    for (const auto& binding : candidate.nodeBindings) {
      if (binding.node == wanted.node) { return LabelOracleResult::InvalidBindingFact; }
    }
  }

  zc::Vector<bool> consumedFailures;
  consumedFailures.resize(candidate.sourceFailures.size());
  for (auto& consumed : consumedFailures) { consumed = false; }
  for (const auto& wanted : expected) {
    if (wanted.previous == zc::none) { continue; }
    size_t match = candidate.sourceFailures.size();
    for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
      const auto& failureFact = candidate.sourceFailures[index];
      const uint8_t emitterSite = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
      const uint32_t schemaOrdinal =
          static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
      if (emitterSite == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) &&
          schemaOrdinal == wanted.schemaPreorderOrdinal) {
        if (match != candidate.sourceFailures.size()) {
          return LabelOracleResult::InvalidBindingFact;
        }
        match = index;
      }
    }
    if (match == candidate.sourceFailures.size()) {
      return LabelOracleResult::MissingRequiredResolution;
    }
    const auto& failureFact = candidate.sourceFailures[match];
    const uint16_t localOrdinal = static_cast<uint16_t>(failureFact.emitterOrdinal);
    if (failureFact.diagnostic != BinderDiagnosticCode::DuplicateIdentifier || localOrdinal != 0 ||
        !sameSpan(failureFact.primary, wanted.source) || failureFact.notes.size() != 1 ||
        failureFact.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere) {
      return LabelOracleResult::InvalidBindingFact;
    }
    ZC_IF_SOME(previous, wanted.previous) {
      if (!sameSpan(failureFact.notes[0].source, previous)) {
        return LabelOracleResult::InvalidBindingFact;
      }
    }
    consumedFailures[match] = true;
  }
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const uint8_t emitterSite =
        static_cast<uint8_t>(candidate.sourceFailures[index].emitterOrdinal >> 56);
    if (emitterSite == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) &&
        candidate.sourceFailures[index].diagnostic == BinderDiagnosticCode::DuplicateIdentifier &&
        !consumedFailures[index]) {
      return LabelOracleResult::InvalidBindingFact;
    }
  }
  return LabelOracleResult::Valid;
}

BinderInvariantKind labelOracleInvariant(LabelOracleResult result) {
  switch (result) {
    case LabelOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case LabelOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case LabelOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case LabelOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
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
  zc::Vector<bool> consumedResolutions;
  consumedFacts.resize(candidate.controlTransfers.size());
  consumedFailures.resize(candidate.sourceFailures.size());
  consumedResolutions.resize(candidate.nodeBindings.size());
  for (auto& consumed : consumedFacts) { consumed = false; }
  for (auto& consumed : consumedFailures) { consumed = false; }
  for (auto& consumed : consumedResolutions) { consumed = false; }

  ControlOracleResult result = ControlOracleResult::Valid;
  const auto consumeFailure = [&](ast::NodeId node, size_t factIndex, size_t resolutionIndex,
                                  BinderDiagnosticCode diagnostic,
                                  const identity::SourceSpan& primary,
                                  BinderEmitterSite emitterSite) {
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
    const uint8_t actualSite = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
    const uint32_t schemaOrdinal =
        static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
    const uint16_t localOrdinal = static_cast<uint16_t>(failureFact.emitterOrdinal);
    if (failureFact.diagnostic != diagnostic || !sameSpan(failureFact.primary, primary) ||
        !failureFact.notes.empty() || actualSite != static_cast<uint8_t>(emitterSite) ||
        schemaOrdinal != schemaOrdinals[node.value] || localOrdinal != 0) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    consumedFailures[failureIndex] = true;
    consumedResolutions[resolutionIndex] = true;
  };

  const auto consumeSuccessful = [&](ast::NodeId node, const ast::Node& syntax, bool isBreak,
                                     size_t expectedLabel, zc::Maybe<ScopeId> expectedScope,
                                     uint8_t targetTag) {
    const size_t factIndex = factByNode[node.value];
    const size_t resolutionIndex = resolutionByNode[node.value];
    if (factIndex == kMissing) {
      result = resolutionIndex == kMissing ? ControlOracleResult::MissingRequiredResolution
                                           : ControlOracleResult::InvalidBindingFact;
      return;
    }
    const auto& fact = candidate.controlTransfers[factIndex];
    auto expectedSource = input.parsedModule().spanFor(syntax.range);
    if (fact.kind != (isBreak ? ControlTransferKind::Break : ControlTransferKind::Continue) ||
        expectedSource == zc::none) {
      result = ControlOracleResult::InvalidBindingFact;
      return;
    }
    ZC_IF_SOME(source, expectedSource) {
      if (!sameSpan(fact.source, source)) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
    }

    if (targetTag == 0x01) {
      if (expectedLabel >= candidate.labels.size() ||
          !fact.target.is<ExplicitLabelControlTarget>()) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      if (resolutionIndex == kMissing) {
        result = ControlOracleResult::MissingRequiredResolution;
        return;
      }
      const auto& expected = candidate.labels[expectedLabel];
      if (fact.target.get<ExplicitLabelControlTarget>().label != expected.identity) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      const auto& resolution = candidate.nodeBindings[resolutionIndex];
      if (!resolution.value.is<BoundLabelResolution>()) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      const auto& bound = resolution.value.get<BoundLabelResolution>();
      if (bound.label != expected.identity || bound.target != expected.target) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      consumedResolutions[resolutionIndex] = true;
    } else {
      if (resolutionIndex != kMissing || expectedScope == zc::none) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
      const auto scope = ZC_ASSERT_NONNULL(expectedScope);
      if ((targetTag == 0x02 && (!fact.target.is<LoopControlTarget>() ||
                                 fact.target.get<LoopControlTarget>().scope != scope)) ||
          (targetTag == 0x03 && (!fact.target.is<MatchControlTarget>() ||
                                 fact.target.get<MatchControlTarget>().scope != scope))) {
        result = ControlOracleResult::InvalidBindingFact;
        return;
      }
    }
    consumedFacts[factIndex] = true;
  };

  zc::Vector<size_t> activeLabels;
  auto visit = [&](auto& self, ast::NodeId node) -> void {
    if (result != ControlOracleResult::Valid) { return; }
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      result = ControlOracleResult::MalformedScopeGraph;
      return;
    }

    const auto nodeScope = scopeByNode[node.value];
    const auto scopeKind = arena.scopes[nodeScope].kind;
    const bool resetsLabels = scopeKind == ScopeKind::Function || scopeKind == ScopeKind::Closure;
    zc::Vector<size_t> savedLabels;
    if (resetsLabels) {
      savedLabels = zc::mv(activeLabels);
      activeLabels = zc::Vector<size_t>();
    }

    const auto& syntax = tree.node(node);
    if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      const ast::NodeId statement(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      size_t labelIndex = candidate.labels.size();
      for (size_t index = 0; index < candidate.labels.size(); ++index) {
        if (candidate.labels[index].statement != statement) { continue; }
        if (labelIndex != candidate.labels.size()) {
          result = ControlOracleResult::InvalidBindingFact;
          break;
        }
        labelIndex = index;
      }
      if (result == ControlOracleResult::Valid &&
          (labelIndex == candidate.labels.size() || !tree.contains(statement))) {
        result = ControlOracleResult::MissingRequiredResolution;
      }
      if (result == ControlOracleResult::Valid) {
        activeLabels.add(labelIndex);
        self(self, statement);
        activeLabels.removeLast();
      }
    } else {
      if (syntax.kind == ast::SyntaxKind::BreakStmt ||
          syntax.kind == ast::SyntaxKind::ContinueStatement) {
        const bool isBreak = syntax.kind == ast::SyntaxKind::BreakStmt;
        const uint32_t label =
            syntax.payload
                .words[isBreak ? ast::kBreakStmtLabelWord : ast::kContinueStatementLabelWord];
        const size_t factIndex = factByNode[node.value];
        const size_t resolutionIndex = resolutionByNode[node.value];
        if (label != 0) {
          auto name = identity::SemanticIdentifier::fromSource(tree.ident(ast::IdentId(label)));
          auto primary =
              input.parsedModule().retainedTokenSpan(node, 1, ast::SyntaxKind::Identifier);
          if (name == zc::none || primary == zc::none) {
            result = ControlOracleResult::InvalidBindingFact;
          } else {
            size_t targetLabel = candidate.labels.size();
            ZC_IF_SOME(nameValue, name) {
              for (size_t offset = activeLabels.size(); offset > 0; --offset) {
                const size_t active = activeLabels[offset - 1];
                if (active >= candidate.labels.size()) {
                  result = ControlOracleResult::InvalidBindingFact;
                  break;
                }
                if (candidate.labels[active].name == nameValue) {
                  targetLabel = active;
                  break;
                }
              }
            }
            if (result == ControlOracleResult::Valid) {
              ZC_IF_SOME(primaryValue, primary) {
                if (targetLabel == candidate.labels.size()) {
                  consumeFailure(node, factIndex, resolutionIndex,
                                 BinderDiagnosticCode::UndefinedIdentifier, primaryValue,
                                 BinderEmitterSite::LabelAndClosure);
                } else if (!isBreak &&
                           candidate.labels[targetLabel].target.value().is<BlockLabelTarget>()) {
                  consumeFailure(node, factIndex, resolutionIndex,
                                 BinderDiagnosticCode::ContinueTargetNotLoop, primaryValue,
                                 BinderEmitterSite::BodyBinding);
                } else {
                  zc::Maybe<ScopeId> noScope;
                  consumeSuccessful(node, syntax, isBreak, targetLabel, zc::mv(noScope), 0x01);
                }
              }
            }
          }
        } else {
          enum class ExpectedTarget : uint8_t { None, Loop, Match };
          ExpectedTarget target = ExpectedTarget::None;
          zc::Maybe<ScopeId> targetScope;
          uint32_t scopeIndex = scopeByNode[node.value];
          bool terminated = false;
          for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
            if (scopeIndex >= arena.scopes.size()) {
              result = ControlOracleResult::MalformedScopeGraph;
              break;
            }
            const auto& scope = arena.scopes[scopeIndex];
            if (scope.kind == ScopeKind::Loop) {
              target = ExpectedTarget::Loop;
              targetScope = scope.id;
              terminated = true;
              break;
            }
            if (scope.kind == ScopeKind::Match && isBreak) {
              target = ExpectedTarget::Match;
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
          if (result == ControlOracleResult::Valid && !terminated) {
            result = ControlOracleResult::MalformedScopeGraph;
          } else if (result == ControlOracleResult::Valid && target != ExpectedTarget::None) {
            consumeSuccessful(node, syntax, isBreak, candidate.labels.size(), zc::mv(targetScope),
                              target == ExpectedTarget::Loop ? 0x02 : 0x03);
          } else if (result == ControlOracleResult::Valid) {
            auto primary = input.parsedModule().retainedTokenSpan(
                node, 0,
                isBreak ? ast::SyntaxKind::BreakKeyword : ast::SyntaxKind::ContinueKeyword);
            if (primary == zc::none) {
              result = ControlOracleResult::InvalidBindingFact;
            } else {
              ZC_IF_SOME(primaryValue, primary) {
                consumeFailure(node, factIndex, resolutionIndex,
                               isBreak ? BinderDiagnosticCode::BreakTargetNotFound
                                       : BinderDiagnosticCode::ContinueTargetNotFound,
                               primaryValue, BinderEmitterSite::BodyBinding);
              }
            }
          }
        }
      }
      if (result == ControlOracleResult::Valid) {
        ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) { self(self, child); });
      }
    }

    if (resetsLabels) { activeLabels = zc::mv(savedLabels); }
  };
  visit(visit, tree.root());
  if (result != ControlOracleResult::Valid) { return result; }
  for (const auto consumed : consumedFacts) {
    if (!consumed) { return ControlOracleResult::InvalidBindingFact; }
  }
  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    if (candidate.nodeBindings[index].value.is<BoundLabelResolution>() &&
        !consumedResolutions[index]) {
      return ControlOracleResult::InvalidBindingFact;
    }
  }
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const auto diagnostic = candidate.sourceFailures[index].diagnostic;
    const auto emitterSite =
        static_cast<uint8_t>(candidate.sourceFailures[index].emitterOrdinal >> 56);
    const bool explicitLookup =
        diagnostic == BinderDiagnosticCode::UndefinedIdentifier &&
        emitterSite == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure);
    if ((explicitLookup || diagnostic == BinderDiagnosticCode::BreakTargetNotFound ||
         diagnostic == BinderDiagnosticCode::ContinueTargetNotFound ||
         diagnostic == BinderDiagnosticCode::ContinueTargetNotLoop) &&
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

bool encodeDefinition(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      identity::DefId definition) {
  ZC_IF_SOME(key, input.definitions().definitionKey(definition)) {
    key.encode(encoder);
    return true;
  }
  return false;
}

bool encodeLabelOwner(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                      const LabelOwner& owner) {
  const auto& value = owner.value();
  if (value.is<ModuleLabelOwner>()) {
    if (value.get<ModuleLabelOwner>().module != input.module()) { return false; }
    encoder.encodeUint8(0x01);
    input.moduleKey().encode(encoder);
    return true;
  }
  encoder.encodeUint8(0x02);
  return encodeDefinition(encoder, input, value.get<CallableLabelOwner>().callable);
}

bool encodeLabelId(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                   const LabelId& identity) {
  if (!identity.belongsTo(input.semanticContext()) ||
      !encodeLabelOwner(encoder, input, identity.owner())) {
    return false;
  }
  encoder.encodeUint32(identity.index());
  return true;
}

bool encodeLabelTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                       const LabelTarget& target) {
  const auto& value = target.value();
  if (value.is<BlockLabelTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeScopeId(encoder, input, value.get<BlockLabelTarget>().scope);
  }
  encoder.encodeUint8(0x02);
  return encodeScopeId(encoder, input, value.get<LoopLabelTarget>().scope);
}

bool encodeControlTarget(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                         const ControlTarget& target) {
  if (target.is<ExplicitLabelControlTarget>()) {
    encoder.encodeUint8(0x01);
    return encodeLabelId(encoder, input, target.get<ExplicitLabelControlTarget>().label);
  }
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

bool encodeLabelFact(identity::CanonicalEncoder& encoder, const VerifiedBindingInput& input,
                     const LabelFact& fact) {
  if (fact.identity.owner() != fact.owner || !encodeLabelId(encoder, input, fact.identity)) {
    return false;
  }
  fact.name.encode(encoder);
  if (!encodeLabelOwner(encoder, input, fact.owner)) { return false; }
  encoder.encodeUint32(fact.statement.value);
  if (!encodeLabelTarget(encoder, input, fact.target)) { return false; }
  fact.source.encode(encoder);
  return true;
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

zc::Maybe<zc::Array<uint8_t>> encodeAllocationLabelRecord(const VerifiedBindingInput& input,
                                                          zc::ArrayPtr<const ScopeRecord> scopes,
                                                          const LabelFact& fact) {
  if (fact.identity.owner() != fact.owner || !fact.identity.belongsTo(input.semanticContext())) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  if (!encodeLabelOwner(encoder, input, fact.owner)) { return zc::none; }
  encoder.encodeUint32(fact.identity.index());
  fact.name.encode(encoder);
  const auto& target = fact.target.value();
  ScopeId scope = target.is<BlockLabelTarget>() ? target.get<BlockLabelTarget>().scope
                                                : target.get<LoopLabelTarget>().scope;
  encoder.encodeUint8(target.is<BlockLabelTarget>() ? 0x01 : 0x02);
  if (scope.module() != input.module() || !scope.belongsTo(input.semanticContext()) ||
      scope.index() >= scopes.size() || scopes[scope.index()].id != scope) {
    return zc::none;
  }
  const auto expectedKind = target.is<BlockLabelTarget>() ? ScopeKind::Block : ScopeKind::Loop;
  if (scopes[scope.index()].kind != expectedKind) { return zc::none; }
  encoder.encodeUint32(scope.index());
  fact.source.encode(encoder);
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
    } else if (value.is<BoundLabelResolution>()) {
      const auto& bound = value.get<BoundLabelResolution>();
      encoder.encodeUint8(0x02);
      if (!encodeLabelId(encoder, input, bound.label) ||
          !encodeLabelTarget(encoder, input, bound.target)) {
        return zc::none;
      }
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
  for (const auto& fact : candidate.labels) {
    if (!encodeLabelFact(encoder, input, fact)) { return zc::none; }
  }
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
                                                          zc::ArrayPtr<const ScopeRecord> scopes,
                                                          zc::ArrayPtr<const LabelFact> labels) {
  zc::Vector<zc::Array<uint8_t>> scopeStorage;
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].id.index() != index) { return zc::none; }
    auto encoded = encodeAllocationScopeRecord(input, scopes[index]);
    ZC_IF_SOME(value, encoded) { scopeStorage.add(zc::mv(value)); }
    else { return zc::none; }
  }
  zc::Vector<zc::Array<uint8_t>> labelStorage;
  zc::Array<uint8_t> previousLabelIdentity;
  bool hasPreviousLabelIdentity = false;
  for (const auto& fact : labels) {
    if (fact.identity.owner() != fact.owner) { return zc::none; }
    identity::CanonicalEncoder identityEncoder;
    if (!encodeLabelId(identityEncoder, input, fact.identity)) { return zc::none; }
    auto labelIdentity = identityEncoder.finish();
    if (hasPreviousLabelIdentity &&
        compareCanonicalBytes(previousLabelIdentity.asPtr(), labelIdentity.asPtr()) >= 0) {
      return zc::none;
    }
    previousLabelIdentity = zc::mv(labelIdentity);
    hasPreviousLabelIdentity = true;
    auto encoded = encodeAllocationLabelRecord(input, scopes, fact);
    ZC_IF_SOME(value, encoded) { labelStorage.add(zc::mv(value)); }
    else { return zc::none; }
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> scopeRecords;
  for (const auto& value : scopeStorage) { scopeRecords.add(value.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> labelRecords;
  for (const auto& value : labelStorage) { labelRecords.add(value.asPtr()); }
  return frameBindingAllocationDump(scopeRecords.asPtr(), labelRecords.asPtr());
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
  auto labelResult = LabelBuilder::build(input, arena);
  if (!labelResult.is<LabelFactsCandidate>()) {
    return zc::mv(labelResult.get<BinderInvariantFact>());
  }
  auto labels = zc::mv(labelResult.get<LabelFactsCandidate>());
  auto controlResult = ControlTransferBuilder::build(input, arena, labels);
  if (!controlResult.is<ControlTransferCandidate>()) {
    return zc::mv(controlResult.get<BinderInvariantFact>());
  }
  auto control = zc::mv(controlResult.get<ControlTransferCandidate>());
  for (auto& binding : control.nodeBindings) { body.nodeBindings.add(zc::mv(binding)); }

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
  for (size_t index = 0; index < labels.duplicates.size(); ++index) {
    const auto& duplicate = labels.duplicates[index];
    failureOrder.insert(
        PendingFailureOrderKey{duplicate.primary.byteStart(), duplicate.primary.byteEnd(),
                               static_cast<uint16_t>(BinderDiagnosticCode::DuplicateIdentifier),
                               static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure),
                               duplicate.schemaPreorderOrdinal, sequence++},
        PendingFailureRef{PendingFailureKind::LabelDuplicate, index});
  }
  for (size_t index = 0; index < control.failures.size(); ++index) {
    const auto& controlFailure = control.failures[index];
    failureOrder.insert(
        PendingFailureOrderKey{controlFailure.source.byteStart(), controlFailure.source.byteEnd(),
                               static_cast<uint16_t>(controlFailure.diagnostic),
                               static_cast<uint8_t>(controlFailure.emitterSite),
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

    if (ordered.value.kind == PendingFailureKind::LabelDuplicate) {
      const auto& duplicate = labels.duplicates[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(duplicate.primary);
      auto previousLocation = input.parsedModule().sourceLocFor(duplicate.previous);
      if (primaryLocation == zc::none || previousLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact,
                       BinderEmitterSite::LabelAndClosure, schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(previous, previousLocation) {
            if (!BindingDiagnosticAdapter::emitRedeclaration(
                    engine, BinderDiagnosticCode::DuplicateIdentifier, primary, previous,
                    VerifiedIdentifierArgument::from(duplicate.name))) {
              return failure(input, BinderInvariantKind::InvalidBindingFact,
                             BinderEmitterSite::LabelAndClosure, schemaOrdinal);
            }
          }
        }
      }
      zc::Vector<BindingDiagnosticNoteRef> notes;
      notes.add(BindingDiagnosticNoteRef{BinderDiagnosticCode::PreviousDeclarationHere,
                                         duplicate.previous.clone()});
      sourceFailures.add(BindingFailureRef{BinderDiagnosticCode::DuplicateIdentifier,
                                           duplicate.primary.clone(), emitterOrdinal,
                                           zc::mv(notes)});
      continue;
    }

    if (ordered.value.kind == PendingFailureKind::ControlTransfer) {
      const auto& controlFailure = control.failures[ordered.value.index];
      auto primaryLocation = input.parsedModule().sourceLocFor(controlFailure.source);
      if (primaryLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, controlFailure.emitterSite,
                       schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        bool emitted = false;
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(label, controlFailure.label) {
            emitted = BindingDiagnosticAdapter::emitLabelLookupFailure(
                engine, controlFailure.diagnostic, primary,
                VerifiedIdentifierArgument::from(label));
          }
          else {
            emitted = BindingDiagnosticAdapter::emitControlTransferFailure(
                engine, controlFailure.diagnostic, primary);
          }
        }
        if (!emitted) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, controlFailure.emitterSite,
                         schemaOrdinal);
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
        candidate.labels = zc::mv(labels.labels);
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
  const auto expectedLabels = verifyLabelFacts(input, expected);
  if (expectedLabels != LabelOracleResult::Valid) {
    return rejectBinderInvariant(verifierFailure(input, labelOracleInvariant(expectedLabels)));
  }
  const auto candidateLabels = verifyLabelFacts(input, candidate);
  if (candidateLabels != LabelOracleResult::Valid) {
    return rejectBinderInvariant(verifierFailure(input, labelOracleInvariant(candidateLabels)));
  }
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
      candidate.labels.size() < expected.labels.size() ||
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
      candidate.labels.size() > expected.labels.size() ||
      candidate.controlTransfers.size() > expected.controlTransfers.size() ||
      candidate.shadowTargets.size() > expected.shadowTargets.size()) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  if (!candidate.moduleAliases.empty() || !candidate.imports.empty() ||
      !candidate.localExports.empty() || !candidate.deferredMembers.empty() ||
      !candidate.closureFreeVariables.empty()) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  auto candidateAllocation =
      encodeBindingAllocationDump(input, candidate.scopes.asPtr(), candidate.labels.asPtr());
  auto expectedAllocation =
      encodeBindingAllocationDump(input, expected.scopes.asPtr(), expected.labels.asPtr());
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
