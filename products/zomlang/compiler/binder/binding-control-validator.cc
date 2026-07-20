// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-control-validator.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;
constexpr size_t kMissingFact = SIZE_MAX;

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool isControlTransfer(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::BreakStmt || kind == ast::SyntaxKind::ContinueStatement;
}

bool isLoop(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::WhileStmt || kind == ast::SyntaxKind::ForStmt ||
         kind == ast::SyntaxKind::ForInStatement || kind == ast::SyntaxKind::DoWhileStatement;
}

uint64_t emitterOrdinal(BinderEmitterSite site, uint32_t schemaOrdinal) {
  return (uint64_t(static_cast<uint8_t>(site)) << 56) | (uint64_t(schemaOrdinal) << 16);
}

struct OwnerCounter final {
  uint32_t scope;
  uint64_t nextIndex;
};

struct ExpectedLabel final {
  ExpectedLabel(ast::NodeId declaration, ast::NodeId statement, uint32_t ownerScope,
                uint32_t targetScope, bool loopTarget, uint32_t ownerIndex, size_t factIndex,
                identity::SemanticIdentifier&& name, identity::SourceSpan&& source) noexcept
      : declaration(declaration),
        statement(statement),
        ownerScope(ownerScope),
        targetScope(targetScope),
        loopTarget(loopTarget),
        ownerIndex(ownerIndex),
        factIndex(factIndex),
        name(zc::mv(name)),
        source(zc::mv(source)) {}
  ExpectedLabel(ExpectedLabel&&) noexcept = default;
  ExpectedLabel& operator=(ExpectedLabel&&) noexcept = default;
  ZC_DISALLOW_COPY(ExpectedLabel);

  ast::NodeId declaration;
  ast::NodeId statement;
  uint32_t ownerScope;
  uint32_t targetScope;
  bool loopTarget;
  uint32_t ownerIndex;
  size_t factIndex;
  identity::SemanticIdentifier name;
  identity::SourceSpan source;
};

class ControlSemanticValidator final {
public:
  ControlSemanticValidator(const VerifiedBindingInput& input,
                           const BindingMetadataCandidate& candidate)
      : input(input), tree(input.tree()), candidate(candidate) {}

  zc::Maybe<BinderInvariantKind> run() {
    if (!initialize()) { return failure; }
    if (!reconstructLabels()) { return failure; }
    visitControl(tree.root());
    if (failure != zc::none) { return failure; }
    if (candidate.controlTransfers.size() < expectedControlCount) {
      return BinderInvariantKind::MissingRequiredResolution;
    }
    if (candidate.controlTransfers.size() != expectedControlCount) {
      return BinderInvariantKind::InvalidBindingFact;
    }
    for (const auto matched : matchedControlFacts) {
      if (matched == 0) { return BinderInvariantKind::InvalidBindingFact; }
    }
    for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
      if (matchedFailures[index] != 0 || !isControlDomainFailure(candidate.sourceFailures[index])) {
        continue;
      }
      return BinderInvariantKind::InvalidBindingFact;
    }
    return zc::none;
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  const BindingMetadataCandidate& candidate;
  zc::Vector<uint32_t> scopeByNode;
  zc::Vector<uint32_t> schemaOrdinalByNode;
  zc::Vector<ast::NodeId> nodeBySchemaOrdinal;
  zc::Vector<size_t> resolutionByNode;
  zc::Vector<size_t> controlFactByNode;
  zc::Vector<uint8_t> matchedLabelFacts;
  zc::Vector<uint8_t> matchedControlFacts;
  zc::Vector<uint8_t> matchedFailures;
  zc::Vector<OwnerCounter> ownerCounters;
  zc::Vector<ExpectedLabel> expectedLabels;
  zc::Vector<size_t> activeLabels;
  size_t expectedControlCount = 0;
  zc::Maybe<BinderInvariantKind> failure;

  bool reject(BinderInvariantKind kind) {
    if (failure == zc::none || kind == BinderInvariantKind::InvalidBindingFact) { failure = kind; }
    return false;
  }

