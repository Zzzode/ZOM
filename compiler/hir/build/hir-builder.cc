// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include <cstdint>

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/binder/metadata/definition-site.h"
#include "compiler/binder/metadata/immutable-binding-metadata.h"
#include "compiler/binder/metadata/immutable-definition-inventory.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/hir/build/hir-fn-builder.h"
#include "compiler/hir/build/hir-pending.h"
#include "compiler/hir/build/lower-expr-binary.h"
#include "compiler/hir/hir-candidate-impl.h"
#include "compiler/hir/hir-internal.h"
#include "compiler/hir/hir-shape.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/ownership/admission/surface-admission.h"

namespace zomlang::compiler::hir {
namespace detail {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

void sortPendingDeclarations(zc::Vector<PendingValueDeclaration>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0) {
      const auto& previous = values[insertion - 1];
      const bool less =
          current.declarationSpan.byteStart() < previous.declarationSpan.byteStart() ||
          (current.declarationSpan.byteStart() == previous.declarationSpan.byteStart() &&
           lessBytes(current.orderingKey.asPtr(), previous.orderingKey.asPtr()));
      if (!less) break;
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

void sortPendingFunctions(zc::Vector<PendingFunctionDeclaration>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0) {
      const auto& previous = values[insertion - 1];
      const bool less =
          current.declarationSpan.byteStart() < previous.declarationSpan.byteStart() ||
          (current.declarationSpan.byteStart() == previous.declarationSpan.byteStart() &&
           lessBytes(current.orderingKey.asPtr(), previous.orderingKey.asPtr()));
      if (!less) break;
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

}  // namespace
}  // namespace detail

using namespace detail;

HirModuleCandidate::HirModuleCandidate(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
HirModuleCandidate::~HirModuleCandidate() noexcept(false) = default;
HirModuleCandidate::HirModuleCandidate(HirModuleCandidate&&) noexcept = default;
HirModuleCandidate& HirModuleCandidate::operator=(HirModuleCandidate&&) noexcept = default;

ir::IrOperationResult<HirModuleCandidate> HirBuilder::build(VerifiedCheckedModule&& checkedModule) {
  const auto module = checkedModule.module();
  const auto registries = checkedModule.retainIdentityAuthority();
  const auto& facts = checkedModule.checkedFacts();
  const auto bound = checkedModule.retainAdmittedBoundModule();
  const auto borrowCapability = checkedModule.borrowEvidenceCapability();
  const auto borrowEvidence = borrowCapability.lookup(checkedModule.borrowEvidenceLease());
  if (registries.semanticContext() != checkedModule.semanticContext() ||
      registries.fingerprint().digest() != checkedModule.contextFingerprint().digest() ||
      registries.boundModule(module) == zc::none ||
      checkedModule.checkedRepository().lookup(checkedModule.checkedEvidenceLease()) == zc::none ||
      !borrowEvidence.isResolved() ||
      borrowEvidence.evidence().revision().digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.borrowEvidenceLease().key().revision.digest() !=
          checkedModule.borrowEvidenceRevision().digest() ||
      checkedModule.dispatchFacts().facts().size() != facts.calls().size() ||
      !noUnsupportedFacts(facts)) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 0);
  }

