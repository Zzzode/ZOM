// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "binding-oracle-components.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

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
  const auto& arena = candidate;
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
  const auto& arena = candidate;
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

}  // namespace

zc::Maybe<BinderInvariantKind> verifyLabelOracle(const VerifiedBindingInput& input,
                                                 const BindingMetadataCandidate& candidate) {
  const auto result = verifyLabelFacts(input, candidate);
  if (result == LabelOracleResult::Valid) { return zc::none; }
  return labelOracleInvariant(result);
}

zc::Maybe<BinderInvariantKind> verifyControlTransferOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate) {
  const auto result = verifyControlTransferFacts(input, candidate);
  if (result == ControlOracleResult::Valid) { return zc::none; }
  return controlOracleInvariant(result);
}

}  // namespace zomlang::compiler::binder
