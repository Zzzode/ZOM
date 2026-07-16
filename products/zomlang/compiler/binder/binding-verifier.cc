// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-verifier.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/internal/binding-skeleton.h"
#include "zomlang/compiler/binder/internal/body-binding.h"
#include "zomlang/compiler/binder/internal/closure-free-variables.h"
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
  for (const auto& fact : candidate.selfTypes) {
    const auto& owner = fact.owner;
    if ((owner.is<NominalSelfOwner>() &&
         !owner.get<NominalSelfOwner>().definition.belongsTo(input.semanticContext())) ||
        (owner.is<InterfaceSelfOwner>() &&
         !owner.get<InterfaceSelfOwner>().definition.belongsTo(input.semanticContext())) ||
        (owner.is<ImplSelfOwner>() &&
         !owner.get<ImplSelfOwner>().implementation.belongsTo(input.semanticContext()))) {
      return true;
    }
  }
  for (const auto& fact : candidate.thisBindings) {
    if (!fact.binding.receiverParameter.belongsTo(input.semanticContext())) { return true; }
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
  for (const auto& closure : candidate.closureFreeVariables) {
    if (!closure.closure.belongsTo(input.semanticContext())) { return true; }
    for (const auto& variable : closure.variables) {
      if (!variable.target.belongsTo(input.semanticContext())) { return true; }
    }
  }
  for (const auto& closure : candidate.explicitClosureCaptures) {
    if (!closure.closure.belongsTo(input.semanticContext())) { return true; }
    for (const auto& capture : closure.captures) {
      if (!capture.target.belongsTo(input.semanticContext())) { return true; }
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
  for (const auto& resolution : candidate.nodeBindings) {
    if (resolution.value.is<DeferredMemberFact>() &&
        spanIsInvalid(resolution.value.get<DeferredMemberFact>().source)) {
      return true;
    }
  }
  for (const auto& fact : candidate.selfTypes) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.thisBindings) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.deferredMembers) {
    if (spanIsInvalid(fact.source)) { return true; }
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
  for (const auto& closure : candidate.explicitClosureCaptures) {
    if (spanIsInvalid(closure.source)) { return true; }
    for (const auto& capture : closure.captures) {
      if (spanIsInvalid(capture.source)) { return true; }
    }
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

zc::Maybe<ast::SyntaxKind> syntaxKindAtSchemaOrdinal(const ast::Tree& tree, uint32_t wanted) {
  zc::Maybe<ast::SyntaxKind> result;
  uint32_t ordinal = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (ordinal++ == wanted) { result = syntax.kind; }
  });
  return result;
}

bool hasCompleteLexicalBindingSites(const VerifiedBindingInput& input,
                                    zc::ArrayPtr<const BindingResolution> bindings,
                                    zc::ArrayPtr<const BoundSelfType> selfTypes,
                                    zc::ArrayPtr<const BoundThis> thisBindings) {
  const auto& tree = input.tree();
  zc::Vector<uint8_t> requiredNamespaces;
  zc::Vector<bool> published;
  zc::Vector<bool> implicitReceiverTypes;
  requiredNamespaces.resize(tree.nodeCount() + 1);
  published.resize(tree.nodeCount() + 1);
  implicitReceiverTypes.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < requiredNamespaces.size(); ++index) {
    requiredNamespaces[index] = 0;
    published[index] = false;
    implicitReceiverTypes[index] = false;
  }
  zc::Vector<bool> contextualSelfTypes;
  contextualSelfTypes.resize(tree.nodeCount() + 1);
  for (auto& value : contextualSelfTypes) { value = false; }
  for (const auto& fact : selfTypes) {
    if (!tree.contains(fact.syntax) || fact.syntax.value >= contextualSelfTypes.size()) {
      return false;
    }
    contextualSelfTypes[fact.syntax.value] = true;
  }
  zc::Vector<bool> boundThisExpressions;
  boundThisExpressions.resize(tree.nodeCount() + 1);
  for (auto& value : boundThisExpressions) { value = false; }
  for (const auto& fact : thisBindings) {
    if (!tree.contains(fact.expression) || fact.expression.value >= boundThisExpressions.size() ||
        tree.node(fact.expression).kind != ast::SyntaxKind::ThisExpr ||
        boundThisExpressions[fact.expression.value]) {
      return false;
    }
    boundThisExpressions[fact.expression.value] = true;
  }
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId parameter, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::FunctionParameterDecl) { return; }
    const ast::NodeId parameterType(syntax.payload.words[ast::kFunctionParameterDeclTyWord]);
    if (tree.contains(parameterType) &&
        input.parsedModule().functionParameterHasImplicitSelfType(parameter)) {
      implicitReceiverTypes[parameterType.value] = true;
    }
  });
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
      case ast::SyntaxKind::ThisExpr:
        if (boundThisExpressions[node.value]) { return; }
        requireSite(node, ast::SyntaxKind::ThisExpr, Namespace::Value);
        return;
      case ast::SyntaxKind::CaptureItem:
        requireSite(node, ast::SyntaxKind::CaptureItem, Namespace::Value);
        return;
      case ast::SyntaxKind::NamedTypeExpr:
        if (node.value < implicitReceiverTypes.size() && implicitReceiverTypes[node.value]) {
          return;
        }
        if (node.value < contextualSelfTypes.size() && contextualSelfTypes[node.value]) { return; }
        {
          const ast::NodeId path(syntax.payload.words[ast::kNamedTypeExprPathWord]);
          if (tree.contains(path) && tree.node(path).kind == ast::SyntaxKind::ModulePath) {
            const auto& pathSyntax = tree.node(path);
            const ast::IdentList segments{
                pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
            if (tree.contains(segments)) {
              const auto names = tree.identList(segments);
              if (names.size() != 0 && tree.ident(names[0]) == "Self"_zc) {
                requireSite(node, ast::SyntaxKind::NamedTypeExpr, Namespace::Type);
                return;
              }
            }
          }
        }
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
      case ast::SyntaxKind::MemberExpression:
        requireSite(node, ast::SyntaxKind::MemberExpression, Namespace::Value);
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
    if (nodeKind == ast::SyntaxKind::MemberExpression) {
      if (!binding.value.is<DeferredMemberFact>()) { return false; }
      published[binding.node.value] = true;
      ++publishedCount;
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

enum class DeferredMemberOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact
};

bool sameDeferredMemberFact(const DeferredMemberFact& left, const DeferredMemberFact& right) {
  if (left.node != right.node || left.base != right.base || left.member != right.member ||
      left.expectedNamespaces.size() != right.expectedNamespaces.size() ||
      left.genericArguments.size() != right.genericArguments.size() ||
      !sameSpan(left.source, right.source)) {
    return false;
  }
  for (size_t index = 0; index < left.expectedNamespaces.size(); ++index) {
    if (left.expectedNamespaces[index] != right.expectedNamespaces[index]) { return false; }
  }
  for (size_t index = 0; index < left.genericArguments.size(); ++index) {
    if (left.genericArguments[index] != right.genericArguments[index]) { return false; }
  }
  return true;
}

DeferredMemberOracleResult verifyDeferredMemberFacts(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<ast::NodeList> directCallTypeArguments;
  zc::Vector<bool> hasDirectCall;
  zc::Vector<size_t> factByNode;
  zc::Vector<size_t> resolutionByNode;
  directCallTypeArguments.resize(tree.nodeCount() + 1);
  hasDirectCall.resize(tree.nodeCount() + 1);
  factByNode.resize(tree.nodeCount() + 1);
  resolutionByNode.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < factByNode.size(); ++index) {
    hasDirectCall[index] = false;
    factByNode[index] = kMissing;
    resolutionByNode[index] = kMissing;
  }

  size_t memberCount = 0;
  bool treeIsValid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (!treeIsValid) { return; }
    if (syntax.kind == ast::SyntaxKind::MemberExpression) {
      const auto access = static_cast<ast::MemberAccessKind>(
          syntax.payload.words[ast::kMemberExpressionAccessWord]);
      if (access != ast::MemberAccessKind::Dot && access != ast::MemberAccessKind::Optional) {
        treeIsValid = false;
        return;
      }
      ++memberCount;
      return;
    }
    if (syntax.kind != ast::SyntaxKind::CallExpression) { return; }
    const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
    const ast::NodeList typeArguments{syntax.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                      syntax.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
    if (!tree.contains(callee) || !tree.contains(typeArguments)) {
      treeIsValid = false;
      return;
    }
    for (const ast::NodeId argument : tree.list(typeArguments)) {
      if (!tree.contains(argument)) {
        treeIsValid = false;
        return;
      }
    }
    if (tree.node(callee).kind != ast::SyntaxKind::MemberExpression) { return; }
    if (callee.value >= hasDirectCall.size() || hasDirectCall[callee.value]) {
      treeIsValid = false;
      return;
    }
    hasDirectCall[callee.value] = true;
    directCallTypeArguments[callee.value] = typeArguments;
  });
  if (!treeIsValid) { return DeferredMemberOracleResult::InvalidBindingFact; }

  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.deferredMembers.size(); ++index) {
    const auto& fact = candidate.deferredMembers[index];
    if (!tree.contains(fact.node) || fact.node.value >= factByNode.size() ||
        tree.node(fact.node).kind != ast::SyntaxKind::MemberExpression ||
        factByNode[fact.node.value] != kMissing ||
        (index != 0 && fact.node.value <= previousNode)) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    factByNode[fact.node.value] = index;
    previousNode = fact.node.value;
  }
  if (candidate.deferredMembers.size() > memberCount) {
    return DeferredMemberOracleResult::InvalidBindingFact;
  }

  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto& resolution = candidate.nodeBindings[index];
    if (!tree.contains(resolution.node)) { return DeferredMemberOracleResult::InvalidBindingFact; }
    const bool isMember = tree.node(resolution.node).kind == ast::SyntaxKind::MemberExpression;
    if (resolution.value.is<DeferredMemberFact>() && !isMember) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    if (!isMember) { continue; }
    if (resolution.node.value >= resolutionByNode.size() ||
        resolutionByNode[resolution.node.value] != kMissing) {
      return DeferredMemberOracleResult::InvalidBindingFact;
    }
    resolutionByNode[resolution.node.value] = index;
  }

  DeferredMemberOracleResult result = DeferredMemberOracleResult::Valid;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != DeferredMemberOracleResult::Valid ||
        syntax.kind != ast::SyntaxKind::MemberExpression) {
      return;
    }
    const size_t factIndex = factByNode[node.value];
    const size_t resolutionIndex = resolutionByNode[node.value];
    if (factIndex == kMissing || resolutionIndex == kMissing) {
      result = DeferredMemberOracleResult::MissingRequiredResolution;
      return;
    }
    const auto& resolution = candidate.nodeBindings[resolutionIndex];
    if (!resolution.value.is<DeferredMemberFact>()) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    const auto& fact = candidate.deferredMembers[factIndex];
    const auto& inlineFact = resolution.value.get<DeferredMemberFact>();
    if (!sameDeferredMemberFact(fact, inlineFact)) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }

    const ast::NodeId base(syntax.payload.words[ast::kMemberExpressionObjectWord]);
    const auto access =
        static_cast<ast::MemberAccessKind>(syntax.payload.words[ast::kMemberExpressionAccessWord]);
    auto name = identity::DeclaredDefinitionName::fromSource(
        tree.ident(ast::IdentId(syntax.payload.words[ast::kMemberExpressionPropertyWord])));
    auto source = input.parsedModule().spanFor(syntax.range);
    if (!tree.contains(base) || name == zc::none || source == zc::none || fact.node != node ||
        fact.base != base || fact.expectedNamespaces.size() != 1 ||
        fact.expectedNamespaces[0] != Namespace::Value ||
        (access != ast::MemberAccessKind::Dot && access != ast::MemberAccessKind::Optional)) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    ZC_IF_SOME(nameValue, name) {
      if (fact.member != nameValue) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }
    ZC_IF_SOME(sourceValue, source) {
      if (!sameSpan(fact.source, sourceValue)) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }

    const auto expectedArguments = hasDirectCall[node.value]
                                       ? tree.list(directCallTypeArguments[node.value])
                                       : zc::ArrayPtr<const ast::NodeId>();
    if (fact.genericArguments.size() != expectedArguments.size()) {
      result = DeferredMemberOracleResult::InvalidBindingFact;
      return;
    }
    for (size_t index = 0; index < expectedArguments.size(); ++index) {
      if (fact.genericArguments[index] != expectedArguments[index]) {
        result = DeferredMemberOracleResult::InvalidBindingFact;
        return;
      }
    }
  });
  if (result != DeferredMemberOracleResult::Valid) { return result; }
  return candidate.deferredMembers.size() == memberCount
             ? DeferredMemberOracleResult::Valid
             : DeferredMemberOracleResult::MissingRequiredResolution;
}

