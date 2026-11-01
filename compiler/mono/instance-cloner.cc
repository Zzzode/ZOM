// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mono/instance-cloner.h"

#include "zc/core/debug.h"
#include "compiler/type/semantic-type-store.h"
#include "compiler/type/type-substitution.h"

namespace zomlang::compiler::mono {
namespace {

using identity::IdentityInvariant;
using mir::MirBasicBlock;
using mir::MirFunction;
using mir::MirLocalDeclaration;
using mir::MirNominalAggregateElement;
using mir::MirOperand;
using mir::MirPlace;
using mir::MirProjection;
using mir::MirRvalue;
using mir::MirSourceScope;
using mir::MirStatement;
using mir::MirSwitchIntArm;
using mir::MirTerminator;
using type::semantic::TypeSubstitution;
using type::SemanticTypeStore;

/// \brief Mutable walk state carrying the canonical mapping and interning store.
struct SubstitutionWalk final {
  zc::ArrayPtr<const TypeSubstitution> substitution;
  SemanticTypeStore& semanticTypes;
  uint64_t substitutedNodeCount = 0;
  zc::Maybe<IdentityInvariant> failure;

  /// \brief Rewrites one type slot in place, interning the ground result.
  bool mapType(identity::SemanticTypeId& type) {
    auto result = type::semantic::substituteType(semanticTypes, type, substitution);
    if (result.is<IdentityInvariant>()) {
      failure = zc::mv(result.get<IdentityInvariant>());
      return false;
    }
    type = result.get<identity::SemanticTypeId>();
    ++substitutedNodeCount;
    return true;
  }
};

zc::Maybe<MirProjection> substituteProjection(const MirProjection& projection,
                                               SubstitutionWalk& walk) {
  auto mappedInput = projection.inputType();
  auto mappedResult = projection.resultType();
  if (!walk.mapType(mappedInput) || !walk.mapType(mappedResult)) { return zc::none; }
  switch (projection.kind()) {
    case mir::MirProjectionKind::Field:
      return MirProjection::field(projection.fieldValue().field, mappedInput, mappedResult);
    case mir::MirProjectionKind::Index:
      return MirProjection::index(projection.indexValue().index, mappedInput, mappedResult);
    case mir::MirProjectionKind::Dereference:
      return MirProjection::dereference(mappedInput, mappedResult);
    case mir::MirProjectionKind::Downcast:
      return MirProjection::downcast(projection.downcastValue().variant, mappedInput,
                                     mappedResult);
    case mir::MirProjectionKind::Subslice: {
      const auto& value = projection.subsliceValue();
      return MirProjection::subslice(value.first, value.pastLast, mappedInput, mappedResult);
    }
  }
  return zc::none;
}

zc::Maybe<MirPlace> substitutePlace(const MirPlace& place, SubstitutionWalk& walk) {
  auto root = place.rootType();
  auto result = place.resultType();
  if (!walk.mapType(root) || !walk.mapType(result)) { return zc::none; }
  zc::Vector<MirProjection> projections;
  for (const auto& projection : place.projections()) {
    auto mapped = substituteProjection(projection, walk);
    if (mapped == zc::none) { return zc::none; }
    projections.add(zc::mv(ZC_ASSERT_NONNULL(mapped)));
  }
  return MirPlace(place.local(), root, zc::mv(projections), result);
}

zc::Maybe<MirOperand> substituteOperand(const MirOperand& operand, SubstitutionWalk& walk) {
  if (operand.kind() == mir::MirOperandKind::Constant) {
    const auto& constant = operand.constantValue();
    auto type = constant.type;
    if (!walk.mapType(type)) { return zc::none; }
    return MirOperand::constant(type, constant.value.clone());
  }
  auto place = substitutePlace(operand.place(), walk);
  if (place == zc::none) { return zc::none; }
  if (operand.kind() == mir::MirOperandKind::Copy) {
    return MirOperand::copy(zc::mv(ZC_ASSERT_NONNULL(place)));
  }
  return MirOperand::move(zc::mv(ZC_ASSERT_NONNULL(place)));
}

zc::Maybe<MirRvalue> substituteRvalue(const MirRvalue& rvalue, SubstitutionWalk& walk) {
  switch (rvalue.kind()) {
    case mir::MirRvalueKind::Use: {
      auto operand = substituteOperand(rvalue.useValue().operand, walk);
      if (operand == zc::none) { return zc::none; }
      return MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
    }
    case mir::MirRvalueKind::NominalAggregate: {
      const auto& aggregate = rvalue.nominalAggregateValue();
      auto type = aggregate.type;
      if (!walk.mapType(type)) { return zc::none; }
      zc::Vector<MirNominalAggregateElement> elements;
      for (const auto& element : aggregate.elements) {
        auto operand = substituteOperand(element.operand, walk);
        if (operand == zc::none) { return zc::none; }
        elements.add(MirNominalAggregateElement{element.field, zc::mv(ZC_ASSERT_NONNULL(operand))});
      }
      return MirRvalue::nominalAggregate(aggregate.definition, type, zc::mv(elements));
    }
    case mir::MirRvalueKind::Comparison: {
      const auto& comparison = rvalue.comparisonValue();
      auto left = substituteOperand(comparison.left, walk);
      auto right = substituteOperand(comparison.right, walk);
      auto result = comparison.resultType;
      if (left == zc::none || right == zc::none || !walk.mapType(result)) { return zc::none; }
      return MirRvalue::comparison(comparison.op, zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), result);
    }
    case mir::MirRvalueKind::Arithmetic: {
      const auto& arithmetic = rvalue.arithmeticValue();
      auto left = substituteOperand(arithmetic.left, walk);
      auto right = substituteOperand(arithmetic.right, walk);
      auto result = arithmetic.resultType;
      if (left == zc::none || right == zc::none || !walk.mapType(result)) { return zc::none; }
      return MirRvalue::arithmetic(arithmetic.op, zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), result);
    }
    case mir::MirRvalueKind::Erase: {
      const auto& erase = rvalue.eraseValue();
      auto source = substituteOperand(erase.source, walk);
      auto sourceType = erase.sourceType;
      auto targetType = erase.targetType;
      if (source == zc::none || !walk.mapType(sourceType) || !walk.mapType(targetType)) {
        return zc::none;
      }
      return MirRvalue::erase(zc::mv(ZC_ASSERT_NONNULL(source)), sourceType, targetType);
    }
    case mir::MirRvalueKind::ErrorUnion: {
      const auto& errorUnion = rvalue.errorUnionValue();
      auto payload = substituteOperand(errorUnion.payload, walk);
      auto unionType = errorUnion.unionType;
      auto successType = errorUnion.successType;
      auto residualType = errorUnion.residualType;
      if (payload == zc::none || !walk.mapType(unionType) || !walk.mapType(successType) ||
          !walk.mapType(residualType)) {
        return zc::none;
      }
      return MirRvalue::errorUnion(unionType, successType, residualType, errorUnion.tag,
                                   zc::mv(ZC_ASSERT_NONNULL(payload)));
    }
    case mir::MirRvalueKind::ExtractField: {
      const auto& extract = rvalue.extractFieldValue();
      auto source = substituteOperand(extract.source, walk);
      auto result = extract.resultType;
      if (source == zc::none || !walk.mapType(result)) { return zc::none; }
      return MirRvalue::extractField(zc::mv(ZC_ASSERT_NONNULL(source)), extract.fieldIndex, result);
    }
  }
  return zc::none;
}

