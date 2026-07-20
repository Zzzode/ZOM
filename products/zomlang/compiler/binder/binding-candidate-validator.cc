// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-candidate-validator.h"

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/import-binding.h"
#include "zomlang/compiler/binder/internal/binding-candidate-codec.h"
#include "zomlang/compiler/binder/internal/binding-definition-fact-validator.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::binder {
namespace {

bool targetHasForeignContext(const VerifiedBindingInput& input, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    return !value.get<DefinitionBindingTarget>().definition.belongsTo(input.semanticContext());
  }
  if (value.is<GenericParameterBindingTarget>()) {
    return !value.get<GenericParameterBindingTarget>().parameter.belongsTo(input.semanticContext());
  }
  if (value.is<CallableParameterBindingTarget>()) {
    return !value.get<CallableParameterBindingTarget>().parameter.belongsTo(
        input.semanticContext());
  }
  if (value.is<OwnerLocalBindingTarget>()) {
    return !value.get<OwnerLocalBindingTarget>().binding.belongsTo(input.semanticContext()) ||
           !value.get<OwnerLocalBindingTarget>().binding.belongsTo(input.module());
  }
  if (value.is<SemanticImportBindingTarget>()) { return false; }
  return !value.get<ModuleBindingTarget>().module.belongsTo(input.semanticContext());
}

bool labelIdHasForeignContext(const VerifiedBindingInput& input, const LabelId& label) {
  if (!label.belongsTo(input.semanticContext())) { return true; }
  const auto& owner = label.owner().value();
  if (owner.is<ModuleLabelOwner>()) {
    return owner.get<ModuleLabelOwner>().module != input.module();
  }
  if (owner.is<CallableLabelOwner>()) {
    return input.definitions().definitionKey(owner.get<CallableLabelOwner>().callable) == zc::none;
  }
  const auto& anonymous = owner.get<AnonymousLabelOwner>();
  return anonymous.module != input.module();
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
        targetHasForeignContext(input, step.bindingIdentity) ||
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
  for (const auto& fact : candidate.genericParameters) {
    if (!fact.identity.belongsTo(input.semanticContext()) ||
        !fact.declaringScope.belongsTo(input.semanticContext()) ||
        fact.declaringScope.module() != input.module()) {
      return true;
    }
  }
  for (const auto& fact : candidate.callableParameters) {
    if (!fact.identity.belongsTo(input.semanticContext()) ||
        !fact.declaringScope.belongsTo(input.semanticContext()) ||
        fact.declaringScope.module() != input.module()) {
      return true;
    }
  }
  for (const auto& fact : candidate.ownerLocalBindings) {
    if (!fact.identity.belongsTo(input.semanticContext()) ||
        !fact.identity.belongsTo(input.module()) ||
        !fact.declaringScope.belongsTo(input.semanticContext()) ||
        fact.declaringScope.module() != input.module()) {
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
         !owner.get<ImplScopeOwner>().occurrence.belongsTo(input.semanticContext()))) {
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
         !owner.get<ImplSelfOwner>().occurrence.belongsTo(input.semanticContext()))) {
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
    } else if (owner.is<CallableLabelOwner>()) {
      const auto callable = owner.get<CallableLabelOwner>().callable;
      if (input.definitions().definitionKey(callable) == zc::none) { return true; }
    } else {
      const auto& anonymous = owner.get<AnonymousLabelOwner>();
      if (anonymous.module != input.module()) { return true; }
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
    if (targetHasForeignContext(input, shadow.binding) ||
        targetHasForeignContext(input, shadow.target)) {
      return true;
    }
  }
  for (const auto& closure : candidate.closureFreeVariables) {
    for (const auto& variable : closure.variables) {
      if (targetHasForeignContext(input, variable.target)) { return true; }
    }
  }
  for (const auto& closure : candidate.explicitClosureCaptures) {
    for (const auto& capture : closure.captures) {
      if (targetHasForeignContext(input, capture.target)) { return true; }
    }
  }
  for (const auto& entry : candidate.currentSurface.visibleEntries) {
    if (entryHasForeignContext(input, entry)) { return true; }
  }
  for (const auto& entry : candidate.currentSurface.exports) {
    if (entryHasForeignContext(input, entry)) { return true; }
  }
  for (const auto& fact : candidate.impls) {
    if (!fact.occurrence.belongsTo(input.semanticContext()) ||
        !fact.authority.belongsTo(input.semanticContext()) ||
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
    if (!fact.sourceModule.belongsTo(input.semanticContext()) ||
        targetHasForeignContext(input, fact.canonicalTarget)) {
      return true;
    }
  }
  for (const auto& fact : candidate.localExports) {
    if (targetHasForeignContext(input, fact.sourceBinding) ||
        targetHasForeignContext(input, fact.canonicalTarget)) {
      return true;
    }
  }
  return false;
}