BinderInvariantKind deferredMemberOracleInvariant(DeferredMemberOracleResult result) {
  switch (result) {
    case DeferredMemberOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case DeferredMemberOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case DeferredMemberOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

enum class ContextualSelfOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

bool sameSelfOwner(const SelfOwner& left, const SelfOwner& right) {
  if (left.is<NominalSelfOwner>()) {
    return right.is<NominalSelfOwner>() &&
           left.get<NominalSelfOwner>().definition == right.get<NominalSelfOwner>().definition;
  }
  if (left.is<InterfaceSelfOwner>()) {
    return right.is<InterfaceSelfOwner>() &&
           left.get<InterfaceSelfOwner>().definition == right.get<InterfaceSelfOwner>().definition;
  }
  return right.is<ImplSelfOwner>() &&
         left.get<ImplSelfOwner>().implementation == right.get<ImplSelfOwner>().implementation;
}

zc::Maybe<SelfOwner> reconstructContextualSelfOwner(const VerifiedBindingInput& input,
                                                    zc::ArrayPtr<const ast::NodeId> parentNodes,
                                                    ast::NodeId node, bool& malformed) {
  const auto& tree = input.tree();
  ast::NodeId child = node;
  size_t remaining = parentNodes.size();
  while (tree.contains(child) && child.value < parentNodes.size() && remaining != 0) {
    --remaining;
    const ast::NodeId parent = parentNodes[child.value];
    if (!parent) { return zc::none; }
    if (!tree.contains(parent)) {
      malformed = true;
      return zc::none;
    }

    const auto& syntax = tree.node(parent);
    ast::NodeId body;
    bool nominal = false;
    bool interface = false;
    bool implementation = false;
    switch (syntax.kind) {
      case ast::SyntaxKind::ClassDecl:
        body = ast::NodeId(syntax.payload.words[ast::kClassDeclMembersIdWord]);
        nominal = true;
        break;
      case ast::SyntaxKind::StructDecl:
        body = ast::NodeId(syntax.payload.words[ast::kStructDeclMembersIdWord]);
        nominal = true;
        break;
      case ast::SyntaxKind::EnumDeclaration:
        body = ast::NodeId(syntax.payload.words[ast::kEnumDeclarationVariantsIdWord]);
        nominal = true;
        break;
      case ast::SyntaxKind::ErrorDecl:
        body = ast::NodeId(syntax.payload.words[ast::kErrorDeclMembersIdWord]);
        nominal = true;
        break;
      case ast::SyntaxKind::InterfaceDecl:
        body = ast::NodeId(syntax.payload.words[ast::kInterfaceDeclMembersIdWord]);
        interface = true;
        break;
      case ast::SyntaxKind::StandaloneImplDecl:
        body = ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclMembersIdWord]);
        implementation = true;
        break;
      default:
        break;
    }
    if (body && !tree.contains(body)) {
      malformed = true;
      return zc::none;
    }
    if (body && child == body) {
      if (implementation) {
        auto owner = input.definitions().implAt(parent);
        if (owner == zc::none) {
          malformed = true;
          return zc::none;
        }
        return SelfOwner(ImplSelfOwner{ZC_ASSERT_NONNULL(owner)});
      }
      auto owner = input.definitions().definitionAt(parent);
      if (owner == zc::none || (!nominal && !interface)) {
        malformed = true;
        return zc::none;
      }
      if (interface) { return SelfOwner(InterfaceSelfOwner{ZC_ASSERT_NONNULL(owner)}); }
      return SelfOwner(NominalSelfOwner{ZC_ASSERT_NONNULL(owner)});
    }
    child = parent;
  }
  if (remaining == 0) { malformed = true; }
  return zc::none;
}

ContextualSelfOracleResult verifyContextualSelfFacts(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<ast::NodeId> parentNodes;
  zc::Vector<bool> visited;
  zc::Vector<size_t> factByNode;
  parentNodes.resize(tree.nodeCount() + 1);
  visited.resize(tree.nodeCount() + 1);
  factByNode.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < parentNodes.size(); ++index) {
    parentNodes[index] = ast::NodeId();
    visited[index] = false;
    factByNode[index] = kMissing;
  }

  size_t visitedCount = 0;
  bool treeIsValid = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (!treeIsValid || !tree.contains(node) || node.value >= visited.size() ||
        visited[node.value]) {
      treeIsValid = false;
      return;
    }
    visited[node.value] = true;
    ++visitedCount;
    ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) {
      if (!tree.contains(child) || child.value >= parentNodes.size() || parentNodes[child.value]) {
        treeIsValid = false;
        return;
      }
      parentNodes[child.value] = node;
    });
  });
  if (!treeIsValid || visitedCount != tree.nodeCount()) {
    return ContextualSelfOracleResult::MalformedScopeGraph;
  }

  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.selfTypes.size(); ++index) {
    const auto& fact = candidate.selfTypes[index];
    if (!tree.contains(fact.syntax) || fact.syntax.value >= factByNode.size() ||
        tree.node(fact.syntax).kind != ast::SyntaxKind::NamedTypeExpr ||
        factByNode[fact.syntax.value] != kMissing ||
        (index != 0 && fact.syntax.value <= previousNode)) {
      return ContextualSelfOracleResult::InvalidBindingFact;
    }
    factByNode[fact.syntax.value] = index;
    previousNode = fact.syntax.value;
  }

  size_t expectedCount = 0;
  ContextualSelfOracleResult result = ContextualSelfOracleResult::Valid;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (result != ContextualSelfOracleResult::Valid ||
        syntax.kind != ast::SyntaxKind::NamedTypeExpr) {
      return;
    }
    const ast::NodeId path(syntax.payload.words[ast::kNamedTypeExprPathWord]);
    if (!tree.contains(path) || tree.node(path).kind != ast::SyntaxKind::ModulePath) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& pathSyntax = tree.node(path);
    const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                  pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
    if (!tree.contains(segments)) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto names = tree.identList(segments);
    if (names.size() == 0 || tree.ident(names[0]) != "Self"_zc) { return; }

    const ast::NodeId parent = parentNodes[node.value];
    if (tree.contains(parent) && tree.node(parent).kind == ast::SyntaxKind::FunctionParameterDecl) {
      const auto& parameter = tree.node(parent);
      const ast::NodeId type(parameter.payload.words[ast::kFunctionParameterDeclTyWord]);
      if (type == node && input.parsedModule().functionParameterHasImplicitSelfType(parent)) {
        return;
      }
    }

    bool malformedOwner = false;
    auto owner = reconstructContextualSelfOwner(input, parentNodes.asPtr(), node, malformedOwner);
    if (malformedOwner) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    if (owner == zc::none) { return; }
    ++expectedCount;

    const size_t factIndex = factByNode[node.value];
    if (factIndex == kMissing) {
      result = ContextualSelfOracleResult::MissingRequiredResolution;
      return;
    }
    auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
    if (source == zc::none) {
      result = ContextualSelfOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& fact = candidate.selfTypes[factIndex];
    if (!sameSelfOwner(fact.owner, ZC_ASSERT_NONNULL(owner)) ||
        !sameSpan(fact.source, ZC_ASSERT_NONNULL(source))) {
      result = ContextualSelfOracleResult::InvalidBindingFact;
    }
  });
  if (result != ContextualSelfOracleResult::Valid) { return result; }
  if (candidate.selfTypes.size() < expectedCount) {
    return ContextualSelfOracleResult::MissingRequiredResolution;
  }
  return candidate.selfTypes.size() == expectedCount
             ? ContextualSelfOracleResult::Valid
             : ContextualSelfOracleResult::InvalidBindingFact;
}