zc::Maybe<MirStatement> substituteStatement(const MirStatement& statement,
                                            SubstitutionWalk& walk) {
  auto span = statement.sourceSpan().clone();
  switch (statement.kind()) {
    case mir::MirStatementKind::Assign: {
      const auto& assignment = statement.assignmentValue();
      auto destination = substitutePlace(assignment.destination, walk);
      auto value = substituteRvalue(assignment.value, walk);
      if (destination == zc::none || value == zc::none) { return zc::none; }
      return MirStatement::assign(zc::mv(ZC_ASSERT_NONNULL(destination)),
                                  zc::mv(ZC_ASSERT_NONNULL(value)), assignment.initialization,
                                  zc::mv(span));
    }
    case mir::MirStatementKind::StorageLive:
      return MirStatement::storageLive(statement.storageLocal(), zc::mv(span));
    case mir::MirStatementKind::StorageDead:
      return MirStatement::storageDead(statement.storageLocal(), zc::mv(span));
    case mir::MirStatementKind::BorrowCreation: {
      const auto& borrow = statement.borrowCreationValue();
      auto destination = substitutePlace(borrow.destination, walk);
      auto source = substitutePlace(borrow.source, walk);
      if (destination == zc::none || source == zc::none) { return zc::none; }
      return MirStatement::borrowCreation(zc::mv(ZC_ASSERT_NONNULL(destination)), borrow.kind,
                                          zc::mv(ZC_ASSERT_NONNULL(source)), zc::mv(span));
    }
    case mir::MirStatementKind::SetDiscriminant: {
      const auto& discriminant = statement.setDiscriminantValue();
      auto destination = substitutePlace(discriminant.destination, walk);
      if (destination == zc::none) { return zc::none; }
      return MirStatement::setDiscriminant(zc::mv(ZC_ASSERT_NONNULL(destination)),
                                           discriminant.variant, zc::mv(span));
    }
    case mir::MirStatementKind::Deinitialize: {
      auto destination = substitutePlace(statement.deinitializeValue().destination, walk);
      if (destination == zc::none) { return zc::none; }
      return MirStatement::deinitialize(zc::mv(ZC_ASSERT_NONNULL(destination)), zc::mv(span));
    }
    case mir::MirStatementKind::UnsafeScopeBoundary: {
      const auto& boundary = statement.unsafeScopeBoundaryValue();
      return MirStatement::unsafeScopeBoundary(boundary.kind, boundary.scope, zc::mv(span));
    }
  }
  return zc::none;
}