bool hasInvalidSourceRange(const VerifiedBindingInput& input,
                           const BindingMetadataCandidate& candidate) {
  const auto spanIsInvalid = [&](const identity::SourceSpan& span) {
    return !span.belongsTo(input.parsedModule().source()) || span.byteStart() > span.byteEnd() ||
           span.byteEnd() > input.parsedModule().byteLength();
  };
  const auto retainedSpanIsInvalid = [&](const identity::SourceSpan& span) {
    return span.byteStart() > span.byteEnd();
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
  for (const auto& fact : candidate.genericParameters) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.callableParameters) {
    if (spanIsInvalid(fact.source)) { return true; }
  }
  for (const auto& fact : candidate.ownerLocalBindings) {
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
  for (const auto& fact : candidate.moduleAliases) {
    if (spanIsInvalid(fact.declarationSpan) || spanIsInvalid(fact.targetSpan)) { return true; }
  }
  for (const auto& fact : candidate.imports) {
    if (spanIsInvalid(fact.declarationSpan)) { return true; }
    ZC_IF_SOME(alias, fact.aliasSpan) {
      if (spanIsInvalid(alias)) { return true; }
    }
    for (const auto& step : fact.reexportChain) {
      if (retainedSpanIsInvalid(step.exportSpan)) { return true; }
    }
  }
  for (const auto& fact : candidate.localExports) {
    if (spanIsInvalid(fact.bindingSpan) || retainedSpanIsInvalid(fact.canonicalDeclarationSpan) ||
        spanIsInvalid(fact.exportSpan)) {
      return true;
    }
    ZC_IF_SOME(alias, fact.aliasSpan) {
      if (spanIsInvalid(alias)) { return true; }
    }
    for (const auto& step : fact.reexportChain) {
      if (retainedSpanIsInvalid(step.exportSpan)) { return true; }
    }
  }
  const auto entryIsInvalid = [&](const ExportSurfaceEntry& entry) {
    if (spanIsInvalid(entry.bindingSpan) || retainedSpanIsInvalid(entry.canonicalDeclarationSpan)) {
      return true;
    }
    ZC_IF_SOME(alias, entry.aliasSpan) {
      if (spanIsInvalid(alias)) { return true; }
    }
    ZC_IF_SOME(exportSpan, entry.exportSpan) {
      if (spanIsInvalid(exportSpan)) { return true; }
    }
    for (const auto& step : entry.reexportChain) {
      if (retainedSpanIsInvalid(step.exportSpan)) { return true; }
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
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
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
zc::Maybe<ScopeKind> expectedScopeKindForSyntax(ast::SyntaxKind kind) {
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

enum class CandidateStructureResult : uint8_t {
  Valid,
  MissingRequiredResolution,
  MalformedScopeGraph,
  InvalidBindingFact,
  InvalidEmitterOrdinal
};

bool sameDefinitionSite(const DefinitionSite& left, const DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DeclarationDefinitionSite>() != rightValue.is<DeclarationDefinitionSite>()) {
    return false;
  }
  if (leftValue.is<DeclarationDefinitionSite>()) {
    return leftValue.get<DeclarationDefinitionSite>().node ==
           rightValue.get<DeclarationDefinitionSite>().node;
  }
  const auto& leftPattern = leftValue.get<PatternBindingSite>();
  const auto& rightPattern = rightValue.get<PatternBindingSite>();
  if (leftPattern.introducer != rightPattern.introducer ||
      leftPattern.patternPath.size() != rightPattern.patternPath.size()) {
    return false;
  }
  for (size_t index = 0; index < leftPattern.patternPath.size(); ++index) {
    if (leftPattern.patternPath[index] != rightPattern.patternPath[index]) { return false; }
  }
  return true;
}

bool sameDefinitionName(const identity::DeclaredDefinitionName& left,
                        const identity::DeclaredDefinitionName& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  return leftEncoder.finish() == rightEncoder.finish();
}

bool sameScopeOwner(const ScopeOwner& left, const ScopeOwner& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<ModuleScopeOwner>()) {
    return rightValue.is<ModuleScopeOwner>() &&
           leftValue.get<ModuleScopeOwner>().module == rightValue.get<ModuleScopeOwner>().module;
  }
  if (leftValue.is<DefinitionScopeOwner>()) {
    return rightValue.is<DefinitionScopeOwner>() &&
           leftValue.get<DefinitionScopeOwner>().definition ==
               rightValue.get<DefinitionScopeOwner>().definition;
  }
  if (leftValue.is<ImplScopeOwner>()) {
    return rightValue.is<ImplScopeOwner>() && leftValue.get<ImplScopeOwner>().occurrence ==
                                                  rightValue.get<ImplScopeOwner>().occurrence;
  }
  return rightValue.is<AnonymousScopeOwner>() &&
         leftValue.get<AnonymousScopeOwner>().anonymous ==
             rightValue.get<AnonymousScopeOwner>().anonymous;
}

bool sameTarget(const BindingTarget& left, const BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DefinitionBindingTarget>()) {
    return rightValue.is<DefinitionBindingTarget>() &&
           leftValue.get<DefinitionBindingTarget>().definition ==
               rightValue.get<DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<GenericParameterBindingTarget>()) {
    return rightValue.is<GenericParameterBindingTarget>() &&
           leftValue.get<GenericParameterBindingTarget>().parameter ==
               rightValue.get<GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<CallableParameterBindingTarget>()) {
    return rightValue.is<CallableParameterBindingTarget>() &&
           leftValue.get<CallableParameterBindingTarget>().parameter ==
               rightValue.get<CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<OwnerLocalBindingTarget>()) {
    return rightValue.is<OwnerLocalBindingTarget>() &&
           leftValue.get<OwnerLocalBindingTarget>().binding ==
               rightValue.get<OwnerLocalBindingTarget>().binding;
  }
  if (leftValue.is<SemanticImportBindingTarget>()) {
    return rightValue.is<SemanticImportBindingTarget>() &&
           leftValue.get<SemanticImportBindingTarget>().binding ==
               rightValue.get<SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<ModuleBindingTarget>() && leftValue.get<ModuleBindingTarget>().module ==
                                                     rightValue.get<ModuleBindingTarget>().module;
}

bool targetExists(const VerifiedBindingInput& input, const BindingTarget& target) {
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    if (input.definitions().definitionKey(value.get<DefinitionBindingTarget>().definition) !=
        zc::none) {
      return true;
    }
  } else if (value.is<GenericParameterBindingTarget>()) {
    return input.definitions().genericParameterKey(
               value.get<GenericParameterBindingTarget>().parameter) != zc::none;
  } else if (value.is<CallableParameterBindingTarget>()) {
    return input.definitions().callableParameterKey(
               value.get<CallableParameterBindingTarget>().parameter) != zc::none;
  } else if (value.is<OwnerLocalBindingTarget>()) {
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.binding == value.get<OwnerLocalBindingTarget>().binding) { return true; }
    }
  } else if (value.is<SemanticImportBindingTarget>()) {
    for (const auto& import : input.resolvedImports()) {
      if (value.get<SemanticImportBindingTarget>().binding == import.binding()) { return true; }
    }
  } else if (value.get<ModuleBindingTarget>().module == input.module()) {
    return true;
  }
  for (const auto& surface : input.dependencySurfaces()) {
    for (const auto& entry : surface.visibleEntries()) {
      if (sameTarget(target, entry.bindingIdentity) || sameTarget(target, entry.canonicalTarget)) {
        return true;
      }
    }
  }
  for (const auto& alias : input.resolvedModuleAliases()) {
    if (value.is<ModuleBindingTarget>() &&
        value.get<ModuleBindingTarget>().module == alias.targetModule()) {
      return true;
    }
  }
  for (const auto& import : input.resolvedImports()) {
    if (sameTarget(target, import.canonicalTarget()) ||
        (value.is<ModuleBindingTarget>() &&
         value.get<ModuleBindingTarget>().module == import.sourceModule())) {
      return true;
    }
  }
  return false;
}

zc::Maybe<zc::Array<uint8_t>> encodeTargetKey(const VerifiedBindingInput& input,
                                              const BindingTarget& target) {
  identity::CanonicalEncoder encoder;
  const auto& value = target.value();
  if (value.is<DefinitionBindingTarget>()) {
    ZC_IF_SOME(key,
               input.definitions().definitionKey(value.get<DefinitionBindingTarget>().definition)) {
      encoder.encodeUint8(0x01);
      key.encode(encoder);
      return encoder.finish();
    }
    return zc::none;
  }
  if (value.is<GenericParameterBindingTarget>()) {
    ZC_IF_SOME(key, input.definitions().genericParameterKey(
                        value.get<GenericParameterBindingTarget>().parameter)) {
      encoder.encodeUint8(0x02);
      key.encode(encoder);
      return encoder.finish();
    }
    return zc::none;
  }
  if (value.is<CallableParameterBindingTarget>()) {
    ZC_IF_SOME(key, input.definitions().callableParameterKey(
                        value.get<CallableParameterBindingTarget>().parameter)) {
      encoder.encodeUint8(0x03);
      key.encode(encoder);
      return encoder.finish();
    }
    return zc::none;
  }
  if (value.is<OwnerLocalBindingTarget>()) {
    for (const auto& entry : input.definitions().ownerLocalBindings()) {
      if (entry.binding != value.get<OwnerLocalBindingTarget>().binding) { continue; }
      encoder.encodeUint8(0x04);
      entry.key.encode(encoder);
      return encoder.finish();
    }
    return zc::none;
  }
  if (value.is<SemanticImportBindingTarget>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeByteString(value.get<SemanticImportBindingTarget>().binding.encode().asPtr());
    return encoder.finish();
  }
  ZC_IF_SOME(key, input.moduleKey(value.get<ModuleBindingTarget>().module)) {
    encoder.encodeUint8(0x06);
    key.encode(encoder);
    return encoder.finish();
  }
  return zc::none;
}