BinderInvariantKind contextualSelfOracleInvariant(ContextualSelfOracleResult result) {
  switch (result) {
    case ContextualSelfOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ContextualSelfOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ContextualSelfOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ContextualSelfOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
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

enum class ClosureFreeVariableOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

struct OracleCaptureTriple final {
  identity::DefId closure;
  identity::DefId target;
  ast::NodeId referenceSite;
};

struct ClosureFreeOracleTripleOrderKey final {
  size_t closureRank;
  size_t targetRank;
  uint64_t start;
  uint64_t end;
  uint32_t schemaPreorderOrdinal;

  bool operator==(const ClosureFreeOracleTripleOrderKey& other) const noexcept {
    return closureRank == other.closureRank && targetRank == other.targetRank &&
           start == other.start && end == other.end &&
           schemaPreorderOrdinal == other.schemaPreorderOrdinal;
  }
  bool operator<(const ClosureFreeOracleTripleOrderKey& other) const noexcept {
    if (closureRank != other.closureRank) { return closureRank < other.closureRank; }
    if (targetRank != other.targetRank) { return targetRank < other.targetRank; }
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    if (schemaPreorderOrdinal != other.schemaPreorderOrdinal) {
      return schemaPreorderOrdinal < other.schemaPreorderOrdinal;
    }
    return false;
  }
};

struct ExplicitOracleSourceOrderKey final {
  uint64_t start;
  uint64_t end;
  size_t inventoryIndex;

  bool operator==(const ExplicitOracleSourceOrderKey& other) const noexcept {
    return start == other.start && end == other.end && inventoryIndex == other.inventoryIndex;
  }
  bool operator<(const ExplicitOracleSourceOrderKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return inventoryIndex < other.inventoryIndex;
  }
};

struct ExplicitOracleActiveScope final {
  zc::HashMap<zc::String, size_t> values;
  zc::HashMap<zc::String, size_t> types;
  size_t receiver = static_cast<size_t>(-1);
};

struct ExplicitOracleReceiverRecord final {
  uint32_t scopeIndex;
  size_t inventoryIndex;
  ast::NodeId node;
  uint64_t start;
  uint64_t end;
};

struct ExplicitOracleReceiverOrderKey final {
  uint32_t scopeIndex;
  uint64_t start;
  uint64_t end;
  uint32_t node;

  bool operator==(const ExplicitOracleReceiverOrderKey& other) const noexcept {
    return scopeIndex == other.scopeIndex && start == other.start && end == other.end &&
           node == other.node;
  }
  bool operator<(const ExplicitOracleReceiverOrderKey& other) const noexcept {
    if (scopeIndex != other.scopeIndex) { return scopeIndex < other.scopeIndex; }
    if (start != other.start) { return start < other.start; }
    if (end != other.end) { return end < other.end; }
    return node < other.node;
  }
};

struct ExplicitOracleSpanKey final {
  uint64_t start;
  uint64_t end;

  bool operator==(const ExplicitOracleSpanKey& other) const noexcept {
    return start == other.start && end == other.end;
  }
  bool operator<(const ExplicitOracleSpanKey& other) const noexcept {
    if (start != other.start) { return start < other.start; }
    return end < other.end;
  }
};

bool explicitOracleOwnsScope(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::Closure:
      return true;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return false;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<Namespace> explicitOracleNamespace(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
      return Namespace::Value;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::TypeParameter:
      return Namespace::Type;
    case DefinitionKind::ModuleAlias:
      return Namespace::Module;
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Closure:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return zc::none;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<DefinitionActivation> explicitOracleActivation(const ast::Tree& tree,
                                                         const FrozenDefinitionEntry& entry) {
  using identity::DefinitionKind;
  switch (entry.kind) {
    case DefinitionKind::TypeParameter:
      return DefinitionActivation::GenericList;
    case DefinitionKind::Parameter:
      return DefinitionActivation::ParameterList;
    case DefinitionKind::Closure:
      return DefinitionActivation::ExpressionIntroduction;
    case DefinitionKind::Local:
      return DefinitionActivation::AfterInitializer;
    case DefinitionKind::PatternBinding: {
      if (!entry.site.value().is<PatternBindingSite>()) { return zc::none; }
      const auto introducer = entry.site.value().get<PatternBindingSite>().introducer;
      if (!tree.contains(introducer)) { return zc::none; }
      if (tree.node(introducer).kind == ast::SyntaxKind::ForInStatement) {
        return DefinitionActivation::LoopPattern;
      }
      if (tree.node(introducer).kind == ast::SyntaxKind::MatchArmStmt) {
        return DefinitionActivation::MatchPattern;
      }
      return zc::none;
    }
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return DefinitionActivation::ModuleSkeleton;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::ImportAlias:
      return DefinitionActivation::ImportSurface;
    case DefinitionKind::ReexportAlias:
      return DefinitionActivation::ReexportSurface;
  }
  ZC_UNREACHABLE;
}

enum class ExplicitCaptureOracleResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  InvalidBindingFact,
  MalformedScopeGraph
};

ExplicitCaptureOracleResult verifyExplicitCaptureFacts(const VerifiedBindingInput& input,
                                                       const BindingMetadataCandidate& candidate) {
  constexpr size_t kMissing = static_cast<size_t>(-1);
  const auto& tree = input.tree();
  const auto inventory = input.definitions().definitions();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) {
    return ExplicitCaptureOracleResult::MalformedScopeGraph;
  }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.nodeScopes.size() != tree.nodeCount()) {
    return ExplicitCaptureOracleResult::MalformedScopeGraph;
  }

  zc::Vector<uint32_t> scopeByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  for (auto& value : scopeByNode) { value = UINT32_MAX; }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext()) || (index == 0 && scope.parent != zc::none) ||
        (index != 0 && scope.parent == zc::none)) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }
  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) { return ExplicitCaptureOracleResult::MalformedScopeGraph; }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));

  zc::TreeMap<zc::String, size_t> inventoryByCanonicalKey;
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    auto registeredKey = input.definitions().definitionKey(entry.definition);
    if (registeredKey == zc::none) { return ExplicitCaptureOracleResult::MalformedScopeGraph; }
    const auto entryBytes = entry.key.encode();
    const auto registeredBytes = ZC_ASSERT_NONNULL(registeredKey).encode();
    if (compareCanonicalBytes(entryBytes.asPtr(), registeredBytes.asPtr()) != 0) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    auto canonicalKey = zc::str(entryBytes.asChars());
    if (inventoryByCanonicalKey.find(canonicalKey) != zc::none) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    inventoryByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }
  const auto inventoryIndex = [&](identity::DefId definition) -> zc::Maybe<size_t> {
    auto registeredKey = input.definitions().definitionKey(definition);
    if (registeredKey == zc::none) { return zc::none; }
    const auto bytes = ZC_ASSERT_NONNULL(registeredKey).encode();
    const auto canonicalKey = zc::str(bytes.asChars());
    auto found = inventoryByCanonicalKey.find(canonicalKey);
    ZC_IF_SOME(index, found) {
      if (index < inventory.size() && inventory[index].definition == definition) { return index; }
    }
    return zc::none;
  };

  zc::Vector<size_t> resolutionByNode;
  resolutionByNode.resize(tree.nodeCount() + 1);
  for (auto& value : resolutionByNode) { value = kMissing; }
  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto node = candidate.nodeBindings[index].node;
    if (!tree.contains(node) || node.value >= resolutionByNode.size() ||
        resolutionByNode[node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    resolutionByNode[node.value] = index;
  }
  zc::Vector<size_t> thisBindingByNode;
  thisBindingByNode.resize(tree.nodeCount() + 1);
  for (auto& value : thisBindingByNode) { value = kMissing; }
  for (size_t index = 0; index < candidate.thisBindings.size(); ++index) {
    const auto node = candidate.thisBindings[index].expression;
    if (!tree.contains(node) || node.value >= thisBindingByNode.size() ||
        tree.node(node).kind != ast::SyntaxKind::ThisExpr ||
        thisBindingByNode[node.value] != kMissing || resolutionByNode[node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    thisBindingByNode[node.value] = index;
  }
  const auto capturable = [](identity::DefinitionKind kind) {
    return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
           kind == identity::DefinitionKind::PatternBinding;
  };
  enum class ExplicitOracleClosureSyntax : uint8_t { NotClosure, Inferred, Explicit };

  zc::Vector<uint32_t> definitionScopeIndices;
  definitionScopeIndices.resize(inventory.size());
  zc::Vector<uint32_t> owningCallableScopeIndices;
  owningCallableScopeIndices.resize(inventory.size());
  zc::Vector<bool> receiverDefinitions;
  receiverDefinitions.resize(inventory.size());
  zc::Vector<zc::Maybe<identity::SourceSpan>> receiverSources;
  receiverSources.resize(inventory.size());
  zc::Vector<ExplicitOracleClosureSyntax> closureSyntaxDomains;
  closureSyntaxDomains.resize(inventory.size());
  zc::Vector<zc::Vector<size_t>> definitionsByIntroducer;
  definitionsByIntroducer.resize(tree.nodeCount() + 1);
  zc::Vector<zc::Vector<size_t>> definitionsByScope;
  definitionsByScope.resize(arena.scopes.size());
  zc::Vector<ExplicitOracleActiveScope> activeScopes;
  activeScopes.reserve(arena.scopes.size());
  for (size_t index = 0; index < arena.scopes.size(); ++index) { activeScopes.add(); }
  zc::TreeMap<ExplicitOracleReceiverOrderKey, ExplicitOracleReceiverRecord> receiverOrder;
  size_t closureCount = 0;
  for (size_t index = 0; index < inventory.size(); ++index) {
    definitionScopeIndices[index] = UINT32_MAX;
    owningCallableScopeIndices[index] = UINT32_MAX;
    receiverDefinitions[index] = false;
    closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::NotClosure;
    const auto& entry = inventory[index];
    if (!tree.contains(entry.node) || entry.node.value >= scopeByNode.size() ||
        scopeByNode[entry.node.value] == UINT32_MAX) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    uint32_t declaringScope = scopeByNode[entry.node.value];
    if (explicitOracleOwnsScope(entry.kind)) {
      if (declaringScope >= arena.scopes.size() ||
          arena.scopes[declaringScope].parent == zc::none) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      ZC_IF_SOME(parent, arena.scopes[declaringScope].parent) { declaringScope = parent.index(); }
    }
    if (declaringScope >= definitionsByScope.size()) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    definitionScopeIndices[index] = declaringScope;
    definitionsByScope[declaringScope].add(index);

    ast::NodeId introducer = entry.node;
    if (entry.site.value().is<PatternBindingSite>()) {
      introducer = entry.site.value().get<PatternBindingSite>().introducer;
    }
    if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    definitionsByIntroducer[introducer.value].add(index);

    if (entry.kind == identity::DefinitionKind::Closure) {
      ++closureCount;
      const auto& closureSyntax = tree.node(entry.node);
      if (closureSyntax.kind == ast::SyntaxKind::LambdaExpression) {
        closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Inferred;
      } else if (closureSyntax.kind == ast::SyntaxKind::FunctionExpression) {
        const ast::NodeId captureList(
            closureSyntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        if (!captureList) {
          closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Inferred;
        } else {
          if (!tree.contains(captureList) ||
              tree.node(captureList).kind != ast::SyntaxKind::CaptureList) {
            return ExplicitCaptureOracleResult::InvalidBindingFact;
          }
          closureSyntaxDomains[index] = ExplicitOracleClosureSyntax::Explicit;
        }
      } else {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
    }

    if (entry.kind != identity::DefinitionKind::Parameter ||
        tree.node(entry.node).kind != ast::SyntaxKind::FunctionParameterDecl) {
      continue;
    }
    auto tokenSource =
        input.parsedModule().functionParameterNameSpan(entry.node, ast::SyntaxKind::ThisKeyword);
    if (tokenSource == zc::none) { continue; }
    if (entry.bindingName != zc::none ||
        (arena.scopes[declaringScope].kind != ScopeKind::Function &&
         arena.scopes[declaringScope].kind != ScopeKind::Closure)) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    ZC_IF_SOME(source, tokenSource) {
      receiverDefinitions[index] = true;
      receiverSources[index] = source.clone();
      const ExplicitOracleReceiverOrderKey orderKey{declaringScope, source.byteStart(),
                                                    source.byteEnd(), entry.node.value};
      if (receiverOrder.find(orderKey) != zc::none) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      receiverOrder.insert(orderKey,
                           ExplicitOracleReceiverRecord{declaringScope, index, entry.node,
                                                        source.byteStart(), source.byteEnd()});
    }
  }

  zc::Vector<size_t> explicitOrder;
  explicitOrder.reserve(closureCount);
  for (const auto& ordered : inventoryByCanonicalKey) {
    if (closureSyntaxDomains[ordered.value] == ExplicitOracleClosureSyntax::Explicit) {
      explicitOrder.add(ordered.value);
    }
  }
  zc::Vector<ExplicitOracleReceiverRecord> receivers;
  receivers.reserve(receiverOrder.size());
  for (const auto& ordered : receiverOrder) { receivers.add(ordered.value); }

  zc::Vector<size_t> callableInventoryByScope;
  callableInventoryByScope.resize(arena.scopes.size());
  zc::Vector<uint32_t> ownedCallableScopeByInventory;
  ownedCallableScopeByInventory.resize(inventory.size());
  for (auto& index : callableInventoryByScope) { index = kMissing; }
  for (auto& index : ownedCallableScopeByInventory) { index = UINT32_MAX; }
  for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    if (scope.kind != ScopeKind::Function && scope.kind != ScopeKind::Closure) { continue; }
    const auto& owner = scope.owner.value();
    if (!owner.is<DefinitionScopeOwner>()) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    auto callableIndex = inventoryIndex(owner.get<DefinitionScopeOwner>().definition);
    if (callableIndex == zc::none ||
        (scope.kind == ScopeKind::Closure &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind != identity::DefinitionKind::Closure) ||
        (scope.kind == ScopeKind::Function &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind == identity::DefinitionKind::Closure) ||
        ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] != UINT32_MAX) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    callableInventoryByScope[scopeIndex] = ZC_ASSERT_NONNULL(callableIndex);
    ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] =
        static_cast<uint32_t>(scopeIndex);
  }
  for (size_t index = 0; index < inventory.size(); ++index) {
    uint32_t scopeIndex = definitionScopeIndices[index];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        owningCallableScopeIndices[index] = scopeIndex;
        break;
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
  }

  zc::Vector<size_t> candidateDefinitionCounts;
  zc::Vector<size_t> candidateDefinitionSlots;
  zc::Vector<bool> mentionedByScopeBinding;
  zc::Vector<bool> mentionedBySurface;
  candidateDefinitionCounts.resize(inventory.size());
  candidateDefinitionSlots.resize(inventory.size());
  mentionedByScopeBinding.resize(inventory.size());
  mentionedBySurface.resize(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    candidateDefinitionCounts[index] = 0;
    candidateDefinitionSlots[index] = kMissing;
    mentionedByScopeBinding[index] = false;
    mentionedBySurface[index] = false;
  }
  for (size_t index = 0; index < candidate.definitions.size(); ++index) {
    auto definitionIndex = inventoryIndex(candidate.definitions[index].identity);
    ZC_IF_SOME(found, definitionIndex) {
      if (candidateDefinitionCounts[found] == 0) { candidateDefinitionSlots[found] = index; }
      ++candidateDefinitionCounts[found];
    }
  }
  const auto markDefinitionTarget = [&](const BindingTarget& target, zc::Vector<bool>& census) {
    const auto& value = target.value();
    if (!value.is<DefinitionBindingTarget>()) { return; }
    auto definitionIndex = inventoryIndex(value.get<DefinitionBindingTarget>().definition);
    ZC_IF_SOME(found, definitionIndex) { census[found] = true; }
  };
  for (const auto& scope : candidate.scopes) {
    for (const auto& binding : scope.bindings) {
      markDefinitionTarget(binding.binding.bindingIdentity, mentionedByScopeBinding);
      markDefinitionTarget(binding.binding.canonicalTarget, mentionedByScopeBinding);
    }
  }
  const auto censusSurface = [&](const ExportSurfaceEntry& surface) {
    markDefinitionTarget(surface.bindingIdentity, mentionedBySurface);
    markDefinitionTarget(surface.canonicalTarget, mentionedBySurface);
    for (const auto& step : surface.reexportChain) {
      auto aliasIndex = inventoryIndex(step.alias);
      ZC_IF_SOME(found, aliasIndex) { mentionedBySurface[found] = true; }
      markDefinitionTarget(step.canonicalTarget, mentionedBySurface);
    }
  };
  for (const auto& surface : candidate.currentSurface.visibleEntries) { censusSurface(surface); }
  for (const auto& surface : candidate.currentSurface.exports) { censusSurface(surface); }

  zc::TreeMap<ExplicitOracleSpanKey, size_t> sourceFailureCountsBySpan;
  zc::Vector<zc::Vector<size_t>> receiverFailuresBySchema;
  zc::Vector<zc::Vector<size_t>> duplicateFailuresBySchema;
  receiverFailuresBySchema.resize(tree.nodeCount());
  duplicateFailuresBySchema.resize(tree.nodeCount());
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const auto& failureFact = candidate.sourceFailures[index];
    const ExplicitOracleSpanKey spanKey{failureFact.primary.byteStart(),
                                        failureFact.primary.byteEnd()};
    auto spanCount = sourceFailureCountsBySpan.find(spanKey);
    ZC_IF_SOME(count, spanCount) { ++count; }
    else { sourceFailureCountsBySpan.insert(spanKey, 1); }

    const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
    const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
    if (ordinal >= tree.nodeCount()) { continue; }
    if (site == static_cast<uint8_t>(BinderEmitterSite::ModuleSkeleton)) {
      receiverFailuresBySchema[ordinal].add(index);
    }
    if (site == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) &&
        failureFact.diagnostic == BinderDiagnosticCode::DuplicateIdentifier) {
      duplicateFailuresBySchema[ordinal].add(index);
    }
  }

  zc::Vector<bool> receiverActivated;
  receiverActivated.resize(inventory.size());
  for (auto& activated : receiverActivated) { activated = false; }
  size_t firstReceiverIndex = 0;
  for (size_t receiverIndex = 0; receiverIndex < receivers.size(); ++receiverIndex) {
    const auto& wanted = receivers[receiverIndex];
    const auto& entry = inventory[wanted.inventoryIndex];
    const size_t definitionMatches = candidateDefinitionCounts[wanted.inventoryIndex];
    if (definitionMatches == 0) { return ExplicitCaptureOracleResult::MissingRequiredResolution; }
    if (definitionMatches != 1) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    const size_t factSlot = candidateDefinitionSlots[wanted.inventoryIndex];
    if (factSlot >= candidate.definitions.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const auto& fact = candidate.definitions[factSlot];
    identity::CanonicalEncoder expectedNameEncoder;
    identity::CanonicalEncoder actualNameEncoder;
    entry.name.encode(expectedNameEncoder);
    fact.name.encode(actualNameEncoder);
    const auto expectedName = expectedNameEncoder.finish();
    const auto actualName = actualNameEncoder.finish();
    const auto& entrySite = entry.site.value();
    const auto& factSite = fact.site.value();
    if (fact.kind != identity::DefinitionKind::Parameter || fact.nameSpace != Namespace::Value ||
        fact.declaringScope != arena.scopes[wanted.scopeIndex].id ||
        fact.activation != DefinitionActivation::ParameterList ||
        !sameSpan(fact.source, entry.source) ||
        compareCanonicalBytes(expectedName.asPtr(), actualName.asPtr()) != 0 ||
        !entrySite.is<DeclarationDefinitionSite>() || !factSite.is<DeclarationDefinitionSite>() ||
        entrySite.get<DeclarationDefinitionSite>().node != entry.node ||
        factSite.get<DeclarationDefinitionSite>().node != entry.node) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    if (mentionedByScopeBinding[wanted.inventoryIndex] ||
        mentionedBySurface[wanted.inventoryIndex] ||
        resolutionByNode[entry.node.value] != kMissing) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }

    const bool first =
        receiverIndex == 0 || receivers[receiverIndex - 1].scopeIndex != wanted.scopeIndex;
    if (first) { firstReceiverIndex = receiverIndex; }
    const size_t previousIndex = firstReceiverIndex;
    if (receiverSources[wanted.inventoryIndex] == zc::none ||
        receiverSources[receivers[previousIndex].inventoryIndex] == zc::none) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const auto& primary = ZC_ASSERT_NONNULL(receiverSources[wanted.inventoryIndex]);
    const auto& previous =
        ZC_ASSERT_NONNULL(receiverSources[receivers[previousIndex].inventoryIndex]);
    const ExplicitOracleSpanKey primaryKey{primary.byteStart(), primary.byteEnd()};
    size_t relatedFailures = 0;
    ZC_IF_SOME(count, sourceFailureCountsBySpan.find(primaryKey)) { relatedFailures = count; }
    const uint32_t schemaOrdinal = schemaOrdinals[entry.node.value];
    if (schemaOrdinal >= receiverFailuresBySchema.size()) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    const auto& matchingFailures = receiverFailuresBySchema[schemaOrdinal];
    const size_t failureMatches = matchingFailures.size();
    bool failureValid = true;
    for (const auto failureIndex : matchingFailures) {
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      if (first || failureFact.diagnostic != BinderDiagnosticCode::RedeclareParameter ||
          static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, primary) || failureFact.notes.size() != 1 ||
          failureFact.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere ||
          !sameSpan(failureFact.notes[0].source, previous)) {
        failureValid = false;
      }
    }
    if (first && relatedFailures != 0) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    if (first && failureMatches != 0) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    if (!first && failureMatches == 0) {
      return relatedFailures == 0 ? ExplicitCaptureOracleResult::MissingRequiredResolution
                                  : ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    if (!first && (failureMatches != 1 || !failureValid)) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
  }

  const auto activateDefinition = [&](size_t index) {
    if (index >= inventory.size() || definitionScopeIndices[index] == UINT32_MAX ||
        definitionScopeIndices[index] >= activeScopes.size()) {
      return false;
    }
    const auto& entry = inventory[index];
    auto& active = activeScopes[definitionScopeIndices[index]];
    if (receiverDefinitions[index]) {
      receiverActivated[index] = true;
      if (active.receiver == kMissing) { active.receiver = index; }
      return true;
    }
    auto nameSpace = explicitOracleNamespace(entry.kind);
    if (nameSpace == zc::none || entry.bindingName == zc::none) { return true; }
    ZC_IF_SOME(value, nameSpace) {
      if (value != Namespace::Value && value != Namespace::Type) { return true; }
      ZC_IF_SOME(name, entry.bindingName) {
        auto& bindings = value == Namespace::Value ? active.values : active.types;
        if (bindings.find(name.text()) == zc::none) {
          bindings.insert(zc::str(name.text()), index);
        }
      }
    }
    return true;
  };

  const auto activateIntroducer = [&](ast::NodeId introducer,
                                      DefinitionActivation expectedActivation) {
    if (!tree.contains(introducer) || introducer.value >= definitionsByIntroducer.size()) {
      return false;
    }
    zc::TreeMap<ExplicitOracleSourceOrderKey, size_t> order;
    for (const auto index : definitionsByIntroducer[introducer.value]) {
      auto activation = explicitOracleActivation(tree, inventory[index]);
      if (activation == zc::none || activation != expectedActivation) { continue; }
      const auto& source = inventory[index].source;
      order.insert(ExplicitOracleSourceOrderKey{source.byteStart(), source.byteEnd(), index},
                   index);
    }
    for (const auto& ordered : order) {
      if (!activateDefinition(ordered.value)) { return false; }
    }
    return true;
  };

  const auto seedDefinitions = [&](DefinitionActivation expectedActivation) {
    for (size_t scopeIndex = 0; scopeIndex < definitionsByScope.size(); ++scopeIndex) {
      zc::TreeMap<ExplicitOracleSourceOrderKey, size_t> order;
      for (const auto index : definitionsByScope[scopeIndex]) {
        auto activation = explicitOracleActivation(tree, inventory[index]);
        if (activation == zc::none || activation != expectedActivation) { continue; }
        const auto& source = inventory[index].source;
        order.insert(ExplicitOracleSourceOrderKey{source.byteStart(), source.byteEnd(), index},
                     index);
      }
      for (const auto& ordered : order) {
        if (!activateDefinition(ordered.value)) { return false; }
      }
    }
    return true;
  };
  if (!seedDefinitions(DefinitionActivation::ModuleSkeleton) ||
      !seedDefinitions(DefinitionActivation::GenericList)) {
    return ExplicitCaptureOracleResult::MalformedScopeGraph;
  }

  const auto activeDefinition = [&](uint32_t scopeIndex, Namespace nameSpace,
                                    zc::StringPtr name) -> zc::Maybe<size_t> {
    uint32_t current = scopeIndex;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (current >= arena.scopes.size()) { return zc::none; }
      const auto& bindings = nameSpace == Namespace::Value ? activeScopes[current].values
                                                           : activeScopes[current].types;
      auto found = bindings.find(name);
      ZC_IF_SOME(index, found) { return index; }
      const auto& scope = arena.scopes[current];
      if (scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  };

  const auto activeReceiver = [&](uint32_t scopeIndex) -> zc::Maybe<size_t> {
    uint32_t current = scopeIndex;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (current >= arena.scopes.size()) { return zc::none; }
      if (activeScopes[current].receiver != kMissing) { return activeScopes[current].receiver; }
      const auto& scope = arena.scopes[current];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Module ||
          scope.parent == zc::none) {
        break;
      }
      ZC_IF_SOME(parent, scope.parent) { current = parent.index(); }
    }
    return zc::none;
  };

  zc::Vector<zc::TreeMap<size_t, size_t>> expectedTargetsByClosure;
  expectedTargetsByClosure.reserve(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) { expectedTargetsByClosure.add(); }
  zc::Vector<bool> explicitClosureProcessed;
  explicitClosureProcessed.resize(inventory.size());
  for (auto& processed : explicitClosureProcessed) { processed = false; }
  zc::Vector<bool> expectedCaptureSeen;
  zc::Vector<bool> expectedCaptureBound;
  zc::Vector<size_t> expectedCaptureTarget;
  zc::Vector<uint16_t> expectedCaptureDiagnostic;
  zc::Vector<bool> expectedThisSeen;
  expectedCaptureSeen.resize(tree.nodeCount() + 1);
  expectedCaptureBound.resize(tree.nodeCount() + 1);
  expectedCaptureTarget.resize(tree.nodeCount() + 1);
  expectedCaptureDiagnostic.resize(tree.nodeCount() + 1);
  expectedThisSeen.resize(tree.nodeCount() + 1);
  for (size_t index = 0; index < expectedCaptureSeen.size(); ++index) {
    expectedCaptureSeen[index] = false;
    expectedCaptureBound[index] = false;
    expectedCaptureTarget[index] = kMissing;
    expectedCaptureDiagnostic[index] = 0;
    expectedThisSeen[index] = false;
  }

  const auto owningCallableScope = [&](size_t targetIndex) -> zc::Maybe<uint32_t> {
    if (targetIndex >= owningCallableScopeIndices.size() ||
        owningCallableScopeIndices[targetIndex] == UINT32_MAX) {
      return zc::none;
    }
    return owningCallableScopeIndices[targetIndex];
  };

  enum class ExplicitOracleCaptureAccess : uint8_t { Allowed, Denied, Malformed };
  const auto captureAccess = [&](uint32_t referenceScope,
                                 size_t targetIndex) -> ExplicitOracleCaptureAccess {
    if (targetIndex >= inventory.size() || !capturable(inventory[targetIndex].kind)) {
      return ExplicitOracleCaptureAccess::Malformed;
    }
    auto targetScope = owningCallableScope(targetIndex);
    if (targetScope == zc::none) { return ExplicitOracleCaptureAccess::Denied; }
    const uint32_t targetScopeIndex = ZC_ASSERT_NONNULL(targetScope);
    if (targetScopeIndex >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetScopeIndex] == kMissing) {
      return ExplicitOracleCaptureAccess::Malformed;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetScopeIndex];
    uint32_t scopeIndex = referenceScope;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) { return ExplicitOracleCaptureAccess::Malformed; }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableInventoryByScope.size() ||
            callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitOracleCaptureAccess::Malformed;
        }
        const size_t callableIndex = callableInventoryByScope[scopeIndex];
        if (callableIndex == targetCallableIndex) { return ExplicitOracleCaptureAccess::Allowed; }
        if (scope.kind == ScopeKind::Function) { return ExplicitOracleCaptureAccess::Denied; }
        if (callableIndex >= closureSyntaxDomains.size() ||
            inventory[callableIndex].kind != identity::DefinitionKind::Closure ||
            closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::NotClosure) {
          return ExplicitOracleCaptureAccess::Malformed;
        }
        if (closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::Explicit) {
          if (!explicitClosureProcessed[callableIndex]) {
            return ExplicitOracleCaptureAccess::Malformed;
          }
          if (expectedTargetsByClosure[callableIndex].find(targetIndex) == zc::none) {
            return ExplicitOracleCaptureAccess::Denied;
          }
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        return ExplicitOracleCaptureAccess::Denied;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return ExplicitOracleCaptureAccess::Malformed;
  };

  ExplicitCaptureOracleResult oracleResult = ExplicitCaptureOracleResult::Valid;
  const auto visit = [&](auto& self, ast::NodeId node) -> void {
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    const uint32_t scopeIndex = scopeByNode[node.value];
    if (scopeIndex >= arena.scopes.size()) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    const auto& syntax = tree.node(node);

    if (syntax.kind == ast::SyntaxKind::ThisExpr) {
      if (expectedThisSeen[node.value]) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      expectedThisSeen[node.value] = true;
      auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::ThisKeyword);
      if (source == zc::none) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      auto expectedReceiver = activeReceiver(scopeByNode[node.value]);
      bool expectedSuccess = false;
      ZC_IF_SOME(receiverIndex, expectedReceiver) {
        const auto access = captureAccess(scopeByNode[node.value], receiverIndex);
        if (access == ExplicitOracleCaptureAccess::Malformed) {
          oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
          return;
        }
        expectedSuccess = access == ExplicitOracleCaptureAccess::Allowed;
      }
      if (expectedSuccess) {
        if (thisBindingByNode[node.value] == kMissing || resolutionByNode[node.value] != kMissing) {
          oracleResult = ExplicitCaptureOracleResult::MissingRequiredResolution;
          return;
        }
        const auto& binding = candidate.thisBindings[thisBindingByNode[node.value]];
        auto targetIndex = inventoryIndex(binding.binding.receiverParameter);
        if (targetIndex == zc::none || expectedReceiver == zc::none ||
            ZC_ASSERT_NONNULL(expectedReceiver) != ZC_ASSERT_NONNULL(targetIndex) ||
            !receiverDefinitions[ZC_ASSERT_NONNULL(targetIndex)] ||
            !sameSpan(binding.source, ZC_ASSERT_NONNULL(source))) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        return;
      }
      if (thisBindingByNode[node.value] != kMissing || resolutionByNode[node.value] == kMissing) {
        oracleResult = ExplicitCaptureOracleResult::MissingRequiredResolution;
        return;
      }
      const auto& resolution = candidate.nodeBindings[resolutionByNode[node.value]];
      if (!resolution.value.is<FailedBindingResolution>()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
      if (failureIndex >= candidate.sourceFailures.size()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
      const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
      if (failureFact.diagnostic != BinderDiagnosticCode::UndefinedIdentifier ||
          site != static_cast<uint8_t>(BinderEmitterSite::BodyBinding) ||
          ordinal != schemaOrdinals[node.value] ||
          static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, ZC_ASSERT_NONNULL(source)) || !failureFact.notes.empty()) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::LetStmt) {
      const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
      if (!tree.contains(declarations) ||
          tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& list = tree.node(declarations);
      const ast::NodeList declarators{
          list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
          list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
      if (!tree.contains(declarators)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto declarator : tree.list(declarators)) {
        if (!tree.contains(declarator) ||
            tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        const auto& declaration = tree.node(declarator);
        const ast::NodeId type(declaration.payload.words[ast::kVariableDeclaratorTyWord]);
        const ast::NodeId initializer(declaration.payload.words[ast::kVariableDeclaratorInitWord]);
        const ast::NodeId pattern(declaration.payload.words[ast::kVariableDeclaratorPatternWord]);
        if (tree.contains(type)) { self(self, type); }
        if (tree.contains(initializer)) { self(self, initializer); }
        if (tree.contains(pattern)) { self(self, pattern); }
        if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
        if (!activateIntroducer(declarator, DefinitionActivation::AfterInitializer)) {
          oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
          return;
        }
      }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::ForInStatement) {
      const ast::NodeId expression(syntax.payload.words[ast::kForInStatementExpressionWord]);
      const ast::NodeId binding(syntax.payload.words[ast::kForInStatementBindingWord]);
      const ast::NodeId body(syntax.payload.words[ast::kForInStatementBodyWord]);
      if (tree.contains(expression)) { self(self, expression); }
      if (tree.contains(binding)) { self(self, binding); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(node, DefinitionActivation::LoopPattern)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      if (tree.contains(body)) { self(self, body); }
      return;
    }

    if (syntax.kind == ast::SyntaxKind::MatchArmStmt) {
      const ast::NodeId pattern(syntax.payload.words[ast::kMatchArmStmtPatternWord]);
      const ast::NodeId guard(syntax.payload.words[ast::kMatchArmStmtGuardWord]);
      const ast::NodeId body(syntax.payload.words[ast::kMatchArmStmtBodyWord]);
      if (tree.contains(pattern)) { self(self, pattern); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(node, DefinitionActivation::MatchPattern)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      if (tree.contains(guard)) { self(self, guard); }
      if (tree.contains(body)) { self(self, body); }
      return;
    }

    const bool callable = syntax.kind == ast::SyntaxKind::FunctionDecl ||
                          syntax.kind == ast::SyntaxKind::MethodDecl ||
                          syntax.kind == ast::SyntaxKind::ConstructorDecl ||
                          syntax.kind == ast::SyntaxKind::DestructorDecl ||
                          syntax.kind == ast::SyntaxKind::ExternDecl ||
                          syntax.kind == ast::SyntaxKind::FunctionExpression ||
                          syntax.kind == ast::SyntaxKind::LambdaExpression;
    if (!callable) {
      if (syntax.kind == ast::SyntaxKind::FunctionParameterDecl ||
          syntax.kind == ast::SyntaxKind::CaptureList ||
          syntax.kind == ast::SyntaxKind::CaptureItem) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) { self(self, child); });
      return;
    }

    ast::NodeId parameterList;
    ast::NodeId genericParameters;
    ast::NodeId captures;
    ast::NodeId returnType;
    ast::NodeId raisesType;
    ast::NodeId body;
    ast::NodeId expressionBody;
    bool isExtern = false;
    bool isClosure = false;
    switch (syntax.kind) {
      case ast::SyntaxKind::FunctionDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]);
        genericParameters = ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kFunctionDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kFunctionDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kFunctionDeclBodyWord]);
        break;
      case ast::SyntaxKind::MethodDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]);
        genericParameters = ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kMethodDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kMethodDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kMethodDeclBodyWord]);
        break;
      case ast::SyntaxKind::ConstructorDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kConstructorDeclParamsIdWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kConstructorDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kConstructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::DestructorDecl:
        parameterList = ast::NodeId(syntax.payload.words[ast::kDestructorDeclParamsIdWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kDestructorDeclRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kDestructorDeclBodyWord]);
        break;
      case ast::SyntaxKind::ExternDecl:
        isExtern = true;
        returnType = ast::NodeId(syntax.payload.words[ast::kExternDeclRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kExternDeclRaisesTyWord]);
        break;
      case ast::SyntaxKind::FunctionExpression:
        isClosure = true;
        parameterList = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionParamsIdWord]);
        genericParameters =
            ast::NodeId(syntax.payload.words[ast::kFunctionExpressionTypeParamsIdWord]);
        captures = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kFunctionExpressionBodyWord]);
        break;
      case ast::SyntaxKind::LambdaExpression:
        isClosure = true;
        parameterList = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionParamsIdWord]);
        returnType = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionRetTyWord]);
        raisesType = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionRaisesTyWord]);
        body = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionBodyWord]);
        expressionBody = ast::NodeId(syntax.payload.words[ast::kLambdaExpressionExprBodyWord]);
        break;
      default:
        ZC_UNREACHABLE;
    }

    if (isClosure && !activateIntroducer(node, DefinitionActivation::ExpressionIntroduction)) {
      oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
      return;
    }
    if (tree.contains(genericParameters)) { self(self, genericParameters); }
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }

    if (tree.contains(captures)) {
      if (syntax.kind != ast::SyntaxKind::FunctionExpression ||
          tree.node(captures).kind != ast::SyntaxKind::CaptureList || scopeIndex == 0 ||
          arena.scopes[scopeIndex].kind != ScopeKind::Closure ||
          arena.scopes[scopeIndex].parent == zc::none) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      if (scopeIndex >= callableInventoryByScope.size() ||
          callableInventoryByScope[scopeIndex] == kMissing) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      const size_t closureInventoryIndex = callableInventoryByScope[scopeIndex];
      if (inventory[closureInventoryIndex].node != node ||
          inventory[closureInventoryIndex].kind != identity::DefinitionKind::Closure) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& captureList = tree.node(captures);
      const ast::NodeList items{captureList.payload.words[ast::kCaptureListCapturesFirstWord],
                                captureList.payload.words[ast::kCaptureListCapturesSizeWord]};
      if (!tree.contains(items) ||
          captureList.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const uint32_t enclosingScope = ZC_ASSERT_NONNULL(arena.scopes[scopeIndex].parent).index();
      for (const auto item : tree.list(items)) {
        if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
            scopeByNode[item.value] != scopeIndex || expectedCaptureSeen[item.value]) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        expectedCaptureSeen[item.value] = true;
        const auto& captureSyntax = tree.node(item);
        const auto mode =
            static_cast<ast::CaptureMode>(captureSyntax.payload.words[ast::kCaptureItemModeWord]);
        size_t tokenOrdinal = 0;
        ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
        if (mode == ast::CaptureMode::ByRef) {
          tokenOrdinal = 1;
        } else if (mode == ast::CaptureMode::This) {
          tokenKind = ast::SyntaxKind::ThisKeyword;
        } else if (mode != ast::CaptureMode::ByValue) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        auto itemSource = input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
        if (itemSource == zc::none) {
          oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
          return;
        }
        const ast::IdentId identifier(captureSyntax.payload.words[ast::kCaptureItemNameWord]);
        zc::Maybe<size_t> target;
        BinderDiagnosticCode failureDiagnostic = BinderDiagnosticCode::UndefinedIdentifier;
        if (mode == ast::CaptureMode::This) {
          if (tree.ident(identifier) != "this"_zc) {
            oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
            return;
          }
          target = activeReceiver(enclosingScope);
        } else {
          auto semanticName = identity::SemanticIdentifier::fromSource(tree.ident(identifier));
          if (semanticName == zc::none) {
            oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
            return;
          }
          ZC_IF_SOME(name, semanticName) {
            target = activeDefinition(enclosingScope, Namespace::Value, name.text());
            if (target == zc::none &&
                activeDefinition(enclosingScope, Namespace::Type, name.text()) != zc::none) {
              failureDiagnostic = BinderDiagnosticCode::SymbolNamespaceMismatch;
            }
          }
        }
        bool accepted = false;
        ZC_IF_SOME(targetIndex, target) {
          if (capturable(inventory[targetIndex].kind)) {
            const auto access = captureAccess(enclosingScope, targetIndex);
            if (access == ExplicitOracleCaptureAccess::Malformed) {
              oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
              return;
            }
            if (access == ExplicitOracleCaptureAccess::Allowed) {
              expectedCaptureBound[item.value] = true;
              expectedCaptureTarget[item.value] = targetIndex;
              auto& expectedTargets = expectedTargetsByClosure[closureInventoryIndex];
              if (expectedTargets.find(targetIndex) == zc::none) {
                expectedTargets.insert(targetIndex, item.value);
              }
              accepted = true;
            }
          }
        }
        if (!accepted) {
          expectedCaptureDiagnostic[item.value] = static_cast<uint16_t>(failureDiagnostic);
        }
      }
      explicitClosureProcessed[closureInventoryIndex] = true;
    }

    zc::Vector<ast::NodeId> parameters;
    if (isExtern) {
      const ast::NodeList values{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                                 syntax.payload.words[ast::kExternDeclParamsSizeWord]};
      if (!tree.contains(values)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto parameter : tree.list(values)) { parameters.add(parameter); }
    } else {
      if (!tree.contains(parameterList) ||
          tree.node(parameterList).kind != ast::SyntaxKind::FunctionParameterList) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      const auto& list = tree.node(parameterList);
      const ast::NodeList values{list.payload.words[ast::kFunctionParameterListParamsFirstWord],
                                 list.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      if (!tree.contains(values)) {
        oracleResult = ExplicitCaptureOracleResult::InvalidBindingFact;
        return;
      }
      for (const auto parameter : tree.list(values)) { parameters.add(parameter); }
    }
    for (const auto parameter : parameters) {
      if (!tree.contains(parameter) ||
          tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl ||
          scopeByNode[parameter.value] != scopeIndex) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
      const auto& parameterSyntax = tree.node(parameter);
      const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
      const ast::NodeId attributes(
          parameterSyntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
      if (tree.contains(type) &&
          !input.parsedModule().functionParameterHasImplicitSelfType(parameter)) {
        self(self, type);
      }
      if (tree.contains(attributes)) { self(self, attributes); }
    }
    if (tree.contains(returnType)) { self(self, returnType); }
    if (tree.contains(raisesType)) { self(self, raisesType); }
    if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
    for (const auto parameter : parameters) {
      const auto& parameterSyntax = tree.node(parameter);
      const ast::NodeId defaultValue(
          parameterSyntax.payload.words[ast::kFunctionParameterDeclDefaultWord]);
      if (tree.contains(defaultValue)) { self(self, defaultValue); }
      if (oracleResult != ExplicitCaptureOracleResult::Valid) { return; }
      if (!activateIntroducer(parameter, DefinitionActivation::ParameterList)) {
        oracleResult = ExplicitCaptureOracleResult::MalformedScopeGraph;
        return;
      }
    }
    if (tree.contains(body)) { self(self, body); }
    if (tree.contains(expressionBody)) { self(self, expressionBody); }
  };
  visit(visit, tree.root());
  if (oracleResult != ExplicitCaptureOracleResult::Valid) { return oracleResult; }
  bool completeThisCensus = true;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::ThisExpr && !expectedThisSeen[node.value]) {
      completeThisCensus = false;
    }
  });
  if (!completeThisCensus) { return ExplicitCaptureOracleResult::MissingRequiredResolution; }
  for (const auto& wanted : receivers) {
    if (!receiverActivated[wanted.inventoryIndex]) {
      return ExplicitCaptureOracleResult::MissingRequiredResolution;
    }
  }
  for (size_t index = 0; index < receivers.size();) {
    const auto scopeIndex = receivers[index].scopeIndex;
    const auto expectedReceiver = receivers[index].inventoryIndex;
    if (activeScopes[scopeIndex].receiver != expectedReceiver) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    while (index < receivers.size() && receivers[index].scopeIndex == scopeIndex) { ++index; }
  }

  if (candidate.explicitClosureCaptures.size() < explicitOrder.size()) {
    return ExplicitCaptureOracleResult::MissingRequiredResolution;
  }
  if (candidate.explicitClosureCaptures.size() > explicitOrder.size()) {
    return ExplicitCaptureOracleResult::InvalidBindingFact;
  }

  zc::Vector<size_t> explicitCaptureRowByClosure;
  zc::Vector<size_t> explicitCaptureRowCounts;
  zc::Vector<size_t> inferredCaptureRowByClosure;
  zc::Vector<size_t> inferredCaptureRowCounts;
  zc::Vector<zc::TreeMap<size_t, size_t>> validatedCaptureTargetsByClosure;
  explicitCaptureRowByClosure.resize(inventory.size());
  explicitCaptureRowCounts.resize(inventory.size());
  inferredCaptureRowByClosure.resize(inventory.size());
  inferredCaptureRowCounts.resize(inventory.size());
  validatedCaptureTargetsByClosure.reserve(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    explicitCaptureRowByClosure[index] = kMissing;
    explicitCaptureRowCounts[index] = 0;
    inferredCaptureRowByClosure[index] = kMissing;
    inferredCaptureRowCounts[index] = 0;
    validatedCaptureTargetsByClosure.add();
  }
  for (size_t rowIndex = 0; rowIndex < candidate.explicitClosureCaptures.size(); ++rowIndex) {
    auto closureIndex = inventoryIndex(candidate.explicitClosureCaptures[rowIndex].closure);
    if (closureIndex == zc::none ||
        inventory[ZC_ASSERT_NONNULL(closureIndex)].kind != identity::DefinitionKind::Closure) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const size_t index = ZC_ASSERT_NONNULL(closureIndex);
    if (explicitCaptureRowCounts[index] == 0) { explicitCaptureRowByClosure[index] = rowIndex; }
    ++explicitCaptureRowCounts[index];
  }
  for (size_t rowIndex = 0; rowIndex < candidate.closureFreeVariables.size(); ++rowIndex) {
    auto closureIndex = inventoryIndex(candidate.closureFreeVariables[rowIndex].closure);
    if (closureIndex == zc::none ||
        inventory[ZC_ASSERT_NONNULL(closureIndex)].kind != identity::DefinitionKind::Closure) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const size_t index = ZC_ASSERT_NONNULL(closureIndex);
    if (inferredCaptureRowCounts[index] == 0) { inferredCaptureRowByClosure[index] = rowIndex; }
    ++inferredCaptureRowCounts[index];
  }

  const auto duplicateFailure = [&](ast::NodeId item, const identity::SourceSpan& primary,
                                    zc::Maybe<const identity::SourceSpan&> previous) {
    if (item.value >= schemaOrdinals.size() || schemaOrdinals[item.value] == UINT32_MAX ||
        schemaOrdinals[item.value] >= duplicateFailuresBySchema.size()) {
      return false;
    }
    const auto& matchingFailures = duplicateFailuresBySchema[schemaOrdinals[item.value]];
    const size_t matches = matchingFailures.size();
    bool valid = true;
    for (const auto failureIndex : matchingFailures) {
      const auto& failureFact = candidate.sourceFailures[failureIndex];
      if (previous == zc::none || static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
          !sameSpan(failureFact.primary, primary) || failureFact.notes.size() != 1 ||
          failureFact.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere ||
          !sameSpan(failureFact.notes[0].source, ZC_ASSERT_NONNULL(previous))) {
        valid = false;
      }
    }
    return valid && matches == (previous == zc::none ? 0 : 1);
  };

  for (size_t rowIndex = 0; rowIndex < explicitOrder.size(); ++rowIndex) {
    const size_t closureInventoryIndex = explicitOrder[rowIndex];
    const auto& entry = inventory[closureInventoryIndex];
    const auto& syntax = tree.node(entry.node);
    const ast::NodeId listNode(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
    const auto& list = tree.node(listNode);
    auto listSource = input.parsedModule().spanFor(list.range);
    const auto& actual = candidate.explicitClosureCaptures[rowIndex];
    if (listSource == zc::none || actual.closure != entry.definition ||
        actual.captureList != listNode || !sameSpan(actual.source, ZC_ASSERT_NONNULL(listSource)) ||
        explicitCaptureRowCounts[closureInventoryIndex] != 1 ||
        explicitCaptureRowByClosure[closureInventoryIndex] != rowIndex ||
        inferredCaptureRowCounts[closureInventoryIndex] != 0) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const ast::NodeList items{list.payload.words[ast::kCaptureListCapturesFirstWord],
                              list.payload.words[ast::kCaptureListCapturesSizeWord]};
    if (!tree.contains(items) || list.payload.words[ast::kCaptureListNCapturesWord] != items.size) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    size_t captureIndex = 0;
    for (const auto item : tree.list(items)) {
      if (!tree.contains(item) || tree.node(item).kind != ast::SyntaxKind::CaptureItem ||
          item.value >= resolutionByNode.size() || resolutionByNode[item.value] == kMissing ||
          !expectedCaptureSeen[item.value]) {
        return ExplicitCaptureOracleResult::MissingRequiredResolution;
      }
      const auto& captureSyntax = tree.node(item);
      const auto mode =
          static_cast<ast::CaptureMode>(captureSyntax.payload.words[ast::kCaptureItemModeWord]);
      size_t tokenOrdinal = 0;
      ast::SyntaxKind tokenKind = ast::SyntaxKind::Identifier;
      if (mode == ast::CaptureMode::ByRef) {
        tokenOrdinal = 1;
      } else if (mode == ast::CaptureMode::This) {
        tokenKind = ast::SyntaxKind::ThisKeyword;
      } else if (mode != ast::CaptureMode::ByValue) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      auto itemSource = input.parsedModule().retainedTokenSpan(item, tokenOrdinal, tokenKind);
      if (itemSource == zc::none) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
      const auto& resolution = candidate.nodeBindings[resolutionByNode[item.value]];
      if (!expectedCaptureBound[item.value]) {
        if (!resolution.value.is<FailedBindingResolution>()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        const auto failureIndex = resolution.value.get<FailedBindingResolution>().failureIndex;
        if (failureIndex >= candidate.sourceFailures.size()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        const auto& failureFact = candidate.sourceFailures[failureIndex];
        const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
        const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
        if (failureFact.diagnostic !=
                static_cast<BinderDiagnosticCode>(expectedCaptureDiagnostic[item.value]) ||
            site != static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) ||
            ordinal != schemaOrdinals[item.value] ||
            static_cast<uint16_t>(failureFact.emitterOrdinal) != 0 ||
            !sameSpan(failureFact.primary, ZC_ASSERT_NONNULL(itemSource)) ||
            !failureFact.notes.empty()) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        continue;
      }
      if (!resolution.value.is<BoundNameResolution>()) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      if (expectedCaptureTarget[item.value] == kMissing ||
          expectedCaptureTarget[item.value] >= inventory.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      if (captureIndex >= actual.captures.size()) {
        return ExplicitCaptureOracleResult::MissingRequiredResolution;
      }
      const auto& bound = resolution.value.get<BoundNameResolution>();
      const auto& bindingIdentity = bound.bindingIdentity.value();
      const auto& canonicalTarget = bound.canonicalTarget.value();
      if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration ||
          !bindingIdentity.is<DefinitionBindingTarget>() ||
          !canonicalTarget.is<DefinitionBindingTarget>() ||
          bindingIdentity.get<DefinitionBindingTarget>().definition !=
              inventory[expectedCaptureTarget[item.value]].definition ||
          canonicalTarget.get<DefinitionBindingTarget>().definition !=
              inventory[expectedCaptureTarget[item.value]].definition) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      const size_t targetIndex = expectedCaptureTarget[item.value];
      const auto target = inventory[targetIndex].definition;
      if (!capturable(inventory[targetIndex].kind)) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      const auto& targetEntry = inventory[targetIndex];
      const ast::IdentId itemName(captureSyntax.payload.words[ast::kCaptureItemNameWord]);
      if (mode == ast::CaptureMode::This) {
        if (!receiverDefinitions[targetIndex] || tree.ident(itemName) != "this"_zc) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      } else {
        auto name = identity::SemanticIdentifier::fromSource(tree.ident(itemName));
        if (name == zc::none || targetEntry.bindingName == zc::none ||
            ZC_ASSERT_NONNULL(name) != ZC_ASSERT_NONNULL(targetEntry.bindingName)) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      }
      const auto& capture = actual.captures[captureIndex];
      if (capture.item != item || capture.target != target ||
          !sameSpan(capture.source, ZC_ASSERT_NONNULL(itemSource))) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      zc::Maybe<const identity::SourceSpan&> previous;
      auto& validatedTargets = validatedCaptureTargetsByClosure[closureInventoryIndex];
      auto firstCaptureIndex = validatedTargets.find(targetIndex);
      ZC_IF_SOME(firstIndex, firstCaptureIndex) {
        if (firstIndex >= captureIndex) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
        previous = actual.captures[firstIndex].source;
      }
      if (!duplicateFailure(item, capture.source, previous)) {
        return ExplicitCaptureOracleResult::InvalidBindingFact;
      }
      if (firstCaptureIndex == zc::none) { validatedTargets.insert(targetIndex, captureIndex); }
      ++captureIndex;
    }
    if (captureIndex != actual.captures.size()) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
  }

  size_t partitionCount = 0;
  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto& entry = inventory[index];
    if (entry.kind != identity::DefinitionKind::Closure) { continue; }
    if (explicitCaptureRowCounts[index] > 1 || inferredCaptureRowCounts[index] > 1) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    const bool explicitFact = explicitCaptureRowCounts[index] == 1;
    const bool inferredFact = inferredCaptureRowCounts[index] == 1;
    if (explicitFact && inferredFact) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
    if (!explicitFact && !inferredFact) {
      return ExplicitCaptureOracleResult::MissingRequiredResolution;
    }
    if ((explicitFact && closureSyntaxDomains[index] != ExplicitOracleClosureSyntax::Explicit) ||
        (inferredFact && closureSyntaxDomains[index] != ExplicitOracleClosureSyntax::Inferred) ||
        (explicitFact &&
         explicitCaptureRowByClosure[index] >= candidate.explicitClosureCaptures.size()) ||
        (inferredFact &&
         inferredCaptureRowByClosure[index] >= candidate.closureFreeVariables.size())) {
      return ExplicitCaptureOracleResult::InvalidBindingFact;
    }
    ++partitionCount;
  }
  if (partitionCount != closureCount) { return ExplicitCaptureOracleResult::InvalidBindingFact; }

  for (const auto& resolution : candidate.nodeBindings) {
    if (!resolution.value.is<BoundNameResolution>() || !tree.contains(resolution.node) ||
        resolution.node.value >= scopeByNode.size() ||
        scopeByNode[resolution.node.value] == UINT32_MAX) {
      continue;
    }
    if (tree.node(resolution.node).kind == ast::SyntaxKind::CaptureItem) { continue; }
    if (tree.node(resolution.node).kind == ast::SyntaxKind::ThisExpr) { continue; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    const auto& bindingIdentity = bound.bindingIdentity.value();
    const auto& canonicalTarget = bound.canonicalTarget.value();
    if (!bindingIdentity.is<DefinitionBindingTarget>() ||
        !canonicalTarget.is<DefinitionBindingTarget>() ||
        bindingIdentity.get<DefinitionBindingTarget>().definition !=
            canonicalTarget.get<DefinitionBindingTarget>().definition) {
      continue;
    }
    auto targetIndex = inventoryIndex(bindingIdentity.get<DefinitionBindingTarget>().definition);
    if (targetIndex == zc::none || !capturable(inventory[ZC_ASSERT_NONNULL(targetIndex)].kind)) {
      continue;
    }
    const size_t targetInventoryIndex = ZC_ASSERT_NONNULL(targetIndex);
    auto targetScope = owningCallableScope(targetInventoryIndex);
    if (targetScope == zc::none) {
      if (targetInventoryIndex >= definitionScopeIndices.size() ||
          definitionScopeIndices[targetInventoryIndex] == UINT32_MAX) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const uint32_t targetDefinitionScope = definitionScopeIndices[targetInventoryIndex];
      uint32_t directScopeIndex = scopeByNode[resolution.node.value];
      bool reachedDirectTarget = false;
      for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
        if (directScopeIndex >= arena.scopes.size()) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        if (directScopeIndex == targetDefinitionScope) {
          reachedDirectTarget = true;
          break;
        }
        const auto& directScope = arena.scopes[directScopeIndex];
        if (directScope.kind == ScopeKind::Function || directScope.kind == ScopeKind::Closure) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        if (directScope.parent == zc::none) { break; }
        ZC_IF_SOME(parent, directScope.parent) { directScopeIndex = parent.index(); }
      }
      if (!reachedDirectTarget) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
      continue;
    }
    const uint32_t targetCallableScope = ZC_ASSERT_NONNULL(targetScope);
    if (targetCallableScope >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetCallableScope] == kMissing) {
      return ExplicitCaptureOracleResult::MalformedScopeGraph;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetCallableScope];
    uint32_t scopeIndex = scopeByNode[resolution.node.value];
    bool reachedTarget = false;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        return ExplicitCaptureOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
        if (scopeIndex >= callableInventoryByScope.size() ||
            callableInventoryByScope[scopeIndex] == kMissing) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        const size_t callableIndex = callableInventoryByScope[scopeIndex];
        if (callableIndex == targetCallableIndex) {
          reachedTarget = true;
          break;
        }
        if (scope.kind == ScopeKind::Function) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
        if (callableIndex >= closureSyntaxDomains.size() ||
            closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::NotClosure) {
          return ExplicitCaptureOracleResult::MalformedScopeGraph;
        }
        if (closureSyntaxDomains[callableIndex] == ExplicitOracleClosureSyntax::Explicit &&
            validatedCaptureTargetsByClosure[callableIndex].find(targetInventoryIndex) ==
                zc::none) {
          return ExplicitCaptureOracleResult::InvalidBindingFact;
        }
      }
      if (scope.kind == ScopeKind::Module || scope.parent == zc::none) { break; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    if (!reachedTarget) { return ExplicitCaptureOracleResult::InvalidBindingFact; }
  }
  return ExplicitCaptureOracleResult::Valid;
}