zc::Maybe<MirTerminator> substituteTerminator(const MirTerminator& terminator,
                                              SubstitutionWalk& walk) {
  auto span = terminator.sourceSpan().clone();
  switch (terminator.kind()) {
    case mir::MirTerminatorKind::Return: {
      const auto& ret = terminator.returnValue();
      if (ret.value == zc::none) { return MirTerminator::returnVoid(zc::mv(span)); }
      auto value = substituteOperand(ZC_ASSERT_NONNULL(ret.value), walk);
      if (value == zc::none) { return zc::none; }
      return MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(value)), zc::mv(span));
    }
    case mir::MirTerminatorKind::Unreachable:
      return MirTerminator::unreachable(zc::mv(span));
    case mir::MirTerminatorKind::Goto:
      return MirTerminator::gotoTarget(terminator.gotoValue().target, zc::mv(span));
    case mir::MirTerminatorKind::Call: {
      const auto& call = terminator.callValue();
      auto destination = substitutePlace(call.destination, walk);
      if (destination == zc::none) { return zc::none; }
      zc::Vector<MirOperand> arguments;
      for (const auto& argument : call.arguments) {
        auto mapped = substituteOperand(argument, walk);
        if (mapped == zc::none) { return zc::none; }
        arguments.add(zc::mv(ZC_ASSERT_NONNULL(mapped)));
      }
      zc::Maybe<mir::MirBlockId> unwindTarget = call.unwindTarget;
      return MirTerminator::call(call.callee.clone(), zc::mv(arguments), call.effect.clone(),
                                 zc::mv(ZC_ASSERT_NONNULL(destination)), call.normalTarget,
                                 zc::mv(unwindTarget), zc::mv(span));
    }
    case mir::MirTerminatorKind::SwitchInt: {
      const auto& switchInt = terminator.switchIntValue();
      auto discriminant = substituteOperand(switchInt.discriminant, walk);
      if (discriminant == zc::none) { return zc::none; }
      zc::Vector<MirSwitchIntArm> arms;
      for (const auto& arm : switchInt.arms) {
        arms.add(MirSwitchIntArm{arm.value.clone(), arm.target});
      }
      return MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                      switchInt.defaultTarget, zc::mv(span));
    }
    case mir::MirTerminatorKind::IndirectCall: {
      const auto& indirect = terminator.indirectCallValue();
      auto receiver = substituteOperand(indirect.receiver, walk);
      if (receiver == zc::none) { return zc::none; }
      zc::Vector<MirOperand> arguments;
      for (const auto& argument : indirect.arguments) {
        auto mapped = substituteOperand(argument, walk);
        if (mapped == zc::none) { return zc::none; }
        arguments.add(zc::mv(ZC_ASSERT_NONNULL(mapped)));
      }
      auto destination = substitutePlace(indirect.destination, walk);
      if (destination == zc::none) { return zc::none; }
      zc::Maybe<mir::MirBlockId> unwindTarget = indirect.unwindTarget;
      return MirTerminator::indirectCall(
          indirect.interface, indirect.method, zc::mv(ZC_ASSERT_NONNULL(receiver)),
          zc::mv(arguments), indirect.effect.clone(), zc::mv(ZC_ASSERT_NONNULL(destination)),
          indirect.normalTarget, zc::mv(unwindTarget), zc::mv(span));
    }
  }
  return zc::none;
}

