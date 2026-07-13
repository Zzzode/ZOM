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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/internal/control-transfer.h"

#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissingIndex = UINT32_MAX;

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            BinderEmitterSite site, uint32_t ordinal) {
  return BinderInvariantFact{kind, input.module(), zc::none, site, ordinal};
}

}  // namespace

class ControlTransferBuilder::Cursor final {
public:
  Cursor(const VerifiedBindingInput& input, const ScopeArenaCandidate& arena,
         const LabelFactsCandidate& labels)
      : input(input), tree(input.tree()), arena(arena), labels(labels) {}

  ControlTransferBuildResult run() {
    if (!initialize()) { return takeRejection(); }
    visit(tree.root());
    if (rejected != zc::none) { return takeRejection(); }
    if (!sortFacts()) { return takeRejection(); }
    return zc::mv(candidate);
  }

private:
  const VerifiedBindingInput& input;
  const ast::Tree& tree;
  const ScopeArenaCandidate& arena;
  const LabelFactsCandidate& labels;
  ControlTransferCandidate candidate;
  zc::Vector<uint32_t> nodeScopeIndices;
  zc::Vector<uint32_t> schemaOrdinals;
  zc::Vector<size_t> activeLabels;
  zc::Maybe<BinderInvariantFact> rejected;

  bool initialize() {
    if (!tree.contains(tree.root()) || arena.scopes.empty() ||
        arena.scopes[0].kind != ScopeKind::Module || arena.nodeScopes.size() != tree.nodeCount()) {
      reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding, tree.root());
      return false;
    }
    for (size_t index = 0; index < arena.scopes.size(); ++index) {
      const auto& scope = arena.scopes[index];
      if (scope.id.module() != input.module() || scope.id.index() != index ||
          !scope.id.belongsTo(input.semanticContext())) {
        reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding,
               tree.root());
        return false;
      }
      if ((index == 0 && scope.parent != zc::none) || (index != 0 && scope.parent == zc::none)) {
        reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding,
               tree.root());
        return false;
      }
      ZC_IF_SOME(parent, scope.parent) {
        if (parent.module() != input.module() || parent.index() >= index ||
            !parent.belongsTo(input.semanticContext())) {
          reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding,
                 tree.root());
          return false;
        }
      }
    }

    nodeScopeIndices.resize(tree.nodeCount() + 1);
    schemaOrdinals.resize(tree.nodeCount() + 1);
    for (size_t index = 0; index < nodeScopeIndices.size(); ++index) {
      nodeScopeIndices[index] = kMissingIndex;
      schemaOrdinals[index] = kMissingIndex;
    }
    for (const auto& fact : arena.nodeScopes) {
      if (!tree.contains(fact.node) || fact.node.value >= nodeScopeIndices.size() ||
          nodeScopeIndices[fact.node.value] != kMissingIndex ||
          fact.scope.module() != input.module() || fact.scope.index() >= arena.scopes.size() ||
          arena.scopes[fact.scope.index()].id != fact.scope) {
        reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding,
               tree.root());
        return false;
      }
      nodeScopeIndices[fact.node.value] = fact.scope.index();
    }

    uint32_t ordinal = 0;
    bool valid = true;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
      if (!tree.contains(node) || node.value >= schemaOrdinals.size() ||
          schemaOrdinals[node.value] != kMissingIndex ||
          nodeScopeIndices[node.value] == kMissingIndex) {
        valid = false;
        return;
      }
      schemaOrdinals[node.value] = ordinal++;
    });
    if (!valid || ordinal != tree.nodeCount()) {
      reject(BinderInvariantKind::InvalidEmitterOrdinal, BinderEmitterSite::BodyBinding,
             tree.root());
      return false;
    }
    return true;
  }

  bool isCallableBoundary(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= nodeScopeIndices.size()) { return false; }
    const uint32_t scopeIndex = nodeScopeIndices[node.value];
    if (scopeIndex >= arena.scopes.size()) { return false; }
    const auto kind = arena.scopes[scopeIndex].kind;
    return kind == ScopeKind::Function || kind == ScopeKind::Closure;
  }

  zc::Maybe<size_t> labelForStatement(ast::NodeId statement) {
    zc::Maybe<size_t> result;
    for (size_t index = 0; index < labels.labels.size(); ++index) {
      if (labels.labels[index].statement != statement) { continue; }
      if (result != zc::none) { return zc::none; }
      result = index;
    }
    return result;
  }

  void visit(ast::NodeId node) {
    if (rejected != zc::none || !tree.contains(node)) { return; }

    zc::Vector<size_t> savedLabels;
    const bool resetsLabels = isCallableBoundary(node);
    if (resetsLabels) {
      savedLabels = zc::mv(activeLabels);
      activeLabels = zc::Vector<size_t>();
    }

    const auto& syntax = tree.node(node);
    if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      const ast::NodeId statement(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      auto labelIndex = labelForStatement(statement);
      if (!tree.contains(statement) || labelIndex == zc::none) {
        reject(BinderInvariantKind::MissingRequiredResolution, BinderEmitterSite::LabelAndClosure,
               node);
      } else {
        ZC_IF_SOME(index, labelIndex) {
          activeLabels.add(index);
          visit(statement);
          activeLabels.removeLast();
        }
      }
    } else {
      if (syntax.kind == ast::SyntaxKind::BreakStmt ||
          syntax.kind == ast::SyntaxKind::ContinueStatement) {
        const bool isBreak = syntax.kind == ast::SyntaxKind::BreakStmt;
        const uint32_t label =
            syntax.payload
                .words[isBreak ? ast::kBreakStmtLabelWord : ast::kContinueStatementLabelWord];
        if (label == 0) {
          resolveUnlabeled(node, isBreak);
        } else {
          resolveLabel(node, isBreak, ast::IdentId(label));
        }
      }
      if (rejected == zc::none) {
        ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) { visit(child); });
      }
    }

    if (resetsLabels) { activeLabels = zc::mv(savedLabels); }
  }

  void addFailure(BinderDiagnosticCode diagnostic, ast::NodeId node, identity::SourceSpan&& source,
                  zc::Maybe<identity::SemanticIdentifier>&& label, BinderEmitterSite emitterSite) {
    candidate.failures.add(ControlTransferFailureFact{
        diagnostic, node, zc::mv(source), zc::mv(label), emitterSite, schemaOrdinals[node.value]});
  }

  void resolveLabel(ast::NodeId node, bool isBreak, ast::IdentId label) {
    auto statementSource = input.parsedModule().spanFor(tree.node(node).range);
    auto labelSource = input.parsedModule().retainedTokenSpan(node, 1, ast::SyntaxKind::Identifier);
    auto name = identity::SemanticIdentifier::fromSource(tree.ident(label));
    if (statementSource == zc::none || labelSource == zc::none || name == zc::none) {
      reject(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::LabelAndClosure, node);
      return;
    }

    size_t match = labels.labels.size();
    ZC_IF_SOME(nameValue, name) {
      for (size_t offset = activeLabels.size(); offset > 0; --offset) {
        const size_t factIndex = activeLabels[offset - 1];
        if (factIndex >= labels.labels.size()) {
          reject(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::LabelAndClosure, node);
          return;
        }
        if (labels.labels[factIndex].name == nameValue) {
          match = factIndex;
          break;
        }
      }
      if (match == labels.labels.size()) {
        ZC_IF_SOME(source, labelSource) {
          zc::Maybe<identity::SemanticIdentifier> failedName = zc::mv(nameValue);
          addFailure(BinderDiagnosticCode::UndefinedIdentifier, node, zc::mv(source),
                     zc::mv(failedName), BinderEmitterSite::LabelAndClosure);
        }
        return;
      }
    }

    const auto& fact = labels.labels[match];
    if (!isBreak && fact.target.value().is<BlockLabelTarget>()) {
      ZC_IF_SOME(source, labelSource) {
        zc::Maybe<identity::SemanticIdentifier> noLabel;
        addFailure(BinderDiagnosticCode::ContinueTargetNotLoop, node, zc::mv(source),
                   zc::mv(noLabel), BinderEmitterSite::BodyBinding);
      }
      return;
    }

    candidate.nodeBindings.add(
        BindingResolution{node, BindingResolutionValue(BoundLabelResolution{
                                    ControlTransferBuilder::cloneLabelId(fact.identity),
                                    ControlTransferBuilder::cloneLabelTarget(fact.target)})});
    ZC_IF_SOME(source, statementSource) {
      candidate.controlTransfers.add(ControlTransferFact{
          node, isBreak ? ControlTransferKind::Break : ControlTransferKind::Continue,
          ControlTarget(
              ExplicitLabelControlTarget{ControlTransferBuilder::cloneLabelId(fact.identity)}),
          zc::mv(source)});
    }
  }

  void resolveUnlabeled(ast::NodeId node, bool isBreak) {
    auto statementSource = input.parsedModule().spanFor(tree.node(node).range);
    auto keywordSource = input.parsedModule().retainedTokenSpan(
        node, 0, isBreak ? ast::SyntaxKind::BreakKeyword : ast::SyntaxKind::ContinueKeyword);
    if (statementSource == zc::none || keywordSource == zc::none ||
        node.value >= nodeScopeIndices.size() || nodeScopeIndices[node.value] == kMissingIndex) {
      reject(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::BodyBinding, node);
      return;
    }

    uint32_t scopeIndex = nodeScopeIndices[node.value];
    for (size_t traversed = 0; traversed < arena.scopes.size(); ++traversed) {
      if (scopeIndex >= arena.scopes.size()) {
        reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding, node);
        return;
      }
      const auto& scope = arena.scopes[scopeIndex];
      if (scope.kind == ScopeKind::Loop) {
        ZC_IF_SOME(source, statementSource) {
          candidate.controlTransfers.add(ControlTransferFact{
              node, isBreak ? ControlTransferKind::Break : ControlTransferKind::Continue,
              ControlTarget(LoopControlTarget{scope.id}), zc::mv(source)});
        }
        return;
      }
      if (scope.kind == ScopeKind::Match && isBreak) {
        ZC_IF_SOME(source, statementSource) {
          candidate.controlTransfers.add(
              ControlTransferFact{node, ControlTransferKind::Break,
                                  ControlTarget(MatchControlTarget{scope.id}), zc::mv(source)});
        }
        return;
      }
      if (scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||
          scope.kind == ScopeKind::Module || scope.parent == zc::none) {
        ZC_IF_SOME(source, keywordSource) {
          zc::Maybe<identity::SemanticIdentifier> noLabel;
          addFailure(isBreak ? BinderDiagnosticCode::BreakTargetNotFound
                             : BinderDiagnosticCode::ContinueTargetNotFound,
                     node, zc::mv(source), zc::mv(noLabel), BinderEmitterSite::BodyBinding);
        }
        return;
      }
      ZC_IF_SOME(parent, scope.parent) { scopeIndex = parent.index(); }
    }
    reject(BinderInvariantKind::MalformedScopeGraph, BinderEmitterSite::BodyBinding, node);
  }

  bool sortFacts() {
    zc::TreeMap<uint32_t, size_t> order;
    for (size_t index = 0; index < candidate.controlTransfers.size(); ++index) {
      if (order.find(candidate.controlTransfers[index].node.value) != zc::none) {
        reject(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::BodyBinding,
               candidate.controlTransfers[index].node);
        return false;
      }
      order.insert(candidate.controlTransfers[index].node.value, index);
    }
    zc::Vector<ControlTransferFact> sorted;
    for (const auto& entry : order) { sorted.add(zc::mv(candidate.controlTransfers[entry.value])); }
    candidate.controlTransfers = zc::mv(sorted);
    return true;
  }

  void reject(BinderInvariantKind kind, BinderEmitterSite site, ast::NodeId node) {
    if (rejected != zc::none) { return; }
    const uint32_t ordinal =
        node && node.value < schemaOrdinals.size() && schemaOrdinals[node.value] != kMissingIndex
            ? schemaOrdinals[node.value]
            : 0;
    rejected = failure(input, kind, site, ordinal);
  }

  ControlTransferBuildResult takeRejection() { return zc::mv(ZC_ASSERT_NONNULL(rejected)); }
};

ControlTransferBuildResult ControlTransferBuilder::build(const VerifiedBindingInput& input,
                                                         const ScopeArenaCandidate& arena,
                                                         const LabelFactsCandidate& labels) {
  return Cursor(input, arena, labels).run();
}

LabelId ControlTransferBuilder::cloneLabelId(const LabelId& value) { return value.clone(); }

LabelTarget ControlTransferBuilder::cloneLabelTarget(const LabelTarget& value) {
  return value.clone();
}

}  // namespace zomlang::compiler::binder