BinderInvariantKind explicitCaptureOracleInvariant(ExplicitCaptureOracleResult result) {
  switch (result) {
    case ExplicitCaptureOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ExplicitCaptureOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ExplicitCaptureOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ExplicitCaptureOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

ClosureFreeVariableOracleResult verifyClosureFreeVariableFacts(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto& tree = input.tree();
  const auto inventory = input.definitions().definitions();
  auto arenaResult = ScopeArenaBuilder::build(input);
  if (!arenaResult.is<ScopeArenaCandidate>()) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }
  auto arena = zc::mv(arenaResult.get<ScopeArenaCandidate>());
  if (arena.scopes.empty() || arena.nodeScopes.size() != tree.nodeCount()) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }

  zc::Vector<uint32_t> scopeByNode;
  scopeByNode.resize(tree.nodeCount() + 1);
  for (auto& scope : scopeByNode) { scope = UINT32_MAX; }
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    const auto& scope = arena.scopes[index];
    if (scope.id.module() != input.module() || scope.id.index() != index ||
        !scope.id.belongsTo(input.semanticContext()) || (index == 0 && scope.parent != zc::none) ||
        (index != 0 && scope.parent == zc::none)) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.module() != input.module() || parent.index() >= index ||
          !parent.belongsTo(input.semanticContext())) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
    }
    if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure) {
      const auto& owner = scope.owner.value();
      if (!owner.is<DefinitionScopeOwner>() ||
          input.definitions().definitionKey(owner.get<DefinitionScopeOwner>().definition) ==
              zc::none) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
    }
  }
  for (const auto& fact : arena.nodeScopes) {
    if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
        scopeByNode[fact.node.value] != UINT32_MAX || fact.scope.module() != input.module() ||
        fact.scope.index() >= arena.scopes.size() ||
        arena.scopes[fact.scope.index()].id != fact.scope) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    scopeByNode[fact.node.value] = fact.scope.index();
  }
  auto schemaOrdinalsResult = schemaPreorderOrdinals(tree);
  if (schemaOrdinalsResult == zc::none) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }
  auto schemaOrdinals = zc::mv(ZC_ASSERT_NONNULL(schemaOrdinalsResult));

  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::TreeMap<zc::String, size_t> inventoryByCanonicalKey;
  zc::Vector<size_t> canonicalRankByInventory;
  canonicalRankByInventory.resize(inventory.size());
  for (size_t index = 0; index < inventory.size(); ++index) {
    canonicalRankByInventory[index] = kMissing;
    const auto& entry = inventory[index];
    auto registeredKey = input.definitions().definitionKey(entry.definition);
    if (registeredKey == zc::none) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    const auto entryBytes = entry.key.encode();
    const auto registeredBytes = ZC_ASSERT_NONNULL(registeredKey).encode();
    if (compareCanonicalBytes(entryBytes.asPtr(), registeredBytes.asPtr()) != 0) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    auto canonicalKey = zc::str(entryBytes.asChars());
    if (inventoryByCanonicalKey.find(canonicalKey) != zc::none) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    inventoryByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }
  size_t canonicalRank = 0;
  for (const auto& ordered : inventoryByCanonicalKey) {
    canonicalRankByInventory[ordered.value] = canonicalRank++;
  }
  const auto inventoryIndex = [&](identity::DefId definition) -> zc::Maybe<size_t> {
    auto registeredKey = input.definitions().definitionKey(definition);
    if (registeredKey == zc::none) { return zc::none; }
    const auto bytes = ZC_ASSERT_NONNULL(registeredKey).encode();
    const auto canonicalKey = zc::str(bytes.asChars());
    auto found = inventoryByCanonicalKey.find(canonicalKey);
    ZC_IF_SOME(index, found) {
      if (index < inventory.size() && inventory[index].definition == definition) { return index; }
    }
    return zc::none;
  };

  enum class ClosureFreeOracleClosureSyntax : uint8_t { NotClosure, Inferred, Explicit };
  zc::Vector<ClosureFreeOracleClosureSyntax> closureSyntaxDomains;
  closureSyntaxDomains.resize(inventory.size());
  for (auto& domain : closureSyntaxDomains) { domain = ClosureFreeOracleClosureSyntax::NotClosure; }
  zc::Vector<size_t> closureOrder;
  for (const auto& ordered : inventoryByCanonicalKey) {
    const size_t entryIndex = ordered.value;
    const auto& entry = inventory[entryIndex];
    if (entry.kind != identity::DefinitionKind::Closure) { continue; }
    if (!tree.contains(entry.node)) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    const auto& syntax = tree.node(entry.node);
    if (syntax.kind == ast::SyntaxKind::FunctionExpression) {
      const ast::NodeId captures(syntax.payload.words[ast::kFunctionExpressionCapturesIdWord]);
      if (captures) {
        if (!tree.contains(captures) || tree.node(captures).kind != ast::SyntaxKind::CaptureList) {
          return ClosureFreeVariableOracleResult::InvalidBindingFact;
        }
        closureSyntaxDomains[entryIndex] = ClosureFreeOracleClosureSyntax::Explicit;
        continue;
      }
    } else if (syntax.kind != ast::SyntaxKind::LambdaExpression) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    closureSyntaxDomains[entryIndex] = ClosureFreeOracleClosureSyntax::Inferred;
    closureOrder.add(entryIndex);
  }
  if (candidate.closureFreeVariables.size() < closureOrder.size()) {
    return ClosureFreeVariableOracleResult::MissingRequiredResolution;
  }
  if (candidate.closureFreeVariables.size() > closureOrder.size()) {
    return ClosureFreeVariableOracleResult::InvalidBindingFact;
  }
  for (size_t index = 0; index < closureOrder.size(); ++index) {
    if (candidate.closureFreeVariables[index].closure !=
        inventory[closureOrder[index]].definition) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
  }

  const auto capturable = [](identity::DefinitionKind kind) {
    return kind == identity::DefinitionKind::Parameter || kind == identity::DefinitionKind::Local ||
           kind == identity::DefinitionKind::PatternBinding;
  };

  zc::Vector<uint32_t> definitionScopeIndices;
  zc::Vector<uint32_t> owningCallableScopeIndices;
  zc::Vector<size_t> callableInventoryByScope;
  zc::Vector<uint32_t> ownedCallableScopeByInventory;
  zc::Vector<uint32_t> nearestCallableScopeByScope;
  zc::Vector<uint32_t> parentCallableScopeByScope;
  zc::Vector<uint32_t> scopeEnter;
  zc::Vector<uint32_t> scopeExit;
  definitionScopeIndices.resize(inventory.size());
  owningCallableScopeIndices.resize(inventory.size());
  callableInventoryByScope.resize(arena.scopes.size());
  ownedCallableScopeByInventory.resize(inventory.size());
  nearestCallableScopeByScope.resize(arena.scopes.size());
  parentCallableScopeByScope.resize(arena.scopes.size());
  scopeEnter.resize(arena.scopes.size());
  scopeExit.resize(arena.scopes.size());
  for (auto& index : callableInventoryByScope) { index = kMissing; }
  for (auto& index : ownedCallableScopeByInventory) { index = UINT32_MAX; }
  for (size_t scopeIndex = 0; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    uint32_t parentCallableScope = UINT32_MAX;
    ZC_IF_SOME(parent, scope.parent) {
      if (parent.index() >= scopeIndex) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      parentCallableScope = nearestCallableScopeByScope[parent.index()];
    }
    parentCallableScopeByScope[scopeIndex] = parentCallableScope;
    nearestCallableScopeByScope[scopeIndex] =
        scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure
            ? static_cast<uint32_t>(scopeIndex)
            : parentCallableScope;
    if (scope.kind != ScopeKind::Function && scope.kind != ScopeKind::Closure) { continue; }
    const auto& owner = scope.owner.value();
    if (!owner.is<DefinitionScopeOwner>()) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    auto callableIndex = inventoryIndex(owner.get<DefinitionScopeOwner>().definition);
    if (callableIndex == zc::none ||
        (scope.kind == ScopeKind::Closure &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind != identity::DefinitionKind::Closure) ||
        (scope.kind == ScopeKind::Function &&
         inventory[ZC_ASSERT_NONNULL(callableIndex)].kind == identity::DefinitionKind::Closure) ||
        ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] != UINT32_MAX) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    callableInventoryByScope[scopeIndex] = ZC_ASSERT_NONNULL(callableIndex);
    ownedCallableScopeByInventory[ZC_ASSERT_NONNULL(callableIndex)] =
        static_cast<uint32_t>(scopeIndex);
  }

  zc::Vector<uint32_t> lastChild;
  zc::Vector<uint32_t> previousSibling;
  lastChild.resize(arena.scopes.size());
  previousSibling.resize(arena.scopes.size());
  for (auto& index : lastChild) { index = UINT32_MAX; }
  for (auto& index : previousSibling) { index = UINT32_MAX; }
  for (size_t scopeIndex = 1; scopeIndex < arena.scopes.size(); ++scopeIndex) {
    const auto& scope = arena.scopes[scopeIndex];
    if (scope.parent == zc::none) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    ZC_IF_SOME(parent, scope.parent) {
      previousSibling[scopeIndex] = lastChild[parent.index()];
      lastChild[parent.index()] = static_cast<uint32_t>(scopeIndex);
    }
  }
  struct ScopeTraversalEvent final {
    uint32_t scope;
    bool exiting;
  };
  zc::Vector<ScopeTraversalEvent> traversal;
  traversal.add(ScopeTraversalEvent{0, false});
  uint32_t nextScopeEntry = 0;
  while (!traversal.empty()) {
    const auto event = traversal.back();
    traversal.removeLast();
    if (event.exiting) {
      scopeExit[event.scope] = nextScopeEntry;
      continue;
    }
    scopeEnter[event.scope] = nextScopeEntry++;
    traversal.add(ScopeTraversalEvent{event.scope, true});
    uint32_t child = lastChild[event.scope];
    while (child != UINT32_MAX) {
      traversal.add(ScopeTraversalEvent{child, false});
      child = previousSibling[child];
    }
  }
  if (nextScopeEntry != arena.scopes.size()) {
    return ClosureFreeVariableOracleResult::MalformedScopeGraph;
  }

  for (size_t index = 0; index < inventory.size(); ++index) {
    const auto node = inventory[index].node;
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    definitionScopeIndices[index] = scopeByNode[node.value];
    owningCallableScopeIndices[index] = nearestCallableScopeByScope[definitionScopeIndices[index]];
    if (owningCallableScopeIndices[index] != UINT32_MAX &&
        callableInventoryByScope[owningCallableScopeIndices[index]] == kMissing) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
  }

  zc::TreeMap<ClosureFreeOracleTripleOrderKey, OracleCaptureTriple> triples;
  struct OracleReference final {
    ast::NodeId node;
    identity::DefId target;
  };
  zc::Vector<OracleReference> references;
  for (const auto& resolution : candidate.nodeBindings) {
    if (!tree.contains(resolution.node) || resolution.node.value >= scopeByNode.size() ||
        scopeByNode[resolution.node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    if (!resolution.value.is<BoundNameResolution>()) { continue; }
    const auto& bound = resolution.value.get<BoundNameResolution>();
    if (bound.nameSpace != Namespace::Value || bound.origin != BindingOrigin::LocalDeclaration) {
      continue;
    }
    const auto& bindingIdentity = bound.bindingIdentity.value();
    const auto& canonicalTarget = bound.canonicalTarget.value();
    if (!bindingIdentity.is<DefinitionBindingTarget>() ||
        !canonicalTarget.is<DefinitionBindingTarget>() ||
        bindingIdentity.get<DefinitionBindingTarget>().definition !=
            canonicalTarget.get<DefinitionBindingTarget>().definition) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    const auto target = bindingIdentity.get<DefinitionBindingTarget>().definition;
    references.add(OracleReference{resolution.node, target});
  }
  for (const auto& binding : candidate.thisBindings) {
    if (!tree.contains(binding.expression) ||
        tree.node(binding.expression).kind != ast::SyntaxKind::ThisExpr) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    references.add(OracleReference{binding.expression, binding.binding.receiverParameter});
  }
  for (const auto& reference : references) {
    const auto target = reference.target;
    auto targetInventoryIndex = inventoryIndex(target);
    if (targetInventoryIndex == zc::none) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    const size_t targetIndex = ZC_ASSERT_NONNULL(targetInventoryIndex);
    const auto& targetEntry = inventory[targetIndex];
    if (!capturable(targetEntry.kind)) { continue; }
    if (targetIndex >= owningCallableScopeIndices.size()) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    const uint32_t targetCallableScope = owningCallableScopeIndices[targetIndex];
    if (targetCallableScope == UINT32_MAX) {
      const uint32_t targetDefinitionScope = definitionScopeIndices[targetIndex];
      const uint32_t referenceScope = scopeByNode[reference.node.value];
      if (targetDefinitionScope >= scopeEnter.size() || referenceScope >= scopeEnter.size()) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (nearestCallableScopeByScope[referenceScope] != UINT32_MAX ||
          scopeEnter[referenceScope] < scopeEnter[targetDefinitionScope] ||
          scopeEnter[referenceScope] >= scopeExit[targetDefinitionScope]) {
        return ClosureFreeVariableOracleResult::InvalidBindingFact;
      }
      continue;
    }
    if (targetCallableScope >= callableInventoryByScope.size() ||
        callableInventoryByScope[targetCallableScope] == kMissing) {
      return ClosureFreeVariableOracleResult::MalformedScopeGraph;
    }
    const size_t targetCallableIndex = callableInventoryByScope[targetCallableScope];

    auto source = input.parsedModule().spanFor(tree.node(reference.node).range);
    if (source == zc::none || reference.node.value >= schemaOrdinals.size() ||
        schemaOrdinals[reference.node.value] == UINT32_MAX) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
    const auto start = ZC_ASSERT_NONNULL(source).byteStart();
    const auto end = ZC_ASSERT_NONNULL(source).byteEnd();

    zc::Vector<size_t> crossedClosures;
    uint32_t scopeIndex = nearestCallableScopeByScope[scopeByNode[reference.node.value]];
    bool reachedTarget = false;
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex == UINT32_MAX) { break; }
      if (scopeIndex >= arena.scopes.size() ||
          (arena.scopes[scopeIndex].kind != ScopeKind::Function &&
           arena.scopes[scopeIndex].kind != ScopeKind::Closure)) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scopeIndex >= callableInventoryByScope.size() ||
          callableInventoryByScope[scopeIndex] == kMissing) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const size_t callableIndex = callableInventoryByScope[scopeIndex];
      if (callableIndex == targetCallableIndex) {
        reachedTarget = true;
        break;
      }
      if (scope.kind == ScopeKind::Function) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (callableIndex >= closureSyntaxDomains.size() ||
          inventory[callableIndex].kind != identity::DefinitionKind::Closure ||
          closureSyntaxDomains[callableIndex] == ClosureFreeOracleClosureSyntax::NotClosure) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      if (closureSyntaxDomains[callableIndex] == ClosureFreeOracleClosureSyntax::Inferred) {
        crossedClosures.add(callableIndex);
      }
      scopeIndex = parentCallableScopeByScope[scopeIndex];
    }
    if (!reachedTarget) { return ClosureFreeVariableOracleResult::MalformedScopeGraph; }
    for (const auto closureIndex : crossedClosures) {
      if (closureIndex >= canonicalRankByInventory.size() ||
          targetIndex >= canonicalRankByInventory.size() ||
          canonicalRankByInventory[closureIndex] == kMissing ||
          canonicalRankByInventory[targetIndex] == kMissing) {
        return ClosureFreeVariableOracleResult::MalformedScopeGraph;
      }
      const ClosureFreeOracleTripleOrderKey key{canonicalRankByInventory[closureIndex],
                                                canonicalRankByInventory[targetIndex], start, end,
                                                schemaOrdinals[reference.node.value]};
      auto existing = triples.find(key);
      if (existing == zc::none) {
        triples.insert(
            key, OracleCaptureTriple{inventory[closureIndex].definition, target, reference.node});
      } else {
        ZC_IF_SOME(triple, existing) {
          if (triple.closure != inventory[closureIndex].definition || triple.target != target ||
              triple.referenceSite != reference.node) {
            return ClosureFreeVariableOracleResult::InvalidBindingFact;
          }
        }
      }
    }
  }

  zc::Vector<OracleCaptureTriple> canonicalTriples;
  canonicalTriples.reserve(triples.size());
  for (const auto& ordered : triples) { canonicalTriples.add(ordered.value); }

  size_t tripleIndex = 0;
  for (const auto& closure : candidate.closureFreeVariables) {
    size_t variableIndex = 0;
    while (tripleIndex < canonicalTriples.size() &&
           canonicalTriples[tripleIndex].closure == closure.closure) {
      const auto target = canonicalTriples[tripleIndex].target;
      if (variableIndex >= closure.variables.size()) {
        return ClosureFreeVariableOracleResult::MissingRequiredResolution;
      }
      const auto& variable = closure.variables[variableIndex];
      if (variable.target != target) { return ClosureFreeVariableOracleResult::InvalidBindingFact; }
      size_t siteCount = 0;
      while (tripleIndex + siteCount < canonicalTriples.size() &&
             canonicalTriples[tripleIndex + siteCount].closure == closure.closure &&
             canonicalTriples[tripleIndex + siteCount].target == target) {
        ++siteCount;
      }
      if (variable.referenceSites.size() < siteCount) {
        return ClosureFreeVariableOracleResult::MissingRequiredResolution;
      }
      if (variable.referenceSites.size() > siteCount) {
        return ClosureFreeVariableOracleResult::InvalidBindingFact;
      }
      for (size_t siteIndex = 0; siteIndex < siteCount; ++siteIndex) {
        if (variable.referenceSites[siteIndex] !=
            canonicalTriples[tripleIndex + siteIndex].referenceSite) {
          return ClosureFreeVariableOracleResult::InvalidBindingFact;
        }
      }
      tripleIndex += siteCount;
      ++variableIndex;
    }
    if (variableIndex < closure.variables.size()) {
      return ClosureFreeVariableOracleResult::InvalidBindingFact;
    }
  }
  return tripleIndex == canonicalTriples.size()
             ? ClosureFreeVariableOracleResult::Valid
             : ClosureFreeVariableOracleResult::MissingRequiredResolution;
}