  bool initialize() {
    if (!tree.contains(tree.root()) || candidate.scopes.empty() ||
        candidate.scopes[0].kind != ScopeKind::Module ||
        candidate.nodeScopes.size() != tree.nodeCount()) {
      return reject(BinderInvariantKind::MalformedScopeGraph);
    }

    scopeByNode.resize(tree.nodeCount() + 1);
    schemaOrdinalByNode.resize(tree.nodeCount() + 1);
    resolutionByNode.resize(tree.nodeCount() + 1);
    controlFactByNode.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < scopeByNode.size(); ++index) {
      scopeByNode[index] = kMissingIndex;
      schemaOrdinalByNode[index] = kMissingIndex;
      resolutionByNode[index] = kMissingFact;
      controlFactByNode[index] = kMissingFact;
    }

    for (size_t index = 0; index < candidate.scopes.size(); ++index) {
      const auto& scope = candidate.scopes[index];
      if (scope.id.module() != input.module() || scope.id.index() != index ||
          (index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      ZC_IF_SOME(parent, scope.parent) {
        if (parent.module() != input.module() || parent.index() >= index ||
            candidate.scopes[parent.index()].id != parent) {
          return reject(BinderInvariantKind::MalformedScopeGraph);
        }
      }
    }
    for (const auto& fact : candidate.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= scopeByNode.size() ||
          scopeByNode[fact.node.value] != kMissingIndex ||
          fact.scope.index() >= candidate.scopes.size() ||
          candidate.scopes[fact.scope.index()].id != fact.scope) {
        return reject(BinderInvariantKind::MalformedScopeGraph);
      }
      scopeByNode[fact.node.value] = fact.scope.index();
    }

    uint32_t ordinal = 0;
    bool validPreorder = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
      if (!tree.contains(node) || node.value >= schemaOrdinalByNode.size() ||
          schemaOrdinalByNode[node.value] != kMissingIndex ||
          scopeByNode[node.value] == kMissingIndex) {
        validPreorder = false;
        return;
      }
      schemaOrdinalByNode[node.value] = ordinal++;
      nodeBySchemaOrdinal.add(node);
    });
    if (!validPreorder || ordinal != tree.nodeCount()) {
      return reject(BinderInvariantKind::InvalidEmitterOrdinal);
    }

    for (size_t index = 0; index < candidate.nodeBindings.size(); ++index) {
      const auto node = candidate.nodeBindings[index].node;
      if (!tree.contains(node) || node.value >= resolutionByNode.size() ||
          resolutionByNode[node.value] != kMissingFact) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      resolutionByNode[node.value] = index;
    }
    uint32_t previousControlNode = 0;
    for (size_t index = 0; index < candidate.controlTransfers.size(); ++index) {
      const auto node = candidate.controlTransfers[index].node;
      if (!tree.contains(node) || node.value >= controlFactByNode.size() ||
          controlFactByNode[node.value] != kMissingFact ||
          (index != 0 && node.value <= previousControlNode)) {
        return reject(BinderInvariantKind::InvalidBindingFact);
      }
      controlFactByNode[node.value] = index;
      previousControlNode = node.value;
    }

    matchedLabelFacts.resize(candidate.labels.size());
    matchedControlFacts.resize(candidate.controlTransfers.size());
    matchedFailures.resize(candidate.sourceFailures.size());
    for (auto& value : matchedLabelFacts) { value = 0; }
    for (auto& value : matchedControlFacts) { value = 0; }
    for (auto& value : matchedFailures) { value = 0; }
    return true;
  }

  zc::Maybe<uint32_t> ownerScopeFor(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] == kMissingIndex) {
      return zc::none;
    }
    uint32_t scopeIndex = scopeByNode[node.value];
    for (size_t traversed = 0; traversed < candidate.scopes.size(); ++traversed) {
      if (scopeIndex >= candidate.scopes.size()) { return zc::none; }
      const auto& scope = candidate.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||
          scope.kind == ScopeKind::Module) {
        return scopeIndex;
      }
      if (scope.parent == zc::none) { return zc::none; }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    return zc::none;
  }

  bool labelOwnerMatches(const LabelFact& fact, uint32_t ownerScope) const {
    if (ownerScope >= candidate.scopes.size() || fact.identity.owner() != fact.owner) {
      return false;
    }
    const auto& scope = candidate.scopes[ownerScope];
    const auto& scopeOwner = scope.owner.value();
    const auto& labelOwner = fact.owner.value();
    if (scope.kind == ScopeKind::Module) {
      return scopeOwner.is<ModuleScopeOwner>() && labelOwner.is<ModuleLabelOwner>() &&
             scopeOwner.get<ModuleScopeOwner>().module == input.module() &&
             labelOwner.get<ModuleLabelOwner>().module == input.module();
    }
    if (scope.kind == ScopeKind::Function) {
      return scopeOwner.is<DefinitionScopeOwner>() && labelOwner.is<CallableLabelOwner>() &&
             scopeOwner.get<DefinitionScopeOwner>().definition ==
                 labelOwner.get<CallableLabelOwner>().callable;
    }
    return scope.kind == ScopeKind::Closure && scopeOwner.is<AnonymousScopeOwner>() &&
           labelOwner.is<AnonymousLabelOwner>() &&
           labelOwner.get<AnonymousLabelOwner>().module == input.module() &&
           scopeOwner.get<AnonymousScopeOwner>().anonymous ==
               labelOwner.get<AnonymousLabelOwner>().anonymous;
  }

  uint8_t labelOwnerTag(const LabelOwner& owner) const {
    const auto& value = owner.value();
    if (value.is<ModuleLabelOwner>()) { return 0x01; }
    if (value.is<CallableLabelOwner>()) { return 0x02; }
    return 0x03;
  }

  zc::Maybe<zc::Array<uint8_t>> labelOwnerKey(const LabelOwner& owner) const {
    const auto& value = owner.value();
    if (value.is<ModuleLabelOwner>()) { return input.moduleKey().encode(); }
    if (value.is<CallableLabelOwner>()) {
      auto key = input.definitions().definitionKey(value.get<CallableLabelOwner>().callable);
      if (key == zc::none) { return zc::none; }
      ZC_IF_SOME(definition, key) { return definition.encode(); }
      ZC_UNREACHABLE;
    }
    return value.get<AnonymousLabelOwner>().anonymous.encode();
  }

  bool labelsAreCanonicallyOrdered() const {
    for (size_t index = 1; index < candidate.labels.size(); ++index) {
      const auto& left = candidate.labels[index - 1];
      const auto& right = candidate.labels[index];
      const uint8_t leftTag = labelOwnerTag(left.owner);
      const uint8_t rightTag = labelOwnerTag(right.owner);
      if (leftTag > rightTag) { return false; }
      if (leftTag < rightTag) { continue; }
      auto leftKey = labelOwnerKey(left.owner);
      auto rightKey = labelOwnerKey(right.owner);
      if (leftKey == zc::none || rightKey == zc::none) { return false; }
      const int keyOrder =
          compareBytes(ZC_ASSERT_NONNULL(leftKey).asPtr(), ZC_ASSERT_NONNULL(rightKey).asPtr());
      if (keyOrder > 0 || (keyOrder == 0 && left.identity.index() >= right.identity.index())) {
        return false;
      }
    }
    return true;
  }

  zc::Maybe<uint32_t> nextOwnerIndex(uint32_t ownerScope) {
    for (auto& counter : ownerCounters) {
      if (counter.scope != ownerScope) { continue; }
      if (counter.nextIndex > UINT32_MAX) { return zc::none; }
      return static_cast<uint32_t>(counter.nextIndex++);
    }
    ownerCounters.add(OwnerCounter{ownerScope, 1});
    return static_cast<uint32_t>(0);
  }

  zc::Maybe<size_t> exactFailure(ast::NodeId node, BinderDiagnosticCode diagnostic,
                                 BinderEmitterSite site, const identity::SourceSpan& primary,
                                 zc::Maybe<const identity::SourceSpan&> previous) {
    if (!tree.contains(node) || node.value >= schemaOrdinalByNode.size() ||
        schemaOrdinalByNode[node.value] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidEmitterOrdinal);
      return zc::none;
    }
    const uint32_t ordinal = schemaOrdinalByNode[node.value];
    const uint64_t expectedEmitter = emitterOrdinal(site, ordinal);
    size_t sameOrdinal = kMissingFact;
    for (size_t index = 0; index < candidate.sourceFailures.size(); ++index) {
      const auto& fact = candidate.sourceFailures[index];
      const uint32_t factOrdinal = static_cast<uint32_t>((fact.emitterOrdinal >> 16) & UINT32_MAX);
      if (factOrdinal != ordinal) { continue; }
      sameOrdinal = index;
      const bool notesMatch =
          previous == zc::none
              ? fact.notes.empty()
              : fact.notes.size() == 1 &&
                    fact.notes[0].diagnostic == BinderDiagnosticCode::PreviousDeclarationHere &&
                    sameSpan(fact.notes[0].source, ZC_ASSERT_NONNULL(previous));
      if (fact.diagnostic != diagnostic || fact.emitterOrdinal != expectedEmitter ||
          !sameSpan(fact.primary, primary) || !notesMatch || matchedFailures[index] != 0) {
        continue;
      }
      matchedFailures[index] = 1;
      return index;
    }
    reject(sameOrdinal == kMissingFact ? BinderInvariantKind::MissingRequiredResolution
                                       : BinderInvariantKind::InvalidBindingFact);
    return zc::none;
  }

  bool reconstructLabels() {
    size_t syntaxLabelCount = 0;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
      if (syntax.kind == ast::SyntaxKind::LabeledStatement) { ++syntaxLabelCount; }
    });
    if (candidate.labels.size() < syntaxLabelCount) {
      return reject(BinderInvariantKind::MissingRequiredResolution);
    }
    if (candidate.labels.size() != syntaxLabelCount) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }

    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (!valid || syntax.kind != ast::SyntaxKind::LabeledStatement) { return; }
      const ast::NodeId statement(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      ast::NodeId target = statement;
      for (size_t traversed = 0; traversed <= tree.nodeCount(); ++traversed) {
        if (!tree.contains(target)) {
          valid = reject(BinderInvariantKind::MissingRequiredResolution);
          return;
        }
        const auto& targetSyntax = tree.node(target);
        if (targetSyntax.kind != ast::SyntaxKind::LabeledStatement) { break; }
        target = ast::NodeId(targetSyntax.payload.words[ast::kLabeledStatementStatementWord]);
        if (traversed == tree.nodeCount()) {
          valid = reject(BinderInvariantKind::InvalidBindingFact);
          return;
        }
      }
      if (!valid || target.value >= scopeByNode.size() ||
          scopeByNode[target.value] == kMissingIndex) {
        valid = reject(BinderInvariantKind::MalformedScopeGraph);
        return;
      }
      const auto targetKind = tree.node(target).kind;
      const bool loopTarget = isLoop(targetKind);
      const uint32_t targetScope = scopeByNode[target.value];
      if ((!loopTarget && targetKind != ast::SyntaxKind::BlockStmt) ||
          targetScope >= candidate.scopes.size() ||
          candidate.scopes[targetScope].kind != (loopTarget ? ScopeKind::Loop : ScopeKind::Block)) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }

      auto ownerScope = ownerScopeFor(node);
      auto ownerIndex = ownerScope == zc::none ? zc::Maybe<uint32_t>()
                                               : nextOwnerIndex(ZC_ASSERT_NONNULL(ownerScope));
      auto source = input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier);
      auto name = identity::SemanticIdentifier::fromSource(
          tree.ident(ast::IdentId(syntax.payload.words[ast::kLabeledStatementLabelWord])));
      if (ownerScope == zc::none || ownerIndex == zc::none || source == zc::none ||
          name == zc::none) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }

      size_t factIndex = kMissingFact;
      for (size_t index = 0; index < candidate.labels.size(); ++index) {
        if (matchedLabelFacts[index] == 0 &&
            sameSpan(candidate.labels[index].source, ZC_ASSERT_NONNULL(source))) {
          factIndex = index;
          break;
        }
      }
      if (factIndex == kMissingFact) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }
      const auto& fact = candidate.labels[factIndex];
      const auto& targetValue = fact.target.value();
      const bool targetMatches =
          loopTarget
              ? targetValue.is<LoopLabelTarget>() &&
                    targetValue.get<LoopLabelTarget>().scope == candidate.scopes[targetScope].id
              : targetValue.is<BlockLabelTarget>() &&
                    targetValue.get<BlockLabelTarget>().scope == candidate.scopes[targetScope].id;
      if (!labelOwnerMatches(fact, ZC_ASSERT_NONNULL(ownerScope)) ||
          fact.identity.index() != ZC_ASSERT_NONNULL(ownerIndex) ||
          fact.name != ZC_ASSERT_NONNULL(name) || fact.statement != statement || !targetMatches) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }
      if (node.value >= resolutionByNode.size() || resolutionByNode[node.value] != kMissingFact) {
        valid = reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }

      zc::Maybe<const identity::SourceSpan&> previousDuplicate;
      for (const auto& previous : expectedLabels) {
        if (previous.ownerScope == ZC_ASSERT_NONNULL(ownerScope) &&
            previous.name == ZC_ASSERT_NONNULL(name)) {
          previousDuplicate = previous.source;
          break;
        }
      }
      if (previousDuplicate != zc::none &&
          exactFailure(node, BinderDiagnosticCode::DuplicateIdentifier,
                       BinderEmitterSite::LabelAndClosure, ZC_ASSERT_NONNULL(source),
                       previousDuplicate) == zc::none) {
        valid = false;
        return;
      }

      matchedLabelFacts[factIndex] = 1;
      expectedLabels.add(ExpectedLabel{node, statement, ZC_ASSERT_NONNULL(ownerScope), targetScope,
                                       loopTarget, ZC_ASSERT_NONNULL(ownerIndex), factIndex,
                                       zc::mv(ZC_ASSERT_NONNULL(name)),
                                       zc::mv(ZC_ASSERT_NONNULL(source))});
    });
    if (!valid) { return false; }
    for (const auto matched : matchedLabelFacts) {
      if (matched == 0) { return reject(BinderInvariantKind::InvalidBindingFact); }
    }
    if (!labelsAreCanonicallyOrdered()) { return reject(BinderInvariantKind::InvalidBindingFact); }
    return true;
  }

  zc::Maybe<size_t> labelForDeclaration(ast::NodeId declaration) const {
    for (size_t index = 0; index < expectedLabels.size(); ++index) {
      if (expectedLabels[index].declaration == declaration) { return index; }
    }
    return zc::none;
  }

  bool isCallableBoundary(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= scopeByNode.size() ||
        scopeByNode[node.value] >= candidate.scopes.size()) {
      return false;
    }
    const auto kind = candidate.scopes[scopeByNode[node.value]].kind;
    return kind == ScopeKind::Function || kind == ScopeKind::Closure;
  }

  void visitControl(ast::NodeId node) {
    if (failure != zc::none || !tree.contains(node)) { return; }
    zc::Vector<size_t> savedLabels;
    const bool resetLabels = isCallableBoundary(node);
    if (resetLabels) {
      savedLabels = zc::mv(activeLabels);
      activeLabels = zc::Vector<size_t>();
    }

    const auto& syntax = tree.node(node);
    if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      auto expectedIndex = labelForDeclaration(node);
      if (expectedIndex == zc::none) {
        reject(BinderInvariantKind::MissingRequiredResolution);
      } else {
        ZC_IF_SOME(index, expectedIndex) {
          activeLabels.add(index);
          visitControl(expectedLabels[index].statement);
          activeLabels.removeLast();
        }
      }
    } else {
      if (isControlTransfer(syntax.kind)) { verifyControl(node); }
      if (failure == zc::none) {
        ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) { visitControl(child); });
      }
    }

    if (resetLabels) { activeLabels = zc::mv(savedLabels); }
  }

  void verifyControl(ast::NodeId node) {
    const auto& syntax = tree.node(node);
    const bool isBreak = syntax.kind == ast::SyntaxKind::BreakStmt;
    const uint32_t labelWord =
        syntax.payload.words[isBreak ? ast::kBreakStmtLabelWord : ast::kContinueStatementLabelWord];
    if (labelWord != 0) {
      verifyExplicitControl(node, isBreak, ast::IdentId(labelWord));
      return;
    }
    verifyImplicitControl(node, isBreak);
  }

  void verifyExplicitControl(ast::NodeId node, bool isBreak, ast::IdentId label) {
    auto name = identity::SemanticIdentifier::fromSource(tree.ident(label));
    auto labelSource = input.parsedModule().retainedTokenSpan(node, 1, ast::SyntaxKind::Identifier);
    if (name == zc::none || labelSource == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact);
      return;
    }
    size_t match = kMissingFact;
    for (size_t offset = activeLabels.size(); offset > 0; --offset) {
      const size_t expectedIndex = activeLabels[offset - 1];
      if (expectedIndex >= expectedLabels.size()) {
        reject(BinderInvariantKind::InvalidBindingFact);
        return;
      }
      if (expectedLabels[expectedIndex].name == ZC_ASSERT_NONNULL(name)) {
        match = expectedIndex;
        break;
      }
    }
    if (match == kMissingFact) {
      verifyFailedControl(node, BinderDiagnosticCode::UndefinedIdentifier,
                          BinderEmitterSite::LabelAndClosure, ZC_ASSERT_NONNULL(labelSource));
      return;
    }
    const auto& expected = expectedLabels[match];
    if (!isBreak && !expected.loopTarget) {
      verifyFailedControl(node, BinderDiagnosticCode::ContinueTargetNotLoop,
                          BinderEmitterSite::BodyBinding, ZC_ASSERT_NONNULL(labelSource));
      return;
    }
    auto statementSource = input.parsedModule().spanFor(syntaxRange(node));
    if (statementSource == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact);
      return;
    }
    verifySuccessfulControl(node, isBreak, expected, ZC_ASSERT_NONNULL(statementSource));
  }

  source::SourceRange syntaxRange(ast::NodeId node) const { return tree.node(node).range; }

  void verifyImplicitControl(ast::NodeId node, bool isBreak) {
    auto statementSource = input.parsedModule().spanFor(syntaxRange(node));
    auto keywordSource = input.parsedModule().retainedTokenSpan(
        node, 0, isBreak ? ast::SyntaxKind::BreakKeyword : ast::SyntaxKind::ContinueKeyword);
    if (statementSource == zc::none || keywordSource == zc::none ||
        node.value >= scopeByNode.size() || scopeByNode[node.value] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidBindingFact);
      return;
    }
    uint32_t scopeIndex = scopeByNode[node.value];
    for (size_t traversed = 0; traversed < candidate.scopes.size(); ++traversed) {
      if (scopeIndex >= candidate.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph);
        return;
      }
      const auto& scope = candidate.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Loop || (scope.kind == ScopeKind::Match && isBreak)) {
        verifySuccessfulImplicitControl(node, isBreak, scopeIndex, scope.kind == ScopeKind::Match,
                                        ZC_ASSERT_NONNULL(statementSource));
        return;
      }
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||
          scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        verifyFailedControl(node,
                            isBreak ? BinderDiagnosticCode::BreakTargetNotFound
                                    : BinderDiagnosticCode::ContinueTargetNotFound,
                            BinderEmitterSite::BodyBinding, ZC_ASSERT_NONNULL(keywordSource));
        return;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    reject(BinderInvariantKind::MalformedScopeGraph);
  }

  void verifySuccessfulControl(ast::NodeId node, bool isBreak, const ExpectedLabel& expected,
                               const identity::SourceSpan& source) {
    ++expectedControlCount;
    if (!verifyControlFact(node, isBreak, source, expected.factIndex, kMissingIndex, false)) {
      return;
    }
    if (node.value >= resolutionByNode.size() || resolutionByNode[node.value] == kMissingFact) {
      reject(BinderInvariantKind::MissingRequiredResolution);
      return;
    }
    const auto& value = candidate.nodeBindings[resolutionByNode[node.value]].value;
    const auto& labelFact = candidate.labels[expected.factIndex];
    if (!value.is<BoundLabelResolution>() ||
        value.get<BoundLabelResolution>().label != labelFact.identity ||
        value.get<BoundLabelResolution>().target != labelFact.target || hasFailureForNode(node)) {
      reject(BinderInvariantKind::InvalidBindingFact);
    }
  }

  void verifySuccessfulImplicitControl(ast::NodeId node, bool isBreak, uint32_t targetScope,
                                       bool matchTarget, const identity::SourceSpan& source) {
    ++expectedControlCount;
    if (!verifyControlFact(node, isBreak, source, kMissingFact, targetScope, matchTarget)) {
      return;
    }
    if ((node.value < resolutionByNode.size() && resolutionByNode[node.value] != kMissingFact) ||
        hasFailureForNode(node)) {
      reject(BinderInvariantKind::InvalidBindingFact);
    }
  }

  bool verifyControlFact(ast::NodeId node, bool isBreak, const identity::SourceSpan& source,
                         size_t labelFactIndex, uint32_t scopeIndex, bool matchTarget) {
    if (node.value >= controlFactByNode.size() || controlFactByNode[node.value] == kMissingFact) {
      return true;
    }
    const size_t factIndex = controlFactByNode[node.value];
    if (factIndex >= candidate.controlTransfers.size() || matchedControlFacts[factIndex] != 0) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    const auto& fact = candidate.controlTransfers[factIndex];
    bool targetMatches = false;
    if (labelFactIndex != kMissingFact) {
      targetMatches = fact.target.is<ExplicitLabelControlTarget>() &&
                      labelFactIndex < candidate.labels.size() &&
                      fact.target.get<ExplicitLabelControlTarget>().label ==
                          candidate.labels[labelFactIndex].identity;
    } else if (matchTarget) {
      targetMatches =
          fact.target.is<MatchControlTarget>() &&
          fact.target.get<MatchControlTarget>().scope == candidate.scopes[scopeIndex].id;
    } else {
      targetMatches = fact.target.is<LoopControlTarget>() &&
                      fact.target.get<LoopControlTarget>().scope == candidate.scopes[scopeIndex].id;
    }
    if (fact.kind != (isBreak ? ControlTransferKind::Break : ControlTransferKind::Continue) ||
        !sameSpan(fact.source, source) || !targetMatches) {
      return reject(BinderInvariantKind::InvalidBindingFact);
    }
    matchedControlFacts[factIndex] = 1;
    return true;
  }

  void verifyFailedControl(ast::NodeId node, BinderDiagnosticCode diagnostic,
                           BinderEmitterSite site, const identity::SourceSpan& source) {
    if (node.value < controlFactByNode.size() && controlFactByNode[node.value] != kMissingFact) {
      reject(BinderInvariantKind::InvalidBindingFact);
      return;
    }
    zc::Maybe<const identity::SourceSpan&> noPrevious;
    auto failureIndex = exactFailure(node, diagnostic, site, source, noPrevious);
    if (failureIndex == zc::none) { return; }
    if (node.value >= resolutionByNode.size() || resolutionByNode[node.value] == kMissingFact) {
      reject(BinderInvariantKind::MissingRequiredResolution);
      return;
    }
    const auto& value = candidate.nodeBindings[resolutionByNode[node.value]].value;
    if (!value.is<FailedBindingResolution>() ||
        value.get<FailedBindingResolution>().failureIndex != ZC_ASSERT_NONNULL(failureIndex)) {
      reject(BinderInvariantKind::InvalidBindingFact);
    }
  }

  bool hasFailureForNode(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= schemaOrdinalByNode.size()) { return true; }
    const uint32_t ordinal = schemaOrdinalByNode[node.value];
    for (const auto& fact : candidate.sourceFailures) {
      if (static_cast<uint32_t>((fact.emitterOrdinal >> 16) & UINT32_MAX) == ordinal &&
          isControlDomainFailure(fact)) {
        return true;
      }
    }
    return false;
  }

  bool isControlDomainFailure(const BindingFailureRef& fact) const {
    const uint8_t site = static_cast<uint8_t>(fact.emitterOrdinal >> 56);
    const uint32_t ordinal = static_cast<uint32_t>((fact.emitterOrdinal >> 16) & UINT32_MAX);
    if (ordinal >= nodeBySchemaOrdinal.size()) { return false; }
    const auto kind = tree.node(nodeBySchemaOrdinal[ordinal]).kind;
    if (kind == ast::SyntaxKind::LabeledStatement) {
      return site == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure);
    }
    return isControlTransfer(kind) &&
           (site == static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure) ||
            site == static_cast<uint8_t>(BinderEmitterSite::BodyBinding));
  }
};

}  // namespace

zc::Maybe<BinderInvariantKind> verifyBindingControlSemantics(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  return ControlSemanticValidator(input, candidate).run();
}

}  // namespace zomlang::compiler::binder