void forEachProjectionTypeSlot(const MirProjection& projection,
                               zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  callback(projection.inputType());
  callback(projection.resultType());
}

void forEachPlaceTypeSlot(const MirPlace& place,
                          zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  callback(place.rootType());
  callback(place.resultType());
  for (const auto& projection : place.projections()) {
    forEachProjectionTypeSlot(projection, callback);
  }
}

void forEachOperandTypeSlot(const MirOperand& operand,
                            zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  if (operand.kind() == mir::MirOperandKind::Constant) {
    callback(operand.constantValue().type);
    return;
  }
  forEachPlaceTypeSlot(operand.place(), callback);
}

void forEachRvalueTypeSlot(const MirRvalue& rvalue,
                           zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  switch (rvalue.kind()) {
    case mir::MirRvalueKind::Use:
      forEachOperandTypeSlot(rvalue.useValue().operand, callback);
      return;
    case mir::MirRvalueKind::NominalAggregate: {
      const auto& aggregate = rvalue.nominalAggregateValue();
      callback(aggregate.type);
      for (const auto& element : aggregate.elements) {
        forEachOperandTypeSlot(element.operand, callback);
      }
      return;
    }
    case mir::MirRvalueKind::Comparison: {
      const auto& comparison = rvalue.comparisonValue();
      forEachOperandTypeSlot(comparison.left, callback);
      forEachOperandTypeSlot(comparison.right, callback);
      callback(comparison.resultType);
      return;
    }
    case mir::MirRvalueKind::Arithmetic: {
      const auto& arithmetic = rvalue.arithmeticValue();
      forEachOperandTypeSlot(arithmetic.left, callback);
      forEachOperandTypeSlot(arithmetic.right, callback);
      callback(arithmetic.resultType);
      return;
    }
    case mir::MirRvalueKind::Erase: {
      const auto& erase = rvalue.eraseValue();
      forEachOperandTypeSlot(erase.source, callback);
      callback(erase.sourceType);
      callback(erase.targetType);
      return;
    }
    case mir::MirRvalueKind::ErrorUnion: {
      const auto& errorUnion = rvalue.errorUnionValue();
      forEachOperandTypeSlot(errorUnion.payload, callback);
      callback(errorUnion.unionType);
      callback(errorUnion.successType);
      callback(errorUnion.residualType);
      return;
    }
    case mir::MirRvalueKind::ExtractField: {
      const auto& extract = rvalue.extractFieldValue();
      forEachOperandTypeSlot(extract.source, callback);
      callback(extract.resultType);
      return;
    }
  }
}

void forEachStatementTypeSlot(const MirStatement& statement,
                              zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  switch (statement.kind()) {
    case mir::MirStatementKind::Assign: {
      const auto& assignment = statement.assignmentValue();
      forEachPlaceTypeSlot(assignment.destination, callback);
      forEachRvalueTypeSlot(assignment.value, callback);
      return;
    }
    case mir::MirStatementKind::StorageLive:
    case mir::MirStatementKind::StorageDead:
      return;
    case mir::MirStatementKind::BorrowCreation: {
      const auto& borrow = statement.borrowCreationValue();
      forEachPlaceTypeSlot(borrow.destination, callback);
      forEachPlaceTypeSlot(borrow.source, callback);
      return;
    }
    case mir::MirStatementKind::SetDiscriminant:
      forEachPlaceTypeSlot(statement.setDiscriminantValue().destination, callback);
      return;
    case mir::MirStatementKind::Deinitialize:
      forEachPlaceTypeSlot(statement.deinitializeValue().destination, callback);
      return;
    case mir::MirStatementKind::UnsafeScopeBoundary:
      return;
  }
}