BinderInvariantKind closureFreeVariableOracleInvariant(ClosureFreeVariableOracleResult result) {
  switch (result) {
    case ClosureFreeVariableOracleResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case ClosureFreeVariableOracleResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case ClosureFreeVariableOracleResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case ClosureFreeVariableOracleResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
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
    const uint32_t schemaOrdinal =
        static_cast<uint32_t>((candidate.sourceFailures[index].emitterOrdinal >> 16) & UINT32_MAX);
    const auto sourceKind = syntaxKindAtSchemaOrdinal(tree, schemaOrdinal);
    const bool labelDuplicate = sourceKind != zc::none &&
                                ZC_ASSERT_NONNULL(sourceKind) == ast::SyntaxKind::LabeledStatement;
    if (emitterSite == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) &&
        candidate.sourceFailures[index].diagnostic == BinderDiagnosticCode::DuplicateIdentifier &&
        labelDuplicate && !consumedFailures[index]) {
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
    const uint32_t schemaOrdinal =
        static_cast<uint32_t>((candidate.sourceFailures[index].emitterOrdinal >> 16) & UINT32_MAX);
    const auto sourceKind = syntaxKindAtSchemaOrdinal(tree, schemaOrdinal);
    const bool controlNode = sourceKind != zc::none &&
                             (ZC_ASSERT_NONNULL(sourceKind) == ast::SyntaxKind::BreakStmt ||
                              ZC_ASSERT_NONNULL(sourceKind) == ast::SyntaxKind::ContinueStatement);
    const bool explicitLookup =
        diagnostic == BinderDiagnosticCode::UndefinedIdentifier &&
        emitterSite == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) && controlNode;
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

bool encodeDeferredMemberFact(identity::CanonicalEncoder& encoder,
                              const VerifiedBindingInput& input, const DeferredMemberFact& fact) {
  const auto& tree = input.tree();
  if (!tree.contains(fact.node) || !tree.contains(fact.base)) { return false; }
  encoder.encodeUint32(fact.node.value);
  encoder.encodeUint32(fact.base.value);
  fact.member.encode(encoder);
  encoder.encodeSequenceSize(fact.expectedNamespaces.size());
  for (const auto nameSpace : fact.expectedNamespaces) {
    encoder.encodeUint8(static_cast<uint8_t>(nameSpace));
  }
  encoder.encodeSequenceSize(fact.genericArguments.size());
  for (const auto argument : fact.genericArguments) {
    if (!tree.contains(argument)) { return false; }
    encoder.encodeUint32(argument.value);
  }
  fact.source.encode(encoder);
  return true;
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
    } else if (value.is<DeferredMemberFact>()) {
      encoder.encodeUint8(0x03);
      if (!encodeDeferredMemberFact(encoder, input, value.get<DeferredMemberFact>())) {
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
  for (const auto& fact : candidate.deferredMembers) {
    if (!encodeDeferredMemberFact(encoder, input, fact)) { return zc::none; }
  }
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
  for (const auto& closure : candidate.closureFreeVariables) {
    if (!encodeDefinition(encoder, input, closure.closure)) { return zc::none; }
    encoder.encodeSequenceSize(closure.variables.size());
    for (const auto& variable : closure.variables) {
      if (!encodeDefinition(encoder, input, variable.target)) { return zc::none; }
      encoder.encodeSequenceSize(variable.referenceSites.size());
      for (const auto site : variable.referenceSites) {
        if (!input.tree().contains(site)) { return zc::none; }
        encoder.encodeUint32(site.value);
      }
    }
  }
  encoder.encodeSequenceSize(candidate.explicitClosureCaptures.size());
  for (const auto& closure : candidate.explicitClosureCaptures) {
    if (!encodeDefinition(encoder, input, closure.closure) ||
        !input.tree().contains(closure.captureList)) {
      return zc::none;
    }
    encoder.encodeUint32(closure.captureList.value);
    closure.source.encode(encoder);
    encoder.encodeSequenceSize(closure.captures.size());
    for (const auto& capture : closure.captures) {
      if (!input.tree().contains(capture.item) ||
          !encodeDefinition(encoder, input, capture.target)) {
        return zc::none;
      }
      encoder.encodeUint32(capture.item.value);
      capture.source.encode(encoder);
    }
  }
  if (!encodeBindingExtensionSequences(encoder, input, candidate.selfTypes.asPtr(),
                                       candidate.thisBindings.asPtr())) {
    return zc::none;
  }
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

bool encodeBindingExtensionSequences(identity::CanonicalEncoder& encoder,
                                     const VerifiedBindingInput& input,
                                     zc::ArrayPtr<const BoundSelfType> selfTypes,
                                     zc::ArrayPtr<const BoundThis> thisBindings) {
  encoder.encodeSequenceSize(selfTypes.size());
  for (const auto& fact : selfTypes) {
    if (!input.tree().contains(fact.syntax)) { return false; }
    encoder.encodeUint32(fact.syntax.value);
    if (fact.owner.is<NominalSelfOwner>()) {
      encoder.encodeUint8(0x01);
      if (!encodeDefinition(encoder, input, fact.owner.get<NominalSelfOwner>().definition)) {
        return false;
      }
    } else if (fact.owner.is<InterfaceSelfOwner>()) {
      encoder.encodeUint8(0x02);
      if (!encodeDefinition(encoder, input, fact.owner.get<InterfaceSelfOwner>().definition)) {
        return false;
      }
    } else if (fact.owner.is<ImplSelfOwner>()) {
      encoder.encodeUint8(0x03);
      if (!encodeImplementation(encoder, input, fact.owner.get<ImplSelfOwner>().implementation)) {
        return false;
      }
    } else {
      return false;
    }
    fact.source.encode(encoder);
  }
  encoder.encodeSequenceSize(thisBindings.size());
  for (const auto& fact : thisBindings) {
    if (!input.tree().contains(fact.expression)) { return false; }
    encoder.encodeUint32(fact.expression.value);
    encoder.encodeUint8(0x01);
    if (!encodeDefinition(encoder, input, fact.binding.receiverParameter)) { return false; }
    fact.source.encode(encoder);
  }
  return true;
}

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
zc::ArrayPtr<const BoundSelfType> VerifiedBindingMetadata::selfTypes() const {
  return impl->candidate.selfTypes.asPtr();
}
zc::ArrayPtr<const BoundThis> VerifiedBindingMetadata::thisBindings() const {
  return impl->candidate.thisBindings.asPtr();
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
zc::ArrayPtr<const ExplicitClosureCaptureFact> VerifiedBindingMetadata::explicitClosureCaptures()
    const {
  return impl->candidate.explicitClosureCaptures.asPtr();
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
                               static_cast<uint8_t>(bodyFailure.emitterSite),
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
      auto primaryLocation = input.parsedModule().sourceLocFor(duplicate.primary);
      auto previousLocation = input.parsedModule().sourceLocFor(duplicate.previous);
      if (primaryLocation == zc::none || previousLocation == zc::none) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, duplicate.emitterSite,
                       schemaOrdinal);
      }
      ZC_IF_SOME(engine, diagnostics) {
        bool emitted = false;
        ZC_IF_SOME(primary, primaryLocation) {
          ZC_IF_SOME(previous, previousLocation) {
            emitted = BindingDiagnosticAdapter::emitRedeclaration(
                engine, duplicate.diagnostic, primary, previous,
                VerifiedIdentifierArgument::from(duplicate.name));
          }
        }
        if (!emitted) {
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
    auto primaryLocation = input.parsedModule().sourceLocFor(bodyFailure.source);
    if (primaryLocation == zc::none) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, bodyFailure.emitterSite,
                     schemaOrdinal);
    }
    ZC_IF_SOME(engine, diagnostics) {
      bool emitted = false;
      ZC_IF_SOME(primary, primaryLocation) {
        emitted = BindingDiagnosticAdapter::emitLookupFailure(
            engine, bodyFailure.diagnostic, primary,
            VerifiedIdentifierArgument::from(bodyFailure.name), bodyFailure.expectedNamespace);
      }
      if (!emitted) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, bodyFailure.emitterSite,
                       schemaOrdinal);
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

  auto closureResult = ClosureFreeVariableBuilder::build(
      input, arena, skeleton.definitions.asPtr(), nodeBindings.asPtr(), body.thisBindings.asPtr());
  if (!closureResult.is<zc::Vector<ClosureFreeVariableFact>>()) {
    return zc::mv(closureResult.get<BinderInvariantFact>());
  }
  auto closureFreeVariables = zc::mv(closureResult.get<zc::Vector<ClosureFreeVariableFact>>());

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
        candidate.selfTypes = zc::mv(body.selfTypes);
        candidate.thisBindings = zc::mv(body.thisBindings);
        candidate.deferredMembers = zc::mv(body.deferredMembers);
        candidate.labels = zc::mv(labels.labels);
        candidate.controlTransfers = zc::mv(control.controlTransfers);
        candidate.shadowTargets = zc::mv(body.shadowTargets);
        candidate.closureFreeVariables = zc::mv(closureFreeVariables);
        candidate.explicitClosureCaptures = zc::mv(body.explicitClosureCaptures);
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
  const auto expectedDeferredMembers = verifyDeferredMemberFacts(input, expected);
  if (expectedDeferredMembers != DeferredMemberOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, deferredMemberOracleInvariant(expectedDeferredMembers)));
  }
  const auto candidateDeferredMembers = verifyDeferredMemberFacts(input, candidate);
  if (candidateDeferredMembers != DeferredMemberOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, deferredMemberOracleInvariant(candidateDeferredMembers)));
  }
  const auto expectedContextualSelf = verifyContextualSelfFacts(input, expected);
  if (expectedContextualSelf != ContextualSelfOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, contextualSelfOracleInvariant(expectedContextualSelf)));
  }
  const auto candidateContextualSelf = verifyContextualSelfFacts(input, candidate);
  if (candidateContextualSelf != ContextualSelfOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, contextualSelfOracleInvariant(candidateContextualSelf)));
  }
  const auto expectedExplicitCaptures = verifyExplicitCaptureFacts(input, expected);
  if (expectedExplicitCaptures != ExplicitCaptureOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, explicitCaptureOracleInvariant(expectedExplicitCaptures)));
  }
  const auto candidateExplicitCaptures = verifyExplicitCaptureFacts(input, candidate);
  if (candidateExplicitCaptures != ExplicitCaptureOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, explicitCaptureOracleInvariant(candidateExplicitCaptures)));
  }
  const auto expectedClosureFreeVariables = verifyClosureFreeVariableFacts(input, expected);
  if (expectedClosureFreeVariables != ClosureFreeVariableOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, closureFreeVariableOracleInvariant(expectedClosureFreeVariables)));
  }
  const auto candidateClosureFreeVariables = verifyClosureFreeVariableFacts(input, candidate);
  if (candidateClosureFreeVariables != ClosureFreeVariableOracleResult::Valid) {
    return rejectBinderInvariant(
        verifierFailure(input, closureFreeVariableOracleInvariant(candidateClosureFreeVariables)));
  }
  if (!hasCompleteLexicalBindingSites(input, expected.nodeBindings.asPtr(),
                                      expected.selfTypes.asPtr(), expected.thisBindings.asPtr())) {
    return rejectBinderInvariant(
        bodyBuilderFailure(input, BinderInvariantKind::MissingRequiredResolution));
  }
  if (candidate.scopes.size() < expected.scopes.size() ||
      candidate.definitions.size() < expected.definitions.size() ||
      candidate.impls.size() < expected.impls.size() ||
      candidate.nodeScopes.size() < expected.nodeScopes.size() ||
      candidate.sourceFailures.size() < expected.sourceFailures.size() ||
      candidate.nodeBindings.size() < expected.nodeBindings.size() ||
      candidate.selfTypes.size() < expected.selfTypes.size() ||
      candidate.thisBindings.size() < expected.thisBindings.size() ||
      candidate.deferredMembers.size() < expected.deferredMembers.size() ||
      candidate.labels.size() < expected.labels.size() ||
      candidate.controlTransfers.size() < expected.controlTransfers.size() ||
      candidate.shadowTargets.size() < expected.shadowTargets.size() ||
      candidate.closureFreeVariables.size() < expected.closureFreeVariables.size() ||
      candidate.explicitClosureCaptures.size() < expected.explicitClosureCaptures.size()) {
    return rejectBinderInvariant(
        verifierFailure(input, BinderInvariantKind::MissingRequiredResolution));
  }
  if (candidate.scopes.size() > expected.scopes.size() ||
      candidate.definitions.size() > expected.definitions.size() ||
      candidate.impls.size() > expected.impls.size() ||
      candidate.nodeScopes.size() > expected.nodeScopes.size() ||
      candidate.sourceFailures.size() > expected.sourceFailures.size() ||
      candidate.nodeBindings.size() > expected.nodeBindings.size() ||
      candidate.selfTypes.size() > expected.selfTypes.size() ||
      candidate.thisBindings.size() > expected.thisBindings.size() ||
      candidate.deferredMembers.size() > expected.deferredMembers.size() ||
      candidate.labels.size() > expected.labels.size() ||
      candidate.controlTransfers.size() > expected.controlTransfers.size() ||
      candidate.shadowTargets.size() > expected.shadowTargets.size() ||
      candidate.closureFreeVariables.size() > expected.closureFreeVariables.size() ||
      candidate.explicitClosureCaptures.size() > expected.explicitClosureCaptures.size()) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  if (!candidate.moduleAliases.empty() || !candidate.imports.empty() ||
      !candidate.localExports.empty()) {
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