bool sameSurfaceEntry(const VerifiedBindingInput& input, const ExportSurfaceEntry& left,
                      const ExportSurfaceEntry& right) {
  auto leftBytes = encodeBindingSurfaceEntry(input, left);
  auto rightBytes = encodeBindingSurfaceEntry(input, right);
  if (leftBytes == zc::none || rightBytes == zc::none) { return false; }
  return ZC_ASSERT_NONNULL(leftBytes) == ZC_ASSERT_NONNULL(rightBytes);
}

bool sameMaybeSpan(const zc::Maybe<identity::SourceSpan>& left,
                   zc::Maybe<const identity::SourceSpan&> right) {
  if ((left != zc::none) != (right != zc::none)) { return false; }
  if (left == zc::none) { return true; }
  return sameSpan(ZC_ASSERT_NONNULL(left), ZC_ASSERT_NONNULL(right));
}

bool sameMaybeSpan(const zc::Maybe<identity::SourceSpan>& left,
                   const zc::Maybe<identity::SourceSpan>& right) {
  if ((left != zc::none) != (right != zc::none)) { return false; }
  if (left == zc::none) { return true; }
  return sameSpan(ZC_ASSERT_NONNULL(left), ZC_ASSERT_NONNULL(right));
}

bool sameReexportStep(const ReexportProvenanceStep& left, const ReexportProvenanceStep& right) {
  return left.module == right.module && sameTarget(left.bindingIdentity, right.bindingIdentity) &&
         sameTarget(left.canonicalTarget, right.canonicalTarget) &&
         sameSpan(left.exportSpan, right.exportSpan);
}

bool sameReexportChain(zc::ArrayPtr<const ReexportProvenanceStep> left,
                       zc::ArrayPtr<const ReexportProvenanceStep> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameReexportStep(left[index], right[index])) { return false; }
  }
  return true;
}

bool validNamespace(Namespace value) {
  return value >= Namespace::Value && value <= Namespace::Attribute;
}

bool validActivation(DefinitionActivation value) {
  return value >= DefinitionActivation::ModuleSkeleton &&
         value <= DefinitionActivation::LoopPattern;
}

bool isPatternBindingTarget(const BindingTarget& target, OwnerLocalBindingId identity) {
  const auto& value = target.value();
  return value.is<OwnerLocalBindingTarget>() &&
         value.get<OwnerLocalBindingTarget>().binding == identity;
}

bool matchesPatternScopeBinding(const OwnerLocalBindingFact& fact, const ScopeRecord& scope,
                                const ScopeBindingEntry& entry) {
  return fact.kind == OwnerLocalBindingKind::PatternBinding && fact.declaringScope == scope.id &&
         entry.name.nameSpace() == fact.nameSpace && entry.name.name().text() == fact.name.text() &&
         entry.binding.nameSpace == fact.nameSpace &&
         entry.binding.origin == BindingOrigin::LocalDeclaration &&
         isPatternBindingTarget(entry.binding.bindingIdentity, fact.identity) &&
         isPatternBindingTarget(entry.binding.canonicalTarget, fact.identity) &&
         sameSpan(entry.binding.declarationSpan, fact.source) &&
         entry.binding.aliasSpan == zc::none;
}

zc::Maybe<const OwnerLocalBindingFact&> precedingPatternBinding(
    const BindingMetadataCandidate& candidate, const OwnerLocalBindingFact& fact) {
  zc::Maybe<const OwnerLocalBindingFact&> result;
  for (const auto& other : candidate.ownerLocalBindings) {
    if (other.kind != OwnerLocalBindingKind::PatternBinding ||
        other.declaringScope != fact.declaringScope || other.nameSpace != fact.nameSpace ||
        other.name.text() != fact.name.text() ||
        other.source.byteStart() >= fact.source.byteStart()) {
      continue;
    }
    if (result == zc::none ||
        other.source.byteStart() < ZC_ASSERT_NONNULL(result).source.byteStart()) {
      result = other;
    }
  }
  return result;
}

bool hasPatternRedeclarationFailure(const BindingMetadataCandidate& candidate,
                                    const OwnerLocalBindingFact& fact,
                                    const OwnerLocalBindingFact& previous) {
  for (const auto& failure : candidate.sourceFailures) {
    const auto site = static_cast<uint8_t>(failure.emitterOrdinal >> 56);
    if (failure.diagnostic != BinderDiagnosticCode::RedeclareVariable ||
        site != static_cast<uint8_t>(BinderEmitterSite::BodyBinding) ||
        !sameSpan(failure.primary, fact.source) || failure.notes.size() != 1 ||
        failure.notes[0].diagnostic != BinderDiagnosticCode::PreviousDeclarationHere ||
        !sameSpan(failure.notes[0].source, previous.source)) {
      continue;
    }
    return true;
  }
  return false;
}