void forEachTerminatorTypeSlot(const MirTerminator& terminator,
                               zc::FunctionParam<void(identity::SemanticTypeId)> callback) {
  switch (terminator.kind()) {
    case mir::MirTerminatorKind::Return: {
      const auto& ret = terminator.returnValue();
      ZC_IF_SOME(value, ret.value) { forEachOperandTypeSlot(value, callback); }
      return;
    }
    case mir::MirTerminatorKind::Unreachable:
    case mir::MirTerminatorKind::Goto:
      return;
    case mir::MirTerminatorKind::Call: {
      const auto& call = terminator.callValue();
      for (const auto& argument : call.arguments) {
        forEachOperandTypeSlot(argument, callback);
      }
      forEachPlaceTypeSlot(call.destination, callback);
      return;
    }
    case mir::MirTerminatorKind::SwitchInt: {
      const auto& switchInt = terminator.switchIntValue();
      forEachOperandTypeSlot(switchInt.discriminant, callback);
      return;
    }
    case mir::MirTerminatorKind::IndirectCall: {
      const auto& indirect = terminator.indirectCallValue();
      forEachOperandTypeSlot(indirect.receiver, callback);
      for (const auto& argument : indirect.arguments) {
        forEachOperandTypeSlot(argument, callback);
      }
      forEachPlaceTypeSlot(indirect.destination, callback);
      return;
    }
  }
}

}  // namespace

MirFunction cloneTemplateFunction(const MirFunction& templateFunction) {
  zc::Vector<MirSourceScope> sourceScopes;
  for (const auto& scope : templateFunction.sourceScopes) {
    sourceScopes.add(MirSourceScope{scope.id, scope.parent, scope.sourceSpan.clone()});
  }
  zc::Vector<MirLocalDeclaration> locals;
  for (const auto& local : templateFunction.locals) {
    locals.add(MirLocalDeclaration{local.id, local.kind, local.type, local.sourceScope,
                                   local.sourceSpan.clone()});
  }
  zc::Vector<MirBasicBlock> blocks;
  for (const auto& block : templateFunction.blocks) {
    zc::Vector<MirStatement> statements;
    for (const auto& statement : block.statements) {
      statements.add(statement.clone());
    }
    blocks.add(MirBasicBlock{block.id, block.sourceScope, zc::mv(statements),
                             block.terminator.clone()});
  }
  return MirFunction{templateFunction.owner,
                     templateFunction.kind,
                     templateFunction.sourceDefinitionKind,
                     templateFunction.resultType,
                     templateFunction.sourceSpan.clone(),
                     zc::mv(sourceScopes),
                     zc::mv(locals),
                     zc::mv(blocks)};
}

zc::Maybe<IdentityInvariant> substituteFunctionTypes(
    MirFunction& function, zc::ArrayPtr<const TypeSubstitution> substitution,
    SemanticTypeStore& semanticTypes, uint64_t& substitutedNodeCount) {
  SubstitutionWalk walk{substitution, semanticTypes, 0, zc::none};
  if (!walk.mapType(function.resultType)) { return zc::mv(ZC_ASSERT_NONNULL(walk.failure)); }
  for (auto& local : function.locals) {
    if (!walk.mapType(local.type)) { return zc::mv(ZC_ASSERT_NONNULL(walk.failure)); }
  }
  for (size_t blockIndex = 0; blockIndex < function.blocks.size(); ++blockIndex) {
    const auto& block = function.blocks[blockIndex];
    zc::Vector<MirStatement> statements;
    for (const auto& statement : block.statements) {
      auto mapped = substituteStatement(statement, walk);
      if (mapped == zc::none) { return zc::mv(ZC_ASSERT_NONNULL(walk.failure)); }
      statements.add(zc::mv(ZC_ASSERT_NONNULL(mapped)));
    }
    auto terminator = substituteTerminator(block.terminator, walk);
    if (terminator == zc::none) { return zc::mv(ZC_ASSERT_NONNULL(walk.failure)); }
    function.blocks[blockIndex] = MirBasicBlock{block.id, block.sourceScope, zc::mv(statements),
                                                zc::mv(ZC_ASSERT_NONNULL(terminator))};
  }
  substitutedNodeCount += walk.substitutedNodeCount;
  return zc::none;
}

void forEachFunctionTypeSlot(const MirFunction& function,
                             zc::FunctionParam<void(identity::SemanticTypeId)> callback) noexcept {
  callback(function.resultType);
  for (const auto& local : function.locals) {
    callback(local.type);
  }
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      forEachStatementTypeSlot(statement, callback);
    }
    forEachTerminatorTypeSlot(block.terminator, callback);
  }
}

uint64_t countFunctionTypeCarriers(const MirFunction& function) noexcept {
  uint64_t count = 0;
  forEachFunctionTypeSlot(function, [&](identity::SemanticTypeId) { ++count; });
  return count;
}

}  // namespace zomlang::compiler::mono