  const auto definitions = bound.definitions().definitions();
  if (definitions.size() > UINT32_MAX / 4) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::InvalidFact, module, registries, 1);
  }
  zc::Vector<PendingValueDeclaration> pending;
  zc::Vector<PendingFunctionDeclaration> pendingFunctions;
  const auto& ownInterface = checkedModule.ownModuleInterface();
  const auto& signatures = ownInterface.signatures();
  const auto definitionInventory = binder::DefinitionInventory::collect(bound.tree());
  for (size_t definitionIndex = 0; definitionIndex < definitions.size(); ++definitionIndex) {
    const auto ordinal = static_cast<uint32_t>(definitionIndex);
    const auto& definition = definitions[ordinal];
    if (!hasExecutableBody(definition, bound.definitions())) { continue; }
    if (definition.record.kind() != identity::DefinitionKind::Function &&
        definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      continue;
    }
    if (definition.record.kind() == identity::DefinitionKind::Function) {
      const auto& tree = bound.tree();
      if (!tree.contains(definition.node) ||
          tree.node(definition.node).kind != ast::SyntaxKind::FunctionDecl ||
          !definition.site.value().is<binder::DeclarationDefinitionSite>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      auto bodyShape = functionReturnShape(tree, tree.node(definition.node));
      auto signaturePosition =
          signatureIndex(signatures.definitions.asPtr(), definition.definition);
      auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
      if (bodyShape == zc::none || signaturePosition == zc::none || rootPosition == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      FunctionReturnShape shape{};
      size_t signatureSlot = 0;
      size_t rootSlot = 0;
      ZC_IF_SOME(value, bodyShape) { shape = value; }
      ZC_IF_SOME(index, signaturePosition) { signatureSlot = index; }
      ZC_IF_SOME(index, rootPosition) { rootSlot = index; }
      if (shape.isConditional) {
        // Conditional shape: if/else where both branches return either a scalar
        // literal or a bare parameter reference. Arms may differ in kind.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.condition);
        auto thenTypeIndex = factIndex(facts.nodeTypes(), shape.thenReturnValue);
        auto elseTypeIndex = factIndex(facts.nodeTypes(), shape.elseReturnValue);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.condition).range);
        auto thenSpan = bound.parsedModule().spanFor(tree.node(shape.thenReturnValue).range);
        auto elseSpan = bound.parsedModule().spanFor(tree.node(shape.elseReturnValue).range);
        if (conditionTypeIndex == zc::none || thenTypeIndex == zc::none ||
            elseTypeIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || thenSpan == zc::none ||
            elseSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto functionVisibility = visibility(root.visibility);
        auto functionLinkage = linkage(callable);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.sourceModule != module || root.canonicalDefinition != definition.definition ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        size_t thenTypeSlot = 0;
        size_t elseTypeSlot = 0;
        ZC_IF_SOME(index, thenTypeIndex) { thenTypeSlot = index; }
        ZC_IF_SOME(index, elseTypeIndex) { elseTypeSlot = index; }
        const auto thenType = facts.nodeTypes().entries()[thenTypeSlot].value;
        const auto elseType = facts.nodeTypes().entries()[elseTypeSlot].value;
        if (thenType != callable.success || elseType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // Resolve the condition: either a bare bool parameter reference or an
        // `a == b` equality comparison of two same-typed scalar parameters. The
        // equality form consumes the checker Eq call fact.
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        PendingConditionalCondition pendingCondition;
        if (shape.conditionIsEquality) {
          // The comparison condition consumes the checked relational call fact
          // whose two arguments are the left and right operands. Each operand is
          // either a parameter reference or a scalar literal; at least one is a
          // parameter, from which the shared operand type is derived.
          auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.conditionLeft);
          auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.conditionRight);
          auto callIndex = factIndex(facts.calls(), shape.condition);
          auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.conditionLeft).range);
          auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.conditionRight).range);
          if (leftTypeIndex == zc::none || rightTypeIndex == zc::none || callIndex == zc::none ||
              leftSpan == zc::none || rightSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t leftTypeSlot = 0;
          size_t rightTypeSlot = 0;
          size_t callSlot = 0;
          ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
          ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
          ZC_IF_SOME(index, callIndex) { callSlot = index; }
          const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
          const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
          const auto& callFact = facts.calls().entries()[callSlot].value;
          const auto& call = callFact.invocation;
          const auto& selected = call.selected.variant();
          // Both operand type facts must share the primitive scalar type and the
          // comparison call fact must match the relational-comparison contract
          // exactly for one of the six supported operators.
          if (operandType != rightType || callFact.node != shape.condition ||
              !selected.is<checker::checked::PrimitiveCallable>() ||
              !isScalarComparisonOperation(
                  selected.get<checker::checked::PrimitiveCallable>().operation) ||
              call.calleeType != operandType || call.receiver != zc::none ||
              call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
              call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.conditionLeft ||
              call.arguments[0].sourceType != operandType ||
              call.arguments[1].sourceNode != shape.conditionRight ||
              call.arguments[1].sourceType != operandType || call.successType != conditionType ||
              call.resultType != conditionType || call.substitutions != zc::none ||
              call.witnesses != zc::none || call.raises != zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build one operand: a parameter operand resolves to a parameter
          // reference; a literal operand consumes its checked literal fact. Both
          // operands must share the derived operand type.
          bool operandRejected = false;
          auto buildOperand =
              [&](ast::NodeId operandNode, bool isLiteral,
                  const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
            if (!isLiteral) {
              auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
              if (parameter == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              identity::CallableParameterId handle;
              ZC_IF_SOME(value, parameter) { handle = value; }
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              zc::Maybe<PendingConditionalArm> built;
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& parameterCandidate : parameters) {
                  if (parameterCandidate.key == entry.key() &&
                      parameterCandidate.type == operandType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  operandRejected = true;
                } else {
                  auto reference =
                      HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                      HirValueCategory::Place, operandSpan.clone()};
                  built = PendingConditionalArm{zc::none, zc::mv(reference), operandType,
                                                operandSpan.clone()};
                }
              }
              return built;
            }
            auto literalIndex = factIndex(facts.literals(), operandNode);
            if (literalIndex == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            return PendingConditionalArm{literalFact.literal.clone(), zc::none, operandType,
                                         operandSpan.clone()};
          };
          auto leftOperand = buildOperand(shape.conditionLeft, shape.conditionLeftIsLiteral,
                                          ZC_ASSERT_NONNULL(leftSpan));
          auto rightOperand = buildOperand(shape.conditionRight, shape.conditionRightIsLiteral,
                                           ZC_ASSERT_NONNULL(rightSpan));
          if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          pendingCondition.equality = PendingEqualityCondition{
              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
              zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
              operandType,
              conditionType,
              selected.get<checker::checked::PrimitiveCallable>().operation,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
        } else {
          auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.condition);
          if (conditionParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          identity::CallableParameterId conditionHandle;
          ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
          auto conditionAuthority = registries.callableParameter(conditionHandle);
          if (conditionAuthority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, conditionAuthority) {
            pendingCondition.parameter = HirParameterReferenceExpression{
                HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
                ZC_ASSERT_NONNULL(conditionSpan).clone()};
          }
        }
        // Build one conditional arm from its return-value node. A scalar-literal
        // arm carries the checked literal fact; a bare-identifier arm resolves to
        // a function parameter reference (no literal fact is required for it).
        bool armRejected = false;
        auto buildArm =
            [&](ast::NodeId armNode, identity::SemanticTypeId armType,
                const identity::SourceSpan& armSpan) -> zc::Maybe<PendingConditionalArm> {
          if (tree.node(armNode).kind == ast::SyntaxKind::IdentExpr) {
            auto parameter = resolvedCallableParameter(bound.bindings(), armNode);
            if (parameter == zc::none) {
              armRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              armRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() && parameterCandidate.type == armType) {
                  matches = true;
                }
              }
              if (!matches) {
                armRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), armType,
                                                    HirValueCategory::Place, armSpan.clone()};
                built =
                    PendingConditionalArm{zc::none, zc::mv(reference), armType, armSpan.clone()};
              }
            }
            return built;
          }
          auto literalIndex = factIndex(facts.literals(), armNode);
          if (literalIndex == zc::none) {
            armRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{literalFact.literal.clone(), zc::none, armType,
                                       armSpan.clone()};
        };
        auto thenArm = buildArm(shape.thenReturnValue, thenType, ZC_ASSERT_NONNULL(thenSpan));
        auto elseArm = buildArm(shape.elseReturnValue, elseType, ZC_ASSERT_NONNULL(elseSpan));
        if (armRejected || thenArm == zc::none || elseArm == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        {
          auto conditionalReturn =
              PendingConditionalReturn{zc::mv(pendingCondition), zc::mv(ZC_ASSERT_NONNULL(thenArm)),
                                       zc::mv(ZC_ASSERT_NONNULL(elseArm)), valueSpanValue.clone()};
          pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                          callable.success,
                                                          zc::mv(parameters),
                                                          zc::mv(visibilityValue),
                                                          linkageValue,
                                                          definition.source.clone(),
                                                          bodySpanValue.clone(),
                                                          returnSpanValue.clone(),
                                                          valueSpanValue.clone(),
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          {},
                                                          {},
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::mv(orderingKey),
                                                          zc::mv(conditionalReturn),
                                                          zc::none,
                                                          zc::none,
                                                          zc::none});
        }
        continue;
      }
      if (shape.isLoop) {
        // Loop shape: a `while` with a bool parameter condition and empty body,
        // followed by a scalar return.
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto returnTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto returnLiteralIndex = factIndex(facts.literals(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        if (conditionTypeIndex == zc::none || returnTypeIndex == zc::none ||
            returnLiteralIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || conditionSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto functionVisibility = visibility(root.visibility);
        auto functionLinkage = linkage(callable);
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.sourceModule != module || root.canonicalDefinition != definition.definition ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        size_t returnTypeSlot = 0;
        size_t returnLiteralSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        ZC_IF_SOME(index, returnLiteralIndex) { returnLiteralSlot = index; }
        const auto returnType = facts.nodeTypes().entries()[returnTypeSlot].value;
        const auto& returnLiteral = facts.literals().entries()[returnLiteralSlot].value;
        if (returnType != callable.success) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.loopCondition);
        if (conditionParameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        ZC_IF_SOME(entry, conditionAuthority) {
          auto conditionRef = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
          auto loopReturn =
              PendingLoopReturn{zc::mv(conditionRef), returnLiteral.literal.clone(), returnType,
                                ZC_ASSERT_NONNULL(loopSpan).clone(), valueSpanValue.clone()};
          pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                          callable.success,
                                                          zc::mv(parameters),
                                                          zc::mv(visibilityValue),
                                                          linkageValue,
                                                          definition.source.clone(),
                                                          bodySpanValue.clone(),
                                                          returnSpanValue.clone(),
                                                          valueSpanValue.clone(),
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          {},
                                                          {},
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::none,
                                                          zc::mv(orderingKey),
                                                          zc::none,
                                                          zc::mv(loopReturn),
                                                          zc::none,
                                                          zc::none});
        }
        continue;
      }
      if (shape.returnsComparison) {
        // Comparison-return shape: `return <a CMP b>`. The comparison result is
        // the function's bool return value. This consumes the same checked
        // relational call fact as the equality-conditional condition, but the
        // result flows into the Return terminator rather than a SwitchInt.
        auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
        auto leftTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonLeft);
        auto rightTypeIndex = factIndex(facts.nodeTypes(), shape.comparisonRight);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
        auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
        auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
        auto leftSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonLeft).range);
        auto rightSpan = bound.parsedModule().spanFor(tree.node(shape.comparisonRight).range);
        if (nodeTypeIndex == zc::none || leftTypeIndex == zc::none || rightTypeIndex == zc::none ||
            callIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
            valueSpan == zc::none || leftSpan == zc::none || rightSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& signature = signatures.definitions[signatureSlot];
        const auto& root = signatures.roots[rootSlot];
        if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
            !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& callable =
            signature.payload.variant().get<checker::signature::CallableSignature>();
        auto functionVisibility = visibility(root.visibility);
        auto functionLinkage = linkage(callable);
        size_t nodeTypeSlot = 0;
        ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
        const auto resultType = facts.nodeTypes().entries()[nodeTypeSlot].value;
        if (functionVisibility == zc::none || functionLinkage == zc::none ||
            signature.definitionKind != identity::DefinitionKind::Function ||
            root.sourceModule != module || root.canonicalDefinition != definition.definition ||
            callable.receiver != zc::none || callable.raises != zc::none ||
            callable.success != resultType ||
            !sameSpan(signature.declarationSpan, definition.source)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const ast::NodeId parameterListNode(
            tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
        if (!tree.contains(parameterListNode) ||
            tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        const auto& parameterList = tree.node(parameterListNode);
        const ast::NodeList parameterNodes{
            parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
            parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
        if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        zc::Vector<HirParameter> parameters(callable.parameters.size());
        for (size_t index = 0; index < callable.parameters.size(); ++index) {
          const auto parameterNode = tree.list(parameterNodes)[index];
          const auto& parameter = callable.parameters[index];
          if (!tree.contains(parameterNode) ||
              tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
              parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
          if (parameterSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(span, parameterSpan) {
            parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
          }
        }
        if (!typeExists(callable.success, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        size_t leftTypeSlot = 0;
        size_t rightTypeSlot = 0;
        size_t callSlot = 0;
        ZC_IF_SOME(index, leftTypeIndex) { leftTypeSlot = index; }
        ZC_IF_SOME(index, rightTypeIndex) { rightTypeSlot = index; }
        ZC_IF_SOME(index, callIndex) { callSlot = index; }
        const auto operandType = facts.nodeTypes().entries()[leftTypeSlot].value;
        const auto rightType = facts.nodeTypes().entries()[rightTypeSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        // The call fact must match the primitive-binary contract exactly for one
        // of the supported operators. A comparison produces the bool return type;
        // an arithmetic/bitwise operator produces the operand type, so for
        // arithmetic the function return type (resultType) equals operandType.
        const auto selectedOperation =
            selected.is<checker::checked::PrimitiveCallable>()
                ? zc::Maybe<checker::PrimitiveOperation>(
                      selected.get<checker::checked::PrimitiveCallable>().operation)
                : zc::Maybe<checker::PrimitiveOperation>(zc::none);
        bool operationSupported = false;
        ZC_IF_SOME(op, selectedOperation) {
          operationSupported = isScalarComparisonOperation(op) ||
                               (isScalarArithmeticOperation(op) && resultType == operandType);
        }
        if (operandType != rightType || callFact.node != shape.value || !operationSupported ||
            call.calleeType != operandType || call.receiver != zc::none ||
            call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
            call.arguments.size() != 2 || call.arguments[0].sourceNode != shape.comparisonLeft ||
            call.arguments[0].sourceType != operandType ||
            call.arguments[1].sourceNode != shape.comparisonRight ||
            call.arguments[1].sourceType != operandType || call.successType != resultType ||
            call.resultType != resultType || call.substitutions != zc::none ||
            call.witnesses != zc::none || call.raises != zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        HirVisibility visibilityValue = HirVisibility::external();
        HirLinkage linkageValue = HirLinkage::Internal;
        ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
        ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
        identity::SourceSpan bodySpanValue = definition.source.clone();
        identity::SourceSpan returnSpanValue = definition.source.clone();
        identity::SourceSpan valueSpanValue = definition.source.clone();
        ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
        ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
        ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
        zc::Array<uint8_t> orderingKey = definition.key.encode();
        // Build one comparison operand: a parameter operand resolves to a
        // parameter reference; a literal operand consumes its checked literal
        // fact. Both operands share the derived operand type.
        bool operandRejected = false;
        auto buildOperand =
            [&](ast::NodeId operandNode, bool isLiteral,
                const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
          if (!isLiteral) {
            auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
            if (parameter == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            identity::CallableParameterId handle;
            ZC_IF_SOME(value, parameter) { handle = value; }
            auto authority = registries.callableParameter(handle);
            if (authority == zc::none) {
              operandRejected = true;
              return zc::none;
            }
            zc::Maybe<PendingConditionalArm> built;
            ZC_IF_SOME(entry, authority) {
              bool matches = false;
              for (const auto& parameterCandidate : parameters) {
                if (parameterCandidate.key == entry.key() &&
                    parameterCandidate.type == operandType) {
                  matches = true;
                }
              }
              if (!matches) {
                operandRejected = true;
              } else {
                auto reference =
                    HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), operandType,
                                                    HirValueCategory::Place, operandSpan.clone()};
                built = PendingConditionalArm{zc::none, zc::mv(reference), operandType,
                                              operandSpan.clone()};
              }
            }
            return built;
          }
          auto literalIndex = factIndex(facts.literals(), operandNode);
          if (literalIndex == zc::none) {
            operandRejected = true;
            return zc::none;
          }
          size_t literalSlot = 0;
          ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
          const auto& literalFact = facts.literals().entries()[literalSlot].value;
          return PendingConditionalArm{literalFact.literal.clone(), zc::none, operandType,
                                       operandSpan.clone()};
        };
        auto leftOperand = buildOperand(shape.comparisonLeft, shape.comparisonLeftIsLiteral,
                                        ZC_ASSERT_NONNULL(leftSpan));
        auto rightOperand = buildOperand(shape.comparisonRight, shape.comparisonRightIsLiteral,
                                         ZC_ASSERT_NONNULL(rightSpan));
        if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto comparisonReturn =
            PendingEqualityCondition{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                     zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                     operandType,
                                     resultType,
                                     selected.get<checker::checked::PrimitiveCallable>().operation,
                                     ZC_ASSERT_NONNULL(valueSpan).clone()};
        pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                        callable.success,
                                                        zc::mv(parameters),
                                                        zc::mv(visibilityValue),
                                                        linkageValue,
                                                        definition.source.clone(),
                                                        bodySpanValue.clone(),
                                                        returnSpanValue.clone(),
                                                        valueSpanValue.clone(),
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        {},
                                                        {},
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(orderingKey),
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(comparisonReturn),
                                                        zc::none});
        continue;
      }
      auto nodeTypeIndex = factIndex(facts.nodeTypes(), shape.value);
      auto bodySpan = bound.parsedModule().spanFor(tree.node(shape.body).range);
      auto returnSpan = bound.parsedModule().spanFor(tree.node(shape.returnStatement).range);
      auto valueSpan = bound.parsedModule().spanFor(tree.node(shape.value).range);
      if (nodeTypeIndex == zc::none || bodySpan == zc::none || returnSpan == zc::none ||
          valueSpan == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      zc::Maybe<identity::SourceSpan> unsafeBlockSpan;
      ZC_IF_SOME(unsafeNode, shape.unsafeBlock) {
        auto unsafeSpan = bound.parsedModule().spanFor(tree.node(unsafeNode).range);
        if (unsafeSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, unsafeSpan) { unsafeBlockSpan = span.clone(); }
      }
      size_t nodeTypeSlot = 0;
      ZC_IF_SOME(index, nodeTypeIndex) { nodeTypeSlot = index; }
      const auto& signature = signatures.definitions[signatureSlot];
      const auto& root = signatures.roots[rootSlot];
      const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
      if (!signature.payload.variant().is<checker::signature::CallableSignature>() ||
          !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& callable =
          signature.payload.variant().get<checker::signature::CallableSignature>();
      auto functionVisibility = visibility(root.visibility);
      auto functionLinkage = linkage(callable);
      if (functionVisibility == zc::none || functionLinkage == zc::none ||
          signature.definitionKind != identity::DefinitionKind::Function ||
          root.sourceModule != module || root.canonicalDefinition != definition.definition ||
          callable.receiver != zc::none || callable.raises != zc::none ||
          callable.success != nodeType.value ||
          !sameSpan(signature.declarationSpan, definition.source)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const ast::NodeId parameterListNode(
          tree.node(definition.node).payload.words[ast::kFunctionDeclParamsIdWord]);
      if (!tree.contains(parameterListNode) ||
          tree.node(parameterListNode).kind != ast::SyntaxKind::FunctionParameterList) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      const auto& parameterList = tree.node(parameterListNode);
      const ast::NodeList parameterNodes{
          parameterList.payload.words[ast::kFunctionParameterListParamsFirstWord],
          parameterList.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      if (!tree.contains(parameterNodes) || parameterNodes.size != callable.parameters.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      zc::Vector<HirParameter> parameters(callable.parameters.size());
      for (size_t index = 0; index < callable.parameters.size(); ++index) {
        const auto parameterNode = tree.list(parameterNodes)[index];
        const auto& parameter = callable.parameters[index];
        if (!tree.contains(parameterNode) ||
            tree.node(parameterNode).kind != ast::SyntaxKind::FunctionParameterDecl ||
            parameter.hasDefault || !typeExists(parameter.type, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        auto parameterSpan = bound.parsedModule().spanFor(tree.node(parameterNode).range);
        if (parameterSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(span, parameterSpan) {
          parameters.add(HirParameter{parameter.parameter.clone(), parameter.type, span.clone()});
        }
      }
      if (!typeExists(callable.success, checkedModule.semanticTypes())) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      HirVisibility visibilityValue = HirVisibility::external();
      HirLinkage linkageValue = HirLinkage::Internal;
      identity::SourceSpan bodySpanValue = definition.source.clone();
      identity::SourceSpan returnSpanValue = definition.source.clone();
      identity::SourceSpan valueSpanValue = definition.source.clone();
      zc::Array<uint8_t> orderingKey;
      ZC_IF_SOME(value, functionVisibility) { visibilityValue = zc::mv(value); }
      ZC_IF_SOME(value, functionLinkage) { linkageValue = value; }
      ZC_IF_SOME(value, bodySpan) { bodySpanValue = value.clone(); }
      ZC_IF_SOME(value, returnSpan) { returnSpanValue = value.clone(); }
      ZC_IF_SOME(value, valueSpan) { valueSpanValue = value.clone(); }
      orderingKey = definition.key.encode();
      zc::Maybe<checker::checked::CanonicalConstValue> literal;
      zc::Maybe<HirDirectCallExpression> call;
      zc::Maybe<HirReceiverCallExpression> receiverCall;
      zc::Maybe<HirLocalBinding> local;
      zc::Maybe<HirNominalAggregateExpression> aggregate;
      zc::Vector<HirLocalWriteStatement> localWrites;
      zc::Vector<PendingLocalWriteValue> localWriteValues;
      zc::Maybe<HirLocalReferenceExpression> localReference;
      zc::Maybe<HirLocalFieldProjectionExpression> localFieldProjection;
      zc::Maybe<HirParameterReferenceExpression> parameterReference;
      zc::Maybe<HirParameterIndexExpression> parameterIndex;
      zc::Maybe<HirParameterReborrowExpression> parameterReborrow;
      zc::Maybe<HirLocalBorrowExpression> localBorrow;
      if (shape.isSequentialLocalReturn) {
        auto sequentialShapeMaybe = sequentialLocalShape(tree, shape.body);
        if (sequentialShapeMaybe == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        SequentialLocalShape sequentialShape{};
        ZC_IF_SOME(value, sequentialShapeMaybe) { sequentialShape = zc::mv(value); }
        const auto returnTypeIndex = factIndex(facts.nodeTypes(), sequentialShape.returnValue);
        if (returnTypeIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t returnTypeSlot = 0;
        ZC_IF_SOME(index, returnTypeIndex) { returnTypeSlot = index; }
        const auto sequentialType = facts.nodeTypes().entries()[returnTypeSlot].value;
        if (sequentialType != callable.success ||
            !typeExists(sequentialType, checkedModule.semanticTypes())) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        zc::Vector<PendingSequentialBinding> pendingBindings;
        zc::Vector<binder::OwnerLocalBindingId> localBindingIds;
        bool rejected = false;
        for (size_t bindingIndex = 0; bindingIndex < sequentialShape.bindings.size();
             ++bindingIndex) {
          const auto& binding = sequentialShape.bindings[bindingIndex];
          auto ownerBinding =
              ownerLocalBindingForPattern(bound.definitions(), binding.pattern, tree);
          auto patternSpan = bound.parsedModule().spanFor(tree.node(binding.pattern).range);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(binding.initializer).range);
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), binding.initializer);
          if (ownerBinding == zc::none || patternSpan == zc::none || initializerSpan == zc::none ||
              initializerTypeIndex == zc::none ||
              !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(ownerBinding),
                                 binding.pattern, tree)) {
            rejected = true;
            break;
          }
          // Every binding shares the function result type in this slice.
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          // Each leading local keeps its own declared type (the initializer's
          // node-type fact, which the checker has already verified matches the
          // binding pattern's annotation); only the returned local must match
          // the function result type, checked separately above.
          const identity::SemanticTypeId bindingType =
              facts.nodeTypes().entries()[initializerTypeSlot].value;
          if (!typeExists(bindingType, checkedModule.semanticTypes())) {
            rejected = true;
            break;
          }
          // Locals must be distinct bindings.
          for (const auto existing : localBindingIds) {
            if (existing == ZC_ASSERT_NONNULL(ownerBinding)) rejected = true;
          }
          if (rejected) break;
          localBindingIds.add(ZC_ASSERT_NONNULL(ownerBinding));
          zc::Maybe<checker::checked::CanonicalConstValue> bindingLiteral;
          zc::Maybe<HirNominalAggregateExpression> bindingAggregate;
          zc::Maybe<identity::CallableParameterKey> bindingParameter;
          zc::Maybe<checker::PrimitiveOperation> bindingOperation;
          identity::SemanticTypeId bindingOperandType = bindingType;
          zc::Maybe<PendingSequentialBinaryOperand> bindingLeftOperand;
          zc::Maybe<PendingSequentialBinaryOperand> bindingRightOperand;
          if (binding.initializerKind == SequentialInitializerKind::Literal) {
            auto literalIndex = factIndex(facts.literals(), binding.initializer);
            if (literalIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& literalFact = facts.literals().entries()[literalSlot].value;
            if (literalFact.type != bindingType ||
                !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            bindingLiteral = literalFact.literal.clone();
          } else if (binding.initializerKind == SequentialInitializerKind::Aggregate) {
            auto aggregateIndex = factIndex(facts.aggregates(), binding.initializer);
            if (aggregateIndex == zc::none) {
              rejected = true;
              break;
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != binding.initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != bindingType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              rejected = true;
              break;
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                rejected = true;
                break;
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                rejected = true;
                break;
              }
              size_t elementSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { elementSlot = index; }
              const auto& literalFact = facts.literals().entries()[elementSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                rejected = true;
                break;
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            if (rejected) break;
            bindingAggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                bindingType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
            // A reference to an earlier local must resolve to that owner binding.
            auto referenceBinding = resolvedOwnerLocal(bound.bindings(), binding.initializer);
            if (referenceBinding == zc::none ||
                ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[binding.referencedLocal]) {
              rejected = true;
              break;
            }
          } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
            // A primitive binary initializer: validate its checked call fact and
            // resolve each operand to a literal, a parameter, or an earlier local.
            auto callIndex = factIndex(facts.calls(), binding.initializer);
            if (callIndex == zc::none || binding.leftOperand == zc::none ||
                binding.rightOperand == zc::none) {
              rejected = true;
              break;
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              rejected = true;
              break;
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const bool comparison = isScalarComparisonOperation(operation);
            const bool arithmetic = isScalarArithmeticOperation(operation);
            // The operand type is the shared argument type; a comparison yields
            // a bool result while an arithmetic operator yields its operand
            // type. The binding (result) type is the local's own declared type.
            const auto binaryOperandType =
                call.arguments.size() == 2 ? call.arguments[0].sourceType : bindingType;
            const bool operationSupported =
                comparison || (arithmetic && bindingType == binaryOperandType);
            const ast::NodeId binaryLeft(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRight(
                tree.node(binding.initializer).payload.words[ast::kBinaryExprRhsWord]);
            if (!operationSupported || callFact.node != binding.initializer ||
                call.calleeType != binaryOperandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                call.arguments[0].sourceType != binaryOperandType ||
                call.arguments[1].sourceNode != binaryRight ||
                call.arguments[1].sourceType != binaryOperandType ||
                call.successType != bindingType || call.resultType != bindingType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              rejected = true;
              break;
            }
            // Resolve each classified operand into a materializable operand. A
            // leaf resolver builds a literal/parameter/local operand; the operand
            // resolver additionally handles a nested one-level binary by
            // validating its own checked call fact keyed on the operand node.
            auto resolveLeaf = [&](const SequentialBinaryLeafOperand& leaf,
                                   identity::SemanticTypeId leafType)
                -> zc::Maybe<PendingSequentialBinaryLeafOperand> {
              auto leafSpan = bound.parsedModule().spanFor(tree.node(leaf.node).range);
              if (leafSpan == zc::none) return zc::none;
              if (leaf.kind == SequentialBinaryOperandKind::Literal) {
                auto leafLiteral = factIndex(facts.literals(), leaf.node);
                if (leafLiteral == zc::none) return zc::none;
                size_t leafLiteralSlot = 0;
                ZC_IF_SOME(index, leafLiteral) { leafLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[leafLiteralSlot].value;
                if (literalFact.type != leafType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{SequentialBinaryOperandKind::Literal,
                                                          leafType,
                                                          ZC_ASSERT_NONNULL(leafSpan).clone(),
                                                          zc::mv(literalValue),
                                                          zc::mv(noParameter),
                                                          0};
              }
              if (leaf.kind == SequentialBinaryOperandKind::LocalReference) {
                if (leaf.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), leaf.node);
                if (referenceBinding == zc::none ||
                    ZC_ASSERT_NONNULL(referenceBinding) != localBindingIds[leaf.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryLeafOperand{
                    SequentialBinaryOperandKind::LocalReference,
                    leafType,
                    ZC_ASSERT_NONNULL(leafSpan).clone(),
                    zc::mv(noLiteral),
                    zc::mv(noParameter),
                    leaf.referencedLocal};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), leaf.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == leafType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryLeafOperand{
                  SequentialBinaryOperandKind::ParameterReference,
                  leafType,
                  ZC_ASSERT_NONNULL(leafSpan).clone(),
                  zc::mv(noLiteral),
                  zc::mv(resolvedKey),
                  0};
            };
            auto resolveOperand = [&](const SequentialBinaryOperand& operand)
                -> zc::Maybe<PendingSequentialBinaryOperand> {
              auto operandSpan = bound.parsedModule().spanFor(tree.node(operand.node).range);
              if (operandSpan == zc::none) return zc::none;
              zc::Maybe<checker::PrimitiveOperation> noNestedOperation;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedLeft;
              zc::Maybe<PendingSequentialBinaryLeafOperand> noNestedRight;
              if (operand.kind == SequentialBinaryOperandKind::NestedBinary) {
                // A nested one-level binary carries its own checked call fact keyed
                // on the operand node; validate it as a same-typed arithmetic (or
                // comparison agreeing on type) binary of two same-typed leaves.
                auto nestedCallIndex = factIndex(facts.calls(), operand.node);
                if (nestedCallIndex == zc::none || operand.nestedLeft == zc::none ||
                    operand.nestedRight == zc::none || operand.nestedOperation == zc::none) {
                  return zc::none;
                }
                size_t nestedCallSlot = 0;
                ZC_IF_SOME(index, nestedCallIndex) { nestedCallSlot = index; }
                const auto& nestedFact = facts.calls().entries()[nestedCallSlot].value;
                const auto& nestedCall = nestedFact.invocation;
                const auto& nestedSelected = nestedCall.selected.variant();
                if (!nestedSelected.is<checker::checked::PrimitiveCallable>()) return zc::none;
                const auto nestedOp =
                    nestedSelected.get<checker::checked::PrimitiveCallable>().operation;
                const bool nestedComparison = isScalarComparisonOperation(nestedOp);
                const bool nestedArithmetic = isScalarArithmeticOperation(nestedOp);
                const auto nestedOperandType = nestedCall.arguments.size() == 2
                                                   ? nestedCall.arguments[0].sourceType
                                                   : binaryOperandType;
                // The nested result feeds the parent operand slot, so its result
                // type must equal the parent operand type; a comparison result
                // (bool) can only agree when the parent operand type is bool.
                const bool nestedSupported =
                    (nestedArithmetic && nestedOperandType == binaryOperandType) ||
                    (nestedComparison && binaryOperandType == nestedCall.resultType);
                const ast::NodeId nestedLeftNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprLhsWord]);
                const ast::NodeId nestedRightNode(
                    tree.node(operand.node).payload.words[ast::kBinaryExprRhsWord]);
                if (!nestedSupported || nestedOp != ZC_ASSERT_NONNULL(operand.nestedOperation) ||
                    nestedFact.node != operand.node || nestedCall.calleeType != nestedOperandType ||
                    nestedCall.receiver != zc::none || nestedCall.receiverMode != zc::none ||
                    nestedCall.receiverAdjustment != zc::none || nestedCall.arguments.size() != 2 ||
                    nestedCall.arguments[0].sourceNode != nestedLeftNode ||
                    nestedCall.arguments[0].sourceType != nestedOperandType ||
                    nestedCall.arguments[1].sourceNode != nestedRightNode ||
                    nestedCall.arguments[1].sourceType != nestedOperandType ||
                    nestedCall.resultType != binaryOperandType ||
                    nestedCall.substitutions != zc::none || nestedCall.witnesses != zc::none ||
                    nestedCall.raises != zc::none) {
                  return zc::none;
                }
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedLeft;
                zc::Maybe<PendingSequentialBinaryLeafOperand> resolvedNestedRight;
                ZC_IF_SOME(leaf, operand.nestedLeft) {
                  resolvedNestedLeft = resolveLeaf(leaf, nestedOperandType);
                }
                ZC_IF_SOME(leaf, operand.nestedRight) {
                  resolvedNestedRight = resolveLeaf(leaf, nestedOperandType);
                }
                if (resolvedNestedLeft == zc::none || resolvedNestedRight == zc::none) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                zc::Maybe<checker::PrimitiveOperation> nestedOperation = nestedOp;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::NestedBinary,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(nestedOperation),
                                                      zc::mv(resolvedNestedLeft),
                                                      zc::mv(resolvedNestedRight)};
              }
              if (operand.kind == SequentialBinaryOperandKind::Literal) {
                auto operandLiteral = factIndex(facts.literals(), operand.node);
                if (operandLiteral == zc::none) return zc::none;
                size_t operandLiteralSlot = 0;
                ZC_IF_SOME(index, operandLiteral) { operandLiteralSlot = index; }
                const auto& literalFact = facts.literals().entries()[operandLiteralSlot].value;
                if (literalFact.type != binaryOperandType) return zc::none;
                zc::Maybe<checker::checked::CanonicalConstValue> literalValue =
                    literalFact.literal.clone();
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::Literal,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(literalValue),
                                                      zc::mv(noParameter),
                                                      0,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight)};
              }
              if (operand.kind == SequentialBinaryOperandKind::LocalReference) {
                if (operand.referencedLocal >= localBindingIds.size()) return zc::none;
                auto referenceBinding = resolvedOwnerLocal(bound.bindings(), operand.node);
                if (referenceBinding == zc::none || ZC_ASSERT_NONNULL(referenceBinding) !=
                                                        localBindingIds[operand.referencedLocal]) {
                  return zc::none;
                }
                zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
                zc::Maybe<identity::CallableParameterKey> noParameter;
                return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::LocalReference,
                                                      binaryOperandType,
                                                      ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                      zc::mv(noLiteral),
                                                      zc::mv(noParameter),
                                                      operand.referencedLocal,
                                                      zc::mv(noNestedOperation),
                                                      zc::mv(noNestedLeft),
                                                      zc::mv(noNestedRight)};
              }
              auto parameterHandle = resolvedCallableParameter(bound.bindings(), operand.node);
              if (parameterHandle == zc::none) return zc::none;
              zc::Maybe<identity::CallableParameterKey> resolvedKey;
              ZC_IF_SOME(handle, parameterHandle) {
                auto authority = registries.callableParameter(handle);
                ZC_IF_SOME(entry, authority) {
                  for (const auto& parameter : parameters) {
                    if (parameter.key == entry.key() && parameter.type == binaryOperandType) {
                      resolvedKey = entry.key().clone();
                    }
                  }
                }
              }
              if (resolvedKey == zc::none) return zc::none;
              zc::Maybe<checker::checked::CanonicalConstValue> noLiteral;
              return PendingSequentialBinaryOperand{SequentialBinaryOperandKind::ParameterReference,
                                                    binaryOperandType,
                                                    ZC_ASSERT_NONNULL(operandSpan).clone(),
                                                    zc::mv(noLiteral),
                                                    zc::mv(resolvedKey),
                                                    0,
                                                    zc::mv(noNestedOperation),
                                                    zc::mv(noNestedLeft),
                                                    zc::mv(noNestedRight)};
            };
            ZC_IF_SOME(leftClassified, binding.leftOperand) {
              ZC_IF_SOME(rightClassified, binding.rightOperand) {
                auto resolvedLeft = resolveOperand(leftClassified);
                auto resolvedRight = resolveOperand(rightClassified);
                if (resolvedLeft == zc::none || resolvedRight == zc::none) {
                  rejected = true;
                } else {
                  bindingOperation = operation;
                  bindingOperandType = binaryOperandType;
                  bindingLeftOperand = zc::mv(resolvedLeft);
                  bindingRightOperand = zc::mv(resolvedRight);
                }
              }
            }
            if (rejected) break;
          } else {
            // A reference to a parameter must resolve to a declared parameter.
            auto parameterHandle = resolvedCallableParameter(bound.bindings(), binding.initializer);
            if (parameterHandle == zc::none) {
              rejected = true;
              break;
            }
            zc::Maybe<identity::CallableParameterKey> resolvedKey;
            ZC_IF_SOME(handle, parameterHandle) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& parameter : parameters) {
                  if (parameter.key == entry.key() && parameter.type == bindingType) {
                    resolvedKey = entry.key().clone();
                  }
                }
              }
            }
            if (resolvedKey == zc::none) {
              rejected = true;
              break;
            }
            bindingParameter = zc::mv(resolvedKey);
          }
          pendingBindings.add(PendingSequentialBinding{
              bindingType, ZC_ASSERT_NONNULL(patternSpan).clone(),
              ZC_ASSERT_NONNULL(initializerSpan).clone(), binding.initializerKind,
              zc::mv(bindingLiteral), zc::mv(bindingAggregate), zc::mv(bindingParameter),
              binding.referencedLocal, zc::mv(bindingOperation), bindingOperandType,
              zc::mv(bindingLeftOperand), zc::mv(bindingRightOperand)});
        }
        if (rejected) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        // The returned value names a parameter or one of the declared locals.
        zc::Maybe<identity::CallableParameterKey> returnParameter;
        size_t returnLocal = 0;
        ZC_IF_SOME(index, sequentialShape.returnsLocal) { returnLocal = index; }
        if (sequentialShape.returnsLocal == zc::none) {
          auto parameterHandle =
              resolvedCallableParameter(bound.bindings(), sequentialShape.returnValue);
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(handle, parameterHandle) {
            auto authority = registries.callableParameter(handle);
            ZC_IF_SOME(entry, authority) {
              for (const auto& parameter : parameters) {
                if (parameter.key == entry.key() && parameter.type == sequentialType) {
                  returnParameter = entry.key().clone();
                }
              }
            }
          }
          if (returnParameter == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
        }
        auto returnValueSpan =
            bound.parsedModule().spanFor(tree.node(sequentialShape.returnValue).range);
        if (returnValueSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        PendingSequentialLocalReturn sequential{zc::mv(pendingBindings), sequentialType,
                                                zc::mv(returnParameter), returnLocal,
                                                ZC_ASSERT_NONNULL(returnValueSpan).clone()};
        pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                        callable.success,
                                                        zc::mv(parameters),
                                                        zc::mv(visibilityValue),
                                                        linkageValue,
                                                        definition.source.clone(),
                                                        bodySpanValue.clone(),
                                                        returnSpanValue.clone(),
                                                        valueSpanValue.clone(),
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        {},
                                                        {},
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::mv(sequential),
                                                        zc::mv(unsafeBlockSpan),
                                                        zc::mv(orderingKey),
                                                        zc::none,
                                                        zc::none,
                                                        zc::none,
                                                        zc::none});
        continue;
      }
      if (shape.returnsLocal) {
        auto localBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
        auto patternSpan = bound.parsedModule().spanFor(tree.node(shape.localPattern).range);
        if (localBinding == zc::none || patternSpan == zc::none ||
            !ownerLocalMatches(bound.definitions(), ZC_ASSERT_NONNULL(localBinding),
                               shape.localPattern, tree)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::SemanticTypeId localType = nodeType.value;
        if (shape.returnsLocalBorrow) {
          auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
          if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
              !borrowTypeLookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>()) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = borrowTypeLookup.get<type::SemanticTypeLookup>()
                          .data()
                          .get<type::semantic::ReferenceTypeData>()
                          .referent;
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          if (memberIndex == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          if (member.node != shape.value || member.memberType != nodeType.value ||
              member.adjustment != zc::none ||
              !typeExists(member.receiverType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localType = member.receiverType;
        }
        zc::Maybe<HirNodeId> noInitializer;
        zc::Maybe<identity::SourceSpan> noInitializerSpan;
        local = HirLocalBinding{HirNodeId(),
                                HirLocalId(),
                                localType,
                                zc::mv(noInitializer),
                                ZC_ASSERT_NONNULL(patternSpan).clone(),
                                zc::mv(noInitializerSpan)};
        if (!shape.returnsLocalField && !shape.returnsLocalReborrow && !shape.returnsLocalBorrow) {
          localReference =
              HirLocalReferenceExpression{HirNodeId(), HirLocalId(), nodeType.value,
                                          HirValueCategory::Place, valueSpanValue.clone()};
        }
        ZC_IF_SOME(initializer, shape.localInitializer) {
          auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
          auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
          if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t initializerTypeSlot = 0;
          ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
          const auto& initializerType = facts.nodeTypes().entries()[initializerTypeSlot].value;
          if ((!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsLocalBorrow && initializerType != nodeType.value) ||
              (!shape.returnsLocalField && !shape.returnsReceiverCall &&
               !shape.returnsLocalBorrow && initializerType != callable.success) ||
              (shape.returnsLocalBorrow && initializerType != localType)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Maybe<HirNodeId> initializerNode;
          initializerNode = HirNodeId();
          zc::Maybe<identity::SourceSpan> initializerSource;
          ZC_IF_SOME(value, initializerSpan) { initializerSource = value.clone(); }
          local = HirLocalBinding{HirNodeId(),
                                  HirLocalId(),
                                  initializerType,
                                  zc::mv(initializerNode),
                                  ZC_ASSERT_NONNULL(patternSpan).clone(),
                                  zc::mv(initializerSource)};
          if (!shape.returnsLocalField && !shape.returnsLocalReborrow &&
              !shape.returnsLocalBorrow) {
            identity::SourceSpan referenceSpan = valueSpanValue.clone();
            if (shape.returnsReceiverCall) {
              const auto& sourceCall = tree.node(shape.value);
              const ast::NodeId calleeNode(
                  sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
              const ast::NodeId receiverNode(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
              auto receiverSpan = bound.parsedModule().spanFor(tree.node(receiverNode).range);
              if (receiverSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              referenceSpan = ZC_ASSERT_NONNULL(receiverSpan).clone();
            }
            localReference =
                HirLocalReferenceExpression{HirNodeId(), HirLocalId(), initializerType,
                                            HirValueCategory::Place, zc::mv(referenceSpan)};
          }
          if (isScalarLiteral(tree.node(initializer).kind)) {
            auto literalIndex = factIndex(facts.literals(), initializer);
            if (literalIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != initializerType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            literal = sourceLiteral.literal.clone();
          } else if (tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr) {
            auto aggregateIndex = factIndex(facts.aggregates(), initializer);
            if (aggregateIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t aggregateSlot = 0;
            ZC_IF_SOME(index, aggregateIndex) { aggregateSlot = index; }
            const auto& sourceAggregate = facts.aggregates().entries()[aggregateSlot].value;
            if (sourceAggregate.node != initializer ||
                !sourceAggregate.kind.variant().is<checker::checked::NominalAggregate>() ||
                sourceAggregate.resultType != initializerType ||
                !sameSpan(sourceAggregate.sourceSpan, ZC_ASSERT_NONNULL(initializerSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Vector<HirNominalAggregateElement> elements;
            for (const auto& sourceElement : sourceAggregate.elements) {
              if (sourceElement.field == zc::none ||
                  sourceElement.sourceType != sourceElement.destinationType ||
                  sourceElement.adjustment != zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              auto elementLiteral = factIndex(facts.literals(), sourceElement.sourceNode);
              auto elementSpan =
                  bound.parsedModule().spanFor(tree.node(sourceElement.sourceNode).range);
              if (elementLiteral == zc::none || elementSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(index, elementLiteral) { literalSlot = index; }
              const auto& literalFact = facts.literals().entries()[literalSlot].value;
              if (literalFact.type != sourceElement.destinationType ||
                  !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(elementSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(field, sourceElement.field) {
                elements.add(HirNominalAggregateElement{field, sourceElement.destinationType,
                                                        literalFact.literal.clone(),
                                                        ZC_ASSERT_NONNULL(elementSpan).clone()});
              }
            }
            aggregate = HirNominalAggregateExpression{
                HirNodeId(),
                sourceAggregate.kind.variant().get<checker::checked::NominalAggregate>().definition,
                initializerType,
                zc::mv(elements),
                HirValueCategory::Value,
                ZC_ASSERT_NONNULL(initializerSpan).clone()};
          } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
            auto parameter = resolvedCallableParameter(bound.bindings(), initializer);
            if (parameter == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == initializerType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                parameterReference = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), initializerType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(initializerSpan).clone()};
              }
            }
          }
        }
        if (shape.returnsLocalField) {
          auto memberIndex = factIndex(facts.members(), shape.value);
          auto placeIndex = factIndex(facts.places(), shape.value);
          if (memberIndex == zc::none || placeIndex == zc::none || local == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t memberSlot = 0;
          size_t placeSlot = 0;
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& place = facts.places().entries()[placeSlot].value;
          const auto& root = place.root.variant();
          if (member.node != shape.value || member.receiverType != ZC_ASSERT_NONNULL(local).type ||
              member.memberType != nodeType.value || member.adjustment != zc::none ||
              !root.is<checker::checked::OwnerLocalPlaceRoot>() ||
              root.get<checker::checked::OwnerLocalPlaceRoot>().binding !=
                  ZC_ASSERT_NONNULL(localBinding) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
              place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                  member.member ||
              place.type != nodeType.value || !place.movable) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          localFieldProjection = HirLocalFieldProjectionExpression{
              HirNodeId(),           HirLocalId(),      member.member,
              member.receiverType,   member.memberType, HirValueCategory::Place,
              valueSpanValue.clone()};
        }
        for (size_t writeIndex = 0; writeIndex < shape.localWrites.size; ++writeIndex) {
          auto writeStatement = statementItem(tree, tree.list(shape.localWrites)[writeIndex]);
          if (writeStatement == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ast::NodeId writeStatementNode;
          ZC_IF_SOME(value, writeStatement) { writeStatementNode = value; }
          const ast::NodeId write(
              tree.node(writeStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
          ast::NodeId target;
          ast::NodeId writeValue;
          target = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprLhsWord]);
          writeValue = ast::NodeId(tree.node(write).payload.words[ast::kAssignmentExprRhsWord]);
          auto assignmentTypeIndex = factIndex(facts.nodeTypes(), write);
          auto targetTypeIndex = factIndex(facts.nodeTypes(), target);
          auto valueTypeIndex = factIndex(facts.nodeTypes(), writeValue);
          auto assignmentSpan = bound.parsedModule().spanFor(tree.node(write).range);
          auto valueSpan = bound.parsedModule().spanFor(tree.node(writeValue).range);
          // A write value is a scalar literal, an identifier reference (a
          // parameter, resolved below), or a primitive binary operation. Only a
          // literal write consumes a checked literal fact; a reference or binary
          // write consumes none.
          const bool referenceValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
          const bool binaryWriteValue = tree.node(writeValue).kind == ast::SyntaxKind::BinaryExpr;
          auto literalIndex = (referenceValue || binaryWriteValue)
                                  ? zc::none
                                  : factIndex(facts.literals(), writeValue);
          ast::NodeId targetReference = target;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            targetReference =
                ast::NodeId(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
          }
          auto targetBinding = resolvedOwnerLocal(bound.bindings(), targetReference);
          auto returnBinding = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (assignmentTypeIndex == zc::none || targetTypeIndex == zc::none ||
              valueTypeIndex == zc::none ||
              (!referenceValue && !binaryWriteValue && literalIndex == zc::none) ||
              assignmentSpan == zc::none || valueSpan == zc::none || targetBinding == zc::none ||
              returnBinding == zc::none || targetBinding != returnBinding) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t assignmentSlot = 0;
          size_t targetSlot = 0;
          size_t valueSlot = 0;
          ZC_IF_SOME(index, assignmentTypeIndex) { assignmentSlot = index; }
          ZC_IF_SOME(index, targetTypeIndex) { targetSlot = index; }
          ZC_IF_SOME(index, valueTypeIndex) { valueSlot = index; }
          const auto& assignmentType = facts.nodeTypes().entries()[assignmentSlot].value;
          const auto& targetType = facts.nodeTypes().entries()[targetSlot].value;
          const auto& valueType = facts.nodeTypes().entries()[valueSlot].value;
          identity::SemanticTypeId writeType = targetType;
          zc::Maybe<identity::DefId> field;
          if (tree.node(target).kind == ast::SyntaxKind::MemberExpression) {
            auto memberIndex = factIndex(facts.members(), target);
            auto placeIndex = factIndex(facts.places(), target);
            if (memberIndex == zc::none || placeIndex == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t memberSlot = 0;
            size_t placeSlot = 0;
            ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
            ZC_IF_SOME(index, placeIndex) { placeSlot = index; }
            const auto& member = facts.members().entries()[memberSlot].value;
            const auto& place = facts.places().entries()[placeSlot].value;
            if (member.node != target || member.memberType != targetType || place.node != target ||
                place.type != targetType || !place.mutablePlace || place.projections.size() != 1 ||
                !place.projections[0].variant().is<checker::checked::FieldProjection>() ||
                place.projections[0].variant().get<checker::checked::FieldProjection>().field !=
                    member.member) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            field = member.member;
          }
          if (local == zc::none || assignmentType != writeType || targetType != writeType ||
              valueType != writeType) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          // Build the per-write value: a scalar literal consumes its checked
          // literal fact; a parameter reference resolves the parameter and
          // matches its type; a primitive binary validates its own checked call
          // fact (keyed on the write value node) and builds its two operands,
          // exactly like the primitive-binary initializer path.
          PendingLocalWriteValue writeValueRecord;
          if (binaryWriteValue) {
            if (field != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const ast::NodeId binaryLeft(
                tree.node(writeValue).payload.words[ast::kBinaryExprLhsWord]);
            const ast::NodeId binaryRight(
                tree.node(writeValue).payload.words[ast::kBinaryExprRhsWord]);
            auto callIndex = factIndex(facts.calls(), writeValue);
            auto leftSpan = bound.parsedModule().spanFor(tree.node(binaryLeft).range);
            auto rightSpan = bound.parsedModule().spanFor(tree.node(binaryRight).range);
            if (callIndex == zc::none || leftSpan == zc::none || rightSpan == zc::none ||
                !tree.contains(binaryLeft) || !tree.contains(binaryRight)) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t callSlot = 0;
            ZC_IF_SOME(index, callIndex) { callSlot = index; }
            const auto& callFact = facts.calls().entries()[callSlot].value;
            const auto& call = callFact.invocation;
            const auto& selected = call.selected.variant();
            if (!selected.is<checker::checked::PrimitiveCallable>()) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            const auto operation = selected.get<checker::checked::PrimitiveCallable>().operation;
            const bool comparison = isScalarComparisonOperation(operation);
            const bool arithmetic = isScalarArithmeticOperation(operation);
            // The operand type is the shared argument type; a comparison yields
            // the (bool) result while an arithmetic operator yields the operand
            // type. The write type is the target local type, so an arithmetic
            // write requires operandType == writeType.
            const auto binaryOperandType =
                call.arguments.size() == 2 ? call.arguments[0].sourceType : writeType;
            const bool operationSupported =
                comparison || (arithmetic && writeType == binaryOperandType);
            if (!operationSupported || callFact.node != writeValue ||
                call.calleeType != binaryOperandType || call.receiver != zc::none ||
                call.receiverMode != zc::none || call.receiverAdjustment != zc::none ||
                call.arguments.size() != 2 || call.arguments[0].sourceNode != binaryLeft ||
                call.arguments[0].sourceType != binaryOperandType ||
                call.arguments[1].sourceNode != binaryRight ||
                call.arguments[1].sourceType != binaryOperandType ||
                call.successType != writeType || call.resultType != writeType ||
                call.substitutions != zc::none || call.witnesses != zc::none ||
                call.raises != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            // Build one binary operand: a parameter operand resolves to a
            // parameter reference; a literal operand consumes its checked literal
            // fact. Both operands share the derived operand type; at least one is
            // a parameter (a literal-vs-literal binary is rejected at admission).
            bool operandRejected = false;
            auto buildWriteOperand =
                [&](ast::NodeId operandNode,
                    const identity::SourceSpan& operandSpan) -> zc::Maybe<PendingConditionalArm> {
              const bool operandIsLiteral = isScalarLiteral(tree.node(operandNode).kind);
              if (!operandIsLiteral) {
                auto parameter = resolvedCallableParameter(bound.bindings(), operandNode);
                if (parameter == zc::none) {
                  operandRejected = true;
                  return zc::none;
                }
                identity::CallableParameterId handle;
                ZC_IF_SOME(value, parameter) { handle = value; }
                auto authority = registries.callableParameter(handle);
                if (authority == zc::none) {
                  operandRejected = true;
                  return zc::none;
                }
                zc::Maybe<PendingConditionalArm> built;
                ZC_IF_SOME(entry, authority) {
                  bool matches = false;
                  for (const auto& parameterCandidate : parameters) {
                    if (parameterCandidate.key == entry.key() &&
                        parameterCandidate.type == binaryOperandType) {
                      matches = true;
                    }
                  }
                  if (!matches) {
                    operandRejected = true;
                  } else {
                    auto reference = HirParameterReferenceExpression{
                        HirNodeId(), entry.key().clone(), binaryOperandType,
                        HirValueCategory::Place, operandSpan.clone()};
                    built = PendingConditionalArm{zc::none, zc::mv(reference), binaryOperandType,
                                                  operandSpan.clone()};
                  }
                }
                return built;
              }
              auto operandLiteralIndex = factIndex(facts.literals(), operandNode);
              if (operandLiteralIndex == zc::none) {
                operandRejected = true;
                return zc::none;
              }
              size_t operandLiteralSlot = 0;
              ZC_IF_SOME(index, operandLiteralIndex) { operandLiteralSlot = index; }
              const auto& literalFact = facts.literals().entries()[operandLiteralSlot].value;
              if (literalFact.type != binaryOperandType) {
                operandRejected = true;
                return zc::none;
              }
              return PendingConditionalArm{literalFact.literal.clone(), zc::none, binaryOperandType,
                                           operandSpan.clone()};
            };
            auto leftOperand = buildWriteOperand(binaryLeft, ZC_ASSERT_NONNULL(leftSpan));
            auto rightOperand = buildWriteOperand(binaryRight, ZC_ASSERT_NONNULL(rightSpan));
            if (operandRejected || leftOperand == zc::none || rightOperand == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.binary =
                PendingLocalWriteBinary{zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                        zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                        binaryOperandType,
                                        writeType,
                                        operation,
                                        ZC_ASSERT_NONNULL(valueSpan).clone()};
          } else if (referenceValue) {
            auto parameter = resolvedCallableParameter(bound.bindings(), writeValue);
            if (parameter == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              if (authority == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              ZC_IF_SOME(entry, authority) {
                bool matches = false;
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == writeType) {
                    matches = true;
                  }
                }
                if (!matches) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                writeValueRecord.parameter = HirParameterReferenceExpression{
                    HirNodeId(), entry.key().clone(), writeType, HirValueCategory::Place,
                    ZC_ASSERT_NONNULL(valueSpan).clone()};
              }
            }
          } else {
            size_t literalSlot = 0;
            ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
            const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
            if (sourceLiteral.type != writeType ||
                !sameSpan(sourceLiteral.sourceSpan, ZC_ASSERT_NONNULL(valueSpan))) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            writeValueRecord.literal = sourceLiteral.literal.clone();
          }
          bool firstFieldWrite = false;
          ZC_IF_SOME(currentField, field) {
            firstFieldWrite = true;
            for (const auto& previous : localWrites) {
              ZC_IF_SOME(previousField, previous.field) {
                if (previousField == currentField) {
                  firstFieldWrite = false;
                  break;
                }
              }
            }
          }
          const auto kind =
              shape.localInitializer == zc::none
                  ? (field != zc::none ? (firstFieldWrite ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite)
                                       : (writeIndex == 0 ? HirLocalWriteKind::Initialize
                                                          : HirLocalWriteKind::Overwrite))
                  : HirLocalWriteKind::Overwrite;
          localWrites.add(HirLocalWriteStatement{
              HirNodeId(), HirLocalId(), zc::mv(field), writeType, HirNodeId(), kind,
              ZC_ASSERT_NONNULL(assignmentSpan).clone(), ZC_ASSERT_NONNULL(valueSpan).clone()});
          localWriteValues.add(zc::mv(writeValueRecord));
        }
      } else if (isScalarLiteral(tree.node(shape.value).kind)) {
        auto literalIndex = factIndex(facts.literals(), shape.value);
        if (literalIndex == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t literalSlot = 0;
        ZC_IF_SOME(index, literalIndex) { literalSlot = index; }
        const auto& sourceLiteral = facts.literals().entries()[literalSlot].value;
        if (sourceLiteral.type != nodeType.value ||
            !sameSpan(valueSpanValue, sourceLiteral.sourceSpan)) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        literal = sourceLiteral.literal.clone();
      } else if (tree.node(shape.value).kind == ast::SyntaxKind::IdentExpr) {
        auto parameter = resolvedCallableParameter(bound.bindings(), shape.value);
        if (parameter == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        ZC_IF_SOME(handle, parameter) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == nodeType.value) {
                matches = true;
              }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            parameterReference =
                HirParameterReferenceExpression{HirNodeId(), entry.key().clone(), nodeType.value,
                                                HirValueCategory::Place, valueSpanValue.clone()};
          }
        }
      } else if (tree.node(shape.value).kind == ast::SyntaxKind::IndexExpression) {
        const auto& sourceIndex = tree.node(shape.value);
        const ast::NodeId base(sourceIndex.payload.words[ast::kIndexExpressionObjectWord]);
        const ast::NodeId index(sourceIndex.payload.words[ast::kIndexExpressionIndexWord]);
        auto parameter = resolvedCallableParameter(bound.bindings(), base);
        auto baseType = factIndex(facts.nodeTypes(), base);
        auto indexType = factIndex(facts.nodeTypes(), index);
        auto indexLiteral = factIndex(facts.literals(), index);
        auto callIndex = factIndex(facts.calls(), shape.value);
        auto placeIndex = factIndex(facts.places(), shape.value);
        auto checkedIndex = factIndex(facts.indexes(), shape.value);
        auto marker = factIndex(facts.markerObligations(), shape.value);
        auto indexSpan = bound.parsedModule().spanFor(tree.node(index).range);
        if (!tree.contains(base) || !tree.contains(index) ||
            tree.node(base).kind != ast::SyntaxKind::IdentExpr ||
            tree.node(index).kind != ast::SyntaxKind::IntLiteral || parameter == zc::none ||
            baseType == zc::none || indexType == zc::none || indexLiteral == zc::none ||
            callIndex == zc::none || placeIndex == zc::none || checkedIndex == zc::none ||
            marker == zc::none || indexSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t baseTypeSlot = 0;
        size_t indexTypeSlot = 0;
        size_t literalSlot = 0;
        size_t callSlot = 0;
        size_t placeSlot = 0;
        size_t indexSlot = 0;
        size_t markerSlot = 0;
        ZC_IF_SOME(value, baseType) { baseTypeSlot = value; }
        ZC_IF_SOME(value, indexType) { indexTypeSlot = value; }
        ZC_IF_SOME(value, indexLiteral) { literalSlot = value; }
        ZC_IF_SOME(value, callIndex) { callSlot = value; }
        ZC_IF_SOME(value, placeIndex) { placeSlot = value; }
        ZC_IF_SOME(value, checkedIndex) { indexSlot = value; }
        ZC_IF_SOME(value, marker) { markerSlot = value; }
        auto authority = registries.callableParameter(ZC_ASSERT_NONNULL(parameter));
        if (authority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const auto& literalFact = facts.literals().entries()[literalSlot].value;
        const auto& callFact = facts.calls().entries()[callSlot].value;
        const auto& call = callFact.invocation;
        const auto& selected = call.selected.variant();
        const auto& place = facts.places().entries()[placeSlot].value;
        const auto& root = place.root.variant();
        const auto& indexFact = facts.indexes().entries()[indexSlot].value;
        const auto& markerFact = facts.markerObligations().entries()[markerSlot].value;
        ZC_IF_SOME(entry, authority) {
          bool matches = false;
          for (const auto& candidate : parameters) {
            if (candidate.key == entry.key() &&
                candidate.type == facts.nodeTypes().entries()[baseTypeSlot].value) {
              matches = true;
            }
          }
          if (!matches || literalFact.node != index ||
              literalFact.type != facts.nodeTypes().entries()[indexTypeSlot].value ||
              !sameSpan(literalFact.sourceSpan, ZC_ASSERT_NONNULL(indexSpan)) ||
              callFact.node != shape.value || !selected.is<checker::checked::PrimitiveCallable>() ||
              selected.get<checker::checked::PrimitiveCallable>().operation !=
                  checker::PrimitiveOperation::Index ||
              call.calleeType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              call.receiver == zc::none || call.receiverMode == zc::none ||
              call.receiverAdjustment == zc::none || call.arguments.size() != 1 ||
              call.successType != nodeType.value || call.resultType != nodeType.value ||
              call.substitutions != zc::none || call.witnesses != zc::none ||
              call.raises != zc::none || !root.is<checker::checked::CallableParameterPlaceRoot>() ||
              root.get<checker::checked::CallableParameterPlaceRoot>().parameter !=
                  ZC_ASSERT_NONNULL(parameter) ||
              place.projections.size() != 1 ||
              !place.projections[0].variant().is<checker::checked::IndexProjection>() ||
              place.projections[0].variant().get<checker::checked::IndexProjection>().index !=
                  index ||
              place.type != nodeType.value || place.mutablePlace || place.movable ||
              indexFact.node != shape.value ||
              indexFact.collectionType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              indexFact.indexType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              indexFact.elementType != nodeType.value ||
              indexFact.accessMode != checker::checked::IndexAccessMode::Read ||
              indexFact.accessResultType != nodeType.value || markerFact.node != shape.value ||
              markerFact.subject != nodeType.value ||
              markerFact.polarity != checker::checked::Polarity::Positive) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          const auto& receiver = ZC_ASSERT_NONNULL(call.receiver);
          const auto& adjustment = ZC_ASSERT_NONNULL(call.receiverAdjustment);
          const auto& argument = call.arguments[0];
          const auto mode = ZC_ASSERT_NONNULL(call.receiverMode);
          if (receiver.sourceNode != base ||
              receiver.sourceType != facts.nodeTypes().entries()[baseTypeSlot].value ||
              receiver.parameterType != receiver.sourceType || receiver.adjustment != zc::none ||
              mode != checker::checked::ReceiverMode::Shared ||
              adjustment.source != receiver.sourceType ||
              adjustment.destination != receiver.parameterType || adjustment.steps.size() != 1 ||
              adjustment.steps[0] != checker::checked::ReceiverAdjustmentStep::BorrowShared ||
              argument.sourceNode != index ||
              argument.sourceType != facts.nodeTypes().entries()[indexTypeSlot].value ||
              argument.parameterType != argument.sourceType || argument.adjustment != zc::none ||
              !sameSpan(callFact.sourceSpan, valueSpanValue)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          parameterIndex = HirParameterIndexExpression{HirNodeId(),
                                                       entry.key().clone(),
                                                       receiver.sourceType,
                                                       argument.sourceType,
                                                       literalFact.literal.clone(),
                                                       nodeType.value,
                                                       HirValueCategory::Place,
                                                       valueSpanValue.clone(),
                                                       ZC_ASSERT_NONNULL(indexSpan).clone()};
        }
      }
      if (!shape.returnsLocalBorrow &&
          tree.node(shape.value).kind == ast::SyntaxKind::UnaryExpression &&
          (static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::Ref ||
           static_cast<ast::UnaryOperatorKind>(
               tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]) ==
               ast::UnaryOperatorKind::RefMut)) {
        const ast::NodeId dereference(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(dereference) ||
            tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
            static_cast<ast::UnaryOperatorKind>(
                tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
                ast::UnaryOperatorKind::Deref) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        const ast::NodeId parameter(
            tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
        auto parameterTypeIndex = factIndex(facts.nodeTypes(), parameter);
        auto dereferenceTypeIndex = factIndex(facts.nodeTypes(), dereference);
        auto parameterHandle = resolvedCallableParameter(bound.bindings(), parameter);
        zc::Maybe<HirLocalId> sourceAlias;
        if (parameterHandle == zc::none && shape.returnsLocalReborrow) {
          auto alias = resolvedOwnerLocal(bound.bindings(), parameter);
          auto localRoot = resolvedOwnerLocal(bound.bindings(), shape.localReference);
          if (alias == zc::none || localRoot == zc::none || local == zc::none ||
              shape.localInitializer == zc::none ||
              ZC_ASSERT_NONNULL(alias) != ZC_ASSERT_NONNULL(localRoot)) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(initializer, shape.localInitializer) {
            parameterHandle = resolvedCallableParameter(bound.bindings(), initializer);
          }
          if (parameterHandle == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          sourceAlias = HirLocalId();
        }
        if (!tree.contains(parameter) || tree.node(parameter).kind != ast::SyntaxKind::IdentExpr ||
            parameterTypeIndex == zc::none || dereferenceTypeIndex == zc::none ||
            parameterHandle == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t parameterTypeSlot = 0;
        size_t dereferenceTypeSlot = 0;
        ZC_IF_SOME(index, parameterTypeIndex) { parameterTypeSlot = index; }
        ZC_IF_SOME(index, dereferenceTypeIndex) { dereferenceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[parameterTypeSlot].value;
        const auto referentType = facts.nodeTypes().entries()[dereferenceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto typeLookup = checkedModule.semanticTypes().get(sourceType);
        if (!typeLookup.is<type::SemanticTypeLookup>() ||
            !typeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            typeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != referentType ||
            sourceType != nodeType.value) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        ZC_IF_SOME(handle, parameterHandle) {
          auto authority = registries.callableParameter(handle);
          if (authority == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          ZC_IF_SOME(entry, authority) {
            bool matches = false;
            for (const auto& candidate : parameters) {
              if (candidate.key == entry.key() && candidate.type == sourceType) { matches = true; }
            }
            if (!matches) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (sourceAlias != zc::none) {
              ast::NodeId initializer;
              ZC_IF_SOME(value, shape.localInitializer) { initializer = value; }
              auto initializerTypeIndex = factIndex(facts.nodeTypes(), initializer);
              auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
              if (initializerTypeIndex == zc::none || initializerSpan == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t initializerTypeSlot = 0;
              ZC_IF_SOME(index, initializerTypeIndex) { initializerTypeSlot = index; }
              if (facts.nodeTypes().entries()[initializerTypeSlot].value != sourceType) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              parameterReference = HirParameterReferenceExpression{
                  HirNodeId(), entry.key().clone(), sourceType, HirValueCategory::Place,
                  ZC_ASSERT_NONNULL(initializerSpan).clone()};
            }
            parameterReborrow = HirParameterReborrowExpression{
                HirNodeId(),    entry.key().clone(), zc::mv(sourceAlias),   sourceType,
                nodeType.value, expectedMutability,  valueSpanValue.clone()};
          }
        }
      }
      if (shape.returnsLocalBorrow) {
        auto sourceTypeIndex = factIndex(facts.nodeTypes(), shape.localReference);
        if (sourceTypeIndex == zc::none || local == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t sourceTypeSlot = 0;
        ZC_IF_SOME(index, sourceTypeIndex) { sourceTypeSlot = index; }
        const auto sourceType = facts.nodeTypes().entries()[sourceTypeSlot].value;
        const auto operation = static_cast<ast::UnaryOperatorKind>(
            tree.node(shape.value).payload.words[ast::kUnaryExpressionOpWord]);
        const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                            ? type::semantic::Mutability::Const
                                            : type::semantic::Mutability::Mutable;
        auto borrowTypeLookup = checkedModule.semanticTypes().get(nodeType.value);
        if (!borrowTypeLookup.is<type::SemanticTypeLookup>() ||
            !borrowTypeLookup.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>() ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .mutability != expectedMutability ||
            borrowTypeLookup.get<type::SemanticTypeLookup>()
                    .data()
                    .get<type::semantic::ReferenceTypeData>()
                    .referent != sourceType ||
            ZC_ASSERT_NONNULL(local).type != sourceType) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::InvalidFact, module, registries,
                                               ordinal + 2);
        }
        localBorrow =
            HirLocalBorrowExpression{HirNodeId(),    HirLocalId(),       sourceType,
                                     nodeType.value, expectedMutability, valueSpanValue.clone()};
      }
      ast::NodeId callNode = shape.value;
      if (!shape.returnsReceiverCall) {
        ZC_IF_SOME(initializer, shape.localInitializer) { callNode = initializer; }
      }
      if (!tree.contains(callNode)) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      if (tree.node(callNode).kind == ast::SyntaxKind::CallExpression) {
        const auto& sourceCall = tree.node(callNode);
        auto callSpan = bound.parsedModule().spanFor(sourceCall.range);
        const ast::NodeId calleeNode(sourceCall.payload.words[ast::kCallExpressionCalleeWord]);
        const ast::NodeList typeArguments{
            sourceCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
            sourceCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        const ast::NodeList arguments{sourceCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                      sourceCall.payload.words[ast::kCallExpressionArgsSizeWord]};
        auto calleeTypeIndex = factIndex(facts.nodeTypes(), calleeNode);
        auto checkedCallIndex = factIndex(facts.calls(), callNode);
        auto callKey = checkedNodeKey(tree, bound.parsedModule(), callNode);
        if (shape.returnsReceiverCall) {
          const ast::NodeId receiverNode(
              tree.node(calleeNode).payload.words[ast::kMemberExpressionObjectWord]);
          auto receiverTypeIndex = factIndex(facts.nodeTypes(), receiverNode);
          auto memberIndex = factIndex(facts.members(), calleeNode);
          auto receiverBinding = resolvedOwnerLocal(bound.bindings(), receiverNode);
          auto dispatchIndex =
              dispatchFactIndex(checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::MemberExpression ||
              static_cast<ast::MemberAccessKind>(
                  tree.node(calleeNode).payload.words[ast::kMemberExpressionAccessWord]) !=
                  ast::MemberAccessKind::Dot ||
              !tree.contains(receiverNode) ||
              tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
              receiverTypeIndex == zc::none || memberIndex == zc::none ||
              receiverBinding == zc::none || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none || dispatchIndex == zc::none ||
              callSpan == zc::none || local == zc::none ||
              ZC_ASSERT_NONNULL(local).type !=
                  facts.nodeTypes().entries()[ZC_ASSERT_NONNULL(receiverTypeIndex)].value) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t receiverTypeSlot = 0;
          size_t memberSlot = 0;
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, receiverTypeIndex) { receiverTypeSlot = index; }
          ZC_IF_SOME(index, memberIndex) { memberSlot = index; }
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& member = facts.members().entries()[memberSlot].value;
          const auto& invocation = facts.calls().entries()[checkedCallSlot].value.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::ConcreteMethodCallable>() ||
              !target.is<checker::dispatch::ConcreteMethodTarget>() ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              selected.get<checker::checked::ConcreteMethodCallable>().method != member.member ||
              target.get<checker::dispatch::ConcreteMethodTarget>().method != member.member ||
              member.node != calleeNode ||
              member.receiverType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
              member.memberType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              member.adjustment != zc::none || invocation.calleeType != member.memberType ||
              invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
              invocation.receiver == zc::none || invocation.receiverMode == zc::none ||
              invocation.receiverAdjustment == zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || !dispatchOwnerMatches ||
              dispatch.fact.receiver == zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.successType != nodeType.value ||
              dispatch.fact.resultType != nodeType.value ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none ||
              !sameSpan(facts.calls().entries()[checkedCallSlot].value.sourceSpan,
                        ZC_ASSERT_NONNULL(callSpan)) ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan))) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          ZC_IF_SOME(receiver, invocation.receiver) {
            ZC_IF_SOME(mode, invocation.receiverMode) {
              ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
                auto receiverParameter = checkedModule.semanticTypes().get(receiver.parameterType);
                if (!receiverParameter.is<type::SemanticTypeLookup>() ||
                    !receiverParameter.get<type::SemanticTypeLookup>()
                         .data()
                         .is<type::semantic::ReferenceTypeData>() ||
                    receiverParameter.get<type::SemanticTypeLookup>()
                            .data()
                            .get<type::semantic::ReferenceTypeData>()
                            .mutability != type::semantic::Mutability::Mutable ||
                    receiverParameter.get<type::SemanticTypeLookup>()
                            .data()
                            .get<type::semantic::ReferenceTypeData>()
                            .referent != receiver.sourceType) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                if (receiver.sourceNode != receiverNode ||
                    receiver.sourceType != facts.nodeTypes().entries()[receiverTypeSlot].value ||
                    receiver.adjustment != zc::none ||
                    mode != checker::checked::ReceiverMode::Mutable ||
                    adjustment.source != receiver.sourceType ||
                    adjustment.destination != receiver.parameterType ||
                    adjustment.steps.size() != 1 ||
                    adjustment.steps[0] !=
                        checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
                  return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                       ir::IrFailureKind::InvalidFact, module,
                                                       registries, ordinal + 2);
                }
                zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
                steps.add(checker::checked::ReceiverAdjustmentStep::BorrowMutable);
                zc::Vector<HirDirectCallArgument> callArguments;
                const auto argumentNodes = tree.list(arguments);
                for (size_t index = 0; index < argumentNodes.size(); ++index) {
                  const auto argument = argumentNodes[index];
                  auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
                  auto literalIndex = factIndex(facts.literals(), argument);
                  auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
                  if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind) ||
                      argumentTypeIndex == zc::none || literalIndex == zc::none ||
                      argumentSpan == zc::none) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::MissingRequiredFact,
                                                         module, registries, ordinal + 2);
                  }
                  size_t argumentTypeSlot = 0;
                  size_t literalSlot = 0;
                  ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
                  ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
                  const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
                  const auto& checkedArgument = invocation.arguments[index];
                  const auto& literal = facts.literals().entries()[literalSlot].value;
                  if (checkedArgument.sourceNode != argument ||
                      checkedArgument.sourceType != argumentType ||
                      checkedArgument.parameterType != argumentType ||
                      checkedArgument.adjustment != zc::none || literal.node != argument ||
                      literal.type != argumentType ||
                      !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                         ir::IrFailureKind::InvalidFact, module,
                                                         registries, ordinal + 2);
                  }
                  callArguments.add(
                      HirDirectCallArgument{argumentType, literal.literal.clone(),
                                            zc::Maybe<identity::CallableParameterKey>(),
                                            ZC_ASSERT_NONNULL(argumentSpan).clone()});
                }
                receiverCall = HirReceiverCallExpression{HirNodeId(),
                                                         HirNodeId(),
                                                         member.member,
                                                         invocation.calleeType,
                                                         receiver.sourceType,
                                                         receiver.parameterType,
                                                         mode,
                                                         zc::mv(steps),
                                                         invocation.resultType,
                                                         zc::mv(callArguments),
                                                         ZC_ASSERT_NONNULL(callSpan).clone()};
              }
            }
          }
          if (receiverCall == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
        } else {
          if (!tree.contains(calleeNode) ||
              tree.node(calleeNode).kind != ast::SyntaxKind::IdentExpr ||
              !tree.contains(typeArguments) || !tree.contains(arguments) ||
              !typeArguments.empty() || calleeTypeIndex == zc::none ||
              checkedCallIndex == zc::none || callKey == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          auto dispatchIndex =
              dispatchFactIndex(checkedModule.dispatchFacts().facts(), ZC_ASSERT_NONNULL(callKey));
          auto callee = resolvedDefinition(bound.bindings(), calleeNode);
          if (dispatchIndex == zc::none || callee == zc::none) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::MissingRequiredFact, module,
                                                 registries, ordinal + 2);
          }
          size_t calleeTypeSlot = 0;
          size_t checkedCallSlot = 0;
          size_t dispatchSlot = 0;
          ZC_IF_SOME(index, calleeTypeIndex) { calleeTypeSlot = index; }
          ZC_IF_SOME(index, checkedCallIndex) { checkedCallSlot = index; }
          ZC_IF_SOME(index, dispatchIndex) { dispatchSlot = index; }
          const auto& checkedCall = facts.calls().entries()[checkedCallSlot].value;
          const auto& invocation = checkedCall.invocation;
          const auto& selected = invocation.selected.variant();
          const auto& dispatch = checkedModule.dispatchFacts().facts()[dispatchSlot];
          const auto& target = dispatch.fact.target.variant();
          const auto& transform = dispatch.fact.resultTransform.variant();
          bool dispatchOwnerMatches = false;
          ZC_IF_SOME(owner, dispatch.owner) {
            dispatchOwnerMatches = owner == definition.definition;
          }
          if (!selected.is<checker::checked::DirectCallable>() ||
              selected.get<checker::checked::DirectCallable>().callee !=
                  ZC_ASSERT_NONNULL(callee) ||
              invocation.calleeType != facts.nodeTypes().entries()[calleeTypeSlot].value ||
              invocation.successType != nodeType.value || invocation.resultType != nodeType.value ||
              invocation.receiver != zc::none || invocation.receiverMode != zc::none ||
              invocation.receiverAdjustment != zc::none ||
              invocation.arguments.size() != arguments.size ||
              invocation.substitutions != zc::none || invocation.witnesses != zc::none ||
              invocation.raises != zc::none || callSpan == zc::none ||
              !sameSpan(checkedCall.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !dispatchOwnerMatches || !target.is<checker::dispatch::DirectTarget>() ||
              target.get<checker::dispatch::DirectTarget>().callee != ZC_ASSERT_NONNULL(callee) ||
              !transform.is<checker::dispatch::IdentityResultTransform>() ||
              dispatch.fact.receiver != zc::none ||
              dispatch.fact.arguments.size() != arguments.size ||
              dispatch.fact.successType != nodeType.value ||
              dispatch.fact.resultType != nodeType.value ||
              dispatch.fact.substitutions != zc::none || dispatch.fact.witnesses != zc::none ||
              dispatch.fact.raises != zc::none ||
              !sameSpan(dispatch.fact.sourceSpan, ZC_ASSERT_NONNULL(callSpan)) ||
              !typeExists(invocation.calleeType, checkedModule.semanticTypes())) {
            return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                 ir::IrFailureKind::InvalidFact, module, registries,
                                                 ordinal + 2);
          }
          zc::Vector<HirDirectCallArgument> callArguments;
          const auto argumentNodes = tree.list(arguments);
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            const auto argument = argumentNodes[index];
            auto argumentTypeIndex = factIndex(facts.nodeTypes(), argument);
            auto argumentKey = checkedNodeKey(tree, bound.parsedModule(), argument);
            auto argumentSpan = bound.parsedModule().spanFor(tree.node(argument).range);
            const bool isLiteralArgument =
                tree.contains(argument) && isScalarLiteral(tree.node(argument).kind);
            const bool isParameterArgument =
                tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::IdentExpr;
            if ((!isLiteralArgument && !isParameterArgument) || argumentTypeIndex == zc::none ||
                argumentKey == zc::none || argumentSpan == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::MissingRequiredFact, module,
                                                   registries, ordinal + 2);
            }
            size_t argumentTypeSlot = 0;
            ZC_IF_SOME(value, argumentTypeIndex) { argumentTypeSlot = value; }
            const auto& checkedArgument = invocation.arguments[index];
            const auto& dispatchArgument = dispatch.fact.arguments[index];
            const auto argumentType = facts.nodeTypes().entries()[argumentTypeSlot].value;
            if (checkedArgument.sourceNode != argument ||
                checkedArgument.sourceType != argumentType ||
                checkedArgument.parameterType != argumentType ||
                checkedArgument.adjustment != zc::none ||
                !sameNodeKey(dispatchArgument.sourceNode, ZC_ASSERT_NONNULL(argumentKey)) ||
                dispatchArgument.sourceType != argumentType ||
                dispatchArgument.parameterType != argumentType ||
                dispatchArgument.adjustment != zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            if (isLiteralArgument) {
              auto literalIndex = factIndex(facts.literals(), argument);
              if (literalIndex == zc::none) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::MissingRequiredFact, module,
                                                     registries, ordinal + 2);
              }
              size_t literalSlot = 0;
              ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
              const auto& literal = facts.literals().entries()[literalSlot].value;
              if (literal.node != argument || literal.type != argumentType ||
                  !sameSpan(literal.sourceSpan, ZC_ASSERT_NONNULL(argumentSpan))) {
                return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                     ir::IrFailureKind::InvalidFact, module,
                                                     registries, ordinal + 2);
              }
              zc::Maybe<identity::CallableParameterKey> noParameter;
              callArguments.add(HirDirectCallArgument{argumentType, literal.literal.clone(),
                                                      zc::mv(noParameter),
                                                      ZC_ASSERT_NONNULL(argumentSpan).clone()});
              continue;
            }
            auto parameter = resolvedCallableParameter(bound.bindings(), argument);
            zc::Maybe<identity::CallableParameterKey> parameterKey;
            ZC_IF_SOME(handle, parameter) {
              auto authority = registries.callableParameter(handle);
              ZC_IF_SOME(entry, authority) {
                for (const auto& candidate : parameters) {
                  if (candidate.key == entry.key() && candidate.type == argumentType) {
                    parameterKey = entry.key().clone();
                  }
                }
              }
            }
            if (parameterKey == zc::none) {
              return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                                   ir::IrFailureKind::InvalidFact, module,
                                                   registries, ordinal + 2);
            }
            zc::Maybe<checker::checked::CanonicalConstValue> noValue;
            callArguments.add(HirDirectCallArgument{argumentType, zc::mv(noValue),
                                                    zc::mv(parameterKey),
                                                    ZC_ASSERT_NONNULL(argumentSpan).clone()});
          }
          call =
              HirDirectCallExpression{HirNodeId(),           ZC_ASSERT_NONNULL(callee),
                                      invocation.calleeType, invocation.resultType,
                                      zc::mv(callArguments), ZC_ASSERT_NONNULL(callSpan).clone()};
        }
      } else if (literal == zc::none && aggregate == zc::none && parameterReference == zc::none &&
                 parameterIndex == zc::none && shape.localInitializer != zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      // Loop-body composite shape: the mut-local writes are the loop body. Build
      // the loop condition parameter reference and the loop span so the loop
      // statement wraps the writes in a reducible CFG. The declaration, writes,
      // and return are already assembled above exactly as for the flat mut-local
      // shape.
      zc::Maybe<PendingLoopBodyReturn> loopBodyReturn;
      if (shape.isLoopBody) {
        auto conditionParameter = resolvedCallableParameter(bound.bindings(), shape.loopCondition);
        auto conditionTypeIndex = factIndex(facts.nodeTypes(), shape.loopCondition);
        auto conditionSpan = bound.parsedModule().spanFor(tree.node(shape.loopCondition).range);
        auto loopSpan = bound.parsedModule().spanFor(tree.node(shape.loopStatement).range);
        if (conditionParameter == zc::none || conditionTypeIndex == zc::none ||
            conditionSpan == zc::none || loopSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        identity::CallableParameterId conditionHandle;
        ZC_IF_SOME(value, conditionParameter) { conditionHandle = value; }
        auto conditionAuthority = registries.callableParameter(conditionHandle);
        if (conditionAuthority == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
        size_t conditionTypeSlot = 0;
        ZC_IF_SOME(index, conditionTypeIndex) { conditionTypeSlot = index; }
        const auto conditionType = facts.nodeTypes().entries()[conditionTypeSlot].value;
        ZC_IF_SOME(entry, conditionAuthority) {
          auto conditionRef = HirParameterReferenceExpression{
              HirNodeId(), entry.key().clone(), conditionType, HirValueCategory::Place,
              ZC_ASSERT_NONNULL(conditionSpan).clone()};
          loopBodyReturn =
              PendingLoopBodyReturn{zc::mv(conditionRef), ZC_ASSERT_NONNULL(loopSpan).clone()};
        }
        if (loopBodyReturn == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, ordinal + 2);
        }
      }
      pendingFunctions.add(PendingFunctionDeclaration{definition.definition,
                                                      callable.success,
                                                      zc::mv(parameters),
                                                      zc::mv(visibilityValue),
                                                      linkageValue,
                                                      definition.source.clone(),
                                                      bodySpanValue.clone(),
                                                      returnSpanValue.clone(),
                                                      valueSpanValue.clone(),
                                                      zc::mv(literal),
                                                      zc::mv(call),
                                                      zc::mv(receiverCall),
                                                      zc::mv(local),
                                                      zc::mv(aggregate),
                                                      zc::mv(localWrites),
                                                      zc::mv(localWriteValues),
                                                      zc::mv(localReference),
                                                      zc::mv(localFieldProjection),
                                                      zc::mv(parameterReference),
                                                      zc::mv(parameterIndex),
                                                      zc::mv(parameterReborrow),
                                                      zc::mv(localBorrow),
                                                      zc::none,
                                                      zc::mv(unsafeBlockSpan),
                                                      zc::mv(orderingKey),
                                                      zc::none,
                                                      zc::none,
                                                      zc::none,
                                                      zc::mv(loopBodyReturn)});
      continue;
    }
    if (definition.record.kind() != identity::DefinitionKind::Static &&
        definition.record.kind() != identity::DefinitionKind::Constant) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }
    auto patternSite = patternBindingSite(definitionInventory, definition);
    if (patternSite == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& bindingSite = ZC_ASSERT_NONNULL(patternSite);
    const auto& tree = bound.tree();
    if (bindingSite.patternPath.size() != 0 || !tree.contains(bindingSite.introducer) ||
        tree.node(bindingSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& declarator = tree.node(bindingSite.introducer);
    const ast::NodeId patternNode(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(patternNode) ||
        tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern ||
        !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    auto definitionTypeIndex = factIndex(facts.definitionTypes(), definition.definition);
    auto patternIndex = factIndex(facts.patterns(), patternNode);
    auto nodeTypeIndex = factIndex(facts.nodeTypes(), initializer);
    auto literalIndex = factIndex(facts.literals(), initializer);
    auto signaturePosition = signatureIndex(signatures.definitions.asPtr(), definition.definition);
    auto rootPosition = signatureRootIndex(signatures.roots.asPtr(), definition.definition);
    auto declarationSpan = bound.parsedModule().spanFor(declarator.range);
    auto initializerSpan = bound.parsedModule().spanFor(tree.node(initializer).range);
    if (definitionTypeIndex == zc::none || patternIndex == zc::none || nodeTypeIndex == zc::none ||
        literalIndex == zc::none || signaturePosition == zc::none || rootPosition == zc::none ||
        declarationSpan == zc::none || initializerSpan == zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, ordinal + 2);
    }

    size_t definitionTypeSlot = 0;
    size_t patternSlot = 0;
    size_t nodeTypeSlot = 0;
    size_t literalSlot = 0;
    size_t signatureSlot = 0;
    size_t rootSlot = 0;
    ZC_IF_SOME(value, definitionTypeIndex) { definitionTypeSlot = value; }
    ZC_IF_SOME(value, patternIndex) { patternSlot = value; }
    ZC_IF_SOME(value, nodeTypeIndex) { nodeTypeSlot = value; }
    ZC_IF_SOME(value, literalIndex) { literalSlot = value; }
    ZC_IF_SOME(value, signaturePosition) { signatureSlot = value; }
    ZC_IF_SOME(value, rootPosition) { rootSlot = value; }
    const auto& definitionType = facts.definitionTypes().entries()[definitionTypeSlot];
    const auto& pattern = facts.patterns().entries()[patternSlot].value;
    const auto& nodeType = facts.nodeTypes().entries()[nodeTypeSlot];
    const auto& literal = facts.literals().entries()[literalSlot].value;
    const auto& signature = signatures.definitions[signatureSlot];
    const auto& root = signatures.roots[rootSlot];
    if (!signature.payload.variant().is<checker::signature::ValueSignature>() ||
        !signature.scope.variant().is<checker::signature::ModuleDefinitionSignatureScope>()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    const auto& valueSignature =
        signature.payload.variant().get<checker::signature::ValueSignature>();
    auto declarationLinkage = linkage(valueSignature);
    auto declarationVisibility = visibility(root.visibility);
    if (declarationLinkage == zc::none || declarationVisibility == zc::none ||
        signature.definitionKind != definition.record.kind() || root.sourceModule != module ||
        root.canonicalDefinition != definition.definition || !valueSignature.hasInitializer ||
        valueSignature.type != definitionType.value || definitionType.value != nodeType.value ||
        literal.type != nodeType.value || pattern.scrutineeType != definitionType.value ||
        pattern.bindings.size() != 1 || pattern.bindings[0].binding != definition.definition ||
        pattern.bindings[0].type != definitionType.value || pattern.refinements.size() != 0 ||
        !pattern.constructor.variant().is<checker::checked::WildcardPattern>() ||
        !pattern.reachable || pattern.guardMayRaise != zc::none ||
        !sameSpan(signature.declarationSpan, definition.source)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }

    auto constantIndex = factIndex(facts.constants(), definition.definition);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    if (definition.record.kind() == identity::DefinitionKind::Constant) {
      if (constantIndex == zc::none || valueSignature.constantValue == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, ordinal + 2);
      }
      size_t constantSlot = 0;
      ZC_IF_SOME(value, constantIndex) { constantSlot = value; }
      const auto& evaluated = facts.constants().entries()[constantSlot].value;
      bool signatureMatches = false;
      ZC_IF_SOME(signatureValue, valueSignature.constantValue) {
        signatureMatches = sameConstant(signatureValue, evaluated.value, module, registries,
                                        checkedModule.semanticTypes());
      }
      if (evaluated.expression != initializer || evaluated.type != definitionType.value ||
          evaluated.dependencies.size() != 0 ||
          !sameConstant(evaluated.value, literal.literal, module, registries,
                        checkedModule.semanticTypes()) ||
          !signatureMatches) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries,
                                             ordinal + 2);
      }
      constant = evaluated.value.clone();
    } else if (constantIndex != zc::none || valueSignature.constantValue != zc::none) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           ordinal + 2);
    }

    if (!typeExists(definitionType.value, checkedModule.semanticTypes())) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    zc::Array<uint8_t> orderingKey;
    orderingKey = definition.key.encode();
    HirVisibility visibilityValue = HirVisibility::external();
    HirLinkage linkageValue = HirLinkage::Internal;
    identity::SourceSpan declarationSpanValue = definition.source.clone();
    identity::SourceSpan initializerSpanValue = literal.sourceSpan.clone();
    ZC_IF_SOME(value, declarationVisibility) { visibilityValue = zc::mv(value); }
    ZC_IF_SOME(value, declarationLinkage) { linkageValue = value; }
    ZC_IF_SOME(value, declarationSpan) { declarationSpanValue = value.clone(); }
    ZC_IF_SOME(value, initializerSpan) { initializerSpanValue = value.clone(); }
    if (!sameSpan(literal.sourceSpan, initializerSpanValue)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::InvalidFact, module, registries,
                                           ordinal + 2);
    }
    pending.add(PendingValueDeclaration{
        definition.definition, definition.record.kind(), valueSignature.type, definitionType.value,
        valueSignature.mutability, zc::mv(visibilityValue), linkageValue,
        declarationSpanValue.clone(), definition.source.clone(), literal.sourceSpan.clone(),
        literal.literal.clone(), zc::mv(constant), zc::mv(orderingKey)});
  }

  size_t directCallCount = 0;
  size_t directCallArgumentCount = 0;
  size_t directCallLiteralArgumentCount = 0;
  size_t receiverCallCount = 0;
  size_t receiverCallArgumentCount = 0;
  size_t localReturnCount = 0;
  size_t uninitializedLocalReturnCount = 0;
  size_t localWriteCount = 0;
  size_t parameterReferenceCount = 0;
  size_t parameterIndexCount = 0;
  size_t parameterReborrowCount = 0;
  size_t localAliasReborrowCount = 0;
  size_t localBorrowCount = 0;
  size_t aggregateCount = 0;
  size_t aggregateElementCount = 0;
  size_t localFieldProjectionCount = 0;
  size_t localFieldWriteCount = 0;
  size_t unsafeBlockCount = 0;
  size_t conditionalCount = 0;
  size_t equalityConditionalCount = 0;
  size_t conditionalLiteralArmCount = 0;
  // Number of comparison-condition operands that are scalar literals rather than
  // parameter references. Each such operand adds one scalar-literal expression
  // and one literal fact beyond the arm literals.
  size_t equalityLiteralOperandCount = 0;
  size_t loopCount = 0;
  // Per-function excess of literal facts a sequential N-local body carries over
  // the single-literal baseline the shared literal equation grants each
  // function: sum of (literal-bearing bindings - 1). Zero for the former
  // two-local literal or aggregate source, so existing bodies keep exact counts.
  // Per-function excess of literal facts a sequential N-local body carries over
  // the single-literal baseline the shared literal equation grants each
  // function: sum of (literal-bearing operands - 1). Signed because an all-binary
  // body with reference operands carries zero literal-bearing operands, so the
  // per-function term is negative. Zero for the former two-local literal or
  // aggregate source, so existing bodies keep exact counts.
  int64_t sequentialLiteralAdjustment = 0;
  // Total primitive-binary initializers across sequential bodies. Each carries
  // one call fact and one dispatch fact and two operand node types beyond the
  // per-binding local node type.
  size_t sequentialBinaryCount = 0;
  // Comparison-return functions (`return <a CMP b>`) and, of their two operands,
  // the count that are scalar literals rather than parameter references.
  size_t comparisonReturnCount = 0;
  size_t comparisonReturnLiteralOperandCount = 0;
  // Mutable-local writes whose value is a primitive binary. Each carries one
  // call fact and one dispatch fact, two operand node types beyond the per-write
  // baseline, materializes one HirPrimitiveBinaryExpression, and its two operands
  // are each a scalar literal (one literal fact + one expression) or a parameter
  // reference (one parameterReference, folded into parameterReferenceCount).
  size_t binaryWriteCount = 0;
  for (const auto& function : pendingFunctions) {
    const bool hasSequentialLocalReturn = function.sequentialLocalReturn != zc::none;
    if (hasSequentialLocalReturn) {
      ZC_IF_SOME(sequential, function.sequentialLocalReturn) {
        // Each binding declares one local; its initializer node carries a node
        // type fact, counted by localReturnCount (+1 per binding, matching the
        // N node types plus the return value node covered by the per-function
        // baseline). Aggregate bindings add their element literals; a primitive
        // binary binding adds its two operand nodes (node types) plus a call and
        // dispatch fact. The shared literal equation grants exactly one literal
        // per function; a sequential body instead carries L literal-initializer
        // + A aggregate-initializer + K binary-literal-operand "literal-bearing"
        // slots, so sequentialLiteralAdjustment records the per-function excess
        // (L + A + K - 1). Parameter- and local-reference operands carry no
        // literal. This term is zero for the former two-local literal/aggregate
        // source (L + A == 1), so existing bodies keep their exact counts.
        int64_t literalBearingSlots = 0;
        for (const auto& binding : sequential.bindings) {
          ++localReturnCount;
          switch (binding.kind) {
            case SequentialInitializerKind::Literal:
              ++literalBearingSlots;
              break;
            case SequentialInitializerKind::Aggregate:
              ++aggregateCount;
              ++literalBearingSlots;
              ZC_IF_SOME(aggregate, binding.aggregate) {
                aggregateElementCount += aggregate.elements.size();
              }
              break;
            case SequentialInitializerKind::LocalReference:
            case SequentialInitializerKind::ParameterReference:
              break;
            case SequentialInitializerKind::PrimitiveBinary:
              ++sequentialBinaryCount;
              for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
                ZC_IF_SOME(value, *operand) {
                  if (value.kind == SequentialBinaryOperandKind::Literal) ++literalBearingSlots;
                  if (value.kind == SequentialBinaryOperandKind::NestedBinary) {
                    // A nested operand is itself a primitive binary carrying its
                    // own call/dispatch fact and two leaf operands; count it as an
                    // additional binary and tally its literal leaves.
                    ++sequentialBinaryCount;
                    for (const auto* leaf : {&value.nestedLeft, &value.nestedRight}) {
                      ZC_IF_SOME(leafValue, *leaf) {
                        if (leafValue.kind == SequentialBinaryOperandKind::Literal) {
                          ++literalBearingSlots;
                        }
                      }
                    }
                  }
                }
              }
              break;
          }
        }
        sequentialLiteralAdjustment += literalBearingSlots - 1;
      }
      if (function.unsafeBlockSpan != zc::none) ++unsafeBlockCount;
      continue;
    }
    if (function.conditionalReturn != zc::none) {
      ++conditionalCount;
      ZC_IF_SOME(conditional, function.conditionalReturn) {
        ZC_IF_SOME(equality, conditional.condition.equality) {
          ++equalityConditionalCount;
          for (const auto* operand : {&equality.left, &equality.right}) {
            if (operand->literal != zc::none) { ++equalityLiteralOperandCount; }
          }
        }
        for (const auto* arm : {&conditional.thenArm, &conditional.elseArm}) {
          if (arm->literal != zc::none) { ++conditionalLiteralArmCount; }
        }
      }
      continue;
    }
    if (function.loopReturn != zc::none) {
      ++loopCount;
      continue;
    }
    if (function.comparisonReturn != zc::none) {
      ++comparisonReturnCount;
      ZC_IF_SOME(comparison, function.comparisonReturn) {
        for (const auto* operand : {&comparison.left, &comparison.right}) {
          if (operand->literal != zc::none) { ++comparisonReturnLiteralOperandCount; }
        }
      }
      continue;
    }
    // A loop-body composite function additionally materializes one loop statement
    // and one condition parameter reference beyond the flat mut-local write shape
    // it shares. The declaration, writes, and local return are counted by the
    // general mut-local path below. The condition parameter reference is counted
    // exclusively via loopCount here (it feeds the nodeTypes term for the
    // condition's type and, unlike a write's parameter operand, replaces no
    // literal slot, so it must stay out of parameterReferenceCount to keep the
    // literals equation balanced). The empty-body loop shape is counted separately
    // via loopReturn above, so its exact counts are unaffected.
    if (function.loopBodyReturn != zc::none) { ++loopCount; }
    bool missingInitializer = false;
    ZC_IF_SOME(local, function.local) { missingInitializer = local.initializer == zc::none; }
    const bool uninitializedLocal = missingInitializer && function.localWrites.size() == 0;
    const bool hasParameterReference = function.parameterReference != zc::none;
    const bool hasParameterIndex = function.parameterIndex != zc::none;
    const bool hasParameterReborrow = function.parameterReborrow != zc::none;
    const bool hasLocalBorrow = function.localBorrow != zc::none;
    bool localAliasReborrow = false;
    ZC_IF_SOME(reborrow, function.parameterReborrow) {
      localAliasReborrow = function.local != zc::none && reborrow.sourceAlias != zc::none;
    }
    if (function.literal == zc::none && function.call == zc::none &&
        function.receiverCall == zc::none && function.aggregate == zc::none &&
        !uninitializedLocal && !hasParameterReference && !hasParameterIndex &&
        !hasParameterReborrow && !hasLocalBorrow && function.localWrites.size() == 0) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, 1);
    }
    if ((function.literal != zc::none && function.call != zc::none) ||
        (function.literal != zc::none && function.aggregate != zc::none) ||
        (function.call != zc::none && function.aggregate != zc::none) ||
        (hasParameterReference &&
         (function.literal != zc::none || function.call != zc::none ||
          function.aggregate != zc::none || (hasParameterReborrow && !localAliasReborrow))) ||
        (hasParameterIndex &&
         (function.literal != zc::none || function.call != zc::none ||
          function.receiverCall != zc::none || function.aggregate != zc::none ||
          function.local != zc::none || function.localReference != zc::none ||
          hasParameterReference || hasParameterReborrow)) ||
        (hasParameterReborrow && (function.literal != zc::none || function.call != zc::none ||
                                  function.aggregate != zc::none)) ||
        (hasLocalBorrow && (function.call != zc::none || function.receiverCall != zc::none ||
                            function.aggregate != zc::none || hasParameterReference ||
                            hasParameterIndex || hasParameterReborrow))) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    ZC_IF_SOME(call, function.call) {
      ++directCallCount;
      directCallArgumentCount += call.arguments.size();
      for (const auto& argument : call.arguments) {
        if (argument.value != zc::none) ++directCallLiteralArgumentCount;
      }
    }
    ZC_IF_SOME(call, function.receiverCall) {
      ++receiverCallCount;
      receiverCallArgumentCount += call.arguments.size();
      if (function.local == zc::none || function.localReference == zc::none ||
          function.call != zc::none || function.literal != zc::none ||
          function.aggregate == zc::none ||
          call.receiverMode != checker::checked::ReceiverMode::Mutable ||
          call.receiverAdjustments.size() != 1 ||
          call.receiverAdjustments[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::InvalidFact, module, registries, 1);
      }
    }
    ZC_IF_SOME(aggregate, function.aggregate) {
      ++aggregateCount;
      aggregateElementCount += aggregate.elements.size();
    }
    if (function.localFieldProjection != zc::none) ++localFieldProjectionCount;
    if (hasParameterReference) ++parameterReferenceCount;
    if (hasParameterIndex) ++parameterIndexCount;
    ZC_IF_SOME(reborrow, function.parameterReborrow) {
      ++parameterReborrowCount;
      if (reborrow.sourceAlias != zc::none) ++localAliasReborrowCount;
    }
    if (hasLocalBorrow) ++localBorrowCount;
    if (function.unsafeBlockSpan != zc::none) ++unsafeBlockCount;
    if ((function.local == zc::none) != (function.localReference == zc::none) &&
        function.localFieldProjection == zc::none && !localAliasReborrow && !hasLocalBorrow) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    if (function.localFieldProjection != zc::none &&
        (function.local == zc::none || function.localReference != zc::none)) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
    if (function.local != zc::none) {
      ++localReturnCount;
      if (missingInitializer) ++uninitializedLocalReturnCount;
    }
    if (function.localWrites.size() != 0) {
      if (function.local == zc::none ||
          function.localWrites.size() != function.localWriteValues.size()) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      localWriteCount += function.localWrites.size();
      for (const auto& write : function.localWrites) {
        if (write.field != zc::none) ++localFieldWriteCount;
      }
      // A write whose value is a parameter reference materializes an entry in
      // parameterReferences and no literal, so it contributes to the same
      // parameterReferenceCount balance the top-level parameter reference does.
      // A literal write materializes a literal expression and no parameter
      // reference. Every write still contributes localWriteCount to the literals
      // equation; a reference write's -parameterReferenceCount term cancels it.
      for (const auto& value : function.localWriteValues) {
        if (value.parameter != zc::none) ++parameterReferenceCount;
        ZC_IF_SOME(binary, value.binary) {
          ++binaryWriteCount;
          // Each binary-write parameter operand materializes a parameter
          // reference, so it joins the same parameterReferenceCount balance as a
          // top-level parameter reference; a literal operand materializes a
          // literal expression instead.
          for (const auto* arm : {&binary.left, &binary.right}) {
            if (arm->parameter != zc::none) { ++parameterReferenceCount; }
          }
        }
      }
    } else if (function.localWriteValues.size() != 0) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::AdditionalFact, module, registries,
                                           1);
    }
  }
  if (facts.nodeTypes().size() !=
      pending.size() + pendingFunctions.size() + directCallCount + receiverCallCount * 2 +
          localReturnCount - uninitializedLocalReturnCount + localWriteCount * 3 +
          aggregateElementCount + localFieldProjectionCount + localFieldWriteCount +
          parameterIndexCount * 2 + parameterReborrowCount * 2 + directCallArgumentCount +
          receiverCallArgumentCount + localBorrowCount + unsafeBlockCount + conditionalCount * 2 +
          equalityConditionalCount * 2 + loopCount + comparisonReturnCount * 2 +
          sequentialBinaryCount * 2 + binaryWriteCount * 2) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 1);
  }
  if (facts.definitionTypes().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }
  if (static_cast<int64_t>(facts.literals().size()) !=
      static_cast<int64_t>(
          pending.size() + pendingFunctions.size() - directCallCount - aggregateCount -
          uninitializedLocalReturnCount - parameterReferenceCount - parameterReborrowCount +
          localAliasReborrowCount + localWriteCount + aggregateElementCount +
          directCallLiteralArgumentCount + receiverCallArgumentCount + conditionalLiteralArmCount +
          equalityLiteralOperandCount - conditionalCount + comparisonReturnLiteralOperandCount -
          comparisonReturnCount + binaryWriteCount) +
          sequentialLiteralAdjustment) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 3);
  }
  if (facts.calls().size() != directCallCount + receiverCallCount + parameterIndexCount +
                                  equalityConditionalCount + comparisonReturnCount +
                                  sequentialBinaryCount + binaryWriteCount ||
      checkedModule.dispatchFacts().facts().size() !=
          directCallCount + receiverCallCount + parameterIndexCount + equalityConditionalCount +
              comparisonReturnCount + sequentialBinaryCount + binaryWriteCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 4);
  }
  if (facts.patterns().size() != pending.size()) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 5);
  }
  if (facts.aggregates().size() != aggregateCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 6);
  }
  if (facts.members().size() !=
      localFieldProjectionCount + localFieldWriteCount + receiverCallCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 7);
  }
  if (facts.places().size() !=
          localFieldProjectionCount + localFieldWriteCount + parameterIndexCount ||
      facts.indexes().size() != parameterIndexCount ||
      facts.markerObligations().size() != parameterIndexCount) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 8);
  }
  size_t expectedConstants = 0;
  for (const auto& value : pending) {
    if (value.definitionKind == identity::DefinitionKind::Constant) ++expectedConstants;
  }
  if (facts.constants().size() != expectedConstants) {
    return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                         ir::IrFailureKind::AdditionalFact, module, registries, 2);
  }

  sortPendingDeclarations(pending);
  sortPendingFunctions(pendingFunctions);
  zc::Vector<HirValueDeclaration> declarations;
  zc::Vector<HirFunctionDeclaration> functions;
  zc::Vector<HirBlockStatement> blocks;
  zc::Vector<HirReturnStatement> returns;
  zc::Vector<HirBindingPattern> patterns;
  zc::Vector<HirScalarLiteralExpression> expressions;
  zc::Vector<HirNominalAggregateExpression> aggregates;
  zc::Vector<HirLocalBinding> locals;
  zc::Vector<HirLocalWriteStatement> localWrites;
  zc::Vector<HirLocalReferenceExpression> localReferences;
  zc::Vector<HirLocalFieldProjectionExpression> localFieldProjections;
  zc::Vector<HirParameterReferenceExpression> parameterReferences;
  zc::Vector<HirParameterIndexExpression> parameterIndexes;
  zc::Vector<HirParameterReborrowExpression> parameterReborrows;
  zc::Vector<HirLocalBorrowExpression> localBorrows;
  zc::Vector<HirDirectCallExpression> calls;
  zc::Vector<HirReceiverCallExpression> receiverCalls;
  zc::Vector<HirUnsafeBlockExpression> unsafeBlocks;
  zc::Vector<HirPrimitiveBinaryExpression> primitiveBinaryOperations;
  zc::Vector<HirConditionalExpression> conditionals;
  zc::Vector<HirLoopStatement> loops;
  uint32_t next = 1;
  for (auto& value : pending) {
    const auto declarationId = hirId(next++);
    const auto patternId = hirId(next++);
    const auto initializerId = hirId(next++);
    zc::Maybe<checker::checked::CanonicalConstValue> constant;
    ZC_IF_SOME(constantValue, value.constant) { constant = constantValue.clone(); }
    declarations.add(HirValueDeclaration{
        declarationId, value.definition, value.definitionKind, value.declaredType,
        value.inferredType, value.mutability, value.visibility.clone(), value.linkage,
        value.declarationSpan.clone(), patternId, initializerId, zc::mv(constant)});
    patterns.add(HirBindingPattern{patternId, value.definition, value.inferredType,
                                   value.inferredType, true, value.patternSpan.clone()});
    expressions.add(HirScalarLiteralExpression{initializerId, value.inferredType,
                                               value.literal.clone(), HirValueCategory::Value,
                                               value.initializerSpan.clone()});
  }
  for (auto& value : pendingFunctions) {
    // RFC 0048 recursive arms, family 1 (literal/reference). A body lowers
    // through the destination-driven per-function driver when it is either a
    // bare `return <literal>` / `return <parameter>` (four node ids: function,
    // body, return, value) or a single initialized
    // `let x: T = <literal | parameter>; return x;` (six node ids: function,
    // body, local, initializer, return, value). Each arm allocates in source
    // preorder, matching the generic materializer's stride exactly; every other
    // shape keeps the generic path until its family arm lands.
    const bool hasScalarLeaf =
        (value.literal != zc::none) != (value.parameterReference != zc::none);
    const bool onlyScalarReturn =
        value.local == zc::none && value.call == zc::none && value.receiverCall == zc::none &&
        value.aggregate == zc::none && value.localWrites.size() == 0 &&
        value.localWriteValues.size() == 0 && value.localReference == zc::none &&
        value.localFieldProjection == zc::none && value.parameterIndex == zc::none &&
        value.parameterReborrow == zc::none && value.localBorrow == zc::none &&
        value.sequentialLocalReturn == zc::none && value.conditionalReturn == zc::none &&
        value.loopReturn == zc::none && value.comparisonReturn == zc::none &&
        value.loopBodyReturn == zc::none && value.unsafeBlockSpan == zc::none;
    const bool singleInitializedLocal =
        value.local != zc::none && ZC_ASSERT_NONNULL(value.local).initializer != zc::none &&
        value.localReference != zc::none && value.call == zc::none &&
        value.receiverCall == zc::none && value.aggregate == zc::none &&
        value.localWrites.size() == 0 && value.localWriteValues.size() == 0 &&
        value.localFieldProjection == zc::none && value.parameterIndex == zc::none &&
        value.parameterReborrow == zc::none && value.localBorrow == zc::none &&
        value.sequentialLocalReturn == zc::none && value.conditionalReturn == zc::none &&
        value.loopReturn == zc::none && value.comparisonReturn == zc::none &&
        value.loopBodyReturn == zc::none && value.unsafeBlockSpan == zc::none;
    if (hasScalarLeaf && (onlyScalarReturn || singleInitializedLocal)) {
      HirFnCtx fnCtx(next, functions, blocks, returns, expressions, parameterReferences, locals,
                     localReferences, primitiveBinaryOperations, aggregates, unsafeBlocks);
      if (onlyScalarReturn) {
        lowerScalarReturnFunction(zc::mv(value), fnCtx);
      } else {
        lowerLocalReturnFunction(zc::mv(value), fnCtx);
      }
      continue;
    }
    // Family 2 sequential N-local body: N leading let bindings (literal,
    // aggregate, parameter/local reference, or primitive binary, including a
    // one-level nested binary operand) followed by a parameter/local return.
    if (value.sequentialLocalReturn != zc::none) {
      HirFnCtx fnCtx(next, functions, blocks, returns, expressions, parameterReferences, locals,
                     localReferences, primitiveBinaryOperations, aggregates, unsafeBlocks);
      lowerSequentialLocalReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    // Family 2: a bare `return <a OP b>` primitive binary (relational or
    // arithmetic), where each operand is a literal or a parameter reference.
    // Six node ids: function, body, left, right, binary, return.
    if (value.comparisonReturn != zc::none && value.literal == zc::none && value.call == zc::none &&
        value.receiverCall == zc::none && value.aggregate == zc::none && value.local == zc::none &&
        value.localWrites.size() == 0 && value.localWriteValues.size() == 0 &&
        value.localReference == zc::none && value.localFieldProjection == zc::none &&
        value.parameterReference == zc::none && value.parameterIndex == zc::none &&
        value.parameterReborrow == zc::none && value.localBorrow == zc::none &&
        value.conditionalReturn == zc::none && value.loopReturn == zc::none &&
        value.loopBodyReturn == zc::none && value.unsafeBlockSpan == zc::none) {
      HirFnCtx fnCtx(next, functions, blocks, returns, expressions, parameterReferences, locals,
                     localReferences, primitiveBinaryOperations, aggregates, unsafeBlocks);
      lowerComparisonReturnFunction(zc::mv(value), fnCtx);
      continue;
    }
    const auto functionId = hirId(next++);
    const auto bodyId = hirId(next++);
    ZC_IF_SOME(conditional, value.conditionalReturn) {
      // Conditional materialization fixed-id layout, relative to the function id.
      //
      // Parameter condition (7 nodes):
      //   +0 function, +1 body block, +2 condition parameter reference,
      //   +3 then value, +4 else value, +5 conditional, +6 return.
      // Equality condition `a == b` (9 nodes):
      //   +0 function, +1 body block, +2 left operand reference,
      //   +3 right operand reference, +4 equality comparison, +5 then value,
      //   +6 else value, +7 conditional, +8 return.
      // Each arm value is emitted either as a scalar literal into expressions or,
      // for a parameter arm, as a parameter reference into parameterReferences;
      // the arm node ordinal is unchanged either way.
      auto materializeArm = [&](HirNodeId armId, PendingConditionalArm& arm) {
        ZC_IF_SOME(reference, arm.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{armId, reference.parameter.clone(), arm.type,
                                              HirValueCategory::Place, arm.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, arm.literal) {
          expressions.add(HirScalarLiteralExpression{
              armId, arm.type, literal.clone(), HirValueCategory::Value, arm.sourceSpan.clone()});
        }
      };
      // Materialize one comparison operand: a parameter operand becomes a
      // parameter reference into parameterReferences, a literal operand becomes a
      // scalar literal into expressions. The node ordinal is fixed either way, so
      // the equality node's left/right ids remain node-kind-agnostic.
      auto materializeOperand = [&](HirNodeId operandId, PendingConditionalArm& operand) {
        ZC_IF_SOME(reference, operand.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{operandId, reference.parameter.clone(), operand.type,
                                              HirValueCategory::Place, operand.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, operand.literal) {
          expressions.add(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                     HirValueCategory::Value,
                                                     operand.sourceSpan.clone()});
        }
      };
      ZC_IF_SOME(equality, conditional.condition.equality) {
        const auto leftId = hirId(next++);
        const auto rightId = hirId(next++);
        const auto equalityId = hirId(next++);
        const auto thenValueId = hirId(next++);
        const auto elseValueId = hirId(next++);
        const auto conditionalId = hirId(next++);
        const auto returnId = hirId(next++);
        functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                             zc::mv(value.parameters), value.visibility.clone(),
                                             value.linkage, value.declarationSpan.clone(), bodyId,
                                             zc::none});
        zc::Vector<HirNodeId> statements;
        statements.add(returnId);
        blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
        returns.add(HirReturnStatement{returnId, value.resultType, conditionalId,
                                       value.returnSpan.clone()});
        materializeOperand(leftId, equality.left);
        materializeOperand(rightId, equality.right);
        primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
            equalityId, leftId, rightId, equality.operandType, equality.type,
            HirValueCategory::Value, equality.operation, equality.sourceSpan.clone()});
        materializeArm(thenValueId, conditional.thenArm);
        materializeArm(elseValueId, conditional.elseArm);
        conditionals.add(HirConditionalExpression{
            conditionalId, equalityId, thenValueId, elseValueId, value.resultType,
            HirValueCategory::Value, conditional.conditionalSpan.clone()});
        continue;
      }
      const auto conditionId = hirId(next++);
      const auto thenValueId = hirId(next++);
      const auto elseValueId = hirId(next++);
      const auto conditionalId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, conditionalId, value.returnSpan.clone()});
      ZC_IF_SOME(parameter, conditional.condition.parameter) {
        parameterReferences.add(HirParameterReferenceExpression{
            conditionId, parameter.parameter.clone(), parameter.type, parameter.category,
            parameter.sourceSpan.clone()});
      }
      materializeArm(thenValueId, conditional.thenArm);
      materializeArm(elseValueId, conditional.elseArm);
      conditionals.add(HirConditionalExpression{
          conditionalId, conditionId, thenValueId, elseValueId, value.resultType,
          HirValueCategory::Value, conditional.conditionalSpan.clone()});
      continue;
    }
    ZC_IF_SOME(loop, value.loopReturn) {
      // Loop materialization fixed-id layout, relative to the function id:
      //   +0 function, +1 body block, +2 condition parameter reference,
      //   +3 return value literal, +4 loop statement, +5 return statement.
      // The body block holds two statements: the loop statement then the return.
      const auto conditionId = hirId(next++);
      const auto returnValueId = hirId(next++);
      const auto loopId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(loopId);
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, returnValueId, value.returnSpan.clone()});
      parameterReferences.add(HirParameterReferenceExpression{
          conditionId, loop.condition.parameter.clone(), loop.condition.type,
          loop.condition.category, loop.condition.sourceSpan.clone()});
      expressions.add(
          HirScalarLiteralExpression{returnValueId, loop.returnType, loop.returnLiteral.clone(),
                                     HirValueCategory::Value, loop.returnValueSpan.clone()});
      loops.add(HirLoopStatement{loopId, conditionId, zc::Vector<HirNodeId>{}, loop.condition.type,
                                 HirValueCategory::Place, loop.loopSpan.clone()});
      continue;
    }
    ZC_IF_SOME(comparison, value.comparisonReturn) {
      // Comparison-return materialization fixed-id layout, relative to the
      // function id (6 nodes):
      //   +0 function, +1 body block, +2 left operand, +3 right operand,
      //   +4 comparison, +5 return statement.
      // The return value is the comparison node directly (no conditional). Each
      // operand is a scalar literal into expressions or, for a parameter operand,
      // a parameter reference into parameterReferences; the ordinal is fixed
      // either way. This is smaller than the equality-conditional shape (which
      // additionally materializes then/else arms and a conditional node).
      auto materializeOperand = [&](HirNodeId operandId, PendingConditionalArm& operand) {
        ZC_IF_SOME(reference, operand.parameter) {
          parameterReferences.add(
              HirParameterReferenceExpression{operandId, reference.parameter.clone(), operand.type,
                                              HirValueCategory::Place, operand.sourceSpan.clone()});
        }
        ZC_IF_SOME(literal, operand.literal) {
          expressions.add(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                     HirValueCategory::Value,
                                                     operand.sourceSpan.clone()});
        }
      };
      const auto leftId = hirId(next++);
      const auto rightId = hirId(next++);
      const auto comparisonId = hirId(next++);
      const auto returnId = hirId(next++);
      functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                           zc::mv(value.parameters), value.visibility.clone(),
                                           value.linkage, value.declarationSpan.clone(), bodyId,
                                           zc::none});
      zc::Vector<HirNodeId> statements;
      statements.add(returnId);
      blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
      returns.add(
          HirReturnStatement{returnId, value.resultType, comparisonId, value.returnSpan.clone()});
      materializeOperand(leftId, comparison.left);
      materializeOperand(rightId, comparison.right);
      primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
          comparisonId, leftId, rightId, comparison.operandType, comparison.type,
          HirValueCategory::Value, comparison.operation, comparison.sourceSpan.clone()});
      continue;
    }
    HirNodeId localId;
    zc::Maybe<HirNodeId> initializerId;
    if (value.local != zc::none) {
      localId = hirId(next++);
      ZC_IF_SOME(local, value.local) {
        if (local.initializer != zc::none) { initializerId = hirId(next++); }
      }
    }
    ZC_IF_SOME(aggregate, value.aggregate) {
      if (initializerId == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      ZC_IF_SOME(identifier, initializerId) {
        aggregates.add(HirNominalAggregateExpression{
            identifier, aggregate.definition, aggregate.type, zc::mv(aggregate.elements),
            aggregate.category, aggregate.sourceSpan.clone()});
      }
    }
    zc::Vector<HirNodeId> writeIds;
    zc::Vector<HirNodeId> writeValueIds;
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      writeIds.add(hirId(next++));
      writeValueIds.add(hirId(next++));
    }
    const auto returnId = hirId(next++);
    HirNodeId receiverId;
    if (value.receiverCall != zc::none) { receiverId = hirId(next++); }
    const auto valueId = hirId(next++);
    zc::Maybe<HirNodeId> unsafeBlockId;
    if (value.unsafeBlockSpan != zc::none) {
      unsafeBlockId = hirId(next++);
      unsafeBlocks.add(HirUnsafeBlockExpression{ZC_ASSERT_NONNULL(unsafeBlockId), valueId,
                                                value.resultType,
                                                ZC_ASSERT_NONNULL(value.unsafeBlockSpan).clone()});
    }
    // A binary write value keeps its two-id-per-write stride: the write's value
    // node id is the HirPrimitiveBinaryExpression itself. Its two operand nodes
    // are allocated in a trailing region after the return value and any unsafe
    // block, so a literal or parameter write's ids never shift. Each binary write
    // reserves exactly two trailing ids, in write order.
    zc::Vector<zc::Maybe<HirNodeId>> writeBinaryLeftIds;
    zc::Vector<zc::Maybe<HirNodeId>> writeBinaryRightIds;
    for (size_t index = 0; index < value.localWriteValues.size(); ++index) {
      zc::Maybe<HirNodeId> leftId;
      zc::Maybe<HirNodeId> rightId;
      if (value.localWriteValues[index].binary != zc::none) {
        leftId = hirId(next++);
        rightId = hirId(next++);
      }
      writeBinaryLeftIds.add(zc::mv(leftId));
      writeBinaryRightIds.add(zc::mv(rightId));
    }
    // Loop-body composite shape: the mutable-local writes are the body of a
    // `while` loop. Allocate the loop's condition parameter reference and the
    // loop statement node in a trailing region after the return value, any
    // unsafe block, and any binary-write operands, so the flat mut-local write
    // ids are byte-identical. The function body block is `[local, loop, return]`
    // and the loop statement carries the write node ids as its body.
    HirNodeId loopConditionId;
    HirNodeId loopId;
    if (value.loopBodyReturn != zc::none) {
      loopConditionId = hirId(next++);
      loopId = hirId(next++);
    }
    functions.add(HirFunctionDeclaration{functionId, value.definition, value.resultType,
                                         zc::mv(value.parameters), value.visibility.clone(),
                                         value.linkage, value.declarationSpan.clone(), bodyId,
                                         zc::mv(unsafeBlockId)});
    zc::Vector<HirNodeId> statements;
    if (value.local != zc::none) { statements.add(localId); }
    if (value.loopBodyReturn != zc::none) {
      statements.add(loopId);
    } else {
      for (const auto writeId : writeIds) { statements.add(writeId); }
    }
    statements.add(returnId);
    blocks.add(HirBlockStatement{bodyId, zc::mv(statements), value.bodySpan.clone()});
    ZC_IF_SOME(loopBody, value.loopBodyReturn) {
      parameterReferences.add(HirParameterReferenceExpression{
          loopConditionId, loopBody.condition.parameter.clone(), loopBody.condition.type,
          loopBody.condition.category, loopBody.condition.sourceSpan.clone()});
      zc::Vector<HirNodeId> loopBodyStatements;
      for (const auto writeId : writeIds) { loopBodyStatements.add(writeId); }
      loops.add(HirLoopStatement{loopId, loopConditionId, zc::mv(loopBodyStatements),
                                 loopBody.condition.type, HirValueCategory::Place,
                                 loopBody.loopSpan.clone()});
    }
    returns.add(HirReturnStatement{returnId, value.resultType, valueId, value.returnSpan.clone()});
    ZC_IF_SOME(literal, value.literal) {
      HirNodeId expressionId = valueId;
      identity::SemanticTypeId expressionType = value.resultType;
      identity::SourceSpan expressionSpan = value.valueSpan.clone();
      ZC_IF_SOME(local, value.local) {
        if (initializerId == zc::none || local.initializerSpan == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, 1);
        }
        ZC_IF_SOME(value, initializerId) { expressionId = value; }
        expressionType = local.type;
        ZC_IF_SOME(span, local.initializerSpan) { expressionSpan = span.clone(); }
      }
      expressions.add(HirScalarLiteralExpression{expressionId, expressionType, literal.clone(),
                                                 HirValueCategory::Value, zc::mv(expressionSpan)});
    }
    if (value.localWrites.size() != value.localWriteValues.size() ||
        writeIds.size() != value.localWrites.size() ||
        writeValueIds.size() != value.localWrites.size()) {
      return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                           ir::IrFailureKind::MissingRequiredFact, module,
                                           registries, 1);
    }
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      const auto& write = value.localWrites[index];
      const auto& writeValue = value.localWriteValues[index];
      // A literal write materializes a scalar literal expression at the write's
      // value node; a parameter write materializes a parameter reference at the
      // same node id. The node id stride (two per write) is identical for both,
      // so downstream fixed-id derivations do not shift.
      ZC_IF_SOME(literal, writeValue.literal) {
        expressions.add(HirScalarLiteralExpression{writeValueIds[index], write.type,
                                                   literal.clone(), HirValueCategory::Value,
                                                   write.valueSpan.clone()});
      }
      ZC_IF_SOME(parameter, writeValue.parameter) {
        parameterReferences.add(HirParameterReferenceExpression{
            writeValueIds[index], parameter.parameter.clone(), write.type, parameter.category,
            parameter.sourceSpan.clone()});
      }
      // A binary write materializes a HirPrimitiveBinaryExpression at the write's
      // value node id, and each of its two operands (a scalar literal or a
      // parameter reference) at the trailing operand node ids reserved above.
      ZC_IF_SOME(binary, writeValue.binary) {
        HirNodeId leftOperandId;
        HirNodeId rightOperandId;
        ZC_IF_SOME(id, writeBinaryLeftIds[index]) { leftOperandId = id; }
        ZC_IF_SOME(id, writeBinaryRightIds[index]) { rightOperandId = id; }
        auto materializeWriteArm = [&](HirNodeId armId, const PendingConditionalArm& arm) {
          ZC_IF_SOME(literal, arm.literal) {
            expressions.add(HirScalarLiteralExpression{
                armId, arm.type, literal.clone(), HirValueCategory::Value, arm.sourceSpan.clone()});
          }
          ZC_IF_SOME(parameter, arm.parameter) {
            parameterReferences.add(
                HirParameterReferenceExpression{armId, parameter.parameter.clone(), arm.type,
                                                parameter.category, parameter.sourceSpan.clone()});
          }
        };
        materializeWriteArm(leftOperandId, binary.left);
        materializeWriteArm(rightOperandId, binary.right);
        primitiveBinaryOperations.add(HirPrimitiveBinaryExpression{
            writeValueIds[index], leftOperandId, rightOperandId, binary.operandType, binary.type,
            HirValueCategory::Value, binary.operation, binary.sourceSpan.clone()});
      }
    }
    ZC_IF_SOME(local, value.local) {
      zc::Maybe<HirNodeId> initializer;
      zc::Maybe<identity::SourceSpan> initializerSpan;
      ZC_IF_SOME(value, initializerId) { initializer = value; }
      ZC_IF_SOME(span, local.initializerSpan) { initializerSpan = span.clone(); }
      locals.add(HirLocalBinding{localId, hirLocalId(1), local.type, zc::mv(initializer),
                                 local.sourceSpan.clone(), zc::mv(initializerSpan)});
    }
    for (size_t index = 0; index < value.localWrites.size(); ++index) {
      const auto& write = value.localWrites[index];
      localWrites.add(HirLocalWriteStatement{writeIds[index], hirLocalId(1), write.field,
                                             write.type, writeValueIds[index], write.kind,
                                             write.sourceSpan.clone(), write.valueSpan.clone()});
    }
    ZC_IF_SOME(reference, value.localReference) {
      const auto referenceId = value.receiverCall != zc::none ? receiverId : valueId;
      localReferences.add(HirLocalReferenceExpression{referenceId, hirLocalId(1), reference.type,
                                                      reference.category,
                                                      reference.sourceSpan.clone()});
    }
    ZC_IF_SOME(projection, value.localFieldProjection) {
      localFieldProjections.add(HirLocalFieldProjectionExpression{
          valueId, hirLocalId(1), projection.field, projection.receiverType, projection.type,
          projection.category, projection.sourceSpan.clone()});
    }
    ZC_IF_SOME(reference, value.parameterReference) {
      HirNodeId referenceId = valueId;
      ZC_IF_SOME(initializer, initializerId) { referenceId = initializer; }
      parameterReferences.add(
          HirParameterReferenceExpression{referenceId, reference.parameter.clone(), reference.type,
                                          reference.category, reference.sourceSpan.clone()});
    }
    ZC_IF_SOME(index, value.parameterIndex) {
      parameterIndexes.add(HirParameterIndexExpression{
          valueId, index.parameter.clone(), index.receiverType, index.indexType,
          index.index.clone(), index.type, index.category, index.sourceSpan.clone(),
          index.indexSpan.clone()});
    }
    ZC_IF_SOME(reborrow, value.parameterReborrow) {
      zc::Maybe<HirLocalId> sourceAlias;
      if (reborrow.sourceAlias != zc::none) { sourceAlias = hirLocalId(1); }
      parameterReborrows.add(HirParameterReborrowExpression{
          valueId, reborrow.parameter.clone(), zc::mv(sourceAlias), reborrow.sourceType,
          reborrow.type, reborrow.mutability, reborrow.sourceSpan.clone()});
    }
    ZC_IF_SOME(borrow, value.localBorrow) {
      localBorrows.add(HirLocalBorrowExpression{valueId, hirLocalId(1), borrow.sourceType,
                                                borrow.type, borrow.mutability,
                                                borrow.sourceSpan.clone()});
    }
    ZC_IF_SOME(call, value.call) {
      HirNodeId callId = valueId;
      if (value.local != zc::none) {
        if (initializerId == zc::none) {
          return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                               ir::IrFailureKind::MissingRequiredFact, module,
                                               registries, 1);
        }
        ZC_IF_SOME(value, initializerId) { callId = value; }
      }
      zc::Vector<HirDirectCallArgument> arguments;
      for (const auto& argument : call.arguments) {
        zc::Maybe<checker::checked::CanonicalConstValue> value;
        ZC_IF_SOME(constant, argument.value) { value = constant.clone(); }
        zc::Maybe<identity::CallableParameterKey> parameter;
        ZC_IF_SOME(key, argument.parameter) { parameter = key.clone(); }
        arguments.add(HirDirectCallArgument{argument.type, zc::mv(value), zc::mv(parameter),
                                            argument.sourceSpan.clone()});
      }
      calls.add(HirDirectCallExpression{callId, call.callee, call.calleeType, call.resultType,
                                        zc::mv(arguments), call.sourceSpan.clone()});
    }
    ZC_IF_SOME(call, value.receiverCall) {
      if (value.local == zc::none || value.localReference == zc::none) {
        return rejectHir<HirModuleCandidate>(ir::IrFailurePhase::HirConstruction,
                                             ir::IrFailureKind::MissingRequiredFact, module,
                                             registries, 1);
      }
      zc::Vector<checker::checked::ReceiverAdjustmentStep> adjustments;
      for (const auto adjustment : call.receiverAdjustments) { adjustments.add(adjustment); }
      zc::Vector<HirDirectCallArgument> arguments;
      for (const auto& argument : call.arguments) {
        zc::Maybe<checker::checked::CanonicalConstValue> value;
        ZC_IF_SOME(constant, argument.value) { value = constant.clone(); }
        zc::Maybe<identity::CallableParameterKey> parameter;
        ZC_IF_SOME(key, argument.parameter) { parameter = key.clone(); }
        arguments.add(HirDirectCallArgument{argument.type, zc::mv(value), zc::mv(parameter),
                                            argument.sourceSpan.clone()});
      }
      receiverCalls.add(HirReceiverCallExpression{
          valueId, receiverId, call.callee, call.calleeType, call.receiverSourceType,
          call.receiverType, call.receiverMode, zc::mv(adjustments), call.resultType,
          zc::mv(arguments), call.sourceSpan.clone()});
    }
  }

  auto impl = zc::heap<HirModuleCandidate::Impl>(
      zc::mv(checkedModule), zc::mv(declarations), zc::mv(functions), zc::mv(blocks),
      zc::mv(returns), zc::mv(patterns), zc::mv(expressions), zc::mv(aggregates), zc::mv(locals),
      zc::mv(localWrites), zc::mv(localReferences), zc::mv(localFieldProjections),
      zc::mv(parameterReferences), zc::mv(parameterIndexes), zc::mv(parameterReborrows),
      zc::mv(localBorrows), zc::mv(calls), zc::mv(receiverCalls), zc::mv(unsafeBlocks),
      zc::mv(primitiveBinaryOperations), zc::mv(conditionals), zc::mv(loops));
  return ir::IrOperationResult<HirModuleCandidate>::verified(HirModuleCandidate(zc::mv(impl)));
}

}  // namespace zomlang::compiler::hir