CandidateStructureResult verifyPatternScopeProjections(const BindingMetadataCandidate& candidate) {
  for (const auto& scope : candidate.scopes) {
    for (const auto& entry : scope.bindings) {
      const auto& target = entry.binding.bindingIdentity.value();
      if (!target.is<OwnerLocalBindingTarget>()) { continue; }
      const auto identity = target.get<OwnerLocalBindingTarget>().binding;
      zc::Maybe<const OwnerLocalBindingFact&> fact;
      for (const auto& candidateFact : candidate.ownerLocalBindings) {
        if (candidateFact.identity == identity) {
          fact = candidateFact;
          break;
        }
      }
      if (fact != zc::none &&
          ZC_ASSERT_NONNULL(fact).kind == OwnerLocalBindingKind::PatternBinding &&
          !matchesPatternScopeBinding(ZC_ASSERT_NONNULL(fact), scope, entry)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
    }
  }

  for (const auto& fact : candidate.ownerLocalBindings) {
    if (fact.kind != OwnerLocalBindingKind::PatternBinding) { continue; }
    if (fact.declaringScope.index() >= candidate.scopes.size()) {
      return CandidateStructureResult::MissingRequiredResolution;
    }
    const auto& scope = candidate.scopes[fact.declaringScope.index()];
    const ScopeKind expectedKind = fact.activation == DefinitionActivation::LoopPattern
                                       ? ScopeKind::Loop
                                       : ScopeKind::MatchArm;
    if (scope.id != fact.declaringScope || scope.kind != expectedKind ||
        (fact.activation != DefinitionActivation::LoopPattern &&
         fact.activation != DefinitionActivation::MatchPattern)) {
      return CandidateStructureResult::InvalidBindingFact;
    }

    size_t targetCount = 0;
    size_t exactCount = 0;
    for (const auto& entry : scope.bindings) {
      if (!isPatternBindingTarget(entry.binding.bindingIdentity, fact.identity)) { continue; }
      ++targetCount;
      if (matchesPatternScopeBinding(fact, scope, entry)) { ++exactCount; }
    }
    if (targetCount != exactCount || exactCount > 1) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    if (exactCount == 1) { continue; }

    auto previous = precedingPatternBinding(candidate, fact);
    if (previous == zc::none ||
        !hasPatternRedeclarationFailure(candidate, fact, ZC_ASSERT_NONNULL(previous))) {
      return CandidateStructureResult::MissingRequiredResolution;
    }
  }

  return CandidateStructureResult::Valid;
}

bool validMemberVisibility(MemberVisibility value) {
  return value >= MemberVisibility::Public && value <= MemberVisibility::Protected;
}

bool requiresMemberVisibility(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::MethodDecl || kind == ast::SyntaxKind::FieldDecl ||
         kind == ast::SyntaxKind::ConstructorDecl || kind == ast::SyntaxKind::DestructorDecl ||
         kind == ast::SyntaxKind::ClassConstDecl;
}

CandidateStructureResult verifyCandidateStructure(const VerifiedBindingInput& input,
                                                  const BindingMetadataCandidate& candidate) {
  // The schema-driven codec is the local record-envelope validator. Cross-record and AST
  // semantics remain handwritten below so validation does not share producer algorithms.
  if (encodeBindingCandidate(input, candidate) == zc::none) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  const auto& tree = input.tree();
  if (candidate.scopes.empty()) { return CandidateStructureResult::MissingRequiredResolution; }

  const auto frozenDefinitions = input.definitions().definitions();
  if (candidate.definitions.size() < frozenDefinitions.size()) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.definitions.size() > frozenDefinitions.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  zc::TreeMap<zc::String, size_t> frozenDefinitionByCanonicalKey;
  for (size_t index = 0; index < frozenDefinitions.size(); ++index) {
    const auto& frozen = frozenDefinitions[index];
    auto registeredKey = input.definitions().definitionKey(frozen.definition);
    if (registeredKey == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    const auto frozenBytes = frozen.key.encode();
    bool keyMatches = false;
    ZC_IF_SOME(key, registeredKey) {
      const auto registeredBytes = key.encode();
      keyMatches = compareCanonicalBytes(frozenBytes.asPtr(), registeredBytes.asPtr()) == 0;
    }
    auto canonicalKey = zc::str(frozenBytes.asChars());
    if (!keyMatches || frozenDefinitionByCanonicalKey.find(canonicalKey) != zc::none) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    frozenDefinitionByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }

  zc::TreeMap<zc::String, size_t> candidateDefinitionByCanonicalKey;
  zc::Array<uint8_t> previousDefinitionKey;
  bool hasPreviousDefinitionKey = false;
  for (size_t index = 0; index < candidate.definitions.size(); ++index) {
    const auto& fact = candidate.definitions[index];
    auto registeredKey = input.definitions().definitionKey(fact.identity);
    if (registeredKey == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    zc::Array<uint8_t> encoded;
    ZC_IF_SOME(key, registeredKey) { encoded = key.encode(); }
    if (hasPreviousDefinitionKey &&
        compareCanonicalBytes(previousDefinitionKey.asPtr(), encoded.asPtr()) >= 0) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    auto canonicalKey = zc::str(encoded.asChars());
    auto frozenIndex = frozenDefinitionByCanonicalKey.find(canonicalKey);
    if (frozenIndex == zc::none ||
        candidateDefinitionByCanonicalKey.find(canonicalKey) != zc::none) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    ZC_IF_SOME(value, frozenIndex) {
      const auto& frozen = frozenDefinitions[value];
      if (fact.declaringScope.index() >= candidate.scopes.size()) {
        return CandidateStructureResult::MissingRequiredResolution;
      }
      if (fact.identity != frozen.definition || fact.kind != frozen.record.kind() ||
          !sameDefinitionSite(fact.site, frozen.site) || frozen.bindingName == zc::none ||
          !sameDefinitionName(fact.name, ZC_ASSERT_NONNULL(frozen.bindingName)) ||
          !sameSpan(fact.source, frozen.source) || !validNamespace(fact.nameSpace) ||
          !validActivation(fact.activation)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      const bool needsVisibility = requiresMemberVisibility(tree.node(frozen.node).kind);
      if (needsVisibility != (fact.memberVisibility != zc::none)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      ZC_IF_SOME(visibility, fact.memberVisibility) {
        if (!validMemberVisibility(visibility)) {
          return CandidateStructureResult::InvalidBindingFact;
        }
      }
    }
    candidateDefinitionByCanonicalKey.insert(zc::mv(canonicalKey), index);
    previousDefinitionKey = zc::mv(encoded);
    hasPreviousDefinitionKey = true;
  }

  const auto definitionResult = verifyBindingDefinitionFacts(input, candidate);
  if (definitionResult == BindingDefinitionFactValidationResult::MissingRequiredResolution) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (definitionResult == BindingDefinitionFactValidationResult::InvalidBindingFact) {
    return CandidateStructureResult::InvalidBindingFact;
  }

  const auto resolvedModuleAliases = input.resolvedModuleAliases();
  if (candidate.moduleAliases.size() < resolvedModuleAliases.size()) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.moduleAliases.size() > resolvedModuleAliases.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  for (size_t index = 0; index < resolvedModuleAliases.size(); ++index) {
    const auto& expected = resolvedModuleAliases[index];
    const auto& actual = candidate.moduleAliases[index];
    if (actual.node != expected.syntax() || actual.alias != expected.alias() ||
        actual.canonicalTarget != expected.targetModule() ||
        actual.targetRevision.digest() != expected.targetRevision().digest() ||
        !sameSpan(actual.declarationSpan, expected.declarationSpan()) ||
        !sameSpan(actual.targetSpan, expected.targetSpan())) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }

  const auto resolvedImports = input.resolvedImports();
  if (candidate.imports.size() < resolvedImports.size()) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.imports.size() > resolvedImports.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  for (size_t index = 0; index < resolvedImports.size(); ++index) {
    const auto& expected = resolvedImports[index];
    const auto& actual = candidate.imports[index];
    if (actual.node != expected.syntax() || actual.binding != expected.binding() ||
        !sameTarget(actual.canonicalTarget, expected.canonicalTarget()) ||
        actual.sourceModule != expected.sourceModule() ||
        actual.sourceRevision.digest() != expected.sourceRevision().digest() ||
        actual.kind != expected.kind() ||
        !sameSpan(actual.declarationSpan, expected.declarationSpan()) ||
        !sameMaybeSpan(actual.aliasSpan, expected.aliasSpan())) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    const auto sourceChain = expected.sourceReexportChain();
    if (actual.kind == ImportBindingKind::Import) {
      if (!sameReexportChain(actual.reexportChain.asPtr(), sourceChain)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      continue;
    }
    if (actual.reexportChain.size() != sourceChain.size() + 1) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    for (size_t chainIndex = 0; chainIndex < sourceChain.size(); ++chainIndex) {
      if (!sameReexportStep(actual.reexportChain[chainIndex], sourceChain[chainIndex])) {
        return CandidateStructureResult::InvalidBindingFact;
      }
    }
    const auto exportSpan = expected.exportSpan();
    if (exportSpan == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    const auto& appended = actual.reexportChain[actual.reexportChain.size() - 1];
    const auto expectedBinding = BindingTarget::semanticImport(actual.binding.clone());
    if (appended.module != input.module() ||
        !sameTarget(appended.bindingIdentity, expectedBinding) ||
        !sameTarget(appended.canonicalTarget, actual.canonicalTarget) ||
        !sameSpan(appended.exportSpan, ZC_ASSERT_NONNULL(exportSpan))) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }

  zc::Vector<bool> scopeHasProducer;
  scopeHasProducer.resize(candidate.scopes.size());
  for (auto& value : scopeHasProducer) { value = false; }
  scopeHasProducer[0] = true;

  for (size_t index = 0; index < candidate.scopes.size(); ++index) {
    const auto& scope = candidate.scopes[index];
    if (scope.id.index() != index || scope.id.module() != input.module()) {
      return CandidateStructureResult::MalformedScopeGraph;
    }
    if (index == 0) {
      if (scope.parent != zc::none || scope.kind != ScopeKind::Module ||
          !scope.owner.value().is<ModuleScopeOwner>() ||
          scope.owner.value().get<ModuleScopeOwner>().module != input.module() ||
          !sameSpan(scope.source, input.parsedModule().rootSpan())) {
        return CandidateStructureResult::MalformedScopeGraph;
      }
    } else {
      if (scope.parent == zc::none) { return CandidateStructureResult::MalformedScopeGraph; }
      ZC_IF_SOME(parent, scope.parent) {
        if (parent.index() >= index || parent.index() >= candidate.scopes.size() ||
            candidate.scopes[parent.index()].id != parent ||
            candidate.scopes[parent.index()].source.byteStart() > scope.source.byteStart() ||
            scope.source.byteEnd() > candidate.scopes[parent.index()].source.byteEnd()) {
          return CandidateStructureResult::MalformedScopeGraph;
        }
      }
    }

    for (size_t bindingIndex = 0; bindingIndex < scope.bindings.size(); ++bindingIndex) {
      const auto& entry = scope.bindings[bindingIndex];
      if (bindingIndex != 0) {
        const auto& previous = scope.bindings[bindingIndex - 1];
        if (static_cast<uint8_t>(previous.name.nameSpace()) >
                static_cast<uint8_t>(entry.name.nameSpace()) ||
            (previous.name.nameSpace() == entry.name.nameSpace() &&
             !(previous.name.name() < entry.name.name()))) {
          return CandidateStructureResult::InvalidBindingFact;
        }
      }
      if (!validNamespace(entry.name.nameSpace()) ||
          entry.name.nameSpace() != entry.binding.nameSpace ||
          !targetExists(input, entry.binding.bindingIdentity) ||
          !targetExists(input, entry.binding.canonicalTarget)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      if (entry.binding.origin < BindingOrigin::LocalDeclaration ||
          entry.binding.origin > BindingOrigin::Prelude ||
          entry.binding.bindingIdentity.value().is<ModuleBindingTarget>()) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      const auto& target = entry.binding.bindingIdentity.value();
      if (target.is<DefinitionBindingTarget>()) {
        const auto definition = target.get<DefinitionBindingTarget>().definition;
        auto registeredKey = input.definitions().definitionKey(definition);
        if (registeredKey == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
        ZC_IF_SOME(key, registeredKey) {
          const auto encoded = key.encode();
          const auto canonicalKey = zc::str(encoded.asChars());
          auto factIndex = candidateDefinitionByCanonicalKey.find(canonicalKey);
          if (factIndex == zc::none) {
            if (entry.binding.origin != BindingOrigin::Prelude) {
              return CandidateStructureResult::InvalidBindingFact;
            }
            continue;
          }
          ZC_IF_SOME(value, factIndex) {
            if (value >= candidate.definitions.size() ||
                candidate.definitions[value].identity != definition ||
                candidate.definitions[value].declaringScope != scope.id) {
              return CandidateStructureResult::InvalidBindingFact;
            }
            const auto& definitionFact = candidate.definitions[value];
            if (entry.binding.origin == BindingOrigin::LocalDeclaration &&
                !sameTarget(entry.binding.bindingIdentity, entry.binding.canonicalTarget) &&
                (definitionFact.kind != identity::DefinitionKind::ModuleAlias ||
                 entry.name.nameSpace() != Namespace::Module ||
                 !entry.binding.canonicalTarget.value().is<ModuleBindingTarget>())) {
              return CandidateStructureResult::InvalidBindingFact;
            }
          }
        }
      }
    }
  }

  const auto patternProjectionResult = verifyPatternScopeProjections(candidate);
  if (patternProjectionResult != CandidateStructureResult::Valid) {
    return patternProjectionResult;
  }

  for (const auto& fact : candidate.definitions) {
    if (fact.declaringScope.index() >= candidate.scopes.size() ||
        candidate.scopes[fact.declaringScope.index()].id != fact.declaringScope) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    if (fact.kind == identity::DefinitionKind::ModuleAlias) {
      bool matched = false;
      for (const auto& alias : resolvedModuleAliases) {
        if (alias.alias() == fact.identity) {
          matched = fact.nameSpace == Namespace::Module &&
                    fact.activation == DefinitionActivation::ImportSurface &&
                    fact.declaringScope == candidate.scopes[0].id;
          break;
        }
      }
      if (!matched) { return CandidateStructureResult::InvalidBindingFact; }
    }
  }
  if (candidate.nodeScopes.size() < tree.nodeCount()) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.nodeScopes.size() > tree.nodeCount()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  for (size_t index = 0; index < candidate.nodeScopes.size(); ++index) {
    const auto& fact = candidate.nodeScopes[index];
    if (fact.scope.index() >= candidate.scopes.size()) {
      return CandidateStructureResult::MissingRequiredResolution;
    }
    if (fact.node.value != index + 1 || !tree.contains(fact.node) ||
        candidate.scopes[fact.scope.index()].id != fact.scope) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    ZC_IF_SOME(kind, expectedScopeKindForSyntax(tree.node(fact.node).kind)) {
      if (fact.scope.index() == 0 || scopeHasProducer[fact.scope.index()] ||
          candidate.scopes[fact.scope.index()].kind != kind) {
        return CandidateStructureResult::MalformedScopeGraph;
      }
      auto source = input.parsedModule().spanFor(tree.node(fact.node).range);
      if (source == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
      ZC_IF_SOME(value, source) {
        if (!sameSpan(candidate.scopes[fact.scope.index()].source, value)) {
          return CandidateStructureResult::MalformedScopeGraph;
        }
      }
      const auto& owner = candidate.scopes[fact.scope.index()].owner.value();
      if (kind == ScopeKind::Function || kind == ScopeKind::TypeBody) {
        auto definition = input.definitions().definitionAt(fact.node);
        if (definition == zc::none || !owner.is<DefinitionScopeOwner>()) {
          return CandidateStructureResult::MalformedScopeGraph;
        }
        ZC_IF_SOME(value, definition) {
          if (owner.get<DefinitionScopeOwner>().definition != value) {
            return CandidateStructureResult::MalformedScopeGraph;
          }
        }
      } else if (kind == ScopeKind::Closure) {
        auto anonymous = input.definitions().anonymousEntityAt(fact.node);
        if (anonymous == zc::none || !owner.is<AnonymousScopeOwner>()) {
          return CandidateStructureResult::MalformedScopeGraph;
        }
        ZC_IF_SOME(value, anonymous) {
          if (owner.get<AnonymousScopeOwner>().anonymous != value.key) {
            return CandidateStructureResult::MalformedScopeGraph;
          }
        }
      } else if (kind == ScopeKind::ImplBody) {
        auto implementation = input.definitions().implAt(fact.node);
        if (implementation == zc::none || !owner.is<ImplScopeOwner>()) {
          return CandidateStructureResult::MalformedScopeGraph;
        }
        ZC_IF_SOME(value, implementation) {
          if (owner.get<ImplScopeOwner>().occurrence != value) {
            return CandidateStructureResult::MalformedScopeGraph;
          }
        }
      } else {
        const auto& scope = candidate.scopes[fact.scope.index()];
        ZC_IF_SOME(parent, scope.parent) {
          if (!sameScopeOwner(scope.owner, candidate.scopes[parent.index()].owner)) {
            return CandidateStructureResult::MalformedScopeGraph;
          }
        }
      }
      scopeHasProducer[fact.scope.index()] = true;
    }
  }
  for (size_t index = 1; index < scopeHasProducer.size(); ++index) {
    if (!scopeHasProducer[index]) { return CandidateStructureResult::MalformedScopeGraph; }
  }
  const auto frozenImpls = input.definitions().impls();
  if (candidate.impls.size() < frozenImpls.size()) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.impls.size() > frozenImpls.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  constexpr size_t kMissing = static_cast<size_t>(-1);
  zc::Vector<size_t> implByScope;
  zc::Vector<size_t> memberCounts;
  implByScope.resize(candidate.scopes.size());
  memberCounts.resize(candidate.impls.size());
  for (auto& value : implByScope) { value = kMissing; }
  for (auto& value : memberCounts) { value = 0; }
  zc::TreeMap<zc::String, size_t> frozenImplByCanonicalKey;
  for (size_t index = 0; index < frozenImpls.size(); ++index) {
    const auto& frozen = frozenImpls[index];
    auto registeredKey = input.definitions().implKey(frozen.authority);
    if (registeredKey == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    const auto frozenBytes = frozen.key.encode();
    bool keyMatches = false;
    ZC_IF_SOME(key, registeredKey) { keyMatches = frozen.key.implementation() == key; }
    auto canonicalKey = zc::str(frozenBytes.asChars());
    if (!keyMatches || frozenImplByCanonicalKey.find(canonicalKey) != zc::none) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    frozenImplByCanonicalKey.insert(zc::mv(canonicalKey), index);
  }
  zc::Array<uint8_t> previousImplKey;
  bool hasPreviousImplKey = false;
  for (size_t index = 0; index < candidate.impls.size(); ++index) {
    const auto& fact = candidate.impls[index];
    auto registeredKey = input.definitions().implKey(fact.authority);
    if (registeredKey == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    zc::Array<uint8_t> encoded;
    bool foundOccurrence = false;
    for (const auto& frozen : frozenImpls) {
      if (frozen.occurrence == fact.occurrence) {
        encoded = frozen.key.encode();
        foundOccurrence = true;
        break;
      }
    }
    if (!foundOccurrence) { return CandidateStructureResult::InvalidBindingFact; }
    if (hasPreviousImplKey &&
        compareCanonicalBytes(previousImplKey.asPtr(), encoded.asPtr()) >= 0) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    const auto canonicalKey = zc::str(encoded.asChars());
    auto frozenIndex = frozenImplByCanonicalKey.find(canonicalKey);
    if (frozenIndex == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    bool frozenMatches = false;
    ZC_IF_SOME(value, frozenIndex) {
      const auto& frozen = frozenImpls[value];
      frozenMatches = fact.occurrence == frozen.occurrence && fact.authority == frozen.authority &&
                      fact.node == frozen.node && sameSpan(fact.source, frozen.source);
    }
    if (!frozenMatches || fact.scope.index() >= candidate.scopes.size() ||
        candidate.scopes[fact.scope.index()].id != fact.scope ||
        candidate.scopes[fact.scope.index()].kind != ScopeKind::ImplBody ||
        !candidate.scopes[fact.scope.index()].owner.value().is<ImplScopeOwner>() ||
        candidate.scopes[fact.scope.index()].owner.value().get<ImplScopeOwner>().occurrence !=
            fact.occurrence ||
        implByScope[fact.scope.index()] != kMissing) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    implByScope[fact.scope.index()] = index;
    previousImplKey = zc::mv(encoded);
    hasPreviousImplKey = true;
  }
  for (const auto& definition : candidate.definitions) {
    if (definition.activation != DefinitionActivation::ModuleSkeleton ||
        definition.declaringScope.index() >= implByScope.size()) {
      continue;
    }
    const size_t implIndex = implByScope[definition.declaringScope.index()];
    if (implIndex == kMissing) { continue; }
    const size_t memberIndex = memberCounts[implIndex]++;
    if (memberIndex >= candidate.impls[implIndex].members.size() ||
        candidate.impls[implIndex].members[memberIndex] != definition.identity) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }
  for (size_t index = 0; index < candidate.impls.size(); ++index) {
    if (memberCounts[index] != candidate.impls[index].members.size()) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }
  uint32_t previousNode = 0;
  for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
    const auto& resolution = candidate.nodeBindings[index];
    if (!tree.contains(resolution.node) || (index != 0 && resolution.node.value <= previousNode)) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    previousNode = resolution.node.value;
    const auto& value = resolution.value;
    if (value.is<BoundNameResolution>()) {
      const auto& bound = value.get<BoundNameResolution>();
      if (!validNamespace(bound.nameSpace) || !targetExists(input, bound.bindingIdentity) ||
          !targetExists(input, bound.canonicalTarget)) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      if (bound.origin == BindingOrigin::LocalDeclaration) {
        if (!sameTarget(bound.bindingIdentity, bound.canonicalTarget)) {
          return CandidateStructureResult::InvalidBindingFact;
        }
        continue;
      }
      bool matchedBinding = false;
      for (const auto& scope : candidate.scopes) {
        for (const auto& entry : scope.bindings) {
          if (entry.binding.nameSpace == bound.nameSpace && entry.binding.origin == bound.origin &&
              sameTarget(entry.binding.bindingIdentity, bound.bindingIdentity) &&
              sameTarget(entry.binding.canonicalTarget, bound.canonicalTarget)) {
            matchedBinding = true;
            break;
          }
        }
        if (matchedBinding) { break; }
      }
      if (!matchedBinding) { return CandidateStructureResult::InvalidBindingFact; }
    } else if (value.is<FailedBindingResolution>()) {
      if (value.get<FailedBindingResolution>().failureIndex >= candidate.sourceFailures.size()) {
        return CandidateStructureResult::InvalidBindingFact;
      }
    }
  }
  zc::TreeMap<uint64_t, bool> emitterOrdinals;
  uint64_t previousStart = 0;
  uint64_t previousEnd = 0;
  uint16_t previousDiagnostic = 0;
  for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
    const auto& fact = candidate.sourceFailures[index];
    const uint8_t site = static_cast<uint8_t>(fact.emitterOrdinal >> 56);
    const uint32_t schemaOrdinal = static_cast<uint32_t>((fact.emitterOrdinal >> 16) & UINT32_MAX);
    if (site < static_cast<uint8_t>(BinderEmitterSite::BindingInput) ||
        site > static_cast<uint8_t>(BinderEmitterSite::BindingVerifier) ||
        schemaOrdinal >= tree.nodeCount() ||
        emitterOrdinals.find(fact.emitterOrdinal) != zc::none) {
      return CandidateStructureResult::InvalidEmitterOrdinal;
    }
    emitterOrdinals.insert(fact.emitterOrdinal, true);
    const uint16_t diagnostic = static_cast<uint16_t>(fact.diagnostic);
    if (index != 0 &&
        (fact.primary.byteStart() < previousStart ||
         (fact.primary.byteStart() == previousStart && fact.primary.byteEnd() < previousEnd) ||
         (fact.primary.byteStart() == previousStart && fact.primary.byteEnd() == previousEnd &&
          diagnostic < previousDiagnostic))) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    previousStart = fact.primary.byteStart();
    previousEnd = fact.primary.byteEnd();
    previousDiagnostic = diagnostic;
    for (const auto& note : fact.notes) {
      if (note.diagnostic != BinderDiagnosticCode::PreviousDeclarationHere) {
        return CandidateStructureResult::InvalidBindingFact;
      }
    }
  }
  zc::Array<uint8_t> previousShadow;
  bool hasPreviousShadow = false;
  for (const auto& shadow : candidate.shadowTargets) {
    if (!targetExists(input, shadow.binding) || !targetExists(input, shadow.target)) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    if (sameTarget(shadow.binding, shadow.target)) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    auto encoded = encodeTargetKey(input, shadow.binding);
    if (encoded == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
    ZC_IF_SOME(value, encoded) {
      if (hasPreviousShadow && compareCanonicalBytes(previousShadow.asPtr(), value.asPtr()) >= 0) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      previousShadow = zc::mv(value);
      hasPreviousShadow = true;
    }
  }
  const auto& moduleBindings = candidate.scopes[0].bindings;
  size_t localFactIndex = 0;
  for (const auto& specifier : input.localExportSpecifiers()) {
    if (localFactIndex >= candidate.localExports.size() ||
        candidate.localExports[localFactIndex].node != specifier.syntax()) {
      bool hasUndefinedFailure = false;
      for (const auto& failureFact : candidate.sourceFailures) {
        const auto site = static_cast<uint8_t>(failureFact.emitterOrdinal >> 56);
        const auto ordinal = static_cast<uint32_t>((failureFact.emitterOrdinal >> 16) & UINT32_MAX);
        if (site == static_cast<uint8_t>(BinderEmitterSite::ImportBinding) &&
            ordinal == specifier.schemaPreorderOrdinal() &&
            failureFact.diagnostic == BinderDiagnosticCode::UndefinedIdentifier) {
          hasUndefinedFailure = true;
          break;
        }
      }
      if (!hasUndefinedFailure) { return CandidateStructureResult::MissingRequiredResolution; }
      continue;
    }
    const auto& fact = candidate.localExports[localFactIndex++];
    if (fact.node != specifier.syntax() || !sameMaybeSpan(fact.aliasSpan, specifier.aliasSpan()) ||
        !sameSpan(fact.exportSpan, specifier.exportSpan())) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    size_t sourceBindingIndex = moduleBindings.size();
    for (size_t index = 0; index < moduleBindings.size(); ++index) {
      const auto& binding = moduleBindings[index];
      if (binding.name.name().text() != specifier.sourceName().text() ||
          !sameTarget(binding.binding.bindingIdentity, fact.sourceBinding)) {
        continue;
      }
      if (sourceBindingIndex != moduleBindings.size()) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      sourceBindingIndex = index;
    }
    if (sourceBindingIndex == moduleBindings.size()) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    const auto& source = moduleBindings[sourceBindingIndex];
    if (!sameTarget(source.binding.canonicalTarget, fact.canonicalTarget) ||
        !sameSpan(source.binding.declarationSpan, fact.bindingSpan)) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    zc::Maybe<identity::SourceSpan> canonicalSpan;
    zc::Vector<ReexportProvenanceStep> sourceChain;
    if (source.binding.origin == BindingOrigin::LocalDeclaration) {
      const auto& sourceIdentity = source.binding.bindingIdentity.value();
      bool moduleAlias = false;
      if (sourceIdentity.is<DefinitionBindingTarget>()) {
        const auto definition = sourceIdentity.get<DefinitionBindingTarget>().definition;
        for (const auto& alias : candidate.moduleAliases) {
          if (alias.alias == definition) {
            canonicalSpan = alias.targetSpan.clone();
            moduleAlias = true;
            break;
          }
        }
      }
      if (!moduleAlias) { canonicalSpan = source.binding.declarationSpan.clone(); }
    } else {
      bool foundProvenance = false;
      for (size_t index = 0; index < candidate.imports.size(); ++index) {
        auto importIdentity =
            BindingTarget::semanticImport(candidate.imports[index].binding.clone());
        if (!sameTarget(source.binding.bindingIdentity, importIdentity)) { continue; }
        canonicalSpan = resolvedImports[index].canonicalDeclarationSpan().clone();
        for (const auto& step : candidate.imports[index].reexportChain) {
          sourceChain.add(step.clone());
        }
        foundProvenance = true;
        break;
      }
      if (!foundProvenance) {
        for (size_t index = 0; index + 1 < localFactIndex; ++index) {
          const auto& previous = candidate.localExports[index];
          if (!sameTarget(previous.sourceBinding, source.binding.bindingIdentity)) { continue; }
          canonicalSpan = previous.canonicalDeclarationSpan.clone();
          for (const auto& step : previous.reexportChain) { sourceChain.add(step.clone()); }
          foundProvenance = true;
          break;
        }
      }
      if (!foundProvenance) { return CandidateStructureResult::InvalidBindingFact; }
    }
    if (canonicalSpan == zc::none ||
        !sameSpan(fact.canonicalDeclarationSpan, ZC_ASSERT_NONNULL(canonicalSpan))) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    sourceChain.add(ReexportProvenanceStep{input.module(), fact.sourceBinding.clone(),
                                           fact.canonicalTarget.clone(), fact.exportSpan.clone()});
    if (!sameReexportChain(fact.reexportChain.asPtr(), sourceChain.asPtr())) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }
  if (localFactIndex < candidate.localExports.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }

  size_t expectedSurfaceSize = 0;
  for (const auto& binding : moduleBindings) {
    if (binding.binding.origin != BindingOrigin::ImportAlias) { ++expectedSurfaceSize; }
  }
  if (candidate.currentSurface.visibleEntries.size() < expectedSurfaceSize) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  if (candidate.currentSurface.visibleEntries.size() > expectedSurfaceSize) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  size_t surfaceIndex = 0;
  for (const auto& binding : moduleBindings) {
    if (binding.binding.origin == BindingOrigin::ImportAlias) { continue; }
    const auto& entry = candidate.currentSurface.visibleEntries[surfaceIndex++];
    if (binding.name.nameSpace() != entry.name.nameSpace() ||
        binding.name.name() != entry.name.name() ||
        !sameTarget(binding.binding.bindingIdentity, entry.bindingIdentity) ||
        !sameTarget(binding.binding.canonicalTarget, entry.canonicalTarget) ||
        !sameSpan(binding.binding.declarationSpan, entry.bindingSpan) ||
        !sameMaybeSpan(binding.binding.aliasSpan, entry.aliasSpan) ||
        entry.exported != (entry.exportSpan != zc::none)) {
      return CandidateStructureResult::InvalidBindingFact;
    }
    if (binding.binding.origin == BindingOrigin::ReexportAlias) {
      if (!entry.exported || !entry.visibility.value().is<ExternalVisibility>()) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      bool matched = false;
      for (size_t index = 0; index < candidate.imports.size(); ++index) {
        const auto& import = candidate.imports[index];
        auto importIdentity = BindingTarget::semanticImport(import.binding.clone());
        if (!sameTarget(binding.binding.bindingIdentity, importIdentity) ||
            import.kind != ImportBindingKind::ForeignReexport) {
          continue;
        }
        const auto exportSpan = resolvedImports[index].exportSpan();
        matched = exportSpan != zc::none &&
                  sameSpan(entry.canonicalDeclarationSpan,
                           resolvedImports[index].canonicalDeclarationSpan()) &&
                  sameMaybeSpan(entry.aliasSpan, resolvedImports[index].aliasSpan()) &&
                  sameSpan(ZC_ASSERT_NONNULL(entry.exportSpan), ZC_ASSERT_NONNULL(exportSpan)) &&
                  sameReexportChain(entry.reexportChain.asPtr(), import.reexportChain.asPtr());
        break;
      }
      if (!matched) {
        for (const auto& local : candidate.localExports) {
          if (!sameTarget(local.sourceBinding, binding.binding.bindingIdentity)) { continue; }
          matched = sameSpan(entry.canonicalDeclarationSpan, local.canonicalDeclarationSpan) &&
                    sameMaybeSpan(entry.aliasSpan, local.aliasSpan) &&
                    sameSpan(ZC_ASSERT_NONNULL(entry.exportSpan), local.exportSpan) &&
                    sameReexportChain(entry.reexportChain.asPtr(), local.reexportChain.asPtr());
          break;
        }
      }
      if (!matched) { return CandidateStructureResult::InvalidBindingFact; }
      continue;
    }
    const auto& identityValue = binding.binding.bindingIdentity.value();
    bool moduleAlias = false;
    if (identityValue.is<DefinitionBindingTarget>()) {
      const auto alias = identityValue.get<DefinitionBindingTarget>().definition;
      for (size_t index = 0; index < candidate.moduleAliases.size(); ++index) {
        if (candidate.moduleAliases[index].alias != alias) { continue; }
        moduleAlias = true;
        const auto& resolved = resolvedModuleAliases[index];
        if (!sameSpan(entry.canonicalDeclarationSpan, candidate.moduleAliases[index].targetSpan) ||
            entry.exported != resolved.exported() || !entry.reexportChain.empty()) {
          return CandidateStructureResult::InvalidBindingFact;
        }
        if (entry.exported) {
          if (entry.exportSpan == zc::none ||
              !sameSpan(ZC_ASSERT_NONNULL(entry.exportSpan), resolved.declarationSpan())) {
            return CandidateStructureResult::InvalidBindingFact;
          }
        }
        break;
      }
    }
    if (binding.binding.origin == BindingOrigin::Prelude) {
      const auto prelude = input.preludeSurface();
      if (prelude == zc::none || entry.exported || entry.aliasSpan != zc::none ||
          entry.exportSpan != zc::none) {
        return CandidateStructureResult::InvalidBindingFact;
      }
      bool matched = false;
      ZC_IF_SOME(surface, prelude) {
        for (const auto& source : surface.visibleEntries()) {
          if (binding.name.nameSpace() == source.name.nameSpace() &&
              binding.name.name() == source.name.name() &&
              sameTarget(binding.binding.bindingIdentity, source.bindingIdentity) &&
              sameTarget(binding.binding.canonicalTarget, source.canonicalTarget) &&
              sameSpan(entry.canonicalDeclarationSpan, source.canonicalDeclarationSpan) &&
              sameReexportChain(entry.reexportChain.asPtr(), source.reexportChain.asPtr())) {
            matched = true;
            break;
          }
        }
      }
      if (!matched) { return CandidateStructureResult::InvalidBindingFact; }
      continue;
    }
    if (!moduleAlias &&
        (!sameSpan(binding.binding.declarationSpan, entry.canonicalDeclarationSpan) ||
         entry.aliasSpan != zc::none || !entry.reexportChain.empty())) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }
  auto visible = encodeBindingSurfaceMap(input, candidate.currentSurface.visibleEntries.asPtr());
  auto exports = encodeBindingSurfaceMap(input, candidate.currentSurface.exports.asPtr());
  if (visible == zc::none || exports == zc::none) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  size_t exportIndex = 0;
  for (const auto& entry : candidate.currentSurface.visibleEntries) {
    if (entry.exported) {
      if (!entry.visibility.value().is<ExternalVisibility>() ||
          exportIndex >= candidate.currentSurface.exports.size() ||
          !sameSurfaceEntry(input, entry, candidate.currentSurface.exports[exportIndex++])) {
        return CandidateStructureResult::InvalidBindingFact;
      }
    } else if (!entry.visibility.value().is<ModuleVisibility>() ||
               entry.visibility.value().get<ModuleVisibility>().module != input.module()) {
      return CandidateStructureResult::InvalidBindingFact;
    }
  }
  if (exportIndex != candidate.currentSurface.exports.size()) {
    return CandidateStructureResult::InvalidBindingFact;
  }
  ZC_IF_SOME(visibleBytes, visible) {
    ZC_IF_SOME(exportBytes, exports) {
      const auto moduleBytes = input.moduleKey().encode();
      const auto packageBytes = input.packageKey().encode();
      auto revision = ExportSurfaceRevision::computeFramed(
          input.semanticFingerprint().digest(), moduleBytes.asPtr(), packageBytes.asPtr(),
          visibleBytes.asPtr(), exportBytes.asPtr());
      if (revision == zc::none) { return CandidateStructureResult::InvalidBindingFact; }
      ZC_IF_SOME(value, revision) {
        if (value.digest() != candidate.currentSurface.revision.digest()) {
          return CandidateStructureResult::InvalidBindingFact;
        }
      }
    }
  }
  if (!hasCompleteLexicalBindingSites(input, candidate.nodeBindings.asPtr(),
                                      candidate.selfTypes.asPtr(),
                                      candidate.thisBindings.asPtr())) {
    return CandidateStructureResult::MissingRequiredResolution;
  }
  return CandidateStructureResult::Valid;
}

BinderInvariantKind candidateStructureInvariant(CandidateStructureResult result) {
  switch (result) {
    case CandidateStructureResult::MissingRequiredResolution:
      return BinderInvariantKind::MissingRequiredResolution;
    case CandidateStructureResult::MalformedScopeGraph:
      return BinderInvariantKind::MalformedScopeGraph;
    case CandidateStructureResult::InvalidEmitterOrdinal:
      return BinderInvariantKind::InvalidEmitterOrdinal;
    case CandidateStructureResult::InvalidBindingFact:
      return BinderInvariantKind::InvalidBindingFact;
    case CandidateStructureResult::Valid:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

}  // namespace

bool bindingCandidateHasForeignContext(const VerifiedBindingInput& input,
                                       const BindingMetadataCandidate& candidate) {
  return hasForeignContext(input, candidate);
}

bool bindingCandidateHasInvalidSourceRange(const VerifiedBindingInput& input,
                                           const BindingMetadataCandidate& candidate) {
  return hasInvalidSourceRange(input, candidate);
}

zc::Maybe<BinderInvariantKind> verifyBindingCandidateStructure(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyCandidateStructure(input, candidate);
  if (result == CandidateStructureResult::Valid) { return zc::none; }
  return candidateStructureInvariant(result);
}

}  // namespace zomlang::compiler::binder
