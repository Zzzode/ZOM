// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/mir/built-mir.h"

#include "zomlang/compiler/checker/body/marker-proof.h"
#include "zomlang/compiler/checker/facts/signature-facts.h"
#include "zomlang/compiler/driver/core/marker-authority.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::mir {

zc::Maybe<MirLocalId> MirLocalId::fromOrdinal(uint32_t ordinal) noexcept {
  if (ordinal == 0) return zc::none;
  return MirLocalId(ordinal);
}

zc::Maybe<MirSourceScopeId> MirSourceScopeId::fromOrdinal(uint32_t ordinal) noexcept {
  if (ordinal == 0) return zc::none;
  return MirSourceScopeId(ordinal);
}

MirProjection::MirProjection(MirFieldProjection value) noexcept : value(value) {}
MirProjection::MirProjection(MirIndexProjection value) noexcept : value(value) {}
MirProjection::MirProjection(MirDereferenceProjection value) noexcept : value(value) {}
MirProjection::MirProjection(MirDowncastProjection value) noexcept : value(value) {}
MirProjection::MirProjection(MirSubsliceProjection value) noexcept : value(value) {}

MirProjection MirProjection::field(identity::DefId field, identity::SemanticTypeId inputType,
                                   identity::SemanticTypeId resultType) noexcept {
  return MirProjection(MirFieldProjection{field, inputType, resultType});
}

MirProjection MirProjection::index(MirLocalId index, identity::SemanticTypeId inputType,
                                   identity::SemanticTypeId resultType) noexcept {
  return MirProjection(MirIndexProjection{index, inputType, resultType});
}

MirProjection MirProjection::dereference(identity::SemanticTypeId inputType,
                                         identity::SemanticTypeId resultType) noexcept {
  return MirProjection(MirDereferenceProjection{inputType, resultType});
}

MirProjection MirProjection::downcast(identity::DefId variant, identity::SemanticTypeId inputType,
                                      identity::SemanticTypeId resultType) noexcept {
  return MirProjection(MirDowncastProjection{variant, inputType, resultType});
}

zc::Maybe<MirProjection> MirProjection::subslice(uint32_t first, uint32_t pastLast,
                                                 identity::SemanticTypeId inputType,
                                                 identity::SemanticTypeId resultType) noexcept {
  if (first > pastLast) return zc::none;
  return MirProjection(MirSubsliceProjection{first, pastLast, inputType, resultType});
}

MirProjection MirProjection::clone() const noexcept {
  switch (kind()) {
    case MirProjectionKind::Field:
      return field(fieldValue().field, inputType(), resultType());
    case MirProjectionKind::Index:
      return index(indexValue().index, inputType(), resultType());
    case MirProjectionKind::Dereference:
      return dereference(inputType(), resultType());
    case MirProjectionKind::Downcast:
      return downcast(downcastValue().variant, inputType(), resultType());
    case MirProjectionKind::Subslice:
      return MirProjection(MirSubsliceProjection{subsliceValue().first, subsliceValue().pastLast,
                                                 inputType(), resultType()});
  }
  ZC_UNREACHABLE
}

MirProjectionKind MirProjection::kind() const noexcept {
  if (value.is<MirFieldProjection>()) return MirProjectionKind::Field;
  if (value.is<MirIndexProjection>()) return MirProjectionKind::Index;
  if (value.is<MirDereferenceProjection>()) return MirProjectionKind::Dereference;
  if (value.is<MirDowncastProjection>()) return MirProjectionKind::Downcast;
  return MirProjectionKind::Subslice;
}

bool MirProjection::isStructurallyValid() const noexcept {
  switch (kind()) {
    case MirProjectionKind::Field:
      return fieldValue().field.isValid();
    case MirProjectionKind::Index:
      return indexValue().index.isValid();
    case MirProjectionKind::Dereference:
      return true;
    case MirProjectionKind::Downcast:
      return downcastValue().variant.isValid();
    case MirProjectionKind::Subslice:
      return subsliceValue().first <= subsliceValue().pastLast;
  }
  return false;
}

identity::SemanticTypeId MirProjection::inputType() const noexcept {
  switch (kind()) {
    case MirProjectionKind::Field:
      return fieldValue().inputType;
    case MirProjectionKind::Index:
      return indexValue().inputType;
    case MirProjectionKind::Dereference:
      return value.get<MirDereferenceProjection>().inputType;
    case MirProjectionKind::Downcast:
      return downcastValue().inputType;
    case MirProjectionKind::Subslice:
      return subsliceValue().inputType;
  }
  ZC_UNREACHABLE
}

identity::SemanticTypeId MirProjection::resultType() const noexcept {
  switch (kind()) {
    case MirProjectionKind::Field:
      return fieldValue().resultType;
    case MirProjectionKind::Index:
      return indexValue().resultType;
    case MirProjectionKind::Dereference:
      return value.get<MirDereferenceProjection>().resultType;
    case MirProjectionKind::Downcast:
      return downcastValue().resultType;
    case MirProjectionKind::Subslice:
      return subsliceValue().resultType;
  }
  ZC_UNREACHABLE
}

const MirFieldProjection& MirProjection::fieldValue() const {
  return value.get<MirFieldProjection>();
}

const MirIndexProjection& MirProjection::indexValue() const {
  return value.get<MirIndexProjection>();
}

const MirDowncastProjection& MirProjection::downcastValue() const {
  return value.get<MirDowncastProjection>();
}

const MirSubsliceProjection& MirProjection::subsliceValue() const {
  return value.get<MirSubsliceProjection>();
}

struct MirPlace::Impl final {
  Impl(MirLocalId local, identity::SemanticTypeId rootType, zc::Vector<MirProjection>&& projections,
       identity::SemanticTypeId resultType) noexcept
      : local(local),
        rootType(rootType),
        projections(zc::mv(projections)),
        resultType(resultType) {}

  MirLocalId local;
  identity::SemanticTypeId rootType;
  zc::Vector<MirProjection> projections;
  identity::SemanticTypeId resultType;
};

MirPlace::MirPlace(MirLocalId local, identity::SemanticTypeId rootType,
                   zc::Vector<MirProjection>&& projections,
                   identity::SemanticTypeId resultType) noexcept
    : impl(zc::heap<Impl>(local, rootType, zc::mv(projections), resultType)) {}
MirPlace::~MirPlace() noexcept(false) = default;
MirPlace::MirPlace(MirPlace&&) noexcept = default;
MirPlace& MirPlace::operator=(MirPlace&&) noexcept = default;

MirPlace MirPlace::clone() const {
  zc::Vector<MirProjection> projections;
  for (const auto& projection : impl->projections) projections.add(projection.clone());
  return MirPlace(impl->local, impl->rootType, zc::mv(projections), impl->resultType);
}

MirLocalId MirPlace::local() const noexcept { return impl->local; }

identity::SemanticTypeId MirPlace::rootType() const noexcept { return impl->rootType; }

zc::ArrayPtr<const MirProjection> MirPlace::projections() const noexcept {
  return impl->projections.asPtr();
}

identity::SemanticTypeId MirPlace::resultType() const noexcept { return impl->resultType; }

bool MirPlace::hasConsistentTypeChain() const noexcept {
  auto previous = rootType();
  for (const auto& projection : projections()) {
    if (projection.inputType() != previous) return false;
    previous = projection.resultType();
  }
  return previous == resultType();
}

MirOperand::MirOperand(MirCopyOperand&& value) noexcept : value(zc::mv(value)) {}
MirOperand::MirOperand(MirMoveOperand&& value) noexcept : value(zc::mv(value)) {}
MirOperand::MirOperand(MirConstantOperand&& value) noexcept : value(zc::mv(value)) {}

MirOperand MirOperand::copy(MirPlace&& place) noexcept {
  return MirOperand(MirCopyOperand{zc::mv(place)});
}

MirOperand MirOperand::move(MirPlace&& place) noexcept {
  return MirOperand(MirMoveOperand{zc::mv(place)});
}

MirOperand MirOperand::constant(identity::SemanticTypeId type,
                                checker::checked::CanonicalConstValue&& value) noexcept {
  return MirOperand(MirConstantOperand{type, zc::mv(value)});
}

MirOperand MirOperand::clone() const {
  if (value.is<MirCopyOperand>()) return copy(value.get<MirCopyOperand>().place.clone());
  if (value.is<MirMoveOperand>()) return move(value.get<MirMoveOperand>().place.clone());
  const auto& constant = value.get<MirConstantOperand>();
  return MirOperand::constant(constant.type, constant.value.clone());
}

MirOperandKind MirOperand::kind() const noexcept {
  if (value.is<MirCopyOperand>()) return MirOperandKind::Copy;
  if (value.is<MirMoveOperand>()) return MirOperandKind::Move;
  return MirOperandKind::Constant;
}

const MirPlace& MirOperand::place() const {
  if (value.is<MirCopyOperand>()) return value.get<MirCopyOperand>().place;
  return value.get<MirMoveOperand>().place;
}

const MirConstantOperand& MirOperand::constantValue() const {
  return value.get<MirConstantOperand>();
}

MirRvalue::MirRvalue(MirUseRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue::MirRvalue(MirNominalAggregateRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue::MirRvalue(MirComparisonRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue::MirRvalue(MirArithmeticRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue MirRvalue::use(MirOperand&& operand) noexcept {
  return MirRvalue(MirUseRvalue{zc::mv(operand)});
}

MirRvalue MirRvalue::nominalAggregate(identity::DefId definition, identity::SemanticTypeId type,
                                      zc::Vector<MirNominalAggregateElement>&& elements) noexcept {
  return MirRvalue(MirNominalAggregateRvalue{definition, type, zc::mv(elements)});
}

MirRvalue MirRvalue::comparison(MirComparisonOperator op, MirOperand&& left, MirOperand&& right,
                                identity::SemanticTypeId resultType) noexcept {
  return MirRvalue(MirComparisonRvalue{op, zc::mv(left), zc::mv(right), resultType});
}

MirRvalue MirRvalue::arithmetic(MirArithmeticOperator op, MirOperand&& left, MirOperand&& right,
                                identity::SemanticTypeId resultType) noexcept {
  return MirRvalue(MirArithmeticRvalue{op, zc::mv(left), zc::mv(right), resultType});
}

MirRvalue MirRvalue::clone() const {
  if (value.is<MirUseRvalue>()) return use(value.get<MirUseRvalue>().operand.clone());
  if (value.is<MirComparisonRvalue>()) {
    const auto& comparison = value.get<MirComparisonRvalue>();
    return MirRvalue::comparison(comparison.op, comparison.left.clone(), comparison.right.clone(),
                                 comparison.resultType);
  }
  if (value.is<MirArithmeticRvalue>()) {
    const auto& arithmetic = value.get<MirArithmeticRvalue>();
    return MirRvalue::arithmetic(arithmetic.op, arithmetic.left.clone(), arithmetic.right.clone(),
                                 arithmetic.resultType);
  }
  const auto& aggregate = value.get<MirNominalAggregateRvalue>();
  zc::Vector<MirNominalAggregateElement> elements;
  for (const auto& element : aggregate.elements) {
    elements.add(MirNominalAggregateElement{element.field, element.operand.clone()});
  }
  return nominalAggregate(aggregate.definition, aggregate.type, zc::mv(elements));
}

MirRvalueKind MirRvalue::kind() const noexcept {
  if (value.is<MirUseRvalue>()) return MirRvalueKind::Use;
  if (value.is<MirComparisonRvalue>()) return MirRvalueKind::Comparison;
  if (value.is<MirArithmeticRvalue>()) return MirRvalueKind::Arithmetic;
  return MirRvalueKind::NominalAggregate;
}

const MirUseRvalue& MirRvalue::useValue() const { return value.get<MirUseRvalue>(); }

const MirNominalAggregateRvalue& MirRvalue::nominalAggregateValue() const {
  return value.get<MirNominalAggregateRvalue>();
}

const MirComparisonRvalue& MirRvalue::comparisonValue() const {
  return value.get<MirComparisonRvalue>();
}

const MirArithmeticRvalue& MirRvalue::arithmeticValue() const {
  return value.get<MirArithmeticRvalue>();
}

MirStatement::MirStatement(MirAssignmentStatement&& value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirStorageLiveStatement value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(value), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirStorageDeadStatement value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(value), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirBorrowCreationStatement&& value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirSetDiscriminantStatement&& value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirDeinitializeStatement&& value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirStatement::MirStatement(MirUnsafeScopeBoundaryStatement value,
                           identity::SourceSpan&& sourceSpan) noexcept
    : value(value), sourceSpanValue(zc::mv(sourceSpan)) {}

MirStatement MirStatement::assign(MirPlace&& destination, MirRvalue&& value,
                                  MirInitializationKind initialization,
                                  identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirAssignmentStatement{zc::mv(destination), zc::mv(value), initialization},
                      zc::mv(sourceSpan));
}

MirStatement MirStatement::storageLive(MirLocalId local,
                                       identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirStorageLiveStatement{local}, zc::mv(sourceSpan));
}

MirStatement MirStatement::storageDead(MirLocalId local,
                                       identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirStorageDeadStatement{local}, zc::mv(sourceSpan));
}

MirStatement MirStatement::borrowCreation(MirPlace&& destination, MirBorrowKind kind,
                                          MirPlace&& source,
                                          identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirBorrowCreationStatement{zc::mv(destination), kind, zc::mv(source)},
                      zc::mv(sourceSpan));
}

MirStatement MirStatement::setDiscriminant(MirPlace&& destination, identity::DefId variant,
                                           identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirSetDiscriminantStatement{zc::mv(destination), variant},
                      zc::mv(sourceSpan));
}

MirStatement MirStatement::deinitialize(MirPlace&& destination,
                                        identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirDeinitializeStatement{zc::mv(destination)}, zc::mv(sourceSpan));
}

MirStatement MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind kind,
                                               MirSourceScopeId scope,
                                               identity::SourceSpan&& sourceSpan) noexcept {
  return MirStatement(MirUnsafeScopeBoundaryStatement{kind, scope}, zc::mv(sourceSpan));
}

MirStatement MirStatement::clone() const {
  switch (kind()) {
    case MirStatementKind::Assign: {
      const auto& assignment = assignmentValue();
      return assign(assignment.destination.clone(), assignment.value.clone(),
                    assignment.initialization, sourceSpan().clone());
    }
    case MirStatementKind::StorageLive:
      return storageLive(storageLocal(), sourceSpan().clone());
    case MirStatementKind::StorageDead:
      return storageDead(storageLocal(), sourceSpan().clone());
    case MirStatementKind::BorrowCreation: {
      const auto& borrow = borrowCreationValue();
      return borrowCreation(borrow.destination.clone(), borrow.kind, borrow.source.clone(),
                            sourceSpan().clone());
    }
    case MirStatementKind::SetDiscriminant: {
      const auto& discriminant = setDiscriminantValue();
      return setDiscriminant(discriminant.destination.clone(), discriminant.variant,
                             sourceSpan().clone());
    }
    case MirStatementKind::Deinitialize:
      return deinitialize(deinitializeValue().destination.clone(), sourceSpan().clone());
    case MirStatementKind::UnsafeScopeBoundary: {
      const auto& boundary = unsafeScopeBoundaryValue();
      return unsafeScopeBoundary(boundary.kind, boundary.scope, sourceSpan().clone());
    }
  }
  ZC_UNREACHABLE
}

MirStatementKind MirStatement::kind() const noexcept {
  if (value.is<MirAssignmentStatement>()) return MirStatementKind::Assign;
  if (value.is<MirStorageLiveStatement>()) return MirStatementKind::StorageLive;
  if (value.is<MirStorageDeadStatement>()) return MirStatementKind::StorageDead;
  if (value.is<MirBorrowCreationStatement>()) return MirStatementKind::BorrowCreation;
  if (value.is<MirSetDiscriminantStatement>()) return MirStatementKind::SetDiscriminant;
  if (value.is<MirDeinitializeStatement>()) return MirStatementKind::Deinitialize;
  return MirStatementKind::UnsafeScopeBoundary;
}

const identity::SourceSpan& MirStatement::sourceSpan() const noexcept { return sourceSpanValue; }

const MirAssignmentStatement& MirStatement::assignmentValue() const {
  return value.get<MirAssignmentStatement>();
}

MirLocalId MirStatement::storageLocal() const {
  if (value.is<MirStorageLiveStatement>()) return value.get<MirStorageLiveStatement>().local;
  return value.get<MirStorageDeadStatement>().local;
}

const MirBorrowCreationStatement& MirStatement::borrowCreationValue() const {
  return value.get<MirBorrowCreationStatement>();
}

const MirSetDiscriminantStatement& MirStatement::setDiscriminantValue() const {
  return value.get<MirSetDiscriminantStatement>();
}

const MirDeinitializeStatement& MirStatement::deinitializeValue() const {
  return value.get<MirDeinitializeStatement>();
}

const MirUnsafeScopeBoundaryStatement& MirStatement::unsafeScopeBoundaryValue() const {
  return value.get<MirUnsafeScopeBoundaryStatement>();
}

MirTerminator::MirTerminator(MirReturnTerminator&& value,
                             identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirTerminator::MirTerminator(MirUnreachableTerminator value,
                             identity::SourceSpan&& sourceSpan) noexcept
    : value(value), sourceSpanValue(zc::mv(sourceSpan)) {}
MirTerminator::MirTerminator(MirCallTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirTerminator::MirTerminator(MirGotoTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}
MirTerminator::MirTerminator(MirSwitchIntTerminator&& value,
                             identity::SourceSpan&& sourceSpan) noexcept
    : value(zc::mv(value)), sourceSpanValue(zc::mv(sourceSpan)) {}

MirTerminator MirTerminator::returnValue(MirOperand&& value,
                                         identity::SourceSpan&& sourceSpan) noexcept {
  zc::Maybe<MirOperand> result = zc::mv(value);
  return MirTerminator(MirReturnTerminator{zc::mv(result)}, zc::mv(sourceSpan));
}

MirTerminator MirTerminator::returnVoid(identity::SourceSpan&& sourceSpan) noexcept {
  zc::Maybe<MirOperand> value;
  return MirTerminator(MirReturnTerminator{zc::mv(value)}, zc::mv(sourceSpan));
}

MirTerminator MirTerminator::unreachable(identity::SourceSpan&& sourceSpan) noexcept {
  return MirTerminator(MirUnreachableTerminator{}, zc::mv(sourceSpan));
}

MirCallEffect::MirCallEffect(MirNoActivationCallEffect value) noexcept : value(value) {}

MirCallEffect::MirCallEffect(MirActivateMutableReceiverCallEffect value) noexcept : value(value) {}

MirCallEffect MirCallEffect::noActivation() noexcept {
  return MirCallEffect(MirNoActivationCallEffect{});
}

MirCallEffect MirCallEffect::activateMutableReceiver(MirLocalId temporary) noexcept {
  return MirCallEffect(MirActivateMutableReceiverCallEffect{temporary});
}

MirCallEffect MirCallEffect::clone() const noexcept {
  if (kind() == MirCallEffectKind::NoActivation) return noActivation();
  return activateMutableReceiver(value.get<MirActivateMutableReceiverCallEffect>().temporary);
}

MirCallEffectKind MirCallEffect::kind() const noexcept {
  return value.is<MirNoActivationCallEffect>() ? MirCallEffectKind::NoActivation
                                               : MirCallEffectKind::ActivateMutableReceiver;
}

bool MirCallEffect::commitsOnNormalEdge() const noexcept {
  return kind() == MirCallEffectKind::ActivateMutableReceiver;
}

zc::Maybe<MirLocalId> MirCallEffect::activatedMutableReceiver() const noexcept {
  if (kind() != MirCallEffectKind::ActivateMutableReceiver) return zc::none;
  return value.get<MirActivateMutableReceiverCallEffect>().temporary;
}

MirTerminator MirTerminator::call(identity::DefId callee, zc::Vector<MirOperand>&& arguments,
                                  MirCallEffect&& effect, MirPlace&& destination,
                                  MirBlockId normalTarget, zc::Maybe<MirBlockId>&& unwindTarget,
                                  identity::SourceSpan&& sourceSpan) noexcept {
  return MirTerminator(MirCallTerminator{callee, zc::mv(arguments), zc::mv(effect),
                                         zc::mv(destination), normalTarget, zc::mv(unwindTarget)},
                       zc::mv(sourceSpan));
}

MirTerminator MirTerminator::gotoTarget(MirBlockId target,
                                        identity::SourceSpan&& sourceSpan) noexcept {
  return MirTerminator(MirGotoTerminator{target}, zc::mv(sourceSpan));
}

MirTerminator MirTerminator::switchInt(MirOperand&& discriminant,
                                       zc::Vector<MirSwitchIntArm>&& arms, MirBlockId defaultTarget,
                                       identity::SourceSpan&& sourceSpan) noexcept {
  return MirTerminator(MirSwitchIntTerminator{zc::mv(discriminant), zc::mv(arms), defaultTarget},
                       zc::mv(sourceSpan));
}

MirTerminator MirTerminator::clone() const {
  if (kind() == MirTerminatorKind::Unreachable) return unreachable(sourceSpan().clone());
  if (kind() == MirTerminatorKind::Call) {
    const auto& source = callValue();
    zc::Vector<MirOperand> arguments;
    for (const auto& argument : source.arguments) arguments.add(argument.clone());
    zc::Maybe<MirBlockId> unwindTarget;
    ZC_IF_SOME(target, source.unwindTarget) { unwindTarget = target; }
    return call(source.callee, zc::mv(arguments), source.effect.clone(), source.destination.clone(),
                source.normalTarget, zc::mv(unwindTarget), sourceSpan().clone());
  }
  if (kind() == MirTerminatorKind::Goto) {
    return gotoTarget(gotoValue().target, sourceSpan().clone());
  }
  if (kind() == MirTerminatorKind::SwitchInt) {
    const auto& source = switchIntValue();
    zc::Vector<MirSwitchIntArm> arms;
    for (const auto& arm : source.arms) {
      arms.add(MirSwitchIntArm{arm.value.clone(), arm.target});
    }
    return switchInt(source.discriminant.clone(), zc::mv(arms), source.defaultTarget,
                     sourceSpan().clone());
  }
  ZC_IF_SOME(operand, returnValue().value) {
    return MirTerminator::returnValue(operand.clone(), sourceSpan().clone());
  }
  return returnVoid(sourceSpan().clone());
}

MirTerminatorKind MirTerminator::kind() const noexcept {
  if (value.is<MirReturnTerminator>()) return MirTerminatorKind::Return;
  if (value.is<MirUnreachableTerminator>()) return MirTerminatorKind::Unreachable;
  if (value.is<MirCallTerminator>()) return MirTerminatorKind::Call;
  if (value.is<MirGotoTerminator>()) return MirTerminatorKind::Goto;
  return MirTerminatorKind::SwitchInt;
}

const identity::SourceSpan& MirTerminator::sourceSpan() const noexcept { return sourceSpanValue; }

const MirReturnTerminator& MirTerminator::returnValue() const {
  return value.get<MirReturnTerminator>();
}

const MirCallTerminator& MirTerminator::callValue() const { return value.get<MirCallTerminator>(); }

const MirGotoTerminator& MirTerminator::gotoValue() const { return value.get<MirGotoTerminator>(); }

const MirSwitchIntTerminator& MirTerminator::switchIntValue() const {
  return value.get<MirSwitchIntTerminator>();
}

namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectMir(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    zc::Maybe<identity::DefId> definition, const checker::CheckerIdentityAuthority& identities,
    uint32_t ordinal, zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  AuthorityIdentityResolver resolver(identities);
  if (definition == zc::none) {
    zc::Vector<identity::IdentityInvariant> failures;
    failures.add(invalidIdentity(identity::IdentityAllocationPhase::Definition, ordinal));
    auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
    }
    ZC_UNREACHABLE
  }
  identity::DefId owner;
  ZC_IF_SOME(value, definition) { owner = value; }
  auto fallback = ir::IrFailureFallbackContext::from(phase, ir::IrFailureOwner::definition(owner));
  ZC_IREQUIRE(fallback != zc::none, "Built MIR failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, phase, kind, ir::IrFailureOwner::definition(owner),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

zc::Maybe<identity::DefId> firstDefinition(const hir::VerifiedHirModule& module) {
  if (module.declarations().size() != 0) return module.declarations()[0].definition;
  if (module.functions().size() != 0) return module.functions()[0].definition;
  return zc::none;
}

MirLocalId localId(uint32_t ordinal) {
  auto value = MirLocalId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

MirSourceScopeId scopeId(uint32_t ordinal) {
  auto value = MirSourceScopeId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

MirBlockId blockId(uint32_t ordinal) {
  auto value = MirBlockId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

bool encodeDefinition(identity::CanonicalEncoder& encoder, identity::DefId definition,
                      const checker::CheckerIdentityAuthority& identities) {
  auto key = identities.definition(definition);
  if (key == zc::none) return false;
  ZC_IF_SOME(value, key) {
    auto bytes = value.key().encode();
    encoder.encodeByteString(bytes.asPtr());
    return true;
  }
  return false;
}

bool encodeType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId type,
                const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  encoder.encodeByteString(lookup.get<type::SemanticTypeLookup>().key().bytes());
  return true;
}

bool encodeConstant(identity::CanonicalEncoder& encoder,
                    const checker::checked::CanonicalConstValue& value, identity::ModuleId module,
                    const checker::CheckerIdentityAuthority& identities,
                    const type::SemanticTypeStore& semanticTypes) {
  auto bytes =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          value, module, identities, semanticTypes);
  if (bytes == zc::none) return false;
  ZC_IF_SOME(record, bytes) {
    encoder.encodeByteString(record.asPtr());
    return true;
  }
  return false;
}

bool encodeProjection(identity::CanonicalEncoder& encoder, const MirProjection& projection,
                      const checker::CheckerIdentityAuthority& identities,
                      const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(projection.kind()));
  if (!encodeType(encoder, projection.inputType(), semanticTypes) ||
      !encodeType(encoder, projection.resultType(), semanticTypes)) {
    return false;
  }
  switch (projection.kind()) {
    case MirProjectionKind::Field:
      return encodeDefinition(encoder, projection.fieldValue().field, identities);
    case MirProjectionKind::Index:
      encoder.encodeUint32(projection.indexValue().index.ordinal());
      return projection.indexValue().index.isValid();
    case MirProjectionKind::Dereference:
      return true;
    case MirProjectionKind::Downcast:
      return encodeDefinition(encoder, projection.downcastValue().variant, identities);
    case MirProjectionKind::Subslice:
      encoder.encodeUint32(projection.subsliceValue().first);
      encoder.encodeUint32(projection.subsliceValue().pastLast);
      return projection.subsliceValue().first <= projection.subsliceValue().pastLast;
  }
  return false;
}

bool encodePlace(identity::CanonicalEncoder& encoder, const MirPlace& place,
                 const checker::CheckerIdentityAuthority& identities,
                 const type::SemanticTypeStore& semanticTypes) {
  if (!place.local().isValid() || !place.hasConsistentTypeChain() ||
      !encodeType(encoder, place.rootType(), semanticTypes) ||
      !encodeType(encoder, place.resultType(), semanticTypes)) {
    return false;
  }
  encoder.encodeUint32(place.local().ordinal());
  encoder.encodeSequenceSize(place.projections().size());
  for (const auto& projection : place.projections()) {
    if (!projection.isStructurallyValid() ||
        !encodeProjection(encoder, projection, identities, semanticTypes)) {
      return false;
    }
  }
  return true;
}

bool encodeOperand(identity::CanonicalEncoder& encoder, const MirOperand& operand,
                   identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
                   const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(operand.kind()));
  if (operand.kind() == MirOperandKind::Copy || operand.kind() == MirOperandKind::Move) {
    return encodePlace(encoder, operand.place(), identities, semanticTypes);
  }
  const auto& constant = operand.constantValue();
  return encodeType(encoder, constant.type, semanticTypes) &&
         encodeConstant(encoder, constant.value, module, identities, semanticTypes);
}

bool encodeRvalue(identity::CanonicalEncoder& encoder, const MirRvalue& value,
                  identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
                  const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(value.kind()));
  if (value.kind() == MirRvalueKind::Use) {
    return encodeOperand(encoder, value.useValue().operand, module, identities, semanticTypes);
  }
  if (value.kind() == MirRvalueKind::Comparison) {
    const auto& comparison = value.comparisonValue();
    encoder.encodeUint8(static_cast<uint8_t>(comparison.op));
    return encodeOperand(encoder, comparison.left, module, identities, semanticTypes) &&
           encodeOperand(encoder, comparison.right, module, identities, semanticTypes) &&
           encodeType(encoder, comparison.resultType, semanticTypes);
  }
  if (value.kind() == MirRvalueKind::Arithmetic) {
    const auto& arithmetic = value.arithmeticValue();
    encoder.encodeUint8(static_cast<uint8_t>(arithmetic.op));
    return encodeOperand(encoder, arithmetic.left, module, identities, semanticTypes) &&
           encodeOperand(encoder, arithmetic.right, module, identities, semanticTypes) &&
           encodeType(encoder, arithmetic.resultType, semanticTypes);
  }
  const auto& aggregate = value.nominalAggregateValue();
  if (!encodeDefinition(encoder, aggregate.definition, identities) ||
      !encodeType(encoder, aggregate.type, semanticTypes)) {
    return false;
  }
  encoder.encodeSequenceSize(aggregate.elements.size());
  for (const auto& element : aggregate.elements) {
    if (!encodeDefinition(encoder, element.field, identities) ||
        !encodeOperand(encoder, element.operand, module, identities, semanticTypes)) {
      return false;
    }
  }
  return true;
}

bool encodeStatement(identity::CanonicalEncoder& encoder, const MirStatement& statement,
                     identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
                     const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(statement.kind()));
  switch (statement.kind()) {
    case MirStatementKind::Assign: {
      const auto& assignment = statement.assignmentValue();
      if (!encodePlace(encoder, assignment.destination, identities, semanticTypes) ||
          !encodeRvalue(encoder, assignment.value, module, identities, semanticTypes)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(assignment.initialization));
      return true;
    }
    case MirStatementKind::StorageLive:
    case MirStatementKind::StorageDead:
      encoder.encodeUint32(statement.storageLocal().ordinal());
      return statement.storageLocal().isValid();
    case MirStatementKind::BorrowCreation: {
      const auto& borrow = statement.borrowCreationValue();
      if (!encodePlace(encoder, borrow.destination, identities, semanticTypes)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(borrow.kind));
      return encodePlace(encoder, borrow.source, identities, semanticTypes);
    }
    case MirStatementKind::SetDiscriminant: {
      const auto& discriminant = statement.setDiscriminantValue();
      return encodePlace(encoder, discriminant.destination, identities, semanticTypes) &&
             encodeDefinition(encoder, discriminant.variant, identities);
    }
    case MirStatementKind::Deinitialize:
      return encodePlace(encoder, statement.deinitializeValue().destination, identities,
                         semanticTypes);
    case MirStatementKind::UnsafeScopeBoundary: {
      const auto& boundary = statement.unsafeScopeBoundaryValue();
      encoder.encodeUint8(static_cast<uint8_t>(boundary.kind));
      encoder.encodeUint32(boundary.scope.ordinal());
      return boundary.scope.isValid();
    }
  }
  return false;
}

bool encodeTerminator(identity::CanonicalEncoder& encoder, const MirTerminator& terminator,
                      identity::ModuleId module,
                      const checker::CheckerIdentityAuthority& identities,
                      const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(terminator.kind()));
  if (terminator.kind() == MirTerminatorKind::Unreachable) return true;
  if (terminator.kind() == MirTerminatorKind::Goto) {
    const auto& gotoTerminator = terminator.gotoValue();
    encoder.encodeUint32(gotoTerminator.target.ordinal());
    return gotoTerminator.target.isValid();
  }
  if (terminator.kind() == MirTerminatorKind::SwitchInt) {
    const auto& switchInt = terminator.switchIntValue();
    if (!encodeOperand(encoder, switchInt.discriminant, module, identities, semanticTypes)) {
      return false;
    }
    encoder.encodeSequenceSize(switchInt.arms.size());
    for (const auto& arm : switchInt.arms) {
      if (!encodeConstant(encoder, arm.value, module, identities, semanticTypes) ||
          !arm.target.isValid()) {
        return false;
      }
      encoder.encodeUint32(arm.target.ordinal());
    }
    encoder.encodeUint32(switchInt.defaultTarget.ordinal());
    return switchInt.defaultTarget.isValid();
  }
  if (terminator.kind() == MirTerminatorKind::Call) {
    const auto& call = terminator.callValue();
    if (!encodeDefinition(encoder, call.callee, identities) ||
        !encodePlace(encoder, call.destination, identities, semanticTypes) ||
        !call.normalTarget.isValid()) {
      return false;
    }
    encoder.encodeUint8(static_cast<uint8_t>(call.effect.kind()));
    ZC_IF_SOME(temporary, call.effect.activatedMutableReceiver()) {
      if (!temporary.isValid()) return false;
      encoder.encodeUint32(temporary.ordinal());
    }
    encoder.encodeSequenceSize(call.arguments.size());
    for (const auto& argument : call.arguments) {
      if (!encodeOperand(encoder, argument, module, identities, semanticTypes)) return false;
    }
    encoder.encodeUint32(call.normalTarget.ordinal());
    ZC_IF_SOME(target, call.unwindTarget) {
      if (!target.isValid()) return false;
      encoder.encodeSome();
      encoder.encodeUint32(target.ordinal());
    } else {
      encoder.encodeNone();
    }
    return true;
  }
  ZC_IF_SOME(value, terminator.returnValue().value) {
    encoder.encodeSome();
    return encodeOperand(encoder, value, module, identities, semanticTypes);
  }
  encoder.encodeNone();
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeFunction(const MirFunction& function, identity::ModuleId module,
                                             const checker::CheckerIdentityAuthority& identities,
                                             const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  if (!encodeDefinition(encoder, function.owner, identities)) return zc::none;
  encoder.encodeUint8(static_cast<uint8_t>(function.kind));
  encoder.encodeUint8(static_cast<uint8_t>(function.sourceDefinitionKind));
  if (!encodeType(encoder, function.resultType, semanticTypes)) return zc::none;
  function.sourceSpan.encode(encoder);
  encoder.encodeSequenceSize(function.sourceScopes.size());
  for (const auto& scope : function.sourceScopes) {
    if (!scope.id.isValid()) return zc::none;
    encoder.encodeUint32(scope.id.ordinal());
    ZC_IF_SOME(parent, scope.parent) {
      if (!parent.isValid()) return zc::none;
      encoder.encodeSome();
      encoder.encodeUint32(parent.ordinal());
    } else {
      encoder.encodeNone();
    }
    scope.sourceSpan.encode(encoder);
  }
  encoder.encodeSequenceSize(function.locals.size());
  for (const auto& local : function.locals) {
    if (!local.id.isValid() || !local.sourceScope.isValid()) return zc::none;
    encoder.encodeUint32(local.id.ordinal());
    encoder.encodeUint8(static_cast<uint8_t>(local.kind));
    if (!encodeType(encoder, local.type, semanticTypes)) return zc::none;
    encoder.encodeUint32(local.sourceScope.ordinal());
    local.sourceSpan.encode(encoder);
  }
  encoder.encodeSequenceSize(function.blocks.size());
  for (const auto& block : function.blocks) {
    if (!block.id.isValid() || !block.sourceScope.isValid()) return zc::none;
    encoder.encodeUint32(block.id.ordinal());
    encoder.encodeUint32(block.sourceScope.ordinal());
    encoder.encodeSequenceSize(block.statements.size());
    for (const auto& statement : block.statements) {
      if (!encodeStatement(encoder, statement, module, identities, semanticTypes)) {
        return zc::none;
      }
    }
    if (!encodeTerminator(encoder, block.terminator, module, identities, semanticTypes)) {
      return zc::none;
    }
  }
  return encoder.finish();
}

struct PendingMirFunction final {
  MirFunction function;
  zc::Array<uint8_t> ownerKey;
};

void sortFunctions(zc::Vector<PendingMirFunction>& functions) {
  for (size_t index = 1; index < functions.size(); ++index) {
    auto current = zc::mv(functions[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           lessBytes(current.ownerKey.asPtr(), functions[insertion - 1].ownerKey.asPtr())) {
      functions[insertion] = zc::mv(functions[insertion - 1]);
      --insertion;
    }
    functions[insertion] = zc::mv(current);
  }
}

zc::Maybe<const hir::HirScalarLiteralExpression&> expressionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirScalarLiteralExpression&> result;
  for (const auto& expression : module.expressions()) {
    if (expression.node != node) continue;
    if (result != zc::none) return zc::none;
    result = expression;
  }
  return result;
}

zc::Maybe<const hir::HirNominalAggregateExpression&> aggregateFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirNominalAggregateExpression&> result;
  for (const auto& aggregate : module.aggregates()) {
    if (aggregate.node != node) continue;
    if (result != zc::none) return zc::none;
    result = aggregate;
  }
  return result;
}

zc::Maybe<const hir::HirDirectCallExpression&> callFor(const hir::VerifiedHirModule& module,
                                                       hir::HirNodeId node) {
  zc::Maybe<const hir::HirDirectCallExpression&> result;
  for (const auto& call : module.calls()) {
    if (call.node != node) continue;
    if (result != zc::none) return zc::none;
    result = call;
  }
  return result;
}

zc::Maybe<const hir::HirReceiverCallExpression&> receiverCallFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirReceiverCallExpression&> result;
  for (const auto& call : module.receiverCalls()) {
    if (call.node != node) continue;
    if (result != zc::none) return zc::none;
    result = call;
  }
  return result;
}

zc::Maybe<const hir::HirLocalBinding&> localFor(const hir::VerifiedHirModule& module,
                                                hir::HirNodeId node) {
  zc::Maybe<const hir::HirLocalBinding&> result;
  for (const auto& local : module.locals()) {
    if (local.node != node) continue;
    if (result != zc::none) return zc::none;
    result = local;
  }
  return result;
}

zc::Maybe<const hir::HirLocalWriteStatement&> localWriteFor(const hir::VerifiedHirModule& module,
                                                            hir::HirNodeId node) {
  zc::Maybe<const hir::HirLocalWriteStatement&> result;
  for (const auto& overwrite : module.localWrites()) {
    if (overwrite.node != node) continue;
    if (result != zc::none) return zc::none;
    result = overwrite;
  }
  return result;
}

zc::Maybe<const hir::HirLocalReferenceExpression&> localReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirLocalReferenceExpression&> result;
  for (const auto& reference : module.localReferences()) {
    if (reference.node != node) continue;
    if (result != zc::none) return zc::none;
    result = reference;
  }
  return result;
}

zc::Maybe<const hir::HirLocalFieldProjectionExpression&> localFieldProjectionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirLocalFieldProjectionExpression&> result;
  for (const auto& projection : module.localFieldProjections()) {
    if (projection.node != node) continue;
    if (result != zc::none) return zc::none;
    result = projection;
  }
  return result;
}

zc::Maybe<const hir::HirParameterReferenceExpression&> parameterReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirParameterReferenceExpression&> result;
  for (const auto& reference : module.parameterReferences()) {
    if (reference.node != node) continue;
    if (result != zc::none) return zc::none;
    result = reference;
  }
  return result;
}

zc::Maybe<const hir::HirParameterReborrowExpression&> parameterReborrowFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirParameterReborrowExpression&> result;
  for (const auto& reborrow : module.parameterReborrows()) {
    if (reborrow.node != node) continue;
    if (result != zc::none) return zc::none;
    result = reborrow;
  }
  return result;
}

zc::Maybe<const hir::HirLocalBorrowExpression&> localBorrowFor(const hir::VerifiedHirModule& module,
                                                               hir::HirNodeId node) {
  zc::Maybe<const hir::HirLocalBorrowExpression&> result;
  for (const auto& borrow : module.localBorrows()) {
    if (borrow.node != node) continue;
    if (result != zc::none) return zc::none;
    result = borrow;
  }
  return result;
}

zc::Maybe<const hir::HirBlockStatement&> blockFor(const hir::VerifiedHirModule& module,
                                                  hir::HirNodeId node) {
  zc::Maybe<const hir::HirBlockStatement&> result;
  for (const auto& block : module.blocks()) {
    if (block.node != node) continue;
    if (result != zc::none) return zc::none;
    result = block;
  }
  return result;
}

zc::Maybe<const hir::HirReturnStatement&> returnFor(const hir::VerifiedHirModule& module,
                                                    hir::HirNodeId node) {
  zc::Maybe<const hir::HirReturnStatement&> result;
  for (const auto& statement : module.returns()) {
    if (statement.node != node) continue;
    if (result != zc::none) return zc::none;
    result = statement;
  }
  return result;
}

zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlockFor(const hir::VerifiedHirModule& module,
                                                               hir::HirNodeId node) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> result;
  for (const auto& block : module.unsafeBlocks()) {
    if (block.node != node) continue;
    if (result != zc::none) return zc::none;
    result = block;
  }
  return result;
}

zc::Maybe<const hir::HirConditionalExpression&> conditionalFor(const hir::VerifiedHirModule& module,
                                                               hir::HirNodeId node) {
  zc::Maybe<const hir::HirConditionalExpression&> result;
  for (const auto& conditional : module.conditionals()) {
    if (conditional.node != node) continue;
    if (result != zc::none) return zc::none;
    result = conditional;
  }
  return result;
}

zc::Maybe<const hir::HirPrimitiveBinaryExpression&> primitiveBinaryFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirPrimitiveBinaryExpression&> result;
  for (const auto& equality : module.primitiveBinaryOperations()) {
    if (equality.node != node) continue;
    if (result != zc::none) return zc::none;
    result = equality;
  }
  return result;
}

// Maps the HIR-carried relational operator to its Built MIR comparison operator.
// Only the six relational comparisons of same-typed scalars are lowerable.
zc::Maybe<MirComparisonOperator> mirComparisonOperatorFor(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Eq:
      return MirComparisonOperator::Eq;
    case checker::PrimitiveOperation::Ne:
      return MirComparisonOperator::Ne;
    case checker::PrimitiveOperation::Lt:
      return MirComparisonOperator::Lt;
    case checker::PrimitiveOperation::Le:
      return MirComparisonOperator::Le;
    case checker::PrimitiveOperation::Gt:
      return MirComparisonOperator::Gt;
    case checker::PrimitiveOperation::Ge:
      return MirComparisonOperator::Ge;
    default:
      return zc::none;
  }
}

// Maps the HIR-carried arithmetic or bitwise operator to its Built MIR
// arithmetic operator. Only the twelve arithmetic and bitwise binary operators
// of same-typed scalars are lowerable; the six relational comparisons and the
// logical short-circuit operators return none so their existing handling stands.
zc::Maybe<MirArithmeticOperator> mirArithmeticOperatorFor(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Add:
      return MirArithmeticOperator::Add;
    case checker::PrimitiveOperation::Sub:
      return MirArithmeticOperator::Sub;
    case checker::PrimitiveOperation::Mul:
      return MirArithmeticOperator::Mul;
    case checker::PrimitiveOperation::Div:
      return MirArithmeticOperator::Div;
    case checker::PrimitiveOperation::Rem:
      return MirArithmeticOperator::Rem;
    case checker::PrimitiveOperation::Pow:
      return MirArithmeticOperator::Pow;
    case checker::PrimitiveOperation::Shl:
      return MirArithmeticOperator::Shl;
    case checker::PrimitiveOperation::Shr:
      return MirArithmeticOperator::Shr;
    case checker::PrimitiveOperation::UShr:
      return MirArithmeticOperator::UShr;
    case checker::PrimitiveOperation::BitAnd:
      return MirArithmeticOperator::BitAnd;
    case checker::PrimitiveOperation::BitOr:
      return MirArithmeticOperator::BitOr;
    case checker::PrimitiveOperation::BitXor:
      return MirArithmeticOperator::BitXor;
    default:
      return zc::none;
  }
}

zc::Maybe<const hir::HirLoopStatement&> loopFor(const hir::VerifiedHirModule& module,
                                                hir::HirNodeId node) {
  zc::Maybe<const hir::HirLoopStatement&> result;
  for (const auto& loop : module.loops()) {
    if (loop.node != node) continue;
    if (result != zc::none) return zc::none;
    result = loop;
  }
  return result;
}

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const checker::CheckerIdentityAuthority& identities,
                  const type::SemanticTypeStore& semanticTypes) {
  auto leftRecord =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          left, module, identities, semanticTypes);
  auto rightRecord =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          right, module, identities, semanticTypes);
  if (leftRecord == zc::none || rightRecord == zc::none) return false;
  bool same = false;
  ZC_IF_SOME(leftBytes, leftRecord) {
    ZC_IF_SOME(rightBytes, rightRecord) { same = leftBytes.asPtr() == rightBytes.asPtr(); }
  }
  return same;
}

bool validBuiltMirInput(const hir::VerifiedHirModule& hirModule,
                        const checker::body::BodyCheckingInput& body) {
  const auto copy = body.standardMarkers.copy();
  return copy.isValid() && body.boundModule.semanticContext() == hirModule.semanticContext() &&
         body.boundModule.module() == hirModule.module() &&
         body.identities.semanticContext() == hirModule.semanticContext() &&
         body.identities.fingerprint().digest() == hirModule.contextFingerprint().digest();
}

zc::Maybe<MirOperandKind> placeUseKind(checker::marker::MarkerProofEngine& proofs,
                                       identity::DefId copy, identity::SemanticTypeId type) {
  auto result = proofs.prove(copy, type);
  if (result.is<checker::marker::MarkerProofPositive>()) return MirOperandKind::Copy;
  if (result.is<checker::marker::MarkerProofNegative>() ||
      result.is<checker::marker::MarkerProofUnsatisfied>()) {
    return MirOperandKind::Move;
  }
  return zc::none;
}

zc::Maybe<MirOperand> placeUse(checker::marker::MarkerProofEngine& proofs, identity::DefId copy,
                               MirPlace&& place) {
  auto kind = placeUseKind(proofs, copy, place.resultType());
  if (kind == zc::none) return zc::none;
  ZC_IF_SOME(value, kind) {
    if (value == MirOperandKind::Copy) return MirOperand::copy(zc::mv(place));
    return MirOperand::move(zc::mv(place));
  }
  ZC_UNREACHABLE
}

bool matchesPlaceUse(const MirOperand& operand, checker::marker::MarkerProofEngine& proofs,
                     identity::DefId copy, identity::SemanticTypeId type) {
  auto kind = placeUseKind(proofs, copy, type);
  return kind != zc::none && operand.kind() == ZC_ASSERT_NONNULL(kind);
}

bool validScalarFunction(const MirFunction& function, const hir::HirValueDeclaration& declaration,
                         const hir::HirScalarLiteralExpression& expression,
                         identity::ModuleId module,
                         const checker::CheckerIdentityAuthority& identities,
                         const type::SemanticTypeStore& semanticTypes,
                         checker::marker::MarkerProofEngine& proofs, identity::DefId copy) {
  if (function.owner != declaration.definition ||
      function.kind != MirFunctionKind::ModuleInitializer ||
      function.sourceDefinitionKind != declaration.definitionKind ||
      function.resultType != declaration.inferredType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::ModuleInitializerResult ||
      local.type != declaration.inferredType || local.sourceScope != scope.id ||
      !sameSpan(local.sourceSpan, expression.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 2 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), expression.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      !sameSpan(block.statements[1].sourceSpan(), expression.sourceSpan)) {
    return false;
  }
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.rootType() != local.type ||
      assignment.destination.resultType() != local.type ||
      assignment.destination.projections().size() != 0 ||
      assignment.initialization != MirInitializationKind::Initialize ||
      assignment.value.kind() != MirRvalueKind::Use ||
      assignment.value.useValue().operand.kind() != MirOperandKind::Constant) {
    return false;
  }
  const auto& constant = assignment.value.useValue().operand.constantValue();
  if (constant.type != expression.type ||
      !sameConstant(constant.value, expression.value, module, identities, semanticTypes) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), expression.sourceSpan)) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn = matchesPlaceUse(value, proofs, copy, local.type) &&
                  value.place().local() == local.id && value.place().rootType() == local.type &&
                  value.place().resultType() == local.type &&
                  value.place().projections().size() == 0;
  }
  return validReturn;
}

bool validScalarReturnFunction(const MirFunction& function, const hir::VerifiedHirModule& hirModule,
                               const hir::HirFunctionDeclaration& declaration,
                               const hir::HirBlockStatement& sourceBlock,
                               const hir::HirReturnStatement& sourceReturn,
                               const hir::HirScalarLiteralExpression& expression,
                               identity::ModuleId module,
                               const checker::CheckerIdentityAuthority& identities,
                               const type::SemanticTypeStore& semanticTypes) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) || function.locals.size() != 0 ||
      function.blocks.size() != 1 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 1 || sourceBlock.statements[0] != sourceReturn.node ||
      sourceReturn.value != expression.node || sourceReturn.resultType != declaration.resultType ||
      expression.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != (hasUnsafeBlock ? 2 : 0) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan)) {
        return false;
      }
      if (block.statements[0].kind() != MirStatementKind::UnsafeScopeBoundary ||
          block.statements[1].kind() != MirStatementKind::UnsafeScopeBoundary) {
        return false;
      }
      const auto& enter = block.statements[0].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[1].unsafeScopeBoundaryValue();
      if (enter.kind != MirUnsafeScopeBoundaryKind::Enter || enter.scope != scopeId(2) ||
          exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
          !sameSpan(block.statements[0].sourceSpan(), unsafeBlockRef.sourceSpan) ||
          !sameSpan(block.statements[1].sourceSpan(), unsafeBlockRef.sourceSpan)) {
        return false;
      }
    }
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn = value.kind() == MirOperandKind::Constant &&
                  value.constantValue().type == expression.type &&
                  sameConstant(value.constantValue().value, expression.value, module, identities,
                               semanticTypes);
  }
  return validReturn;
}

// One conditional arm as seen by the MIR verifier: either a scalar-literal
// expression or a parameter reference. Exactly one Maybe is populated.
struct ConditionalArmView final {
  zc::Maybe<const hir::HirScalarLiteralExpression&> literal;
  zc::Maybe<const hir::HirParameterReferenceExpression&> parameter;
};

bool validConditionalReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirReturnStatement& sourceReturn,
    const hir::HirConditionalExpression& conditional,
    const hir::HirParameterReferenceExpression& conditionRef, const ConditionalArmView& thenArm,
    const ConditionalArmView& elseArm, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  // Resolve each arm to its node id and semantic type independent of arm kind.
  auto armNode = [](const ConditionalArmView& arm) -> zc::Maybe<hir::HirNodeId> {
    ZC_IF_SOME(value, arm.literal) { return value.node; }
    ZC_IF_SOME(value, arm.parameter) { return value.node; }
    return zc::none;
  };
  auto armType = [](const ConditionalArmView& arm) -> zc::Maybe<identity::SemanticTypeId> {
    ZC_IF_SOME(value, arm.literal) { return value.type; }
    ZC_IF_SOME(value, arm.parameter) { return value.type; }
    return zc::none;
  };
  auto thenNode = armNode(thenArm);
  auto elseNode = armNode(elseArm);
  auto thenTypeValue = armType(thenArm);
  auto elseTypeValue = armType(elseArm);
  if (thenNode == zc::none || elseNode == zc::none || thenTypeValue == zc::none ||
      elseTypeValue == zc::none) {
    return false;
  }
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 1 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != conditional.node ||
      sourceReturn.resultType != declaration.resultType ||
      conditional.condition != conditionRef.node ||
      conditional.thenReturnValue != ZC_ASSERT_NONNULL(thenNode) ||
      conditional.elseReturnValue != ZC_ASSERT_NONNULL(elseNode) ||
      conditional.type != declaration.resultType ||
      ZC_ASSERT_NONNULL(thenTypeValue) != declaration.resultType ||
      ZC_ASSERT_NONNULL(elseTypeValue) != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal = localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
  const auto& result = function.locals[declaration.parameters.size()];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 1 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::SwitchInt || thenBlock.id != blockId(2) ||
      thenBlock.sourceScope != scopeId(1) || thenBlock.statements.size() != 1 ||
      thenBlock.terminator.kind() != MirTerminatorKind::Goto ||
      thenBlock.terminator.gotoValue().target != blockId(4) || elseBlock.id != blockId(3) ||
      elseBlock.sourceScope != scopeId(1) || elseBlock.statements.size() != 1 ||
      elseBlock.terminator.kind() != MirTerminatorKind::Goto ||
      elseBlock.terminator.gotoValue().target != blockId(4) || joinBlock.id != blockId(4) ||
      joinBlock.sourceScope != scopeId(1) || joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.arms.size() != 2 || switchInt.defaultTarget != blockId(3)) { return false; }
  const auto& trueArm = switchInt.arms[0];
  const auto& falseArm = switchInt.arms[1];
  if (trueArm.target != blockId(2) || falseArm.target != blockId(3)) { return false; }
  auto trueValue = trueArm.value.booleanValue();
  auto falseValue = falseArm.value.booleanValue();
  if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
      ZC_ASSERT_NONNULL(falseValue)) {
    return false;
  }
  auto parameterLocalIndex = [&](const hir::HirParameterReferenceExpression& reference,
                                 size_t& outIndex) -> bool {
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == reference.parameter) {
        outIndex = i;
        return true;
      }
    }
    return false;
  };
  size_t conditionIndex = 0;
  if (!parameterLocalIndex(conditionRef, conditionIndex)) return false;
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() !=
          localId(static_cast<uint32_t>(conditionIndex + 1)) ||
      switchInt.discriminant.place().rootType() != conditionRef.type ||
      switchInt.discriminant.place().resultType() != conditionRef.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Each branch initializes the result local with the arm value, then jumps to
  // the join block. The single Return in the join reads that result local. A
  // literal arm assigns a constant; a parameter arm assigns a place-use of the
  // parameter local.
  auto branchInitializesResult = [&](const MirBasicBlock& branch,
                                     const ConditionalArmView& arm) -> bool {
    if (branch.statements[0].kind() != MirStatementKind::Assign) { return false; }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != resultLocal ||
        assignment.destination.rootType() != declaration.resultType ||
        assignment.destination.resultType() != declaration.resultType ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    ZC_IF_SOME(literal, arm.literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == literal.type &&
             sameConstant(operand.constantValue().value, literal.value, module, identities,
                          semanticTypes);
    }
    ZC_IF_SOME(parameter, arm.parameter) {
      size_t parameterIndex = 0;
      if (!parameterLocalIndex(parameter, parameterIndex)) return false;
      return matchesPlaceUse(operand, proofs, copy, parameter.type) &&
             operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
             operand.place().rootType() == parameter.type &&
             operand.place().resultType() == parameter.type &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (!branchInitializesResult(thenBlock, thenArm) ||
      !branchInitializesResult(elseBlock, elseArm)) {
    return false;
  }
  ZC_IF_SOME(value, joinBlock.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validEqualityConditionalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirReturnStatement& sourceReturn, const hir::HirConditionalExpression& conditional,
    const hir::HirPrimitiveBinaryExpression& equality, const ConditionalArmView& thenArm,
    const ConditionalArmView& elseArm, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  // Resolve each arm to its node id and semantic type independent of arm kind.
  auto armNode = [](const ConditionalArmView& arm) -> zc::Maybe<hir::HirNodeId> {
    ZC_IF_SOME(value, arm.literal) { return value.node; }
    ZC_IF_SOME(value, arm.parameter) { return value.node; }
    return zc::none;
  };
  auto armType = [](const ConditionalArmView& arm) -> zc::Maybe<identity::SemanticTypeId> {
    ZC_IF_SOME(value, arm.literal) { return value.type; }
    ZC_IF_SOME(value, arm.parameter) { return value.type; }
    return zc::none;
  };
  auto thenNode = armNode(thenArm);
  auto elseNode = armNode(elseArm);
  auto thenTypeValue = armType(thenArm);
  auto elseTypeValue = armType(elseArm);
  if (thenNode == zc::none || elseNode == zc::none || thenTypeValue == zc::none ||
      elseTypeValue == zc::none) {
    return false;
  }
  // The equality condition allocates one extra bool temporary after the function
  // result local, so the local count is parameters + 2 (result + temp).
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 2 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != conditional.node ||
      sourceReturn.resultType != declaration.resultType || conditional.condition != equality.node ||
      conditional.thenReturnValue != ZC_ASSERT_NONNULL(thenNode) ||
      conditional.elseReturnValue != ZC_ASSERT_NONNULL(elseNode) ||
      conditional.type != declaration.resultType ||
      ZC_ASSERT_NONNULL(thenTypeValue) != declaration.resultType ||
      ZC_ASSERT_NONNULL(elseTypeValue) != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal = localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
  const auto conditionTemp = localId(static_cast<uint32_t>(declaration.parameters.size() + 2));
  const auto& result = function.locals[declaration.parameters.size()];
  const auto& temp = function.locals[declaration.parameters.size() + 1];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan) || temp.id != conditionTemp ||
      temp.kind != MirLocalKind::Temporary || temp.type != equality.type ||
      temp.sourceScope != scopeId(1) || !sameSpan(temp.sourceSpan, equality.sourceSpan)) {
    return false;
  }
  auto parameterLocalIndex = [&](const hir::HirParameterReferenceExpression& reference,
                                 size_t& outIndex) -> bool {
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == reference.parameter) {
        outIndex = i;
        return true;
      }
    }
    return false;
  };
  // Each comparison operand is a scalar-literal expression or a parameter
  // reference; exactly one lookup succeeds per operand, at least one is a
  // parameter, and the shared operand type comes from a parameter operand.
  auto leftLiteral = expressionFor(hirModule, equality.left);
  auto leftRef = parameterReferenceFor(hirModule, equality.left);
  auto rightLiteral = expressionFor(hirModule, equality.right);
  auto rightRef = parameterReferenceFor(hirModule, equality.right);
  const bool leftOperandOk = (leftLiteral != zc::none) != (leftRef != zc::none);
  const bool rightOperandOk = (rightLiteral != zc::none) != (rightRef != zc::none);
  if (!leftOperandOk || !rightOperandOk || (leftRef == zc::none && rightRef == zc::none)) {
    return false;
  }
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  identity::SemanticTypeId operandType;
  bool refsOk = true;
  ZC_IF_SOME(value, leftRef) {
    operandType = value.type;
    refsOk &= parameterLocalIndex(value, leftIndex);
  }
  ZC_IF_SOME(value, rightRef) {
    operandType = value.type;
    refsOk &= parameterLocalIndex(value, rightIndex);
  }
  ZC_IF_SOME(leftValue, leftRef) {
    ZC_IF_SOME(rightValue, rightRef) { refsOk &= leftValue.type == rightValue.type; }
  }
  refsOk &= equality.operandType == operandType;
  ZC_IF_SOME(value, leftLiteral) { refsOk &= value.type == operandType; }
  ZC_IF_SOME(value, rightLiteral) { refsOk &= value.type == operandType; }
  if (!refsOk) return false;
  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];
  // Entry block layout: StorageLive(result), StorageLive(temp), Assign(temp =
  // Comparison{Eq, copy(left), copy(right)}), then SwitchInt(copy(temp)).
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 3 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::StorageLive ||
      entry.statements[1].storageLocal() != conditionTemp ||
      !sameSpan(entry.statements[1].sourceSpan(), equality.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[2].sourceSpan(), equality.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::SwitchInt || thenBlock.id != blockId(2) ||
      thenBlock.sourceScope != scopeId(1) || thenBlock.statements.size() != 1 ||
      thenBlock.terminator.kind() != MirTerminatorKind::Goto ||
      thenBlock.terminator.gotoValue().target != blockId(4) || elseBlock.id != blockId(3) ||
      elseBlock.sourceScope != scopeId(1) || elseBlock.statements.size() != 1 ||
      elseBlock.terminator.kind() != MirTerminatorKind::Goto ||
      elseBlock.terminator.gotoValue().target != blockId(4) || joinBlock.id != blockId(4) ||
      joinBlock.sourceScope != scopeId(1) || joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  // The comparison assignment initializes the bool temporary from an Eq of the
  // two parameter locals of the shared operand type.
  const auto& tempAssign = entry.statements[2].assignmentValue();
  if (tempAssign.initialization != MirInitializationKind::Initialize ||
      tempAssign.destination.local() != conditionTemp ||
      tempAssign.destination.rootType() != equality.type ||
      tempAssign.destination.resultType() != equality.type ||
      tempAssign.destination.projections().size() != 0 ||
      tempAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& comparison = tempAssign.value.comparisonValue();
  // The MIR comparison operator must be the one mapped from the HIR-carried
  // relational operator; any other byte is a lowering defect.
  auto expectedOperator = mirComparisonOperatorFor(equality.operation);
  if (expectedOperator == zc::none || comparison.op != ZC_ASSERT_NONNULL(expectedOperator) ||
      comparison.resultType != equality.type) {
    return false;
  }
  // Each comparison operand matches its HIR operand: a literal operand is a
  // Constant of the operand type and value; a parameter operand is a copy of the
  // parameter local with zero projections.
  auto operandMatches = [&](const MirOperand& operand,
                            zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                            zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                            size_t parameterIndex) -> bool {
    ZC_IF_SOME(value, literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == operandType &&
             sameConstant(operand.constantValue().value, value.value, module, identities,
                          semanticTypes);
    }
    ZC_IF_SOME(value, parameter) {
      (void)value;
      const auto local = localId(static_cast<uint32_t>(parameterIndex + 1));
      return matchesPlaceUse(operand, proofs, copy, operandType) &&
             operand.place().local() == local && operand.place().rootType() == operandType &&
             operand.place().resultType() == operandType &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (!operandMatches(comparison.left, leftLiteral, leftRef, leftIndex) ||
      !operandMatches(comparison.right, rightLiteral, rightRef, rightIndex)) {
    return false;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.arms.size() != 2 || switchInt.defaultTarget != blockId(3)) { return false; }
  const auto& trueArm = switchInt.arms[0];
  const auto& falseArm = switchInt.arms[1];
  if (trueArm.target != blockId(2) || falseArm.target != blockId(3)) { return false; }
  auto trueValue = trueArm.value.booleanValue();
  auto falseValue = falseArm.value.booleanValue();
  if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
      ZC_ASSERT_NONNULL(falseValue)) {
    return false;
  }
  // The discriminant is a copy of the bool temporary with zero projections.
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionTemp ||
      switchInt.discriminant.place().rootType() != equality.type ||
      switchInt.discriminant.place().resultType() != equality.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  auto branchInitializesResult = [&](const MirBasicBlock& branch,
                                     const ConditionalArmView& arm) -> bool {
    if (branch.statements[0].kind() != MirStatementKind::Assign) { return false; }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != resultLocal ||
        assignment.destination.rootType() != declaration.resultType ||
        assignment.destination.resultType() != declaration.resultType ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    ZC_IF_SOME(literal, arm.literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == literal.type &&
             sameConstant(operand.constantValue().value, literal.value, module, identities,
                          semanticTypes);
    }
    ZC_IF_SOME(parameter, arm.parameter) {
      size_t parameterIndex = 0;
      if (!parameterLocalIndex(parameter, parameterIndex)) return false;
      return matchesPlaceUse(operand, proofs, copy, parameter.type) &&
             operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
             operand.place().rootType() == parameter.type &&
             operand.place().resultType() == parameter.type &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (!branchInitializesResult(thenBlock, thenArm) ||
      !branchInitializesResult(elseBlock, elseArm)) {
    return false;
  }
  ZC_IF_SOME(value, joinBlock.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

// Verifies a `return <a CMP b>` function: a single block that computes the
// comparison into the bool function-result local and returns it. Each operand is
// a scalar-literal constant or a copy of the compared parameter local. The
// entry-block layout is StorageLive(result), Assign(result = Comparison{...}),
// then a Return reading the result local.
bool validComparisonReturnFunction(const MirFunction& function,
                                   const hir::VerifiedHirModule& hirModule,
                                   const hir::HirFunctionDeclaration& declaration,
                                   const hir::HirBlockStatement& sourceBlock,
                                   const hir::HirReturnStatement& sourceReturn,
                                   const hir::HirPrimitiveBinaryExpression& comparison,
                                   checker::marker::MarkerProofEngine& proofs, identity::DefId copy,
                                   identity::ModuleId module,
                                   const checker::CheckerIdentityAuthority& identities,
                                   const type::SemanticTypeStore& semanticTypes) {
  // The comparison allocates one bool result local after the parameters.
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != comparison.node ||
      sourceReturn.resultType != declaration.resultType ||
      comparison.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal = localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
  const auto& result = function.locals[declaration.parameters.size()];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  auto parameterLocalIndex = [&](const hir::HirParameterReferenceExpression& reference,
                                 size_t& outIndex) -> bool {
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == reference.parameter) {
        outIndex = i;
        return true;
      }
    }
    return false;
  };
  // Each operand is a scalar-literal expression or a parameter reference;
  // exactly one lookup succeeds per operand, at least one is a parameter, and the
  // shared operand type comes from a parameter operand.
  auto leftLiteral = expressionFor(hirModule, comparison.left);
  auto leftRef = parameterReferenceFor(hirModule, comparison.left);
  auto rightLiteral = expressionFor(hirModule, comparison.right);
  auto rightRef = parameterReferenceFor(hirModule, comparison.right);
  const bool leftOperandOk = (leftLiteral != zc::none) != (leftRef != zc::none);
  const bool rightOperandOk = (rightLiteral != zc::none) != (rightRef != zc::none);
  if (!leftOperandOk || !rightOperandOk || (leftRef == zc::none && rightRef == zc::none)) {
    return false;
  }
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  identity::SemanticTypeId operandType;
  bool refsOk = true;
  ZC_IF_SOME(value, leftRef) {
    operandType = value.type;
    refsOk &= parameterLocalIndex(value, leftIndex);
  }
  ZC_IF_SOME(value, rightRef) {
    operandType = value.type;
    refsOk &= parameterLocalIndex(value, rightIndex);
  }
  ZC_IF_SOME(leftValue, leftRef) {
    ZC_IF_SOME(rightValue, rightRef) { refsOk &= leftValue.type == rightValue.type; }
  }
  refsOk &= comparison.operandType == operandType;
  ZC_IF_SOME(value, leftLiteral) { refsOk &= value.type == operandType; }
  ZC_IF_SOME(value, rightLiteral) { refsOk &= value.type == operandType; }
  if (!refsOk) return false;
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 2 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[1].sourceSpan(), comparison.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Return ||
      !sameSpan(entry.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = entry.statements[1].assignmentValue();
  // A comparison lowers to a Comparison rvalue whose result is bool; an
  // arithmetic operator lowers to an Arithmetic rvalue whose result equals the
  // operand type. Resolve the matching operator family and read the two operands
  // from whichever rvalue was emitted.
  const auto expectedComparison = mirComparisonOperatorFor(comparison.operation);
  const auto expectedArithmetic = mirArithmeticOperatorFor(comparison.operation);
  if (assignment.initialization != MirInitializationKind::Initialize ||
      assignment.destination.local() != resultLocal ||
      assignment.destination.rootType() != declaration.resultType ||
      assignment.destination.resultType() != declaration.resultType ||
      assignment.destination.projections().size() != 0) {
    return false;
  }
  zc::Maybe<const MirOperand&> rvalueLeft;
  zc::Maybe<const MirOperand&> rvalueRight;
  if (assignment.value.kind() == MirRvalueKind::Comparison) {
    const auto& comparisonValue = assignment.value.comparisonValue();
    if (expectedComparison == zc::none ||
        comparisonValue.op != ZC_ASSERT_NONNULL(expectedComparison) ||
        comparisonValue.resultType != comparison.type) {
      return false;
    }
    rvalueLeft = comparisonValue.left;
    rvalueRight = comparisonValue.right;
  } else if (assignment.value.kind() == MirRvalueKind::Arithmetic) {
    const auto& arithmeticValue = assignment.value.arithmeticValue();
    // An arithmetic result is the operand type, never bool.
    if (expectedArithmetic == zc::none ||
        arithmeticValue.op != ZC_ASSERT_NONNULL(expectedArithmetic) ||
        arithmeticValue.resultType != comparison.type || comparison.type != operandType) {
      return false;
    }
    rvalueLeft = arithmeticValue.left;
    rvalueRight = arithmeticValue.right;
  } else {
    return false;
  }
  auto operandMatches = [&](const MirOperand& operand,
                            zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                            zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                            size_t parameterIndex) -> bool {
    ZC_IF_SOME(value, literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == operandType &&
             sameConstant(operand.constantValue().value, value.value, module, identities,
                          semanticTypes);
    }
    ZC_IF_SOME(value, parameter) {
      (void)value;
      const auto local = localId(static_cast<uint32_t>(parameterIndex + 1));
      return matchesPlaceUse(operand, proofs, copy, operandType) &&
             operand.place().local() == local && operand.place().rootType() == operandType &&
             operand.place().resultType() == operandType &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (rvalueLeft == zc::none || rvalueRight == zc::none ||
      !operandMatches(ZC_ASSERT_NONNULL(rvalueLeft), leftLiteral, leftRef, leftIndex) ||
      !operandMatches(ZC_ASSERT_NONNULL(rvalueRight), rightLiteral, rightRef, rightIndex)) {
    return false;
  }
  ZC_IF_SOME(value, entry.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validLoopReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirReturnStatement& sourceReturn,
    const hir::HirLoopStatement& loop, const hir::HirParameterReferenceExpression& conditionRef,
    const hir::HirScalarLiteralExpression& returnValue, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 1 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != loop.node || sourceBlock.statements[1] != sourceReturn.node ||
      sourceReturn.value != returnValue.node || sourceReturn.resultType != declaration.resultType ||
      loop.condition != conditionRef.node || loop.type != conditionRef.type ||
      returnValue.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal = localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
  const auto& result = function.locals[declaration.parameters.size()];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];
  // Reducible four-block loop CFG:
  //   bb1 entry:  StorageLive(result) ; Goto(bb2)
  //   bb2 header: SwitchInt(cond, [true -> bb3], default = bb4)
  //   bb3 body:   Goto(bb2)   (reducible back-edge; bb2 dominates bb3)
  //   bb4 exit:   Assign(result = literal, Initialize) ; Return(placeUse(result))
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 1 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != blockId(2) || header.id != blockId(2) ||
      header.sourceScope != scopeId(1) || header.statements.size() != 0 ||
      header.terminator.kind() != MirTerminatorKind::SwitchInt || body.id != blockId(3) ||
      body.sourceScope != scopeId(1) || body.statements.size() != 0 ||
      body.terminator.kind() != MirTerminatorKind::Goto ||
      body.terminator.gotoValue().target != blockId(2) || exit.id != blockId(4) ||
      exit.sourceScope != scopeId(1) || exit.statements.size() != 1 ||
      exit.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.arms.size() != 1 || switchInt.defaultTarget != blockId(4)) { return false; }
  const auto& trueArm = switchInt.arms[0];
  if (trueArm.target != blockId(3)) { return false; }
  auto trueValue = trueArm.value.booleanValue();
  if (trueValue == zc::none || !ZC_ASSERT_NONNULL(trueValue)) { return false; }
  size_t conditionIndex = 0;
  bool found = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == conditionRef.parameter) {
      conditionIndex = i;
      found = true;
      break;
    }
  }
  if (!found) return false;
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() !=
          localId(static_cast<uint32_t>(conditionIndex + 1)) ||
      switchInt.discriminant.place().rootType() != conditionRef.type ||
      switchInt.discriminant.place().resultType() != conditionRef.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // The exit block initializes the result local with the return literal, then
  // returns it.
  if (exit.statements[0].kind() != MirStatementKind::Assign) { return false; }
  const auto& assignment = exit.statements[0].assignmentValue();
  if (assignment.initialization != MirInitializationKind::Initialize ||
      assignment.destination.local() != resultLocal ||
      assignment.destination.rootType() != declaration.resultType ||
      assignment.destination.resultType() != declaration.resultType ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::Use ||
      assignment.value.useValue().operand.kind() != MirOperandKind::Constant ||
      assignment.value.useValue().operand.constantValue().type != returnValue.type ||
      !sameConstant(assignment.value.useValue().operand.constantValue().value, returnValue.value,
                    module, identities, semanticTypes)) {
    return false;
  }
  ZC_IF_SOME(value, exit.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validParameterReturnFunction(const MirFunction& function,
                                  const hir::HirFunctionDeclaration& declaration,
                                  const hir::HirBlockStatement& sourceBlock,
                                  const hir::HirReturnStatement& sourceReturn,
                                  const hir::HirParameterReferenceExpression& reference,
                                  checker::marker::MarkerProofEngine& proofs,
                                  identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.parameters.size() != 1 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 1 || sourceBlock.statements[0] != sourceReturn.node ||
      sourceReturn.value != reference.node || reference.type != declaration.resultType ||
      reference.category != hir::HirValueCategory::Place ||
      declaration.parameters[0].key != reference.parameter ||
      declaration.parameters[0].type != reference.type) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& parameter = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || parameter.id != localId(1) ||
      parameter.kind != MirLocalKind::Parameter || parameter.type != reference.type ||
      parameter.sourceScope != scope.id ||
      !sameSpan(parameter.sourceSpan, declaration.parameters[0].sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 0 ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, parameter.type) &&
           value.place().local() == parameter.id && value.place().rootType() == parameter.type &&
           value.place().resultType() == parameter.type && value.place().projections().size() == 0;
  }
  return false;
}

bool validParameterReborrowReturnFunction(const MirFunction& function,
                                          const hir::VerifiedHirModule& hirModule,
                                          const hir::HirFunctionDeclaration& declaration,
                                          const hir::HirBlockStatement& sourceBlock,
                                          const hir::HirReturnStatement& sourceReturn,
                                          const hir::HirParameterReborrowExpression& reborrow,
                                          checker::marker::MarkerProofEngine& proofs,
                                          identity::DefId copy) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  if (function.owner != declaration.definition || function.resultType != declaration.resultType ||
      declaration.parameters.size() != 1 || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != reborrow.node ||
      reborrow.type != declaration.resultType ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) || function.locals.size() != 2 ||
      function.blocks.size() != 1)
    return false;
  const auto& parameter = function.locals[0];
  const auto& temporary = function.locals[1];
  const auto& block = function.blocks[0];
  if (parameter.id != localId(1) || parameter.kind != MirLocalKind::Parameter ||
      parameter.type != reborrow.sourceType || temporary.id != localId(2) ||
      temporary.kind != MirLocalKind::Temporary || temporary.type != reborrow.type ||
      block.statements.size() != (hasUnsafeBlock ? 4 : 2) ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != temporary.id ||
      block.statements[1].kind() != MirStatementKind::BorrowCreation ||
      !sameSpan(block.statements[1].sourceSpan(), reborrow.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none)
    return false;
  const auto& borrow = block.statements[1].borrowCreationValue();
  const auto expectedBorrowKind = reborrow.mutability == type::semantic::Mutability::Const
                                      ? MirBorrowKind::Shared
                                      : MirBorrowKind::Mutable;
  if (borrow.kind != expectedBorrowKind || borrow.destination.local() != temporary.id ||
      borrow.destination.rootType() != reborrow.type ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != parameter.id ||
      borrow.source.rootType() != reborrow.sourceType || borrow.source.projections().size() != 1 ||
      borrow.source.projections()[0].kind() != MirProjectionKind::Dereference ||
      borrow.source.projections()[0].inputType() != reborrow.sourceType ||
      borrow.source.projections()[0].resultType() != reborrow.type)
    return false;
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan) ||
          block.statements[2].kind() != MirStatementKind::UnsafeScopeBoundary ||
          block.statements[3].kind() != MirStatementKind::UnsafeScopeBoundary) {
        return false;
      }
      const auto& enter = block.statements[2].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[3].unsafeScopeBoundaryValue();
      if (enter.kind != MirUnsafeScopeBoundaryKind::Enter || enter.scope != scopeId(2) ||
          exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
          !sameSpan(block.statements[2].sourceSpan(), unsafeBlockRef.sourceSpan) ||
          !sameSpan(block.statements[3].sourceSpan(), unsafeBlockRef.sourceSpan)) {
        return false;
      }
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, reborrow.type) &&
           value.place().local() == temporary.id && value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalBorrowReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirLocalBinding& sourceLocal, const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalBorrowExpression& borrow, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) || function.locals.size() != 2 ||
      function.blocks.size() != 1 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 2 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceReturn.value != borrow.node ||
      borrow.local != sourceLocal.local || borrow.type != declaration.resultType ||
      borrow.sourceType != sourceLocal.type) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& temporary = function.locals[1];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != borrow.sourceType ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      temporary.id != localId(2) || temporary.kind != MirLocalKind::Temporary ||
      temporary.type != borrow.type || temporary.sourceScope != scope.id ||
      !sameSpan(temporary.sourceSpan, borrow.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != (hasUnsafeBlock ? 6 : 4) ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[1].assignmentValue().destination.local() != local.id ||
      block.statements[1].assignmentValue().destination.rootType() != local.type ||
      block.statements[1].assignmentValue().destination.resultType() != local.type ||
      block.statements[1].assignmentValue().destination.projections().size() != 0 ||
      block.statements[1].assignmentValue().value.kind() != MirRvalueKind::Use ||
      block.statements[1].assignmentValue().value.useValue().operand.kind() !=
          MirOperandKind::Constant ||
      block.statements[2].kind() != MirStatementKind::StorageLive ||
      block.statements[2].storageLocal() != temporary.id ||
      !sameSpan(block.statements[2].sourceSpan(), borrow.sourceSpan) ||
      block.statements[3].kind() != MirStatementKind::BorrowCreation ||
      !sameSpan(block.statements[3].sourceSpan(), borrow.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& borrowStatement = block.statements[3].borrowCreationValue();
  const auto expectedBorrowKind = borrow.mutability == type::semantic::Mutability::Const
                                      ? MirBorrowKind::Shared
                                      : MirBorrowKind::Mutable;
  if (borrowStatement.kind != expectedBorrowKind ||
      borrowStatement.destination.local() != temporary.id ||
      borrowStatement.destination.rootType() != borrow.type ||
      borrowStatement.destination.resultType() != borrow.type ||
      borrowStatement.destination.projections().size() != 0 ||
      borrowStatement.source.local() != local.id ||
      borrowStatement.source.rootType() != borrow.sourceType ||
      borrowStatement.source.resultType() != borrow.sourceType ||
      borrowStatement.source.projections().size() != 0) {
    return false;
  }
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan) ||
          block.statements[4].kind() != MirStatementKind::UnsafeScopeBoundary ||
          block.statements[5].kind() != MirStatementKind::UnsafeScopeBoundary) {
        return false;
      }
      const auto& enter = block.statements[4].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[5].unsafeScopeBoundaryValue();
      if (enter.kind != MirUnsafeScopeBoundaryKind::Enter || enter.scope != scopeId(2) ||
          exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
          !sameSpan(block.statements[4].sourceSpan(), unsafeBlockRef.sourceSpan) ||
          !sameSpan(block.statements[5].sourceSpan(), unsafeBlockRef.sourceSpan)) {
        return false;
      }
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, borrow.type) &&
           value.place().local() == temporary.id && value.place().rootType() == borrow.type &&
           value.place().resultType() == borrow.type && value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalAliasReborrowReturnFunction(const MirFunction& function,
                                           const hir::VerifiedHirModule& hirModule,
                                           const hir::HirFunctionDeclaration& declaration,
                                           const hir::HirBlockStatement& sourceBlock,
                                           const hir::HirLocalBinding& sourceLocal,
                                           const hir::HirParameterReferenceExpression& initializer,
                                           const hir::HirReturnStatement& sourceReturn,
                                           const hir::HirParameterReborrowExpression& reborrow,
                                           checker::marker::MarkerProofEngine& proofs,
                                           identity::DefId copy) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  if (function.owner != declaration.definition || function.resultType != declaration.resultType ||
      declaration.parameters.size() != 1 ||
      declaration.parameters[0].key != initializer.parameter ||
      declaration.parameters[0].type != initializer.type || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node || sourceReturn.value != reborrow.node ||
      reborrow.sourceAlias == zc::none ||
      ZC_ASSERT_NONNULL(reborrow.sourceAlias) != sourceLocal.local ||
      reborrow.parameter != initializer.parameter || sourceLocal.type != initializer.type ||
      reborrow.sourceType != sourceLocal.type || reborrow.type != declaration.resultType ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) || function.locals.size() != 3 ||
      function.blocks.size() != 1) {
    return false;
  }
  const auto& parameter = function.locals[0];
  const auto& local = function.locals[1];
  const auto& temporary = function.locals[2];
  const auto& block = function.blocks[0];
  if (parameter.id != localId(1) || parameter.kind != MirLocalKind::Parameter ||
      parameter.type != initializer.type || local.id != localId(2) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      temporary.id != localId(3) || temporary.kind != MirLocalKind::Temporary ||
      temporary.type != reborrow.type || block.statements.size() != (hasUnsafeBlock ? 6 : 4) ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[1].assignmentValue().destination.local() != local.id ||
      block.statements[1].assignmentValue().value.kind() != MirRvalueKind::Use ||
      !matchesPlaceUse(block.statements[1].assignmentValue().value.useValue().operand, proofs, copy,
                       parameter.type) ||
      block.statements[2].kind() != MirStatementKind::StorageLive ||
      block.statements[2].storageLocal() != temporary.id ||
      block.statements[3].kind() != MirStatementKind::BorrowCreation ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none) {
    return false;
  }
  const auto& initializerPlace =
      block.statements[1].assignmentValue().value.useValue().operand.place();
  const auto& borrow = block.statements[3].borrowCreationValue();
  const auto expectedBorrowKind = reborrow.mutability == type::semantic::Mutability::Const
                                      ? MirBorrowKind::Shared
                                      : MirBorrowKind::Mutable;
  if (initializerPlace.local() != parameter.id || initializerPlace.projections().size() != 0 ||
      borrow.kind != expectedBorrowKind || borrow.destination.local() != temporary.id ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != local.id ||
      borrow.source.rootType() != reborrow.sourceType || borrow.source.projections().size() != 1 ||
      borrow.source.projections()[0].kind() != MirProjectionKind::Dereference ||
      borrow.source.projections()[0].inputType() != reborrow.sourceType ||
      borrow.source.projections()[0].resultType() != reborrow.type) {
    return false;
  }
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan) ||
          block.statements[4].kind() != MirStatementKind::UnsafeScopeBoundary ||
          block.statements[5].kind() != MirStatementKind::UnsafeScopeBoundary) {
        return false;
      }
      const auto& enter = block.statements[4].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[5].unsafeScopeBoundaryValue();
      if (enter.kind != MirUnsafeScopeBoundaryKind::Enter || enter.scope != scopeId(2) ||
          exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
          !sameSpan(block.statements[4].sourceSpan(), unsafeBlockRef.sourceSpan) ||
          !sameSpan(block.statements[5].sourceSpan(), unsafeBlockRef.sourceSpan)) {
        return false;
      }
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, reborrow.type) &&
           value.place().local() == temporary.id && value.place().projections().size() == 0;
  }
  return false;
}

bool validUninitializedLocalReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != zc::none ||
      sourceLocal.initializerSpan != zc::none || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 1 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validUninitializedLocalFieldReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalFieldProjectionExpression& projection,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != zc::none ||
      sourceLocal.initializerSpan != zc::none || sourceReturn.value != projection.node ||
      sourceLocal.local != projection.local || projection.receiverType != sourceLocal.type ||
      projection.type != declaration.resultType ||
      projection.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 1 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    if (!matchesPlaceUse(value, proofs, copy, projection.type) ||
        value.place().local() != local.id || value.place().rootType() != local.type ||
        value.place().resultType() != projection.type || value.place().projections().size() != 1) {
      return false;
    }
    const auto& field = value.place().projections()[0];
    return field.kind() == MirProjectionKind::Field &&
           field.fieldValue().field == projection.field && field.inputType() == local.type &&
           field.resultType() == projection.type;
  }
  return false;
}

bool validInitializedLocalFieldSequenceReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::VerifiedHirModule& hirModule, const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalFieldProjectionExpression& projection, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() < 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[sourceBlock.statements.size() - 1] != sourceReturn.node ||
      sourceLocal.initializer != zc::none || sourceLocal.initializerSpan != zc::none ||
      sourceReturn.value != projection.node || sourceLocal.local != projection.local ||
      projection.receiverType != sourceLocal.type || projection.type != declaration.resultType ||
      projection.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id ||
      block.statements.size() + 1 != sourceBlock.statements.size() ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  zc::Vector<identity::DefId> initializedFields;
  for (size_t writeIndex = 1; writeIndex + 1 < sourceBlock.statements.size(); ++writeIndex) {
    auto sourceWrite = localWriteFor(hirModule, sourceBlock.statements[writeIndex]);
    if (sourceWrite == zc::none || ZC_ASSERT_NONNULL(sourceWrite).local != sourceLocal.local ||
        ZC_ASSERT_NONNULL(sourceWrite).field == zc::none ||
        block.statements[writeIndex].kind() != MirStatementKind::Assign ||
        !sameSpan(block.statements[writeIndex].sourceSpan(),
                  ZC_ASSERT_NONNULL(sourceWrite).sourceSpan)) {
      return false;
    }
    const auto field = ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(sourceWrite).field);
    auto value = expressionFor(hirModule, ZC_ASSERT_NONNULL(sourceWrite).value);
    if (value == zc::none || ZC_ASSERT_NONNULL(value).type != ZC_ASSERT_NONNULL(sourceWrite).type ||
        ZC_ASSERT_NONNULL(value).category != hir::HirValueCategory::Value) {
      return false;
    }
    bool initialized = false;
    for (const auto initializedField : initializedFields) {
      if (initializedField == field) {
        initialized = true;
        break;
      }
    }
    const auto expectedHirKind =
        initialized ? hir::HirLocalWriteKind::Overwrite : hir::HirLocalWriteKind::Initialize;
    const auto expectedMirKind =
        initialized ? MirInitializationKind::Overwrite : MirInitializationKind::Initialize;
    if (ZC_ASSERT_NONNULL(sourceWrite).kind != expectedHirKind) return false;
    const auto& assignment = block.statements[writeIndex].assignmentValue();
    if (assignment.initialization != expectedMirKind ||
        assignment.destination.local() != local.id ||
        assignment.destination.rootType() != local.type ||
        assignment.destination.resultType() != ZC_ASSERT_NONNULL(sourceWrite).type ||
        assignment.destination.projections().size() != 1 ||
        assignment.value.kind() != MirRvalueKind::Use ||
        assignment.value.useValue().operand.kind() != MirOperandKind::Constant ||
        assignment.value.useValue().operand.constantValue().type != ZC_ASSERT_NONNULL(value).type ||
        !sameConstant(assignment.value.useValue().operand.constantValue().value,
                      ZC_ASSERT_NONNULL(value).value, module, identities, semanticTypes)) {
      return false;
    }
    const auto& assignmentField = assignment.destination.projections()[0];
    if (assignmentField.kind() != MirProjectionKind::Field ||
        assignmentField.fieldValue().field != field || assignmentField.inputType() != local.type ||
        assignmentField.resultType() != ZC_ASSERT_NONNULL(sourceWrite).type) {
      return false;
    }
    if (!initialized) initializedFields.add(field);
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    if (!matchesPlaceUse(value, proofs, copy, projection.type) ||
        value.place().local() != local.id || value.place().rootType() != local.type ||
        value.place().resultType() != projection.type || value.place().projections().size() != 1) {
      return false;
    }
    const auto& returnField = value.place().projections()[0];
    return returnField.kind() == MirProjectionKind::Field &&
           returnField.fieldValue().field == projection.field &&
           returnField.inputType() == local.type && returnField.resultType() == projection.type;
  }
  return false;
}

bool validLocalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirLocalBinding& sourceLocal, const hir::HirScalarLiteralExpression& initializer,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) || function.locals.size() != 1 ||
      function.blocks.size() != 1 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 2 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id ||
      block.statements.size() != (hasUnsafeBlock ? 4 : 2) ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[1].assignmentValue().destination.local() != local.id ||
      block.statements[1].assignmentValue().destination.rootType() != local.type ||
      block.statements[1].assignmentValue().destination.resultType() != local.type ||
      block.statements[1].assignmentValue().destination.projections().size() != 0 ||
      block.statements[1].assignmentValue().value.kind() != MirRvalueKind::Use ||
      block.statements[1].assignmentValue().value.useValue().operand.kind() !=
          MirOperandKind::Constant ||
      !sameSpan(block.statements[1].sourceSpan(), initializer.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan)) {
        return false;
      }
      if (block.statements[2].kind() != MirStatementKind::UnsafeScopeBoundary ||
          block.statements[3].kind() != MirStatementKind::UnsafeScopeBoundary) {
        return false;
      }
      const auto& enter = block.statements[2].unsafeScopeBoundaryValue();
      const auto& exit = block.statements[3].unsafeScopeBoundaryValue();
      if (enter.kind != MirUnsafeScopeBoundaryKind::Enter || enter.scope != scopeId(2) ||
          exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
          !sameSpan(block.statements[2].sourceSpan(), unsafeBlockRef.sourceSpan) ||
          !sameSpan(block.statements[3].sourceSpan(), unsafeBlockRef.sourceSpan)) {
        return false;
      }
    }
  }
  const auto& constant =
      block.statements[1].assignmentValue().value.useValue().operand.constantValue();
  if (constant.type != initializer.type ||
      !sameConstant(constant.value, initializer.value, module, identities, semanticTypes)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalAggregateFieldReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalFieldProjectionExpression& projection, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != aggregate.node ||
      sourceReturn.value != projection.node || sourceLocal.local != projection.local ||
      sourceLocal.type != aggregate.type || projection.receiverType != sourceLocal.type ||
      projection.type != declaration.resultType ||
      aggregate.category != hir::HirValueCategory::Value ||
      projection.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 2 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.statements[1].sourceSpan(), aggregate.sourceSpan) ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.rootType() != local.type ||
      assignment.destination.resultType() != local.type ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::NominalAggregate) {
    return false;
  }
  const auto& rvalue = assignment.value.nominalAggregateValue();
  if (rvalue.definition != aggregate.definition || rvalue.type != aggregate.type ||
      rvalue.elements.size() != aggregate.elements.size()) {
    return false;
  }
  for (size_t index = 0; index < aggregate.elements.size(); ++index) {
    const auto& expected = aggregate.elements[index];
    const auto& actual = rvalue.elements[index];
    if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
        actual.operand.constantValue().type != expected.type ||
        !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                      semanticTypes)) {
      return false;
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    if (!matchesPlaceUse(value, proofs, copy, projection.type) ||
        value.place().local() != local.id || value.place().rootType() != local.type ||
        value.place().resultType() != projection.type || value.place().projections().size() != 1) {
      return false;
    }
    const auto& field = value.place().projections()[0];
    return field.kind() == MirProjectionKind::Field &&
           field.fieldValue().field == projection.field && field.inputType() == local.type &&
           field.resultType() == projection.type;
  }
  return false;
}

bool validLocalAggregateReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != aggregate.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != aggregate.type || reference.type != sourceLocal.type ||
      reference.type != declaration.resultType ||
      aggregate.category != hir::HirValueCategory::Value ||
      reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 2 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.statements[1].sourceSpan(), aggregate.sourceSpan) ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.rootType() != local.type ||
      assignment.destination.resultType() != local.type ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::NominalAggregate) {
    return false;
  }
  const auto& rvalue = assignment.value.nominalAggregateValue();
  if (rvalue.definition != aggregate.definition || rvalue.type != aggregate.type ||
      rvalue.elements.size() != aggregate.elements.size()) {
    return false;
  }
  for (size_t index = 0; index < aggregate.elements.size(); ++index) {
    const auto& expected = aggregate.elements[index];
    const auto& actual = rvalue.elements[index];
    if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
        actual.operand.constantValue().type != expected.type ||
        !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                      semanticTypes)) {
      return false;
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalAggregateFieldOverwriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate, const hir::VerifiedHirModule& hirModule,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalFieldProjectionExpression& projection, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() < 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[sourceBlock.statements.size() - 1] != sourceReturn.node ||
      sourceLocal.initializer != aggregate.node || sourceReturn.value != projection.node ||
      sourceLocal.local != projection.local || sourceLocal.type != aggregate.type ||
      projection.receiverType != sourceLocal.type || projection.type != declaration.resultType ||
      aggregate.category != hir::HirValueCategory::Value ||
      projection.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id ||
      block.statements.size() != sourceBlock.statements.size() ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.statements[1].sourceSpan(), aggregate.sourceSpan) ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& initialize = block.statements[1].assignmentValue();
  if (initialize.destination.local() != local.id ||
      initialize.destination.rootType() != local.type ||
      initialize.destination.resultType() != local.type ||
      initialize.destination.projections().size() != 0 ||
      initialize.value.kind() != MirRvalueKind::NominalAggregate) {
    return false;
  }
  const auto& aggregateValue = initialize.value.nominalAggregateValue();
  if (aggregateValue.definition != aggregate.definition || aggregateValue.type != aggregate.type ||
      aggregateValue.elements.size() != aggregate.elements.size()) {
    return false;
  }
  for (size_t index = 0; index < aggregate.elements.size(); ++index) {
    const auto& expected = aggregate.elements[index];
    const auto& actual = aggregateValue.elements[index];
    if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
        actual.operand.constantValue().type != expected.type ||
        !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                      semanticTypes)) {
      return false;
    }
  }
  for (size_t writeIndex = 0; writeIndex + 2 < sourceBlock.statements.size(); ++writeIndex) {
    auto overwrite = localWriteFor(hirModule, sourceBlock.statements[writeIndex + 1]);
    if (overwrite == zc::none) return false;
    auto overwriteValue = expressionFor(hirModule, ZC_ASSERT_NONNULL(overwrite).value);
    if (overwriteValue == zc::none ||
        ZC_ASSERT_NONNULL(overwrite).kind != hir::HirLocalWriteKind::Overwrite ||
        ZC_ASSERT_NONNULL(overwrite).local != sourceLocal.local ||
        ZC_ASSERT_NONNULL(overwrite).field == zc::none ||
        ZC_ASSERT_NONNULL(overwriteValue).type != ZC_ASSERT_NONNULL(overwrite).type) {
      return false;
    }
    const auto& write = block.statements[writeIndex + 2].assignmentValue();
    if (block.statements[writeIndex + 2].kind() != MirStatementKind::Assign ||
        write.initialization != MirInitializationKind::Overwrite ||
        !sameSpan(block.statements[writeIndex + 2].sourceSpan(),
                  ZC_ASSERT_NONNULL(overwrite).sourceSpan) ||
        write.destination.local() != local.id || write.destination.rootType() != local.type ||
        write.destination.resultType() != ZC_ASSERT_NONNULL(overwrite).type ||
        write.destination.projections().size() != 1 || write.value.kind() != MirRvalueKind::Use ||
        write.value.useValue().operand.kind() != MirOperandKind::Constant ||
        write.value.useValue().operand.constantValue().type !=
            ZC_ASSERT_NONNULL(overwriteValue).type ||
        !sameConstant(write.value.useValue().operand.constantValue().value,
                      ZC_ASSERT_NONNULL(overwriteValue).value, module, identities, semanticTypes)) {
      return false;
    }
    const auto& writeField = write.destination.projections()[0];
    if (writeField.kind() != MirProjectionKind::Field ||
        writeField.fieldValue().field != ZC_ASSERT_NONNULL(overwrite).field ||
        writeField.inputType() != local.type ||
        writeField.resultType() != ZC_ASSERT_NONNULL(overwrite).type) {
      return false;
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    if (!matchesPlaceUse(value, proofs, copy, projection.type) ||
        value.place().local() != local.id || value.place().rootType() != local.type ||
        value.place().resultType() != projection.type || value.place().projections().size() != 1) {
      return false;
    }
    const auto& field = value.place().projections()[0];
    return field.kind() == MirProjectionKind::Field &&
           field.fieldValue().field == projection.field && field.inputType() == local.type &&
           field.resultType() == projection.type;
  }
  return false;
}

bool validLocalOverwriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer,
    const hir::HirLocalWriteStatement& overwrite,
    const hir::HirScalarLiteralExpression& overwriteValue,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != overwrite.node ||
      sourceBlock.statements[2] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node ||
      overwrite.kind != hir::HirLocalWriteKind::Overwrite || overwrite.local != sourceLocal.local ||
      overwrite.type != sourceLocal.type || overwrite.value != overwriteValue.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != declaration.resultType || initializer.type != sourceLocal.type ||
      overwriteValue.type != sourceLocal.type || reference.type != sourceLocal.type ||
      reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 3 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[2].kind() != MirStatementKind::Assign ||
      block.statements[2].assignmentValue().initialization != MirInitializationKind::Overwrite ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  for (size_t index = 1; index != 3; ++index) {
    const auto& assignment = block.statements[index].assignmentValue();
    if (assignment.destination.local() != local.id ||
        assignment.destination.rootType() != local.type ||
        assignment.destination.resultType() != local.type ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use ||
        assignment.value.useValue().operand.kind() != MirOperandKind::Constant) {
      return false;
    }
  }
  const auto& initialConstant =
      block.statements[1].assignmentValue().value.useValue().operand.constantValue();
  const auto& overwriteConstant =
      block.statements[2].assignmentValue().value.useValue().operand.constantValue();
  if (initialConstant.type != initializer.type || overwriteConstant.type != overwriteValue.type ||
      !sameConstant(initialConstant.value, initializer.value, module, identities, semanticTypes) ||
      !sameConstant(overwriteConstant.value, overwriteValue.value, module, identities,
                    semanticTypes) ||
      !sameSpan(block.statements[1].sourceSpan(), initializer.sourceSpan) ||
      !sameSpan(block.statements[2].sourceSpan(), overwrite.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

// Verifies `mut x = <lit>; x = <param>; return x;`: an initialized scalar local
// whose overwrite value is a parameter reference. The parameter is localId(1)
// and the user local is localId(2); the overwrite lowers to a copy/move
// place-use of the parameter local, exactly like the return-of-parameter path.
bool validLocalParameterOverwriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer,
    const hir::HirLocalWriteStatement& overwrite,
    const hir::HirParameterReferenceExpression& overwriteParameter,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 1 ||
      declaration.parameters.size() != 1 ||
      declaration.parameters[0].key != overwriteParameter.parameter ||
      declaration.parameters[0].type != sourceLocal.type || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 3 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != overwrite.node ||
      sourceBlock.statements[2] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node ||
      overwrite.kind != hir::HirLocalWriteKind::Overwrite || overwrite.field != zc::none ||
      overwrite.local != sourceLocal.local || overwrite.type != sourceLocal.type ||
      overwrite.value != overwriteParameter.node || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      initializer.type != sourceLocal.type || overwriteParameter.type != sourceLocal.type ||
      overwriteParameter.category != hir::HirValueCategory::Place ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& parameterLocal = function.locals[0];
  const auto& local = function.locals[1];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || parameterLocal.id != localId(1) ||
      parameterLocal.kind != MirLocalKind::Parameter || parameterLocal.type != sourceLocal.type ||
      parameterLocal.sourceScope != scope.id ||
      !sameSpan(parameterLocal.sourceSpan, declaration.parameters[0].sourceSpan) ||
      local.id != localId(2) || local.kind != MirLocalKind::UserLocal ||
      local.type != sourceLocal.type || local.sourceScope != scope.id ||
      !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 3 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[2].kind() != MirStatementKind::Assign ||
      block.statements[2].assignmentValue().initialization != MirInitializationKind::Overwrite ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  // The initialize assignment writes the literal into the user local.
  const auto& initializeAssign = block.statements[1].assignmentValue();
  if (initializeAssign.destination.local() != local.id ||
      initializeAssign.destination.rootType() != local.type ||
      initializeAssign.destination.resultType() != local.type ||
      initializeAssign.destination.projections().size() != 0 ||
      initializeAssign.value.kind() != MirRvalueKind::Use ||
      initializeAssign.value.useValue().operand.kind() != MirOperandKind::Constant) {
    return false;
  }
  const auto& initialConstant = initializeAssign.value.useValue().operand.constantValue();
  if (initialConstant.type != initializer.type ||
      !sameConstant(initialConstant.value, initializer.value, module, identities, semanticTypes) ||
      !sameSpan(block.statements[1].sourceSpan(), initializer.sourceSpan)) {
    return false;
  }
  // The overwrite assignment moves/copies the parameter local into the user
  // local via a place-use of the parameter place (no projections).
  const auto& overwriteAssign = block.statements[2].assignmentValue();
  if (overwriteAssign.destination.local() != local.id ||
      overwriteAssign.destination.rootType() != local.type ||
      overwriteAssign.destination.resultType() != local.type ||
      overwriteAssign.destination.projections().size() != 0 ||
      overwriteAssign.value.kind() != MirRvalueKind::Use ||
      !matchesPlaceUse(overwriteAssign.value.useValue().operand, proofs, copy, local.type) ||
      overwriteAssign.value.useValue().operand.place().local() != parameterLocal.id ||
      overwriteAssign.value.useValue().operand.place().rootType() != parameterLocal.type ||
      overwriteAssign.value.useValue().operand.place().resultType() != parameterLocal.type ||
      overwriteAssign.value.useValue().operand.place().projections().size() != 0 ||
      !sameSpan(block.statements[2].sourceSpan(), overwrite.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

// Verifies `mut x = <lit>; x = a <op> b; return x;`: an initialized scalar local
// whose overwrite value is a primitive binary. Parameters are localId(1..N) and
// the user local is localId(N+1); the overwrite lowers to an Arithmetic (or
// Comparison) rvalue whose operands are scalar-literal constants or copy
// place-uses of the parameter locals, exactly like the primitive-binary
// initializer path.
bool validLocalBinaryOverwriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer,
    const hir::HirLocalWriteStatement& overwrite,
    const hir::HirPrimitiveBinaryExpression& overwriteBinary,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    const hir::VerifiedHirModule& hirModule, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  const auto comparisonOperator = mirComparisonOperatorFor(overwriteBinary.operation);
  const auto arithmeticOperator = mirArithmeticOperatorFor(overwriteBinary.operation);
  const bool isArithmeticBinary = comparisonOperator == zc::none && arithmeticOperator != zc::none;
  const auto userLocalId = localId(parameterCount + 1);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + 1u || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != overwrite.node ||
      sourceBlock.statements[2] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node ||
      overwrite.kind != hir::HirLocalWriteKind::Overwrite || overwrite.field != zc::none ||
      overwrite.local != sourceLocal.local || overwrite.type != sourceLocal.type ||
      overwrite.value != overwriteBinary.node || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      initializer.type != sourceLocal.type || overwriteBinary.type != sourceLocal.type ||
      overwriteBinary.category != hir::HirValueCategory::Value ||
      (comparisonOperator == zc::none && arithmeticOperator == zc::none) ||
      (isArithmeticBinary && overwriteBinary.operandType != sourceLocal.type) ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (uint32_t p = 0; p < parameterCount; ++p) {
    const auto& parameter = function.locals[p];
    if (parameter.id != localId(p + 1) || parameter.kind != MirLocalKind::Parameter ||
        parameter.type != declaration.parameters[p].type || parameter.sourceScope != scope.id ||
        !sameSpan(parameter.sourceSpan, declaration.parameters[p].sourceSpan)) {
      return false;
    }
  }
  const auto& local = function.locals[parameterCount];
  const auto& block = function.blocks[0];
  if (local.id != userLocalId || local.kind != MirLocalKind::UserLocal ||
      local.type != sourceLocal.type || local.sourceScope != scope.id ||
      !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 3 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != userLocalId ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[2].kind() != MirStatementKind::Assign ||
      block.statements[2].assignmentValue().initialization != MirInitializationKind::Overwrite ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  // The initialize assignment writes the literal into the user local.
  const auto& initializeAssign = block.statements[1].assignmentValue();
  if (initializeAssign.destination.local() != userLocalId ||
      initializeAssign.destination.rootType() != local.type ||
      initializeAssign.destination.resultType() != local.type ||
      initializeAssign.destination.projections().size() != 0 ||
      initializeAssign.value.kind() != MirRvalueKind::Use ||
      initializeAssign.value.useValue().operand.kind() != MirOperandKind::Constant) {
    return false;
  }
  const auto& initialConstant = initializeAssign.value.useValue().operand.constantValue();
  if (initialConstant.type != initializer.type ||
      !sameConstant(initialConstant.value, initializer.value, module, identities, semanticTypes) ||
      !sameSpan(block.statements[1].sourceSpan(), initializer.sourceSpan)) {
    return false;
  }
  // The overwrite assignment writes an Arithmetic/Comparison rvalue whose
  // operands match the binary's HIR operand nodes: a scalar literal maps to a
  // constant, a parameter reference to a copy place-use of its parameter local.
  const auto& overwriteAssign = block.statements[2].assignmentValue();
  if (overwriteAssign.destination.local() != userLocalId ||
      overwriteAssign.destination.rootType() != local.type ||
      overwriteAssign.destination.resultType() != local.type ||
      overwriteAssign.destination.projections().size() != 0 ||
      !sameSpan(block.statements[2].sourceSpan(), overwrite.sourceSpan)) {
    return false;
  }
  auto operandMatches = [&](const MirOperand& operand, hir::HirNodeId operandNode) -> bool {
    auto operandLiteral = expressionFor(hirModule, operandNode);
    ZC_IF_SOME(literalValue, operandLiteral) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == overwriteBinary.operandType &&
             literalValue.type == overwriteBinary.operandType &&
             sameConstant(operand.constantValue().value, literalValue.value, module, identities,
                          semanticTypes);
    }
    auto operandParameter = parameterReferenceFor(hirModule, operandNode);
    ZC_IF_SOME(parameter, operandParameter) {
      size_t parameterIndex = 0;
      bool found = false;
      for (size_t p = 0; p < declaration.parameters.size(); ++p) {
        if (declaration.parameters[p].key == parameter.parameter) {
          parameterIndex = p;
          found = true;
          break;
        }
      }
      return found && parameter.type == overwriteBinary.operandType &&
             matchesPlaceUse(operand, proofs, copy, overwriteBinary.operandType) &&
             operand.place().local() == localId(static_cast<uint32_t>(parameterIndex) + 1) &&
             operand.place().rootType() == overwriteBinary.operandType &&
             operand.place().resultType() == overwriteBinary.operandType &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (isArithmeticBinary) {
    if (overwriteAssign.value.kind() != MirRvalueKind::Arithmetic) return false;
    const auto& rvalue = overwriteAssign.value.arithmeticValue();
    if (rvalue.op != ZC_ASSERT_NONNULL(arithmeticOperator) ||
        rvalue.resultType != overwriteBinary.type ||
        !operandMatches(rvalue.left, overwriteBinary.left) ||
        !operandMatches(rvalue.right, overwriteBinary.right)) {
      return false;
    }
  } else {
    if (overwriteAssign.value.kind() != MirRvalueKind::Comparison) return false;
    const auto& rvalue = overwriteAssign.value.comparisonValue();
    if (rvalue.op != ZC_ASSERT_NONNULL(comparisonOperator) ||
        rvalue.resultType != overwriteBinary.type ||
        !operandMatches(rvalue.left, overwriteBinary.left) ||
        !operandMatches(rvalue.right, overwriteBinary.right)) {
      return false;
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) &&
           value.place().local() == userLocalId && value.place().rootType() == local.type &&
           value.place().resultType() == local.type && value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalWriteInitializationReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirLocalWriteStatement& write, const hir::HirScalarLiteralExpression& value,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != sourceLocal.node || sourceBlock.statements[1] != write.node ||
      sourceBlock.statements[2] != sourceReturn.node || sourceLocal.initializer != zc::none ||
      write.kind != hir::HirLocalWriteKind::Initialize || write.local != sourceLocal.local ||
      write.type != sourceLocal.type || write.value != value.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != declaration.resultType || value.type != sourceLocal.type ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 2 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.rootType() != local.type ||
      assignment.destination.resultType() != local.type ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::Use ||
      assignment.value.useValue().operand.kind() != MirOperandKind::Constant) {
    return false;
  }
  const auto& constant = assignment.value.useValue().operand.constantValue();
  if (constant.type != value.type ||
      !sameConstant(constant.value, value.value, module, identities, semanticTypes) ||
      !sameSpan(block.statements[1].sourceSpan(), write.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(operand, block.terminator.returnValue().value) {
    return matchesPlaceUse(operand, proofs, copy, local.type) &&
           operand.place().local() == local.id && operand.place().rootType() == local.type &&
           operand.place().resultType() == local.type && operand.place().projections().size() == 0;
  }
  return false;
}

// Verifies a lowered sequential N-local return function against its HIR block.
// The HIR block is N leading local bindings followed by a single return of a
// user local or a parameter. MIR layout: parameters occupy localId(1..P), user
// local i occupies localId(P + i + 1); the single block is StorageLive + Assign
// per binding (constant for a literal, nominal aggregate for an aggregate, or a
// copy/move place-use for a local- or parameter-reference initializer), an
// optional unsafe Enter/Exit boundary pair, and a Return of the selected place.
bool validSequentialLocalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  (void)module;
  (void)identities;
  (void)semanticTypes;
  if (sourceBlock.statements.size() < 3) return false;
  const size_t bindingCount = sourceBlock.statements.size() - 1;
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
  ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
    unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
    if (unsafeBlock == zc::none) return false;
  }
  const bool hasUnsafeBlock = unsafeBlock != zc::none;
  auto userLocalId = [&](size_t index) {
    return localId(parameterCount + static_cast<uint32_t>(index) + 1);
  };
  // Resolve the HIR bindings.
  zc::Vector<const hir::HirLocalBinding*> bindings;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto localBinding = localFor(hirModule, sourceBlock.statements[i]);
    if (localBinding == zc::none) return false;
    ZC_IF_SOME(local, localBinding) {
      if (local.node != sourceBlock.statements[i] ||
          local.local.ordinal() != static_cast<uint32_t>(i + 1) || local.initializer == zc::none ||
          local.type != declaration.resultType) {
        return false;
      }
      bindings.add(&local);
    }
  }
  auto sourceReturn = returnFor(hirModule, sourceBlock.statements[bindingCount]);
  if (sourceReturn == zc::none) return false;
  // A nested operand (`a + b * c`) lowers to a synthesized Temporary local plus
  // an extra StorageLive + Assign emitted before the outer binding's assignment.
  // Precompute, per binding, the nested-operand HIR node (or none) so the
  // verifier derives the same local count and statement layout the emitter uses.
  auto nestedTempFor = [&](const hir::HirLocalBinding& binding) -> zc::Maybe<hir::HirNodeId> {
    hir::HirNodeId initializer;
    ZC_IF_SOME(value, binding.initializer) { initializer = value; }
    auto outer = primitiveBinaryFor(hirModule, initializer);
    ZC_IF_SOME(value, outer) {
      if (primitiveBinaryFor(hirModule, value.left) != zc::none) return value.left;
      if (primitiveBinaryFor(hirModule, value.right) != zc::none) return value.right;
    }
    return zc::none;
  };
  zc::Vector<zc::Maybe<hir::HirNodeId>> bindingNested;
  zc::Vector<zc::Maybe<uint32_t>> bindingTempOrdinal;
  uint32_t nestedCount = 0;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto nested = nestedTempFor(*bindings[i]);
    if (nested != zc::none) {
      bindingTempOrdinal.add(parameterCount + static_cast<uint32_t>(bindingCount) + nestedCount +
                             1);
      ++nestedCount;
    } else {
      zc::Maybe<uint32_t> noTemp;
      bindingTempOrdinal.add(zc::mv(noTemp));
    }
    bindingNested.add(zc::mv(nested));
  }
  auto userTempId = [&](uint32_t ordinal) { return localId(ordinal); };
  // Function-level shape. Temporaries follow the N user locals, one per nested
  // operand.
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2u : 1u) ||
      function.locals.size() != parameterCount + bindingCount + nestedCount ||
      function.blocks.size() != 1 || declaration.body != sourceBlock.node) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id) {
    return false;
  }
  for (uint32_t p = 0; p < parameterCount; ++p) {
    const auto& parameter = function.locals[p];
    if (parameter.id != localId(p + 1) || parameter.kind != MirLocalKind::Parameter ||
        parameter.type != declaration.parameters[p].type || parameter.sourceScope != scope.id ||
        !sameSpan(parameter.sourceSpan, declaration.parameters[p].sourceSpan)) {
      return false;
    }
  }
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& local = function.locals[parameterCount + i];
    if (local.id != userLocalId(i) || local.kind != MirLocalKind::UserLocal ||
        local.type != bindings[i]->type || local.sourceScope != scope.id ||
        !sameSpan(local.sourceSpan, bindings[i]->sourceSpan)) {
      return false;
    }
  }
  // Each temporary holds a nested operand's inner-binary result; its type is the
  // owning binding's outer-binary operand type, declared in binding order.
  {
    uint32_t verifiedTemps = 0;
    for (size_t i = 0; i < bindingCount; ++i) {
      ZC_IF_SOME(nestedNode, bindingNested[i]) {
        auto nested = primitiveBinaryFor(hirModule, nestedNode);
        hir::HirNodeId initializerNode;
        ZC_IF_SOME(value, bindings[i]->initializer) { initializerNode = value; }
        auto outer = primitiveBinaryFor(hirModule, initializerNode);
        if (nested == zc::none || outer == zc::none) return false;
        identity::SemanticTypeId tempType;
        ZC_IF_SOME(value, outer) { tempType = value.operandType; }
        uint32_t tempOrdinal = 0;
        ZC_IF_SOME(value, bindingTempOrdinal[i]) { tempOrdinal = value; }
        const auto& temp = function.locals[parameterCount + bindingCount + verifiedTemps];
        if (temp.id != userTempId(tempOrdinal) || temp.kind != MirLocalKind::Temporary ||
            temp.type != tempType || temp.sourceScope != scope.id ||
            !sameSpan(temp.sourceSpan, bindings[i]->sourceSpan)) {
          return false;
        }
        ++verifiedTemps;
      }
    }
  }
  // Each binding contributes StorageLive + Assign for its user local, preceded by
  // an extra StorageLive(temp) + Assign(temp) pair for a nested operand. The
  // optional unsafe boundary pair follows the last binding.
  const size_t expectedStatements = bindingCount * 2 + nestedCount * 2 + (hasUnsafeBlock ? 2u : 0u);
  if (block.statements.size() != expectedStatements) { return false; }
  // Verify each binding's statements, tracking the running statement cursor since
  // a nested operand inserts a temp pair before the binding's own pair.
  size_t cursor = 0;
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& local = *bindings[i];
    // A nested operand emits StorageLive(temp) + Assign(temp = inner rvalue)
    // before the binding's own pair.
    ZC_IF_SOME(nestedNode, bindingNested[i]) {
      auto nested = primitiveBinaryFor(hirModule, nestedNode);
      if (nested == zc::none) return false;
      uint32_t tempOrdinal = 0;
      ZC_IF_SOME(value, bindingTempOrdinal[i]) { tempOrdinal = value; }
      const auto& tempLive = block.statements[cursor];
      const auto& tempAssign = block.statements[cursor + 1];
      cursor += 2;
      if (tempLive.kind() != MirStatementKind::StorageLive ||
          tempLive.storageLocal() != userTempId(tempOrdinal) ||
          tempAssign.kind() != MirStatementKind::Assign) {
        return false;
      }
      const auto& tempAssignment = tempAssign.assignmentValue();
      ZC_IF_SOME(nestedValue, nested) {
        const auto nestedComparison = mirComparisonOperatorFor(nestedValue.operation);
        const auto nestedArithmetic = mirArithmeticOperatorFor(nestedValue.operation);
        const bool nestedIsArithmetic =
            nestedComparison == zc::none && nestedArithmetic != zc::none;
        if ((nestedComparison == zc::none && nestedArithmetic == zc::none) ||
            nestedValue.category != hir::HirValueCategory::Value ||
            tempAssignment.initialization != MirInitializationKind::Initialize ||
            tempAssignment.destination.local() != userTempId(tempOrdinal) ||
            tempAssignment.destination.rootType() != nestedValue.type ||
            tempAssignment.destination.resultType() != nestedValue.type ||
            tempAssignment.destination.projections().size() != 0 ||
            !sameSpan(tempAssign.sourceSpan(), nestedValue.sourceSpan)) {
          return false;
        }
        // Validates one nested leaf against its HIR node: a constant matches a
        // scalar literal, a copy place matches a parameter or earlier user local.
        auto leafMatches = [&](const MirOperand& operand, hir::HirNodeId operandNode) -> bool {
          auto operandLiteral = expressionFor(hirModule, operandNode);
          ZC_IF_SOME(literalValue, operandLiteral) {
            return operand.kind() == MirOperandKind::Constant &&
                   operand.constantValue().type == nestedValue.operandType &&
                   literalValue.type == nestedValue.operandType &&
                   sameConstant(operand.constantValue().value, literalValue.value, module,
                                identities, semanticTypes);
          }
          auto operandParameter = parameterReferenceFor(hirModule, operandNode);
          ZC_IF_SOME(parameter, operandParameter) {
            size_t parameterIndex = 0;
            bool found = false;
            for (size_t p = 0; p < declaration.parameters.size(); ++p) {
              if (declaration.parameters[p].key == parameter.parameter) {
                parameterIndex = p;
                found = true;
                break;
              }
            }
            return found && parameter.type == nestedValue.operandType &&
                   matchesPlaceUse(operand, proofs, copy, nestedValue.operandType) &&
                   operand.place().local() == localId(static_cast<uint32_t>(parameterIndex) + 1) &&
                   operand.place().rootType() == nestedValue.operandType &&
                   operand.place().resultType() == nestedValue.operandType &&
                   operand.place().projections().size() == 0;
          }
          auto operandLocal = localReferenceFor(hirModule, operandNode);
          ZC_IF_SOME(reference, operandLocal) {
            return reference.type == nestedValue.operandType && reference.local.ordinal() != 0 &&
                   reference.local.ordinal() <= static_cast<uint32_t>(i) &&
                   matchesPlaceUse(operand, proofs, copy, nestedValue.operandType) &&
                   operand.place().local() == userLocalId(reference.local.ordinal() - 1) &&
                   operand.place().rootType() == nestedValue.operandType &&
                   operand.place().resultType() == nestedValue.operandType &&
                   operand.place().projections().size() == 0;
          }
          return false;
        };
        if (nestedIsArithmetic) {
          if (tempAssignment.value.kind() != MirRvalueKind::Arithmetic) return false;
          const auto& rvalue = tempAssignment.value.arithmeticValue();
          if (rvalue.op != ZC_ASSERT_NONNULL(nestedArithmetic) ||
              rvalue.resultType != nestedValue.type ||
              !leafMatches(rvalue.left, nestedValue.left) ||
              !leafMatches(rvalue.right, nestedValue.right)) {
            return false;
          }
        } else {
          if (tempAssignment.value.kind() != MirRvalueKind::Comparison) return false;
          const auto& rvalue = tempAssignment.value.comparisonValue();
          if (rvalue.op != ZC_ASSERT_NONNULL(nestedComparison) ||
              rvalue.resultType != nestedValue.type ||
              !leafMatches(rvalue.left, nestedValue.left) ||
              !leafMatches(rvalue.right, nestedValue.right)) {
            return false;
          }
        }
      }
    }
    const auto& live = block.statements[cursor];
    const auto& assign = block.statements[cursor + 1];
    cursor += 2;
    if (live.kind() != MirStatementKind::StorageLive || live.storageLocal() != userLocalId(i) ||
        !sameSpan(live.sourceSpan(), local.sourceSpan) ||
        assign.kind() != MirStatementKind::Assign) {
      return false;
    }
    const auto& assignment = assign.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != userLocalId(i) ||
        assignment.destination.rootType() != local.type ||
        assignment.destination.resultType() != local.type ||
        assignment.destination.projections().size() != 0) {
      return false;
    }
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
    auto literal = expressionFor(hirModule, initializerNode);
    auto aggregate = aggregateFor(hirModule, initializerNode);
    auto localReference = localReferenceFor(hirModule, initializerNode);
    auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
    ZC_IF_SOME(value, literal) {
      if (value.type != local.type || value.category != hir::HirValueCategory::Value ||
          assignment.value.kind() != MirRvalueKind::Use ||
          assignment.value.useValue().operand.kind() != MirOperandKind::Constant ||
          assignment.value.useValue().operand.constantValue().type != value.type ||
          !sameConstant(assignment.value.useValue().operand.constantValue().value, value.value,
                        module, identities, semanticTypes) ||
          !sameSpan(assign.sourceSpan(), value.sourceSpan)) {
        return false;
      }
    }
    ZC_IF_SOME(value, aggregate) {
      if (value.type != local.type || value.category != hir::HirValueCategory::Value ||
          assignment.value.kind() != MirRvalueKind::NominalAggregate ||
          !sameSpan(assign.sourceSpan(), value.sourceSpan)) {
        return false;
      }
      const auto& rvalue = assignment.value.nominalAggregateValue();
      if (rvalue.definition != value.definition || rvalue.type != value.type ||
          rvalue.elements.size() != value.elements.size()) {
        return false;
      }
      for (size_t e = 0; e < value.elements.size(); ++e) {
        const auto& expected = value.elements[e];
        const auto& actual = rvalue.elements[e];
        if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
            actual.operand.constantValue().type != expected.type ||
            !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                          semanticTypes)) {
          return false;
        }
      }
    }
    ZC_IF_SOME(value, localReference) {
      if (value.type != local.type || value.category != hir::HirValueCategory::Place ||
          value.local.ordinal() == 0 || value.local.ordinal() > static_cast<uint32_t>(i) ||
          assignment.value.kind() != MirRvalueKind::Use ||
          !matchesPlaceUse(assignment.value.useValue().operand, proofs, copy, local.type) ||
          !sameSpan(assign.sourceSpan(), value.sourceSpan)) {
        return false;
      }
      const auto& place = assignment.value.useValue().operand.place();
      if (place.local() != userLocalId(value.local.ordinal() - 1) ||
          place.rootType() != local.type || place.resultType() != local.type ||
          place.projections().size() != 0) {
        return false;
      }
    }
    ZC_IF_SOME(value, parameterReference) {
      size_t parameterIndex = 0;
      bool found = false;
      for (size_t p = 0; p < declaration.parameters.size(); ++p) {
        if (declaration.parameters[p].key == value.parameter) {
          parameterIndex = p;
          found = true;
          break;
        }
      }
      if (!found || value.type != local.type || value.category != hir::HirValueCategory::Place ||
          assignment.value.kind() != MirRvalueKind::Use ||
          !matchesPlaceUse(assignment.value.useValue().operand, proofs, copy, local.type) ||
          !sameSpan(assign.sourceSpan(), value.sourceSpan)) {
        return false;
      }
      const auto& place = assignment.value.useValue().operand.place();
      if (place.local() != localId(static_cast<uint32_t>(parameterIndex) + 1) ||
          place.rootType() != local.type || place.resultType() != local.type ||
          place.projections().size() != 0) {
        return false;
      }
    }
    // A primitive-binary initializer lowers to an Arithmetic or Comparison
    // rvalue whose operands are a constant, a copy of a parameter local, or a
    // copy of an earlier user local.
    auto binary = primitiveBinaryFor(hirModule, initializerNode);
    ZC_IF_SOME(value, binary) {
      const auto comparisonOperator = mirComparisonOperatorFor(value.operation);
      const auto arithmeticOperator = mirArithmeticOperatorFor(value.operation);
      const bool isArithmeticBinary =
          comparisonOperator == zc::none && arithmeticOperator != zc::none;
      if ((comparisonOperator == zc::none && arithmeticOperator == zc::none) ||
          value.type != local.type || value.category != hir::HirValueCategory::Value ||
          (isArithmeticBinary && value.type != value.operandType) ||
          !sameSpan(assign.sourceSpan(), value.sourceSpan)) {
        return false;
      }
      // Validates one binary operand against its HIR node: a constant matches a
      // scalar literal, a copy place matches a parameter or earlier local.
      auto operandMatches = [&](const MirOperand& operand, hir::HirNodeId operandNode) -> bool {
        auto operandLiteral = expressionFor(hirModule, operandNode);
        ZC_IF_SOME(literalValue, operandLiteral) {
          return operand.kind() == MirOperandKind::Constant &&
                 operand.constantValue().type == value.operandType &&
                 literalValue.type == value.operandType &&
                 sameConstant(operand.constantValue().value, literalValue.value, module, identities,
                              semanticTypes);
        }
        auto operandParameter = parameterReferenceFor(hirModule, operandNode);
        ZC_IF_SOME(parameter, operandParameter) {
          size_t parameterIndex = 0;
          bool found = false;
          for (size_t p = 0; p < declaration.parameters.size(); ++p) {
            if (declaration.parameters[p].key == parameter.parameter) {
              parameterIndex = p;
              found = true;
              break;
            }
          }
          return found && parameter.type == value.operandType &&
                 matchesPlaceUse(operand, proofs, copy, value.operandType) &&
                 operand.place().local() == localId(static_cast<uint32_t>(parameterIndex) + 1) &&
                 operand.place().rootType() == value.operandType &&
                 operand.place().resultType() == value.operandType &&
                 operand.place().projections().size() == 0;
        }
        auto operandLocal = localReferenceFor(hirModule, operandNode);
        ZC_IF_SOME(reference, operandLocal) {
          return reference.type == value.operandType && reference.local.ordinal() != 0 &&
                 reference.local.ordinal() <= static_cast<uint32_t>(i) &&
                 matchesPlaceUse(operand, proofs, copy, value.operandType) &&
                 operand.place().local() == userLocalId(reference.local.ordinal() - 1) &&
                 operand.place().rootType() == value.operandType &&
                 operand.place().resultType() == value.operandType &&
                 operand.place().projections().size() == 0;
        }
        // A nested operand's outer slot is a copy of the synthesized temp holding
        // its inner-binary result. The temp assignment itself was verified above.
        auto operandNested = primitiveBinaryFor(hirModule, operandNode);
        ZC_IF_SOME(nested, operandNested) {
          (void)nested;
          if (bindingTempOrdinal[i] == zc::none) return false;
          uint32_t tempOrdinal = 0;
          ZC_IF_SOME(ordinalValue, bindingTempOrdinal[i]) { tempOrdinal = ordinalValue; }
          return matchesPlaceUse(operand, proofs, copy, value.operandType) &&
                 operand.place().local() == userTempId(tempOrdinal) &&
                 operand.place().rootType() == value.operandType &&
                 operand.place().resultType() == value.operandType &&
                 operand.place().projections().size() == 0;
        }
        return false;
      };
      if (isArithmeticBinary) {
        if (assignment.value.kind() != MirRvalueKind::Arithmetic) return false;
        const auto& rvalue = assignment.value.arithmeticValue();
        if (rvalue.op != ZC_ASSERT_NONNULL(arithmeticOperator) || rvalue.resultType != value.type ||
            !operandMatches(rvalue.left, value.left) ||
            !operandMatches(rvalue.right, value.right)) {
          return false;
        }
      } else {
        if (assignment.value.kind() != MirRvalueKind::Comparison) return false;
        const auto& rvalue = assignment.value.comparisonValue();
        if (rvalue.op != ZC_ASSERT_NONNULL(comparisonOperator) || rvalue.resultType != value.type ||
            !operandMatches(rvalue.left, value.left) ||
            !operandMatches(rvalue.right, value.right)) {
          return false;
        }
      }
    }
    // Exactly one initializer kind must be present.
    const int present = (literal != zc::none ? 1 : 0) + (aggregate != zc::none ? 1 : 0) +
                        (localReference != zc::none ? 1 : 0) +
                        (parameterReference != zc::none ? 1 : 0) + (binary != zc::none ? 1 : 0);
    if (present != 1) { return false; }
  }
  // Unsafe boundary pair.
  if (hasUnsafeBlock) {
    ZC_IF_SOME(unsafe, unsafeBlock) {
      const auto& unsafeScope = function.sourceScopes[1];
      if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
          !sameSpan(unsafeScope.sourceSpan, unsafe.sourceSpan)) {
        return false;
      }
      const auto& enter = block.statements[cursor];
      const auto& exit = block.statements[cursor + 1];
      if (enter.kind() != MirStatementKind::UnsafeScopeBoundary ||
          exit.kind() != MirStatementKind::UnsafeScopeBoundary ||
          enter.unsafeScopeBoundaryValue().kind != MirUnsafeScopeBoundaryKind::Enter ||
          enter.unsafeScopeBoundaryValue().scope != scopeId(2) ||
          exit.unsafeScopeBoundaryValue().kind != MirUnsafeScopeBoundaryKind::Exit ||
          exit.unsafeScopeBoundaryValue().scope != scopeId(2) ||
          !sameSpan(enter.sourceSpan(), unsafe.sourceSpan) ||
          !sameSpan(exit.sourceSpan(), unsafe.sourceSpan)) {
        return false;
      }
    }
  }
  // Return of a user local or a parameter.
  if (block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none) {
    return false;
  }
  hir::HirNodeId returnValueNode;
  ZC_IF_SOME(returnStatement, sourceReturn) {
    returnValueNode = returnStatement.value;
    if (!sameSpan(block.terminator.sourceSpan(), returnStatement.sourceSpan)) return false;
  }
  auto returnLocalReference = localReferenceFor(hirModule, returnValueNode);
  auto returnParameterReference = parameterReferenceFor(hirModule, returnValueNode);
  bool returnValid = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    ZC_IF_SOME(reference, returnLocalReference) {
      if (reference.type == declaration.resultType &&
          reference.category == hir::HirValueCategory::Place && reference.local.ordinal() != 0 &&
          reference.local.ordinal() <= static_cast<uint32_t>(bindingCount) &&
          matchesPlaceUse(value, proofs, copy, reference.type) &&
          value.place().local() == userLocalId(reference.local.ordinal() - 1) &&
          value.place().rootType() == reference.type &&
          value.place().resultType() == reference.type && value.place().projections().size() == 0) {
        returnValid = true;
      }
    }
    ZC_IF_SOME(reference, returnParameterReference) {
      size_t parameterIndex = 0;
      bool found = false;
      for (size_t p = 0; p < declaration.parameters.size(); ++p) {
        if (declaration.parameters[p].key == reference.parameter) {
          parameterIndex = p;
          found = true;
          break;
        }
      }
      if (found && reference.type == declaration.resultType &&
          reference.category == hir::HirValueCategory::Place &&
          matchesPlaceUse(value, proofs, copy, reference.type) &&
          value.place().local() == localId(static_cast<uint32_t>(parameterIndex) + 1) &&
          value.place().rootType() == reference.type &&
          value.place().resultType() == reference.type && value.place().projections().size() == 0) {
        returnValid = true;
      }
    }
  }
  return returnValid;
}

bool validParameterLocalReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirParameterReferenceExpression& initializer,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 1 ||
      declaration.parameters.size() != 1 ||
      declaration.parameters[0].key != initializer.parameter ||
      declaration.parameters[0].type != initializer.type || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 2 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      initializer.type != sourceLocal.type ||
      initializer.category != hir::HirValueCategory::Place || reference.type != sourceLocal.type ||
      reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& parameter = function.locals[0];
  const auto& local = function.locals[1];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || parameter.id != localId(1) ||
      parameter.kind != MirLocalKind::Parameter || parameter.type != initializer.type ||
      parameter.sourceScope != scope.id ||
      !sameSpan(parameter.sourceSpan, declaration.parameters[0].sourceSpan) ||
      local.id != localId(2) || local.kind != MirLocalKind::UserLocal ||
      local.type != sourceLocal.type || local.sourceScope != scope.id ||
      !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 2 ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != local.id ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.statements[1].assignmentValue().destination.local() != local.id ||
      block.statements[1].assignmentValue().value.kind() != MirRvalueKind::Use ||
      !matchesPlaceUse(block.statements[1].assignmentValue().value.useValue().operand, proofs, copy,
                       parameter.type) ||
      !sameSpan(block.statements[1].sourceSpan(), initializer.sourceSpan) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& source = block.statements[1].assignmentValue().value.useValue().operand.place();
  if (source.local() != parameter.id || source.rootType() != parameter.type ||
      source.resultType() != parameter.type || source.projections().size() != 0) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validLocalCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirDirectCallExpression& call, const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalReferenceExpression& reference, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != call.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != declaration.resultType || call.resultType != sourceLocal.type ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      entry.id != blockId(1) || entry.sourceScope != scope.id || entry.statements.size() != 1 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != local.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& terminator = entry.terminator.callValue();
  if (terminator.callee != call.callee || terminator.arguments.size() != call.arguments.size() ||
      terminator.destination.local() != local.id ||
      terminator.destination.rootType() != local.type ||
      terminator.destination.resultType() != local.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::NoActivation ||
      terminator.normalTarget != continuation.id || terminator.unwindTarget != zc::none) {
    return false;
  }
  for (size_t index = 0; index < call.arguments.size(); ++index) {
    const auto& actual = terminator.arguments[index];
    const auto& expected = call.arguments[index];
    if (expected.value == zc::none) return false;
    ZC_IF_SOME(value, expected.value) {
      if (actual.kind() != MirOperandKind::Constant ||
          actual.constantValue().type != expected.type ||
          !sameConstant(actual.constantValue().value, value, module, identities, semanticTypes)) {
        return false;
      }
    }
  }
  ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validReceiverCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& receiver,
    const hir::HirReceiverCallExpression& call, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 3 || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != aggregate.node ||
      sourceReturn.value != call.node || call.receiver != receiver.node ||
      sourceLocal.local != receiver.local || sourceLocal.type != aggregate.type ||
      sourceLocal.type != receiver.type || sourceLocal.type != call.receiverSourceType ||
      aggregate.category != hir::HirValueCategory::Value ||
      receiver.category != hir::HirValueCategory::Place ||
      call.resultType != declaration.resultType ||
      call.receiverMode != checker::checked::ReceiverMode::Mutable ||
      call.receiverAdjustments.size() != 1 ||
      call.receiverAdjustments[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& receiverTemporary = function.locals[1];
  const auto& result = function.locals[2];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      receiverTemporary.id != localId(2) || receiverTemporary.kind != MirLocalKind::Temporary ||
      receiverTemporary.type != call.receiverType || receiverTemporary.sourceScope != scope.id ||
      !sameSpan(receiverTemporary.sourceSpan, receiver.sourceSpan) || result.id != localId(3) ||
      result.kind != MirLocalKind::Temporary || result.type != call.resultType ||
      result.sourceScope != scope.id || !sameSpan(result.sourceSpan, call.sourceSpan) ||
      entry.id != blockId(1) || entry.sourceScope != scope.id || entry.statements.size() != 5 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != local.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != receiverTemporary.id ||
      !sameSpan(entry.statements[2].sourceSpan(), receiver.sourceSpan) ||
      entry.statements[3].kind() != MirStatementKind::BorrowCreation ||
      entry.statements[4].kind() != MirStatementKind::StorageLive ||
      entry.statements[4].storageLocal() != result.id ||
      !sameSpan(entry.statements[4].sourceSpan(), call.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != MirInitializationKind::Initialize ||
      initialization.destination.local() != local.id ||
      initialization.destination.rootType() != local.type ||
      initialization.destination.resultType() != local.type ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != MirRvalueKind::NominalAggregate ||
      !sameSpan(entry.statements[1].sourceSpan(), aggregate.sourceSpan)) {
    return false;
  }
  const auto& loweredAggregate = initialization.value.nominalAggregateValue();
  if (loweredAggregate.definition != aggregate.definition ||
      loweredAggregate.type != aggregate.type ||
      loweredAggregate.elements.size() != aggregate.elements.size()) {
    return false;
  }
  for (size_t index = 0; index < aggregate.elements.size(); ++index) {
    const auto& actual = loweredAggregate.elements[index];
    const auto& expected = aggregate.elements[index];
    if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
        actual.operand.constantValue().type != expected.type ||
        !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                      semanticTypes)) {
      return false;
    }
  }
  const auto& borrow = entry.statements[3].borrowCreationValue();
  if (borrow.kind != MirBorrowKind::Mutable ||
      !sameSpan(entry.statements[3].sourceSpan(), receiver.sourceSpan) ||
      borrow.destination.local() != receiverTemporary.id ||
      borrow.destination.rootType() != receiverTemporary.type ||
      borrow.destination.resultType() != receiverTemporary.type ||
      borrow.destination.projections().size() != 0 || borrow.source.local() != local.id ||
      borrow.source.rootType() != local.type || borrow.source.resultType() != local.type ||
      borrow.source.projections().size() != 0) {
    return false;
  }
  const auto& terminator = entry.terminator.callValue();
  auto activatedReceiver = terminator.effect.activatedMutableReceiver();
  if (terminator.callee != call.callee ||
      terminator.arguments.size() != call.arguments.size() + 1 ||
      terminator.destination.local() != result.id ||
      terminator.destination.rootType() != result.type ||
      terminator.destination.resultType() != result.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::ActivateMutableReceiver ||
      activatedReceiver == zc::none ||
      ZC_ASSERT_NONNULL(activatedReceiver) != receiverTemporary.id ||
      terminator.normalTarget != continuation.id || terminator.unwindTarget != zc::none ||
      !matchesPlaceUse(terminator.arguments[0], proofs, copy, receiverTemporary.type)) {
    return false;
  }
  const auto& receiverOperand = terminator.arguments[0].place();
  if (receiverOperand.local() != receiverTemporary.id ||
      receiverOperand.rootType() != receiverTemporary.type ||
      receiverOperand.resultType() != receiverTemporary.type ||
      receiverOperand.projections().size() != 0) {
    return false;
  }
  for (size_t index = 0; index < call.arguments.size(); ++index) {
    const auto& actual = terminator.arguments[index + 1];
    const auto& expected = call.arguments[index];
    if (expected.value == zc::none) return false;
    ZC_IF_SOME(value, expected.value) {
      if (actual.kind() != MirOperandKind::Constant ||
          actual.constantValue().type != expected.type ||
          !sameConstant(actual.constantValue().value, value, module, identities, semanticTypes)) {
        return false;
      }
    }
  }
  ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, result.type) &&
           value.place().local() == result.id && value.place().rootType() == result.type &&
           value.place().resultType() == result.type && value.place().projections().size() == 0;
  }
  return false;
}

bool validDirectCallReturnFunction(const MirFunction& function,
                                   const hir::HirFunctionDeclaration& declaration,
                                   const hir::HirBlockStatement& sourceBlock,
                                   const hir::HirReturnStatement& sourceReturn,
                                   const hir::HirDirectCallExpression& call,
                                   checker::marker::MarkerProofEngine& proofs, identity::DefId copy,
                                   identity::ModuleId module,
                                   const checker::CheckerIdentityAuthority& identities,
                                   const type::SemanticTypeStore& semanticTypes) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 2 || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != call.node ||
      sourceReturn.resultType != declaration.resultType ||
      call.resultType != declaration.resultType) {
    return false;
  }
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  const auto& scope = function.sourceScopes[0];
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& parameterLocal = function.locals[i];
    if (parameterLocal.id != localId(static_cast<uint32_t>(i + 1)) ||
        parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scopeId(1) ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  auto parameterLocalIndex = [&](const identity::CallableParameterKey& key,
                                 size_t& outIndex) -> bool {
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == key) {
        outIndex = i;
        return true;
      }
    }
    return false;
  };
  const auto temporaryLocalId = localId(parameterCount + 1);
  const auto resultLocalId = localId(parameterCount + 2);
  const auto& temporary = function.locals[parameterCount];
  const auto& result = function.locals[parameterCount + 1];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || temporary.id != temporaryLocalId ||
      temporary.kind != MirLocalKind::Temporary || temporary.type != declaration.resultType ||
      temporary.sourceScope != scope.id || !sameSpan(temporary.sourceSpan, call.sourceSpan) ||
      result.id != resultLocalId || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scope.id ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan) || entry.id != blockId(1) ||
      entry.sourceScope != scope.id || entry.statements.size() != 1 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != temporaryLocalId ||
      !sameSpan(entry.statements[0].sourceSpan(), call.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 3 ||
      continuation.statements[0].kind() != MirStatementKind::StorageLive ||
      continuation.statements[0].storageLocal() != resultLocalId ||
      !sameSpan(continuation.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      continuation.statements[1].kind() != MirStatementKind::Assign ||
      continuation.statements[1].assignmentValue().initialization !=
          MirInitializationKind::Initialize ||
      continuation.statements[1].assignmentValue().destination.local() != resultLocalId ||
      continuation.statements[1].assignmentValue().destination.rootType() != result.type ||
      continuation.statements[1].assignmentValue().destination.resultType() != result.type ||
      continuation.statements[1].assignmentValue().destination.projections().size() != 0 ||
      continuation.statements[1].assignmentValue().value.kind() != MirRvalueKind::Use ||
      continuation.statements[1].assignmentValue().value.useValue().operand.kind() !=
          MirOperandKind::Move ||
      continuation.statements[1].assignmentValue().value.useValue().operand.place().local() !=
          temporaryLocalId ||
      continuation.statements[1].assignmentValue().value.useValue().operand.place().rootType() !=
          temporary.type ||
      continuation.statements[1].assignmentValue().value.useValue().operand.place().resultType() !=
          temporary.type ||
      continuation.statements[1]
              .assignmentValue()
              .value.useValue()
              .operand.place()
              .projections()
              .size() != 0 ||
      !sameSpan(continuation.statements[1].sourceSpan(), sourceReturn.sourceSpan) ||
      continuation.statements[2].kind() != MirStatementKind::StorageDead ||
      continuation.statements[2].storageLocal() != temporaryLocalId ||
      !sameSpan(continuation.statements[2].sourceSpan(), sourceReturn.sourceSpan) ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& terminator = entry.terminator.callValue();
  if (terminator.callee != call.callee || terminator.arguments.size() != call.arguments.size() ||
      terminator.destination.local() != temporaryLocalId ||
      terminator.destination.rootType() != temporary.type ||
      terminator.destination.resultType() != temporary.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::NoActivation ||
      terminator.normalTarget != continuation.id || terminator.unwindTarget != zc::none) {
    return false;
  }
  for (size_t index = 0; index < call.arguments.size(); ++index) {
    const auto& actual = terminator.arguments[index];
    const auto& expected = call.arguments[index];
    ZC_IF_SOME(value, expected.value) {
      if (actual.kind() != MirOperandKind::Constant ||
          actual.constantValue().type != expected.type ||
          !sameConstant(actual.constantValue().value, value, module, identities, semanticTypes)) {
        return false;
      }
      continue;
    }
    ZC_IF_SOME(parameter, expected.parameter) {
      size_t parameterIndex = 0;
      if (!parameterLocalIndex(parameter, parameterIndex) ||
          !matchesPlaceUse(actual, proofs, copy, expected.type) ||
          actual.place().local() != localId(static_cast<uint32_t>(parameterIndex + 1)) ||
          actual.place().rootType() != expected.type ||
          actual.place().resultType() != expected.type ||
          actual.place().projections().size() != 0) {
        return false;
      }
      continue;
    }
    return false;
  }
  ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, result.type) &&
           value.place().local() == resultLocalId && value.place().rootType() == result.type &&
           value.place().resultType() == result.type && value.place().projections().size() == 0;
  }
  return false;
}

// Validates the RFC 0007 unsafe-scope boundary structural contract: every
// boundary names a nonzero source scope owned by the enclosing function,
// enter and exit markers are properly nested (an exit closes only the
// innermost open scope), and every enter has one matching exit. Dominance and
// the per-path exit-cut rule require CFG analysis and remain future work.
bool validateUnsafeScopeBoundaries(const MirFunction& function) {
  for (const auto& scope : function.sourceScopes) {
    if (!scope.id.isValid()) return false;
  }
  zc::Vector<MirSourceScopeId> openScopes;
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      if (statement.kind() != MirStatementKind::UnsafeScopeBoundary) continue;
      const auto& boundary = statement.unsafeScopeBoundaryValue();
      if (!boundary.scope.isValid()) return false;
      bool ownsScope = false;
      for (const auto& scope : function.sourceScopes) {
        if (scope.id == boundary.scope) {
          ownsScope = true;
          break;
        }
      }
      if (!ownsScope) return false;
      if (boundary.kind == MirUnsafeScopeBoundaryKind::Enter) {
        for (const auto& open : openScopes) {
          if (open == boundary.scope) return false;
        }
        openScopes.add(boundary.scope);
      } else if (boundary.kind == MirUnsafeScopeBoundaryKind::Exit) {
        if (openScopes.empty() || openScopes[openScopes.size() - 1] != boundary.scope) {
          return false;
        }
        openScopes.removeLast();
      } else {
        return false;
      }
    }
  }
  return openScopes.empty();
}

bool blockExists(const MirFunction& function, MirBlockId id) {
  for (const auto& block : function.blocks) {
    if (block.id == id) return true;
  }
  return false;
}

/// \brief Validates that every terminator edge targets a block in the same
/// function. Return and Unreachable carry no edges; Call, Goto, and
/// SwitchInt targets must resolve.
bool validateTerminatorTargets(const MirFunction& function) {
  for (const auto& block : function.blocks) {
    const auto& terminator = block.terminator;
    if (terminator.kind() == MirTerminatorKind::Call) {
      const auto& call = terminator.callValue();
      if (!call.normalTarget.isValid() || !blockExists(function, call.normalTarget)) return false;
      ZC_IF_SOME(unwind, call.unwindTarget) {
        if (!unwind.isValid() || !blockExists(function, unwind)) return false;
      }
    } else if (terminator.kind() == MirTerminatorKind::Goto) {
      const auto& gotoTerminator = terminator.gotoValue();
      if (!gotoTerminator.target.isValid() || !blockExists(function, gotoTerminator.target)) {
        return false;
      }
    } else if (terminator.kind() == MirTerminatorKind::SwitchInt) {
      const auto& switchInt = terminator.switchIntValue();
      for (const auto& arm : switchInt.arms) {
        if (!arm.target.isValid() || !blockExists(function, arm.target)) return false;
      }
      if (!switchInt.defaultTarget.isValid() || !blockExists(function, switchInt.defaultTarget)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

MirRevisionId MirRevisionId::fromDigest(const identity::Sha256Digest& digest) noexcept {
  return MirRevisionId(digest);
}

zc::Maybe<zc::Array<uint8_t>> MirRevisionCodec::encodeBuiltFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& checkedFactsRevision,
    const identity::Sha256Digest& dispatchFactsRevision,
    const identity::Sha256Digest& borrowEvidenceRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  if (expandedModuleKey.size() == 0) return zc::none;
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.mir-revision";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(checkedFactsRevision);
  encoder.encodeDigest(dispatchFactsRevision);
  encoder.encodeDigest(borrowEvidenceRevision);
  encoder.encodeSequenceSize(canonicalFunctions.size());
  for (const auto& function : canonicalFunctions) {
    if (function.size() == 0) return zc::none;
    encoder.encodeByteString(function.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> MirRevisionCodec::encodeBuilt(
    const identity::ContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
    const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  return encodeBuiltFramed(contextFingerprint.digest(), expandedModuleKey,
                           checkedFactsRevision.digest(), dispatchFactsRevision.digest(),
                           borrowEvidenceRevision.digest(), canonicalFunctions);
}

zc::Maybe<MirRevisionId> MirRevisionCodec::computeBuilt(
    const identity::ContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
    const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  auto bytes = encodeBuilt(contextFingerprint, expandedModuleKey, checkedFactsRevision,
                           dispatchFactsRevision, borrowEvidenceRevision, canonicalFunctions);
  if (bytes == zc::none) return zc::none;
  ZC_IF_SOME(value, bytes) {
    auto digest = identity::sha256(value.asPtr());
    ZC_IF_SOME(hash, digest) { return MirRevisionId::fromDigest(hash); }
  }
  return zc::none;
}

BuiltMirCandidate::BuiltMirCandidate(const hir::VerifiedHirModule& sourceHir,
                                     zc::Vector<MirFunction>&& functions,
                                     zc::Vector<zc::Array<uint8_t>>&& canonicalFunctions,
                                     MirRevisionId revision) noexcept
    : sourceHir(sourceHir),
      functions(zc::mv(functions)),
      canonicalFunctions(zc::mv(canonicalFunctions)),
      revision(revision) {}

struct VerifiedBuiltMir::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::ContextFingerprint&& contextFingerprint,
       identity::CompilationUnitId compilationUnit, identity::CrateId crate,
       identity::ModuleId module,
       const checker::checked::CheckedFactsRevision& checkedFactsRevision,
       const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
       const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
       ownership::OwnershipAdmittedBoundModule&& boundModule,
       checker::CheckerIdentityAuthority&& identities,
       driver::borrow_evidence::VerifiedBorrowEvidenceLease&& borrowEvidenceLease,
       driver::borrow_evidence::BorrowEvidenceRepositoryCapability&& borrowEvidenceCapability,
       zc::Vector<MirFunction>&& functions, zc::Vector<zc::Array<uint8_t>>&& canonicalFunctions,
       MirRevisionId revision) noexcept
      : boundModule(zc::mv(boundModule)),
        identities(zc::mv(identities)),
        semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        compilationUnit(compilationUnit),
        crate(crate),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        dispatchFactsRevision(dispatchFactsRevision),
        borrowEvidenceRevision(borrowEvidenceRevision),
        borrowEvidenceCapability(zc::mv(borrowEvidenceCapability)),
        functions(zc::mv(functions)),
        canonicalFunctions(zc::mv(canonicalFunctions)),
        revision(revision),
        borrowEvidenceLease(zc::mv(borrowEvidenceLease)) {}

  ownership::OwnershipAdmittedBoundModule boundModule;
  checker::CheckerIdentityAuthority identities;
  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
  identity::ModuleId module;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  checker::dispatch::DispatchFactsRevision dispatchFactsRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  driver::borrow_evidence::BorrowEvidenceRepositoryCapability borrowEvidenceCapability;
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  MirRevisionId revision;
  driver::borrow_evidence::VerifiedBorrowEvidenceLease borrowEvidenceLease;
};

VerifiedBuiltMir::VerifiedBuiltMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedBuiltMir::~VerifiedBuiltMir() noexcept(false) = default;
VerifiedBuiltMir::VerifiedBuiltMir(VerifiedBuiltMir&&) noexcept = default;
VerifiedBuiltMir& VerifiedBuiltMir::operator=(VerifiedBuiltMir&&) noexcept = default;

identity::SemanticContextBrand VerifiedBuiltMir::semanticContext() const noexcept {
  return impl->semanticContext;
}

const identity::ContextFingerprint& VerifiedBuiltMir::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}

identity::CompilationUnitId VerifiedBuiltMir::compilationUnit() const noexcept {
  return impl->compilationUnit;
}
identity::CrateId VerifiedBuiltMir::crate() const noexcept { return impl->crate; }
identity::ModuleId VerifiedBuiltMir::module() const noexcept { return impl->module; }

const checker::checked::CheckedFactsRevision& VerifiedBuiltMir::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}

const checker::dispatch::DispatchFactsRevision& VerifiedBuiltMir::dispatchFactsRevision()
    const noexcept {
  return impl->dispatchFactsRevision;
}

const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedBuiltMir::borrowEvidenceRevision()
    const noexcept {
  return impl->borrowEvidenceRevision;
}

const driver::borrow_evidence::VerifiedBorrowEvidenceLease& VerifiedBuiltMir::borrowEvidenceLease()
    const noexcept {
  return impl->borrowEvidenceLease;
}

ownership::OwnershipAdmittedBoundModule VerifiedBuiltMir::retainAdmittedBoundModule() const {
  return impl->boundModule.retain();
}

checker::CheckerIdentityAuthority VerifiedBuiltMir::retainIdentityAuthority() const {
  return impl->identities.clone();
}

driver::borrow_evidence::VerifiedBorrowEvidenceLease VerifiedBuiltMir::retainBorrowEvidenceLease()
    const {
  return impl->borrowEvidenceLease.clone();
}

driver::borrow_evidence::BorrowEvidenceRepositoryCapability
VerifiedBuiltMir::retainBorrowEvidenceCapability() const {
  return impl->borrowEvidenceCapability.clone();
}

bool VerifiedBuiltMir::matchesBorrowEvidenceInput(
    const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) const noexcept {
  if (!impl->borrowEvidenceLease.matches(lease) ||
      !impl->borrowEvidenceCapability.matches(capability)) {
    return false;
  }
  const auto resolved = capability.lookup(lease);
  const auto embedded = impl->borrowEvidenceCapability.lookup(impl->borrowEvidenceLease);
  return resolved.isResolved() && embedded.isResolved() &&
         resolved.evidence().semanticContext() == embedded.evidence().semanticContext() &&
         resolved.evidence().contextFingerprint().digest() ==
             embedded.evidence().contextFingerprint().digest() &&
         resolved.evidence().module() == embedded.evidence().module() &&
         resolved.evidence().revision().digest() == embedded.evidence().revision().digest();
}

driver::borrow_evidence::BorrowEvidenceLookupResult VerifiedBuiltMir::borrowEvidence()
    const noexcept {
  return impl->borrowEvidenceCapability.lookup(impl->borrowEvidenceLease);
}

const MirRevisionId& VerifiedBuiltMir::revision() const noexcept { return impl->revision; }

zc::ArrayPtr<const MirFunction> VerifiedBuiltMir::functions() const noexcept {
  return impl->functions.asPtr();
}

zc::ArrayPtr<const zc::Array<uint8_t>> VerifiedBuiltMir::canonicalFunctionRecords() const noexcept {
  return impl->canonicalFunctions.asPtr();
}

ir::IrOperationResult<BuiltMirCandidate> BuiltMirBuilder::build(const BuiltMirInput& input) {
  const auto& hirModule = input.hir;
  const auto module = hirModule.module();
  const auto identities = hirModule.retainIdentityAuthority();
  const auto& semanticTypes = hirModule.semanticTypes();
  if (!validBuiltMirInput(hirModule, input.body)) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        firstDefinition(hirModule), identities, 0);
  }
  auto proofInput = checker::marker::MarkerProofInput::from(input.body);
  if (proofInput == zc::none) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        firstDefinition(hirModule), identities, 0);
  }
  checker::marker::MarkerProofEngine proofs(zc::mv(ZC_ASSERT_NONNULL(proofInput)));
  const auto copy = input.body.standardMarkers.copy();
  const auto borrowCapability = hirModule.borrowEvidenceCapability();
  const auto evidence = borrowCapability.lookup(hirModule.borrowEvidenceLease());
  size_t uninitializedLocalReturnCount = 0;
  for (const auto& local : hirModule.locals()) {
    if (local.initializer == zc::none) ++uninitializedLocalReturnCount;
  }
  const auto parameterReturnCount = hirModule.parameterReferences().size();
  const auto parameterReborrowCount = hirModule.parameterReborrows().size();
  size_t localAliasReborrowCount = 0;
  for (const auto& reborrow : hirModule.parameterReborrows()) {
    if (reborrow.sourceAlias != zc::none) ++localAliasReborrowCount;
  }
  // The module value-node checksum grants each HIR function exactly one value
  // node (expression, call, aggregate, parameter reference, ...). A sequential
  // N-local body instead materializes one value node per literal, aggregate, or
  // parameter-reference initializer, per literal/parameter binary operand, plus
  // one for a parameter return; and one primitive-binary operation node per
  // binary initializer (counted on the equation's left via
  // primitiveBinaryOperations). Local- and return-of-local references are not
  // value nodes here. This signed term carries the per-function excess
  // (valueNodes - binaryBindings - 1) so the balance holds for any N; it is zero
  // for the former two-local literal or aggregate source and can be negative when
  // binary operands are earlier locals.
  int64_t sequentialValueNodeExcess = 0;
  for (const auto& sequentialFunction : hirModule.functions()) {
    auto sequentialBlock = blockFor(hirModule, sequentialFunction.body);
    ZC_IF_SOME(block, sequentialBlock) {
      if (block.statements.size() < 3) continue;
      bool allLeadingLocals = true;
      for (size_t i = 0; i + 1 < block.statements.size(); ++i) {
        if (localFor(hirModule, block.statements[i]) == zc::none) {
          allLeadingLocals = false;
          break;
        }
      }
      auto sequentialReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
      if (!allLeadingLocals || sequentialReturn == zc::none) continue;
      int64_t valueNodes = 0;
      int64_t binaryBindings = 0;
      for (size_t i = 0; i + 1 < block.statements.size(); ++i) {
        auto localBinding = localFor(hirModule, block.statements[i]);
        ZC_IF_SOME(local, localBinding) {
          ZC_IF_SOME(initializer, local.initializer) {
            auto binary = primitiveBinaryFor(hirModule, initializer);
            ZC_IF_SOME(value, binary) {
              // The binary op node is on the equation's left; its literal and
              // parameter operands are value nodes on the right. Local operands
              // are localReferences and count on neither side. An operand that is
              // itself a nested primitive binary contributes its own op node (one
              // more binaryBinding) and its two leaf operands as value nodes.
              ++binaryBindings;
              for (const auto operand : {value.left, value.right}) {
                auto nested = primitiveBinaryFor(hirModule, operand);
                ZC_IF_SOME(nestedValue, nested) {
                  ++binaryBindings;
                  for (const auto leaf : {nestedValue.left, nestedValue.right}) {
                    if (expressionFor(hirModule, leaf) != zc::none ||
                        parameterReferenceFor(hirModule, leaf) != zc::none) {
                      ++valueNodes;
                    }
                  }
                  continue;
                }
                if (expressionFor(hirModule, operand) != zc::none ||
                    parameterReferenceFor(hirModule, operand) != zc::none) {
                  ++valueNodes;
                }
              }
            }
            if (binary == zc::none && (expressionFor(hirModule, initializer) != zc::none ||
                                       aggregateFor(hirModule, initializer) != zc::none ||
                                       parameterReferenceFor(hirModule, initializer) != zc::none)) {
              ++valueNodes;
            }
          }
        }
      }
      ZC_IF_SOME(returnStatement, sequentialReturn) {
        if (parameterReferenceFor(hirModule, returnStatement.value) != zc::none) ++valueNodes;
      }
      sequentialValueNodeExcess += valueNodes - binaryBindings - 1;
    }
  }
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != hirModule.borrowEvidenceRevision().digest() ||
      hirModule.borrowEvidenceLease().key().revision.digest() !=
          hirModule.borrowEvidenceRevision().digest() ||
      static_cast<int64_t>(hirModule.declarations().size() + hirModule.functions().size() +
                           hirModule.conditionals().size() * 2 +
                           hirModule.primitiveBinaryOperations().size() +
                           hirModule.loops().size()) +
              sequentialValueNodeExcess !=
          static_cast<int64_t>(hirModule.expressions().size() + hirModule.calls().size() +
                               hirModule.aggregates().size() + uninitializedLocalReturnCount +
                               parameterReturnCount + parameterReborrowCount -
                               localAliasReborrowCount - hirModule.localWrites().size()) ||
      hirModule.functions().size() != hirModule.blocks().size() ||
      hirModule.functions().size() != hirModule.returns().size()) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        firstDefinition(hirModule), identities, 0);
  }

  zc::Vector<PendingMirFunction> pending;
  for (const auto& declaration : hirModule.declarations()) {
    auto expression = expressionFor(hirModule, declaration.initializer);
    auto definition = identities.definition(declaration.definition);
    auto semanticType = semanticTypes.get(declaration.inferredType);
    if (expression == zc::none || definition == zc::none ||
        !semanticType.is<type::SemanticTypeLookup>() ||
        !declaration.definition.belongsTo(hirModule.semanticContext()) ||
        !declaration.inferredType.belongsTo(hirModule.semanticContext())) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
    }
    ZC_IF_SOME(literal, expression) {
      zc::Vector<MirSourceScope> scopes;
      zc::Maybe<MirSourceScopeId> noParent;
      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
      zc::Vector<MirLocalDeclaration> locals;
      locals.add(MirLocalDeclaration{localId(1), MirLocalKind::ModuleInitializerResult,
                                     declaration.inferredType, scopeId(1),
                                     literal.sourceSpan.clone()});
      zc::Vector<MirStatement> statements;
      statements.add(MirStatement::storageLive(localId(1), literal.sourceSpan.clone()));
      zc::Vector<MirProjection> destinationProjections;
      auto constant = MirOperand::constant(declaration.inferredType, literal.value.clone());
      statements.add(
          MirStatement::assign(MirPlace(localId(1), declaration.inferredType,
                                        zc::mv(destinationProjections), declaration.inferredType),
                               MirRvalue::use(zc::mv(constant)), MirInitializationKind::Initialize,
                               literal.sourceSpan.clone()));
      zc::Vector<MirProjection> returnProjections;
      auto returnOperand = placeUse(proofs, copy,
                                    MirPlace(localId(1), declaration.inferredType,
                                             zc::mv(returnProjections), declaration.inferredType));
      if (returnOperand == zc::none) {
        return rejectMir<BuiltMirCandidate>(
            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
            declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
      }
      zc::Vector<MirBasicBlock> blocks;
      blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                               MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                          literal.sourceSpan.clone())});
      MirFunction function{declaration.definition,
                           MirFunctionKind::ModuleInitializer,
                           declaration.definitionKind,
                           declaration.inferredType,
                           declaration.sourceSpan.clone(),
                           zc::mv(scopes),
                           zc::mv(locals),
                           zc::mv(blocks)};
      zc::Array<uint8_t> ownerKey;
      ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
      continue;
    }
    ZC_UNREACHABLE
  }
  // RFC 0007 unsafe-scope lowering: the scalar-return path emits
  // UnsafeScopeBoundary(Enter)/UnsafeScopeBoundary(Exit) around the returned
  // constant when the HIR function declaration carries an unsafe-block node.
  // Other function shapes do not yet lower unsafe blocks.
  for (const auto& declaration : hirModule.functions()) {
    auto sourceBlock = blockFor(hirModule, declaration.body);
    if (sourceBlock == zc::none) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
    }
    ZC_IF_SOME(block, sourceBlock) {
      if (block.statements.size() == 2) {
        auto loop = loopFor(hirModule, block.statements[0]);
        ZC_IF_SOME(loopValue, loop) {
          auto sourceReturn = returnFor(hirModule, block.statements[1]);
          auto definition = identities.definition(declaration.definition);
          auto conditionRef = parameterReferenceFor(hirModule, loopValue.condition);
          ZC_IF_SOME(returnStatement, sourceReturn) {
            auto returnExpr = expressionFor(hirModule, returnStatement.value);
            ZC_IF_SOME(condition, conditionRef) {
              ZC_IF_SOME(returnLiteral, returnExpr) {
                size_t conditionIndex = 0;
                bool found = false;
                for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                  if (declaration.parameters[i].key == condition.parameter) {
                    conditionIndex = i;
                    found = true;
                    break;
                  }
                }
                if (!found || returnLiteral.type != declaration.resultType ||
                    condition.type != declaration.parameters[conditionIndex].type ||
                    definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                const auto conditionLocal = localId(static_cast<uint32_t>(conditionIndex + 1));
                const auto resultLocal =
                    localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                  locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                                 MirLocalKind::Parameter,
                                                 declaration.parameters[i].type, scopeId(1),
                                                 declaration.parameters[i].sourceSpan.clone()});
                }
                locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::FunctionResult,
                                               declaration.resultType, scopeId(1),
                                               returnStatement.sourceSpan.clone()});
                zc::Vector<MirProjection> conditionProjections;
                auto discriminant =
                    placeUse(proofs, copy,
                             MirPlace(conditionLocal, condition.type, zc::mv(conditionProjections),
                                      condition.type));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand =
                    placeUse(proofs, copy,
                             MirPlace(resultLocal, declaration.resultType,
                                      zc::mv(returnProjections), declaration.resultType));
                if (discriminant == zc::none || returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                // Reducible four-block loop CFG. The result local is allocated
                // at function entry (dominating the whole loop); the header
                // block branches into the empty body on a true discriminant and
                // to the exit otherwise; the body jumps back to the header,
                // forming a reducible back-edge.
                zc::Vector<MirStatement> entryStatements;
                entryStatements.add(
                    MirStatement::storageLive(resultLocal, returnStatement.sourceSpan.clone()));
                zc::Vector<MirSwitchIntArm> arms;
                arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                                         blockId(3)});
                zc::Vector<MirProjection> exitProjections;
                zc::Vector<MirStatement> exitStatements;
                exitStatements.add(MirStatement::assign(
                    MirPlace(resultLocal, declaration.resultType, zc::mv(exitProjections),
                             declaration.resultType),
                    MirRvalue::use(
                        MirOperand::constant(returnLiteral.type, returnLiteral.value.clone())),
                    MirInitializationKind::Initialize, returnLiteral.sourceSpan.clone()));
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(entryStatements),
                    MirTerminator::gotoTarget(blockId(2), loopValue.sourceSpan.clone())});
                blocks.add(MirBasicBlock{
                    blockId(2), scopeId(1), zc::Vector<MirStatement>{},
                    MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                             blockId(4), loopValue.sourceSpan.clone())});
                blocks.add(MirBasicBlock{
                    blockId(3), scopeId(1), zc::Vector<MirStatement>{},
                    MirTerminator::gotoTarget(blockId(2), loopValue.sourceSpan.clone())});
                blocks.add(MirBasicBlock{
                    blockId(4), scopeId(1), zc::mv(exitStatements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceReturn = returnFor(hirModule, block.statements[1]);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(returnStatement, sourceReturn) {
            auto receiverCall = receiverCallFor(hirModule, returnStatement.value);
            hir::HirNodeId initializerNode;
            ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
            auto aggregate = aggregateFor(hirModule, initializerNode);
            ZC_IF_SOME(call, receiverCall) {
              ZC_IF_SOME(initializer, aggregate) {
                auto receiver = localReferenceFor(hirModule, call.receiver);
                ZC_IF_SOME(reference, receiver) {
                  if (local.initializer != initializer.node || local.local != reference.local ||
                      local.type != initializer.type || local.type != call.receiverSourceType ||
                      reference.type != call.receiverSourceType ||
                      reference.category != hir::HirValueCategory::Place ||
                      call.resultType != declaration.resultType ||
                      call.receiverMode != checker::checked::ReceiverMode::Mutable ||
                      call.receiverAdjustments.size() != 1 ||
                      call.receiverAdjustments[0] !=
                          checker::checked::ReceiverAdjustmentStep::BorrowMutable ||
                      definition == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirSourceScope> scopes;
                  zc::Maybe<MirSourceScopeId> noParent;
                  scopes.add(
                      MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                  zc::Vector<MirLocalDeclaration> locals;
                  locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                 scopeId(1), local.sourceSpan.clone()});
                  locals.add(MirLocalDeclaration{localId(2), MirLocalKind::Temporary,
                                                 call.receiverType, scopeId(1),
                                                 reference.sourceSpan.clone()});
                  locals.add(MirLocalDeclaration{localId(3), MirLocalKind::Temporary,
                                                 call.resultType, scopeId(1),
                                                 call.sourceSpan.clone()});
                  zc::Vector<MirStatement> entryStatements;
                  entryStatements.add(
                      MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                  zc::Vector<MirNominalAggregateElement> elements;
                  for (const auto& element : initializer.elements) {
                    elements.add(MirNominalAggregateElement{
                        element.field, MirOperand::constant(element.type, element.value.clone())});
                  }
                  zc::Vector<MirProjection> initializerProjections;
                  entryStatements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(initializerProjections), local.type),
                      MirRvalue::nominalAggregate(initializer.definition, initializer.type,
                                                  zc::mv(elements)),
                      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
                  entryStatements.add(
                      MirStatement::storageLive(localId(2), reference.sourceSpan.clone()));
                  zc::Vector<MirProjection> receiverDestinationProjections;
                  zc::Vector<MirProjection> receiverSourceProjections;
                  entryStatements.add(MirStatement::borrowCreation(
                      MirPlace(localId(2), call.receiverType,
                               zc::mv(receiverDestinationProjections), call.receiverType),
                      MirBorrowKind::Mutable,
                      MirPlace(localId(1), local.type, zc::mv(receiverSourceProjections),
                               local.type),
                      reference.sourceSpan.clone()));
                  entryStatements.add(
                      MirStatement::storageLive(localId(3), call.sourceSpan.clone()));
                  zc::Vector<MirProjection> receiverArgumentProjections;
                  auto receiverArgument =
                      placeUse(proofs, copy,
                               MirPlace(localId(2), call.receiverType,
                                        zc::mv(receiverArgumentProjections), call.receiverType));
                  if (receiverArgument == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirOperand> arguments;
                  arguments.add(zc::mv(ZC_ASSERT_NONNULL(receiverArgument)));
                  bool constantArguments = true;
                  for (const auto& argument : call.arguments) {
                    ZC_IF_SOME(value, argument.value) {
                      arguments.add(MirOperand::constant(argument.type, value.clone()));
                    } else {
                      constantArguments = false;
                    }
                  }
                  if (!constantArguments) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirProjection> resultProjections;
                  zc::Maybe<MirBlockId> noUnwind;
                  auto callTerminator =
                      MirTerminator::call(call.callee, zc::mv(arguments),
                                          MirCallEffect::activateMutableReceiver(localId(2)),
                                          MirPlace(localId(3), call.resultType,
                                                   zc::mv(resultProjections), call.resultType),
                                          blockId(2), zc::mv(noUnwind), call.sourceSpan.clone());
                  zc::Vector<MirProjection> returnProjections;
                  auto returnOperand =
                      placeUse(proofs, copy,
                               MirPlace(localId(3), call.resultType, zc::mv(returnProjections),
                                        call.resultType));
                  if (returnOperand == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirStatement> continuationStatements;
                  zc::Vector<MirBasicBlock> blocks;
                  blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                           zc::mv(callTerminator)});
                  blocks.add(MirBasicBlock{
                      blockId(2), scopeId(1), zc::mv(continuationStatements),
                      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                 returnStatement.sourceSpan.clone())});
                  MirFunction function{declaration.definition,
                                       MirFunctionKind::Function,
                                       identity::DefinitionKind::Function,
                                       declaration.resultType,
                                       declaration.sourceSpan.clone(),
                                       zc::mv(scopes),
                                       zc::mv(locals),
                                       zc::mv(blocks)};
                  zc::Array<uint8_t> ownerKey;
                  ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                  pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                  continue;
                }
              }
            }
          }
        }
      }
      // Sequential N-local body: N (>= 2) leading `let` bindings followed by a
      // single `return <local-or-parameter>`. Parameters occupy localId(1..P);
      // user local i occupies localId(P + i + 1). Each binding lowers to
      // StorageLive + Assign of a constant (literal), nominal aggregate, or a
      // copy/move place-use of the referenced parameter or earlier local.
      {
        bool allLeadingLocals = block.statements.size() >= 3;
        for (size_t i = 0; allLeadingLocals && i + 1 < block.statements.size(); ++i) {
          if (localFor(hirModule, block.statements[i]) == zc::none) allLeadingLocals = false;
        }
        auto sequentialReturn =
            allLeadingLocals ? returnFor(hirModule, block.statements[block.statements.size() - 1])
                             : zc::Maybe<const hir::HirReturnStatement&>();
        if (allLeadingLocals && sequentialReturn != zc::none) {
          const size_t bindingCount = block.statements.size() - 1;
          const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
          auto definition = identities.definition(declaration.definition);
          // Resolve every binding and validate its layer-local id.
          bool valid = definition != zc::none;
          zc::Vector<const hir::HirLocalBinding*> bindings;
          for (size_t i = 0; valid && i < bindingCount; ++i) {
            auto localBinding = localFor(hirModule, block.statements[i]);
            if (localBinding == zc::none) {
              valid = false;
              break;
            }
            ZC_IF_SOME(local, localBinding) {
              if (local.local.ordinal() != static_cast<uint32_t>(i + 1) ||
                  local.initializer == zc::none || local.type != declaration.resultType) {
                valid = false;
              }
              bindings.add(&local);
            }
          }
          hir::HirNodeId returnValueNode;
          ZC_IF_SOME(returnStatement, sequentialReturn) { returnValueNode = returnStatement.value; }
          auto returnLocalReference = localReferenceFor(hirModule, returnValueNode);
          auto returnParameterReference = parameterReferenceFor(hirModule, returnValueNode);
          if (valid && returnLocalReference == zc::none && returnParameterReference == zc::none) {
            valid = false;
          }
          if (valid) {
            zc::Vector<MirSourceScope> scopes;
            zc::Maybe<MirSourceScopeId> noParent;
            scopes.add(
                MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
            zc::Vector<MirLocalDeclaration> locals;
            for (uint32_t p = 0; p < parameterCount; ++p) {
              locals.add(MirLocalDeclaration{localId(p + 1), MirLocalKind::Parameter,
                                             declaration.parameters[p].type, scopeId(1),
                                             declaration.parameters[p].sourceSpan.clone()});
            }
            for (size_t i = 0; i < bindingCount; ++i) {
              locals.add(MirLocalDeclaration{localId(parameterCount + static_cast<uint32_t>(i) + 1),
                                             MirLocalKind::UserLocal, bindings[i]->type, scopeId(1),
                                             bindings[i]->sourceSpan.clone()});
            }
            // A nested operand (`a + b * c`) lowers to a synthesized Temporary
            // local holding the inner binary's result; the outer operand is then a
            // copy of that temp. Declare one temp per nested operand, after the N
            // user locals, deriving the same layout the verifier uses. `nestedTemp`
            // returns the HIR nested-binary node for a binding, or none.
            auto nestedTempFor =
                [&](const hir::HirLocalBinding& binding) -> zc::Maybe<hir::HirNodeId> {
              hir::HirNodeId initializer;
              ZC_IF_SOME(value, binding.initializer) { initializer = value; }
              auto outer = primitiveBinaryFor(hirModule, initializer);
              ZC_IF_SOME(value, outer) {
                if (primitiveBinaryFor(hirModule, value.left) != zc::none) return value.left;
                if (primitiveBinaryFor(hirModule, value.right) != zc::none) return value.right;
              }
              return zc::none;
            };
            zc::Vector<zc::Maybe<uint32_t>> bindingTempOrdinal;
            uint32_t tempCount = 0;
            for (size_t i = 0; i < bindingCount; ++i) {
              if (nestedTempFor(*bindings[i]) != zc::none) {
                const uint32_t tempOrdinal =
                    parameterCount + static_cast<uint32_t>(bindingCount) + tempCount + 1;
                bindingTempOrdinal.add(tempOrdinal);
                // The temp holds the inner binary's result, which feeds the outer
                // operand slot, so its type is the outer binary's operand type.
                identity::SemanticTypeId tempType = bindings[i]->type;
                hir::HirNodeId initializer;
                ZC_IF_SOME(value, bindings[i]->initializer) { initializer = value; }
                auto outer = primitiveBinaryFor(hirModule, initializer);
                ZC_IF_SOME(value, outer) { tempType = value.operandType; }
                locals.add(MirLocalDeclaration{localId(tempOrdinal), MirLocalKind::Temporary,
                                               tempType, scopeId(1),
                                               bindings[i]->sourceSpan.clone()});
                ++tempCount;
              } else {
                zc::Maybe<uint32_t> noTemp;
                bindingTempOrdinal.add(zc::mv(noTemp));
              }
            }
            zc::Vector<MirStatement> statements;
            auto userLocalId = [&](size_t index) {
              return localId(parameterCount + static_cast<uint32_t>(index) + 1);
            };
            bool built = true;
            for (size_t i = 0; built && i < bindingCount; ++i) {
              const auto& local = *bindings[i];
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
              auto literal = expressionFor(hirModule, initializerNode);
              auto aggregate = aggregateFor(hirModule, initializerNode);
              auto localReference = localReferenceFor(hirModule, initializerNode);
              auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
              // The binding's own StorageLive is emitted after any nested-operand
              // temp statements (appended while building the rvalue below), so a
              // nested operand's temp pair precedes this binding's pair.
              zc::Maybe<MirRvalue> rvalue;
              identity::SourceSpan assignSpan = local.sourceSpan.clone();
              ZC_IF_SOME(value, literal) {
                if (value.type != local.type || value.category != hir::HirValueCategory::Value) {
                  built = false;
                } else {
                  rvalue = MirRvalue::use(MirOperand::constant(local.type, value.value.clone()));
                  assignSpan = value.sourceSpan.clone();
                }
              }
              ZC_IF_SOME(value, aggregate) {
                if (value.type != local.type || value.category != hir::HirValueCategory::Value) {
                  built = false;
                } else {
                  zc::Vector<MirNominalAggregateElement> elements;
                  for (const auto& element : value.elements) {
                    elements.add(MirNominalAggregateElement{
                        element.field, MirOperand::constant(element.type, element.value.clone())});
                  }
                  rvalue =
                      MirRvalue::nominalAggregate(value.definition, value.type, zc::mv(elements));
                  assignSpan = value.sourceSpan.clone();
                }
              }
              ZC_IF_SOME(value, localReference) {
                if (value.type != local.type || value.category != hir::HirValueCategory::Place ||
                    value.local.ordinal() == 0 ||
                    value.local.ordinal() > static_cast<uint32_t>(i)) {
                  built = false;
                } else {
                  zc::Vector<MirProjection> projections;
                  auto operand = placeUse(proofs, copy,
                                          MirPlace(userLocalId(value.local.ordinal() - 1),
                                                   local.type, zc::mv(projections), local.type));
                  if (operand == zc::none) {
                    built = false;
                  } else {
                    rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
                    assignSpan = value.sourceSpan.clone();
                  }
                }
              }
              ZC_IF_SOME(value, parameterReference) {
                size_t parameterIndex = 0;
                bool found = false;
                for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                  if (declaration.parameters[p].key == value.parameter) {
                    parameterIndex = p;
                    found = true;
                    break;
                  }
                }
                if (!found || value.type != local.type ||
                    value.category != hir::HirValueCategory::Place) {
                  built = false;
                } else {
                  zc::Vector<MirProjection> projections;
                  auto operand =
                      placeUse(proofs, copy,
                               MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                        local.type, zc::mv(projections), local.type));
                  if (operand == zc::none) {
                    built = false;
                  } else {
                    rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
                    assignSpan = value.sourceSpan.clone();
                  }
                }
              }
              // A primitive-binary initializer lowers to an Arithmetic or
              // Comparison rvalue assigned into the local. Each operand is a
              // constant (literal), a copy of a parameter local, or a copy of an
              // earlier user local.
              auto binary = primitiveBinaryFor(hirModule, initializerNode);
              ZC_IF_SOME(value, binary) {
                const auto comparisonOperator = mirComparisonOperatorFor(value.operation);
                const auto arithmeticOperator = mirArithmeticOperatorFor(value.operation);
                const bool isArithmeticBinary =
                    comparisonOperator == zc::none && arithmeticOperator != zc::none;
                // Builds one leaf operand of a binary: a scalar-literal constant, a
                // copy of a parameter local, or a copy of an earlier user local, of
                // the given operand type.
                auto binaryLeaf =
                    [&](hir::HirNodeId operandNode,
                        identity::SemanticTypeId operandType) -> zc::Maybe<MirOperand> {
                  auto operandLiteral = expressionFor(hirModule, operandNode);
                  ZC_IF_SOME(literal, operandLiteral) {
                    if (literal.type != operandType) return zc::none;
                    return MirOperand::constant(operandType, literal.value.clone());
                  }
                  auto operandParameter = parameterReferenceFor(hirModule, operandNode);
                  ZC_IF_SOME(parameter, operandParameter) {
                    if (parameter.type != operandType) return zc::none;
                    size_t parameterIndex = 0;
                    bool found = false;
                    for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                      if (declaration.parameters[p].key == parameter.parameter) {
                        parameterIndex = p;
                        found = true;
                        break;
                      }
                    }
                    if (!found) return zc::none;
                    zc::Vector<MirProjection> projections;
                    return placeUse(proofs, copy,
                                    MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                             operandType, zc::mv(projections), operandType));
                  }
                  auto operandLocal = localReferenceFor(hirModule, operandNode);
                  ZC_IF_SOME(reference, operandLocal) {
                    if (reference.type != operandType || reference.local.ordinal() == 0 ||
                        reference.local.ordinal() > static_cast<uint32_t>(i)) {
                      return zc::none;
                    }
                    zc::Vector<MirProjection> projections;
                    return placeUse(proofs, copy,
                                    MirPlace(userLocalId(reference.local.ordinal() - 1),
                                             operandType, zc::mv(projections), operandType));
                  }
                  return zc::none;
                };
                // The temp ordinal reserved for this binding's nested operand, if
                // any. A nested operand lowers to StorageLive(temp) + Assign(temp =
                // inner rvalue) emitted before the outer assignment; the outer
                // operand slot is then a copy of the temp.
                zc::Maybe<uint32_t> tempOrdinal = bindingTempOrdinal[i];
                // Builds one outer operand: a leaf (literal/parameter/local) or, for
                // a nested operand, a copy of the synthesized temp (whose assignment
                // this lambda also emits, once).
                auto binaryOperand = [&](hir::HirNodeId operandNode) -> zc::Maybe<MirOperand> {
                  auto nested = primitiveBinaryFor(hirModule, operandNode);
                  ZC_IF_SOME(nestedValue, nested) {
                    if (tempOrdinal == zc::none) return zc::none;
                    uint32_t temp = 0;
                    ZC_IF_SOME(ordinalValue, tempOrdinal) { temp = ordinalValue; }
                    const auto nestedComparison = mirComparisonOperatorFor(nestedValue.operation);
                    const auto nestedArithmetic = mirArithmeticOperatorFor(nestedValue.operation);
                    const bool nestedIsArithmetic =
                        nestedComparison == zc::none && nestedArithmetic != zc::none;
                    // The nested result must equal the outer operand type; when the
                    // inner is a comparison its bool result feeds a bool operand.
                    if (nestedValue.type != value.operandType ||
                        nestedValue.category != hir::HirValueCategory::Value ||
                        (nestedComparison == zc::none && nestedArithmetic == zc::none)) {
                      return zc::none;
                    }
                    auto nestedLeft = binaryLeaf(nestedValue.left, nestedValue.operandType);
                    auto nestedRight = binaryLeaf(nestedValue.right, nestedValue.operandType);
                    if (nestedLeft == zc::none || nestedRight == zc::none) return zc::none;
                    auto nestedRvalue =
                        nestedIsArithmetic
                            ? MirRvalue::arithmetic(ZC_ASSERT_NONNULL(nestedArithmetic),
                                                    zc::mv(ZC_ASSERT_NONNULL(nestedLeft)),
                                                    zc::mv(ZC_ASSERT_NONNULL(nestedRight)),
                                                    nestedValue.type)
                            : MirRvalue::comparison(ZC_ASSERT_NONNULL(nestedComparison),
                                                    zc::mv(ZC_ASSERT_NONNULL(nestedLeft)),
                                                    zc::mv(ZC_ASSERT_NONNULL(nestedRight)),
                                                    nestedValue.type);
                    statements.add(
                        MirStatement::storageLive(localId(temp), nestedValue.sourceSpan.clone()));
                    zc::Vector<MirProjection> tempProjections;
                    statements.add(MirStatement::assign(
                        MirPlace(localId(temp), nestedValue.type, zc::mv(tempProjections),
                                 nestedValue.type),
                        zc::mv(nestedRvalue), MirInitializationKind::Initialize,
                        nestedValue.sourceSpan.clone()));
                    zc::Vector<MirProjection> useProjections;
                    return placeUse(proofs, copy,
                                    MirPlace(localId(temp), value.operandType,
                                             zc::mv(useProjections), value.operandType));
                  }
                  return binaryLeaf(operandNode, value.operandType);
                };
                if ((comparisonOperator == zc::none && arithmeticOperator == zc::none) ||
                    value.type != local.type ||
                    (isArithmeticBinary && value.type != value.operandType) ||
                    value.category != hir::HirValueCategory::Value) {
                  built = false;
                } else {
                  auto leftOperand = binaryOperand(value.left);
                  auto rightOperand = binaryOperand(value.right);
                  if (leftOperand == zc::none || rightOperand == zc::none) {
                    built = false;
                  } else {
                    rvalue = isArithmeticBinary
                                 ? MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmeticOperator),
                                                         zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                         zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                         value.type)
                                 : MirRvalue::comparison(ZC_ASSERT_NONNULL(comparisonOperator),
                                                         zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                         zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                         value.type);
                    assignSpan = value.sourceSpan.clone();
                  }
                }
              }
              if (rvalue == zc::none) built = false;
              if (!built) break;
              statements.add(MirStatement::storageLive(userLocalId(i), local.sourceSpan.clone()));
              zc::Vector<MirProjection> destinationProjections;
              statements.add(MirStatement::assign(
                  MirPlace(userLocalId(i), local.type, zc::mv(destinationProjections), local.type),
                  zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
                  zc::mv(assignSpan)));
            }
            // Return operand: a user local or a parameter.
            zc::Maybe<MirOperand> returnOperand;
            identity::SemanticTypeId returnType = declaration.resultType;
            identity::SourceSpan returnValueSpan = declaration.sourceSpan.clone();
            ZC_IF_SOME(reference, returnLocalReference) {
              if (reference.local.ordinal() == 0 ||
                  reference.local.ordinal() > static_cast<uint32_t>(bindingCount) ||
                  reference.type != declaration.resultType ||
                  reference.category != hir::HirValueCategory::Place) {
                built = false;
              } else {
                zc::Vector<MirProjection> projections;
                returnType = reference.type;
                returnValueSpan = reference.sourceSpan.clone();
                returnOperand = placeUse(proofs, copy,
                                         MirPlace(userLocalId(reference.local.ordinal() - 1),
                                                  returnType, zc::mv(projections), returnType));
              }
            }
            ZC_IF_SOME(reference, returnParameterReference) {
              size_t parameterIndex = 0;
              bool found = false;
              for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                if (declaration.parameters[p].key == reference.parameter) {
                  parameterIndex = p;
                  found = true;
                  break;
                }
              }
              if (!found || reference.type != declaration.resultType ||
                  reference.category != hir::HirValueCategory::Place) {
                built = false;
              } else {
                zc::Vector<MirProjection> projections;
                returnType = reference.type;
                returnValueSpan = reference.sourceSpan.clone();
                returnOperand =
                    placeUse(proofs, copy,
                             MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                      returnType, zc::mv(projections), returnType));
              }
            }
            (void)returnValueSpan;
            if (built && returnOperand != zc::none) {
              ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
                auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
                if (unsafeBlock == zc::none) {
                  built = false;
                } else {
                  ZC_IF_SOME(unsafe, unsafeBlock) {
                    auto unsafeSpan = unsafe.sourceSpan.clone();
                    zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
                    scopes.add(
                        MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Enter, scopeId(2), unsafeSpan.clone()));
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Exit, scopeId(2), zc::mv(unsafeSpan)));
                  }
                }
              }
            }
            if (!built || returnOperand == zc::none) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
            ZC_IF_SOME(returnStatement, sequentialReturn) {
              returnSpan = returnStatement.sourceSpan.clone();
            }
            zc::Vector<MirBasicBlock> blocks;
            blocks.add(
                MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                              MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                         zc::mv(returnSpan))});
            MirFunction function{declaration.definition,
                                 MirFunctionKind::Function,
                                 identity::DefinitionKind::Function,
                                 declaration.resultType,
                                 declaration.sourceSpan.clone(),
                                 zc::mv(scopes),
                                 zc::mv(locals),
                                 zc::mv(blocks)};
            zc::Array<uint8_t> ownerKey;
            ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
            pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
            continue;
          }
        }
      }
      bool returnsRootLocal = true;
      auto finalReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
      ZC_IF_SOME(returnStatement, finalReturn) {
        returnsRootLocal = localFieldProjectionFor(hirModule, returnStatement.value) == zc::none;
      }
      if (block.statements.size() >= 4 && returnsRootLocal) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
        auto definition = identities.definition(declaration.definition);
        if (sourceLocal == zc::none || sourceReturn == zc::none || definition == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        zc::Vector<MirStatement> statements;
        identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
        ZC_IF_SOME(returnStatement, sourceReturn) {
          returnSpan = returnStatement.sourceSpan.clone();
        }
        ZC_IF_SOME(local, sourceLocal) {
          hir::HirNodeId referenceNode;
          ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
          auto reference = localReferenceFor(hirModule, referenceNode);
          if (reference == zc::none || local.local != ZC_ASSERT_NONNULL(reference).local ||
              local.type != declaration.resultType ||
              ZC_ASSERT_NONNULL(reference).type != local.type ||
              ZC_ASSERT_NONNULL(reference).category != hir::HirValueCategory::Place) {
            return rejectMir<BuiltMirCandidate>(
                ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
          }
          zc::Vector<MirSourceScope> scopes;
          zc::Maybe<MirSourceScopeId> noParent;
          scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
          zc::Vector<MirLocalDeclaration> locals;
          locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                         scopeId(1), local.sourceSpan.clone()});
          statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
          bool initialized = false;
          ZC_IF_SOME(initializerNode, local.initializer) {
            auto initializer = expressionFor(hirModule, initializerNode);
            if (initializer == zc::none || ZC_ASSERT_NONNULL(initializer).type != local.type) {
              return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
            }
            zc::Vector<MirProjection> projections;
            statements.add(MirStatement::assign(
                MirPlace(localId(1), local.type, zc::mv(projections), local.type),
                MirRvalue::use(
                    MirOperand::constant(local.type, ZC_ASSERT_NONNULL(initializer).value.clone())),
                MirInitializationKind::Initialize,
                ZC_ASSERT_NONNULL(initializer).sourceSpan.clone()));
            initialized = true;
          }
          for (size_t writeIndex = 1; writeIndex + 1 < block.statements.size(); ++writeIndex) {
            auto write = localWriteFor(hirModule, block.statements[writeIndex]);
            if (write == zc::none || ZC_ASSERT_NONNULL(write).local != local.local ||
                ZC_ASSERT_NONNULL(write).type != local.type ||
                (!initialized &&
                 ZC_ASSERT_NONNULL(write).kind != hir::HirLocalWriteKind::Initialize) ||
                (initialized &&
                 ZC_ASSERT_NONNULL(write).kind != hir::HirLocalWriteKind::Overwrite)) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            auto value = expressionFor(hirModule, ZC_ASSERT_NONNULL(write).value);
            if (value == zc::none || ZC_ASSERT_NONNULL(value).type != local.type) {
              return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
            }
            zc::Vector<MirProjection> projections;
            statements.add(MirStatement::assign(
                MirPlace(localId(1), local.type, zc::mv(projections), local.type),
                MirRvalue::use(
                    MirOperand::constant(local.type, ZC_ASSERT_NONNULL(value).value.clone())),
                initialized ? MirInitializationKind::Overwrite : MirInitializationKind::Initialize,
                ZC_ASSERT_NONNULL(write).sourceSpan.clone()));
            initialized = true;
          }
          zc::Vector<MirProjection> returnProjections;
          auto returnOperand =
              placeUse(proofs, copy,
                       MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
          if (returnOperand == zc::none) {
            return rejectMir<BuiltMirCandidate>(
                ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
          }
          ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
            auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
            if (unsafeBlock == zc::none) {
              return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::MissingRequiredFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
            }
            ZC_IF_SOME(block, unsafeBlock) {
              auto unsafeSpan = block.sourceSpan.clone();
              zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
              scopes.add(MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
              statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter,
                                                               scopeId(2), unsafeSpan.clone()));
              statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Exit,
                                                               scopeId(2), zc::mv(unsafeSpan)));
            }
          }
          zc::Vector<MirBasicBlock> blocks;
          blocks.add(
              MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                            MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                       zc::mv(returnSpan))});
          MirFunction function{declaration.definition,
                               MirFunctionKind::Function,
                               identity::DefinitionKind::Function,
                               declaration.resultType,
                               declaration.sourceSpan.clone(),
                               zc::mv(scopes),
                               zc::mv(locals),
                               zc::mv(blocks)};
          zc::Array<uint8_t> ownerKey;
          ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
          pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
          continue;
        }
      }
      if (block.statements.size() >= 3) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto fieldProjection = localFieldProjectionFor(hirModule, referenceNode);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(projection, fieldProjection) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              if (local.initializer == zc::none && local.local == projection.local &&
                  projection.receiverType == local.type &&
                  projection.type == declaration.resultType &&
                  projection.category == hir::HirValueCategory::Place && definition != zc::none) {
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<identity::DefId> initializedFields;
                bool validWrites = true;
                for (size_t writeIndex = 1; writeIndex + 1 < block.statements.size();
                     ++writeIndex) {
                  auto write = localWriteFor(hirModule, block.statements[writeIndex]);
                  if (write == zc::none || ZC_ASSERT_NONNULL(write).local != local.local ||
                      ZC_ASSERT_NONNULL(write).field == zc::none) {
                    validWrites = false;
                    break;
                  }
                  const auto field = ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(write).field);
                  auto value = expressionFor(hirModule, ZC_ASSERT_NONNULL(write).value);
                  if (value == zc::none ||
                      ZC_ASSERT_NONNULL(value).type != ZC_ASSERT_NONNULL(write).type) {
                    validWrites = false;
                    break;
                  }
                  bool initialized = false;
                  for (const auto initializedField : initializedFields) {
                    if (initializedField == field) {
                      initialized = true;
                      break;
                    }
                  }
                  const auto expectedKind = initialized ? hir::HirLocalWriteKind::Overwrite
                                                        : hir::HirLocalWriteKind::Initialize;
                  if (ZC_ASSERT_NONNULL(write).kind != expectedKind) {
                    validWrites = false;
                    break;
                  }
                  zc::Vector<MirProjection> projections;
                  projections.add(
                      MirProjection::field(field, local.type, ZC_ASSERT_NONNULL(write).type));
                  statements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(projections),
                               ZC_ASSERT_NONNULL(write).type),
                      MirRvalue::use(MirOperand::constant(ZC_ASSERT_NONNULL(write).type,
                                                          ZC_ASSERT_NONNULL(value).value.clone())),
                      initialized ? MirInitializationKind::Overwrite
                                  : MirInitializationKind::Initialize,
                      ZC_ASSERT_NONNULL(write).sourceSpan.clone()));
                  if (!initialized) initializedFields.add(field);
                }
                if (validWrites) {
                  zc::Vector<MirProjection> returnProjections;
                  returnProjections.add(
                      MirProjection::field(projection.field, local.type, projection.type));
                  auto returnOperand = placeUse(
                      proofs, copy,
                      MirPlace(localId(1), local.type, zc::mv(returnProjections), projection.type));
                  if (returnOperand != zc::none) {
                    zc::Vector<MirSourceScope> scopes;
                    zc::Maybe<MirSourceScopeId> noParent;
                    scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                              declaration.sourceSpan.clone()});
                    zc::Vector<MirLocalDeclaration> locals;
                    locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                   scopeId(1), local.sourceSpan.clone()});
                    zc::Vector<MirBasicBlock> blocks;
                    blocks.add(MirBasicBlock{
                        blockId(1), scopeId(1), zc::mv(statements),
                        MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                   returnStatement.sourceSpan.clone())});
                    MirFunction function{declaration.definition,
                                         MirFunctionKind::Function,
                                         identity::DefinitionKind::Function,
                                         declaration.resultType,
                                         declaration.sourceSpan.clone(),
                                         zc::mv(scopes),
                                         zc::mv(locals),
                                         zc::mv(blocks)};
                    zc::Array<uint8_t> ownerKey;
                    ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                    pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                    continue;
                  }
                }
              }
            }
          }
        }
      }
      if (block.statements.size() >= 4) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
        hir::HirNodeId initializerNode;
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
        }
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto initializerAggregate = aggregateFor(hirModule, initializerNode);
        auto fieldProjection = localFieldProjectionFor(hirModule, referenceNode);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(aggregate, initializerAggregate) {
            ZC_IF_SOME(projection, fieldProjection) {
              ZC_IF_SOME(returnStatement, sourceReturn) {
                if (local.initializer == aggregate.node && local.local == projection.local &&
                    local.type == aggregate.type && projection.receiverType == local.type &&
                    projection.type == declaration.resultType &&
                    aggregate.category == hir::HirValueCategory::Value &&
                    projection.category == hir::HirValueCategory::Place && definition != zc::none) {
                  zc::Vector<MirStatement> statements;
                  statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                  zc::Vector<MirNominalAggregateElement> elements;
                  for (const auto& element : aggregate.elements) {
                    elements.add(MirNominalAggregateElement{
                        element.field, MirOperand::constant(element.type, element.value.clone())});
                  }
                  zc::Vector<MirProjection> initializeProjections;
                  statements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(initializeProjections), local.type),
                      MirRvalue::nominalAggregate(aggregate.definition, aggregate.type,
                                                  zc::mv(elements)),
                      MirInitializationKind::Initialize, aggregate.sourceSpan.clone()));
                  bool validWrites = true;
                  for (size_t writeIndex = 1; writeIndex + 1 < block.statements.size();
                       ++writeIndex) {
                    auto write = localWriteFor(hirModule, block.statements[writeIndex]);
                    if (write == zc::none || ZC_ASSERT_NONNULL(write).local != local.local ||
                        ZC_ASSERT_NONNULL(write).field == zc::none ||
                        ZC_ASSERT_NONNULL(write).kind != hir::HirLocalWriteKind::Overwrite) {
                      validWrites = false;
                      break;
                    }
                    auto replacement = expressionFor(hirModule, ZC_ASSERT_NONNULL(write).value);
                    if (replacement == zc::none ||
                        ZC_ASSERT_NONNULL(replacement).type != ZC_ASSERT_NONNULL(write).type) {
                      validWrites = false;
                      break;
                    }
                    zc::Vector<MirProjection> overwriteProjections;
                    overwriteProjections.add(
                        MirProjection::field(ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(write).field),
                                             local.type, ZC_ASSERT_NONNULL(write).type));
                    statements.add(MirStatement::assign(
                        MirPlace(localId(1), local.type, zc::mv(overwriteProjections),
                                 ZC_ASSERT_NONNULL(write).type),
                        MirRvalue::use(
                            MirOperand::constant(ZC_ASSERT_NONNULL(write).type,
                                                 ZC_ASSERT_NONNULL(replacement).value.clone())),
                        MirInitializationKind::Overwrite,
                        ZC_ASSERT_NONNULL(write).sourceSpan.clone()));
                  }
                  if (validWrites) {
                    zc::Vector<MirProjection> returnProjections;
                    returnProjections.add(
                        MirProjection::field(projection.field, local.type, projection.type));
                    auto returnOperand =
                        placeUse(proofs, copy,
                                 MirPlace(localId(1), local.type, zc::mv(returnProjections),
                                          projection.type));
                    if (returnOperand != zc::none) {
                      zc::Vector<MirSourceScope> scopes;
                      zc::Maybe<MirSourceScopeId> noParent;
                      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                                declaration.sourceSpan.clone()});
                      zc::Vector<MirLocalDeclaration> locals;
                      locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal,
                                                     local.type, scopeId(1),
                                                     local.sourceSpan.clone()});
                      zc::Vector<MirBasicBlock> blocks;
                      blocks.add(MirBasicBlock{
                          blockId(1), scopeId(1), zc::mv(statements),
                          MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                     returnStatement.sourceSpan.clone())});
                      MirFunction function{declaration.definition,
                                           MirFunctionKind::Function,
                                           identity::DefinitionKind::Function,
                                           declaration.resultType,
                                           declaration.sourceSpan.clone(),
                                           zc::mv(scopes),
                                           zc::mv(locals),
                                           zc::mv(blocks)};
                      zc::Array<uint8_t> ownerKey;
                      ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                      continue;
                    }
                  }
                }
              }
            }
          }
        }
        return rejectMir<BuiltMirCandidate>(
            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
            declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
      }
      if (block.statements.size() == 3) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceOverwrite = localWriteFor(hirModule, block.statements[1]);
        auto sourceReturn = returnFor(hirModule, block.statements[2]);
        hir::HirNodeId initializerNode;
        hir::HirNodeId overwriteValueNode;
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
        }
        ZC_IF_SOME(overwrite, sourceOverwrite) { overwriteValueNode = overwrite.value; }
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto initializer = expressionFor(hirModule, initializerNode);
        auto initializerAggregate = aggregateFor(hirModule, initializerNode);
        auto overwriteValue = expressionFor(hirModule, overwriteValueNode);
        auto overwriteParameter = parameterReferenceFor(hirModule, overwriteValueNode);
        auto overwriteBinary = primitiveBinaryFor(hirModule, overwriteValueNode);
        auto reference = localReferenceFor(hirModule, referenceNode);
        auto fieldProjection = localFieldProjectionFor(hirModule, referenceNode);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(write, sourceOverwrite) {
            ZC_IF_SOME(replacement, overwriteValue) {
              ZC_IF_SOME(projection, fieldProjection) {
                ZC_IF_SOME(returnStatement, sourceReturn) {
                  if (local.initializer == zc::none &&
                      write.kind == hir::HirLocalWriteKind::Initialize &&
                      local.local == write.local && local.local == projection.local &&
                      write.field != zc::none && write.field == projection.field &&
                      replacement.type == write.type && projection.receiverType == local.type &&
                      projection.type == declaration.resultType &&
                      projection.category == hir::HirValueCategory::Place &&
                      definition != zc::none) {
                    zc::Vector<MirSourceScope> scopes;
                    zc::Maybe<MirSourceScopeId> noParent;
                    scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                              declaration.sourceSpan.clone()});
                    zc::Vector<MirLocalDeclaration> locals;
                    locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                   scopeId(1), local.sourceSpan.clone()});
                    zc::Vector<MirStatement> statements;
                    statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                    zc::Vector<MirProjection> initializeProjections;
                    initializeProjections.add(MirProjection::field(ZC_ASSERT_NONNULL(write.field),
                                                                   local.type, write.type));
                    statements.add(MirStatement::assign(
                        MirPlace(localId(1), local.type, zc::mv(initializeProjections), write.type),
                        MirRvalue::use(MirOperand::constant(write.type, replacement.value.clone())),
                        MirInitializationKind::Initialize, write.sourceSpan.clone()));
                    zc::Vector<MirProjection> returnProjections;
                    returnProjections.add(
                        MirProjection::field(projection.field, local.type, projection.type));
                    auto returnOperand =
                        placeUse(proofs, copy,
                                 MirPlace(localId(1), local.type, zc::mv(returnProjections),
                                          projection.type));
                    if (returnOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    zc::Vector<MirBasicBlock> blocks;
                    blocks.add(MirBasicBlock{
                        blockId(1), scopeId(1), zc::mv(statements),
                        MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                   returnStatement.sourceSpan.clone())});
                    MirFunction function{declaration.definition,
                                         MirFunctionKind::Function,
                                         identity::DefinitionKind::Function,
                                         declaration.resultType,
                                         declaration.sourceSpan.clone(),
                                         zc::mv(scopes),
                                         zc::mv(locals),
                                         zc::mv(blocks)};
                    zc::Array<uint8_t> ownerKey;
                    ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                    pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                    continue;
                  }
                }
              }
            }
            ZC_IF_SOME(aggregate, initializerAggregate) {
              ZC_IF_SOME(replacement, overwriteValue) {
                ZC_IF_SOME(projection, fieldProjection) {
                  ZC_IF_SOME(returnStatement, sourceReturn) {
                    if (local.initializer == aggregate.node && local.local == write.local &&
                        local.local == projection.local && local.type == aggregate.type &&
                        write.field != zc::none && replacement.type == write.type &&
                        write.kind == hir::HirLocalWriteKind::Overwrite &&
                        projection.receiverType == local.type &&
                        projection.type == declaration.resultType &&
                        aggregate.category == hir::HirValueCategory::Value &&
                        projection.category == hir::HirValueCategory::Place &&
                        definition != zc::none) {
                      zc::Vector<MirSourceScope> scopes;
                      zc::Maybe<MirSourceScopeId> noParent;
                      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                                declaration.sourceSpan.clone()});
                      zc::Vector<MirLocalDeclaration> locals;
                      locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal,
                                                     local.type, scopeId(1),
                                                     local.sourceSpan.clone()});
                      zc::Vector<MirStatement> statements;
                      statements.add(
                          MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                      zc::Vector<MirNominalAggregateElement> elements;
                      for (const auto& element : aggregate.elements) {
                        elements.add(MirNominalAggregateElement{
                            element.field,
                            MirOperand::constant(element.type, element.value.clone())});
                      }
                      zc::Vector<MirProjection> initializeProjections;
                      statements.add(MirStatement::assign(
                          MirPlace(localId(1), local.type, zc::mv(initializeProjections),
                                   local.type),
                          MirRvalue::nominalAggregate(aggregate.definition, aggregate.type,
                                                      zc::mv(elements)),
                          MirInitializationKind::Initialize, aggregate.sourceSpan.clone()));
                      zc::Vector<MirProjection> overwriteProjections;
                      overwriteProjections.add(MirProjection::field(ZC_ASSERT_NONNULL(write.field),
                                                                    local.type, write.type));
                      statements.add(MirStatement::assign(
                          MirPlace(localId(1), local.type, zc::mv(overwriteProjections),
                                   write.type),
                          MirRvalue::use(
                              MirOperand::constant(write.type, replacement.value.clone())),
                          MirInitializationKind::Overwrite, write.sourceSpan.clone()));
                      zc::Vector<MirProjection> returnProjections;
                      returnProjections.add(
                          MirProjection::field(projection.field, local.type, projection.type));
                      auto returnOperand =
                          placeUse(proofs, copy,
                                   MirPlace(localId(1), local.type, zc::mv(returnProjections),
                                            projection.type));
                      if (returnOperand == zc::none) {
                        return rejectMir<BuiltMirCandidate>(
                            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                            module, declaration.definition, identities,
                            static_cast<uint32_t>(pending.size() + 1));
                      }
                      zc::Vector<MirBasicBlock> blocks;
                      blocks.add(MirBasicBlock{
                          blockId(1), scopeId(1), zc::mv(statements),
                          MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                     returnStatement.sourceSpan.clone())});
                      MirFunction function{declaration.definition,
                                           MirFunctionKind::Function,
                                           identity::DefinitionKind::Function,
                                           declaration.resultType,
                                           declaration.sourceSpan.clone(),
                                           zc::mv(scopes),
                                           zc::mv(locals),
                                           zc::mv(blocks)};
                      zc::Array<uint8_t> ownerKey;
                      ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                      continue;
                    }
                  }
                }
              }
            }
          }
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(write, sourceOverwrite) {
            ZC_IF_SOME(initialValue, overwriteValue) {
              ZC_IF_SOME(localReference, reference) {
                if (local.initializer == zc::none &&
                    write.kind == hir::HirLocalWriteKind::Initialize &&
                    local.local == write.local && local.local == localReference.local &&
                    local.type == declaration.resultType && write.type == local.type &&
                    initialValue.type == local.type && localReference.type == local.type &&
                    localReference.category == hir::HirValueCategory::Place &&
                    definition != zc::none) {
                  identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
                  ZC_IF_SOME(statement, sourceReturn) { returnSpan = statement.sourceSpan.clone(); }
                  zc::Vector<MirSourceScope> scopes;
                  zc::Maybe<MirSourceScopeId> noParent;
                  scopes.add(
                      MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                  zc::Vector<MirLocalDeclaration> locals;
                  locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                 scopeId(1), local.sourceSpan.clone()});
                  zc::Vector<MirStatement> statements;
                  statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                  zc::Vector<MirProjection> projections;
                  statements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(projections), local.type),
                      MirRvalue::use(MirOperand::constant(local.type, initialValue.value.clone())),
                      MirInitializationKind::Initialize, write.sourceSpan.clone()));
                  zc::Vector<MirProjection> returnProjections;
                  auto returnOperand = placeUse(
                      proofs, copy,
                      MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                  if (returnOperand == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirBasicBlock> blocks;
                  blocks.add(MirBasicBlock{
                      blockId(1), scopeId(1), zc::mv(statements),
                      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                 zc::mv(returnSpan))});
                  MirFunction function{declaration.definition,
                                       MirFunctionKind::Function,
                                       identity::DefinitionKind::Function,
                                       declaration.resultType,
                                       declaration.sourceSpan.clone(),
                                       zc::mv(scopes),
                                       zc::mv(locals),
                                       zc::mv(blocks)};
                  zc::Array<uint8_t> ownerKey;
                  ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                  pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                  continue;
                }
              }
            }
          }
        }
        // A `mut x = <lit>; x = <param>; return x;` body: an initialized scalar
        // local whose overwrite value is a parameter reference. The parameter is
        // declared as localId(1) and the user local as localId(2); the overwrite
        // lowers to a copy/move place-use of the parameter local, exactly like the
        // return-of-parameter path.
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(overwrite, sourceOverwrite) {
            ZC_IF_SOME(initialValue, initializer) {
              ZC_IF_SOME(parameterValue, overwriteParameter) {
                ZC_IF_SOME(localReference, reference) {
                  if (local.initializer != zc::none && overwrite.field == zc::none &&
                      overwrite.kind == hir::HirLocalWriteKind::Overwrite &&
                      local.local == overwrite.local && local.local == localReference.local &&
                      local.type == declaration.resultType && overwrite.type == local.type &&
                      initialValue.type == local.type && parameterValue.type == local.type &&
                      parameterValue.category == hir::HirValueCategory::Place &&
                      localReference.type == local.type &&
                      localReference.category == hir::HirValueCategory::Place &&
                      declaration.parameters.size() == 1 &&
                      declaration.parameters[0].key == parameterValue.parameter &&
                      declaration.parameters[0].type == local.type && definition != zc::none) {
                    identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
                    ZC_IF_SOME(statement, sourceReturn) {
                      returnSpan = statement.sourceSpan.clone();
                    }
                    zc::Vector<MirSourceScope> scopes;
                    zc::Maybe<MirSourceScopeId> noParent;
                    scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                              declaration.sourceSpan.clone()});
                    zc::Vector<MirLocalDeclaration> locals;
                    locals.add(MirLocalDeclaration{localId(1), MirLocalKind::Parameter, local.type,
                                                   scopeId(1),
                                                   declaration.parameters[0].sourceSpan.clone()});
                    locals.add(MirLocalDeclaration{localId(2), MirLocalKind::UserLocal, local.type,
                                                   scopeId(1), local.sourceSpan.clone()});
                    zc::Vector<MirStatement> statements;
                    statements.add(MirStatement::storageLive(localId(2), local.sourceSpan.clone()));
                    zc::Vector<MirProjection> initializeProjections;
                    statements.add(MirStatement::assign(
                        MirPlace(localId(2), local.type, zc::mv(initializeProjections), local.type),
                        MirRvalue::use(
                            MirOperand::constant(local.type, initialValue.value.clone())),
                        MirInitializationKind::Initialize, initialValue.sourceSpan.clone()));
                    zc::Vector<MirProjection> parameterProjections;
                    auto overwriteOperand = placeUse(
                        proofs, copy,
                        MirPlace(localId(1), local.type, zc::mv(parameterProjections), local.type));
                    if (overwriteOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    zc::Vector<MirProjection> overwriteProjections;
                    statements.add(MirStatement::assign(
                        MirPlace(localId(2), local.type, zc::mv(overwriteProjections), local.type),
                        MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(overwriteOperand))),
                        MirInitializationKind::Overwrite, overwrite.sourceSpan.clone()));
                    zc::Vector<MirProjection> returnProjections;
                    auto returnOperand = placeUse(
                        proofs, copy,
                        MirPlace(localId(2), local.type, zc::mv(returnProjections), local.type));
                    if (returnOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    zc::Vector<MirBasicBlock> blocks;
                    blocks.add(MirBasicBlock{
                        blockId(1), scopeId(1), zc::mv(statements),
                        MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                   zc::mv(returnSpan))});
                    MirFunction function{declaration.definition,
                                         MirFunctionKind::Function,
                                         identity::DefinitionKind::Function,
                                         declaration.resultType,
                                         declaration.sourceSpan.clone(),
                                         zc::mv(scopes),
                                         zc::mv(locals),
                                         zc::mv(blocks)};
                    zc::Array<uint8_t> ownerKey;
                    ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                    pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                    continue;
                  }
                }
              }
            }
          }
        }
        // A `mut x = <lit>; x = a <op> b; return x;` body: an initialized scalar
        // local whose overwrite value is a primitive binary. Parameters are
        // localId(1..N) and the user local is localId(N+1); the overwrite lowers
        // to an Arithmetic/Comparison rvalue whose operands are constants or
        // copy place-uses of the parameter locals, exactly like the
        // primitive-binary initializer path.
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(overwrite, sourceOverwrite) {
            ZC_IF_SOME(initialValue, initializer) {
              ZC_IF_SOME(binaryValue, overwriteBinary) {
                ZC_IF_SOME(localReference, reference) {
                  const uint32_t parameterCount =
                      static_cast<uint32_t>(declaration.parameters.size());
                  const auto comparisonOperator = mirComparisonOperatorFor(binaryValue.operation);
                  const auto arithmeticOperator = mirArithmeticOperatorFor(binaryValue.operation);
                  const bool isArithmeticBinary =
                      comparisonOperator == zc::none && arithmeticOperator != zc::none;
                  if (local.initializer != zc::none && overwrite.field == zc::none &&
                      overwrite.kind == hir::HirLocalWriteKind::Overwrite &&
                      local.local == overwrite.local && local.local == localReference.local &&
                      local.type == declaration.resultType && overwrite.type == local.type &&
                      initialValue.type == local.type && binaryValue.type == local.type &&
                      binaryValue.category == hir::HirValueCategory::Value &&
                      (comparisonOperator != zc::none ||
                       (arithmeticOperator != zc::none && binaryValue.operandType == local.type)) &&
                      localReference.type == local.type &&
                      localReference.category == hir::HirValueCategory::Place &&
                      definition != zc::none) {
                    const auto userLocalId = localId(parameterCount + 1);
                    // Builds one binary operand: a scalar-literal constant or a
                    // copy place-use of a parameter local, of the operand type.
                    auto buildOperand = [&](hir::HirNodeId operandNode) -> zc::Maybe<MirOperand> {
                      auto operandLiteral = expressionFor(hirModule, operandNode);
                      ZC_IF_SOME(literalValue, operandLiteral) {
                        if (literalValue.type != binaryValue.operandType) return zc::none;
                        return MirOperand::constant(binaryValue.operandType,
                                                    literalValue.value.clone());
                      }
                      auto operandParameter = parameterReferenceFor(hirModule, operandNode);
                      ZC_IF_SOME(parameter, operandParameter) {
                        if (parameter.type != binaryValue.operandType) return zc::none;
                        size_t parameterIndex = 0;
                        bool found = false;
                        for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                          if (declaration.parameters[p].key == parameter.parameter) {
                            parameterIndex = p;
                            found = true;
                            break;
                          }
                        }
                        if (!found) return zc::none;
                        zc::Vector<MirProjection> projections;
                        return placeUse(proofs, copy,
                                        MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                                 binaryValue.operandType, zc::mv(projections),
                                                 binaryValue.operandType));
                      }
                      return zc::none;
                    };
                    auto leftOperand = buildOperand(binaryValue.left);
                    auto rightOperand = buildOperand(binaryValue.right);
                    if (leftOperand == zc::none || rightOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
                    ZC_IF_SOME(statement, sourceReturn) {
                      returnSpan = statement.sourceSpan.clone();
                    }
                    zc::Vector<MirSourceScope> scopes;
                    zc::Maybe<MirSourceScopeId> noParent;
                    scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                              declaration.sourceSpan.clone()});
                    zc::Vector<MirLocalDeclaration> locals;
                    for (uint32_t p = 0; p < parameterCount; ++p) {
                      locals.add(MirLocalDeclaration{localId(p + 1), MirLocalKind::Parameter,
                                                     declaration.parameters[p].type, scopeId(1),
                                                     declaration.parameters[p].sourceSpan.clone()});
                    }
                    locals.add(MirLocalDeclaration{userLocalId, MirLocalKind::UserLocal, local.type,
                                                   scopeId(1), local.sourceSpan.clone()});
                    zc::Vector<MirStatement> statements;
                    statements.add(
                        MirStatement::storageLive(userLocalId, local.sourceSpan.clone()));
                    zc::Vector<MirProjection> initializeProjections;
                    statements.add(MirStatement::assign(
                        MirPlace(userLocalId, local.type, zc::mv(initializeProjections),
                                 local.type),
                        MirRvalue::use(
                            MirOperand::constant(local.type, initialValue.value.clone())),
                        MirInitializationKind::Initialize, initialValue.sourceSpan.clone()));
                    auto overwriteRvalue =
                        isArithmeticBinary
                            ? MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmeticOperator),
                                                    zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                    zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                    binaryValue.type)
                            : MirRvalue::comparison(ZC_ASSERT_NONNULL(comparisonOperator),
                                                    zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                    zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                    binaryValue.type);
                    zc::Vector<MirProjection> overwriteProjections;
                    statements.add(MirStatement::assign(
                        MirPlace(userLocalId, local.type, zc::mv(overwriteProjections), local.type),
                        zc::mv(overwriteRvalue), MirInitializationKind::Overwrite,
                        overwrite.sourceSpan.clone()));
                    zc::Vector<MirProjection> returnProjections;
                    auto returnOperand = placeUse(
                        proofs, copy,
                        MirPlace(userLocalId, local.type, zc::mv(returnProjections), local.type));
                    if (returnOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    zc::Vector<MirBasicBlock> blocks;
                    blocks.add(MirBasicBlock{
                        blockId(1), scopeId(1), zc::mv(statements),
                        MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                   zc::mv(returnSpan))});
                    MirFunction function{declaration.definition,
                                         MirFunctionKind::Function,
                                         identity::DefinitionKind::Function,
                                         declaration.resultType,
                                         declaration.sourceSpan.clone(),
                                         zc::mv(scopes),
                                         zc::mv(locals),
                                         zc::mv(blocks)};
                    zc::Array<uint8_t> ownerKey;
                    ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                    pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                    continue;
                  }
                }
              }
            }
          }
        }
        if (sourceLocal == zc::none || sourceOverwrite == zc::none || sourceReturn == zc::none ||
            initializer == zc::none || overwriteValue == zc::none || reference == zc::none ||
            definition == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        identity::SourceSpan returnSpan = declaration.sourceSpan.clone();
        ZC_IF_SOME(statement, sourceReturn) { returnSpan = statement.sourceSpan.clone(); }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(overwrite, sourceOverwrite) {
            ZC_IF_SOME(initialValue, initializer) {
              ZC_IF_SOME(replacement, overwriteValue) {
                ZC_IF_SOME(localReference, reference) {
                  if (local.initializer == zc::none || local.local != overwrite.local ||
                      local.local != localReference.local || local.type != declaration.resultType ||
                      overwrite.type != local.type || initialValue.type != local.type ||
                      replacement.type != local.type || localReference.type != local.type ||
                      localReference.category != hir::HirValueCategory::Place) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirSourceScope> scopes;
                  zc::Maybe<MirSourceScopeId> noParent;
                  scopes.add(
                      MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                  zc::Vector<MirLocalDeclaration> locals;
                  locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                 scopeId(1), local.sourceSpan.clone()});
                  zc::Vector<MirStatement> statements;
                  statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                  zc::Vector<MirProjection> initializeProjections;
                  statements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(initializeProjections), local.type),
                      MirRvalue::use(MirOperand::constant(local.type, initialValue.value.clone())),
                      MirInitializationKind::Initialize, initialValue.sourceSpan.clone()));
                  zc::Vector<MirProjection> overwriteProjections;
                  statements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(overwriteProjections), local.type),
                      MirRvalue::use(MirOperand::constant(local.type, replacement.value.clone())),
                      MirInitializationKind::Overwrite, overwrite.sourceSpan.clone()));
                  zc::Vector<MirProjection> returnProjections;
                  auto returnOperand = placeUse(
                      proofs, copy,
                      MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                  if (returnOperand == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::InvalidFact, module,
                                                        declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  zc::Vector<MirBasicBlock> blocks;
                  blocks.add(MirBasicBlock{
                      blockId(1), scopeId(1), zc::mv(statements),
                      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                 zc::mv(returnSpan))});
                  MirFunction function{declaration.definition,
                                       MirFunctionKind::Function,
                                       identity::DefinitionKind::Function,
                                       declaration.resultType,
                                       declaration.sourceSpan.clone(),
                                       zc::mv(scopes),
                                       zc::mv(locals),
                                       zc::mv(blocks)};
                  zc::Array<uint8_t> ownerKey;
                  ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                  pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                  continue;
                }
              }
            }
          }
        }
      }
      if (block.statements.size() == 1) {
        auto sourceReturn = returnFor(hirModule, block.statements[0]);
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto reference = parameterReferenceFor(hirModule, referenceNode);
        auto reborrow = parameterReborrowFor(hirModule, referenceNode);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(returnStatement, sourceReturn) {
          ZC_IF_SOME(parameterReborrow, reborrow) {
            if (declaration.parameters.size() != 1 ||
                declaration.parameters[0].key != parameterReborrow.parameter ||
                declaration.parameters[0].type != parameterReborrow.sourceType ||
                parameterReborrow.type != declaration.resultType || definition == zc::none) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            zc::Vector<MirSourceScope> scopes;
            zc::Maybe<MirSourceScopeId> noParent;
            scopes.add(
                MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
            zc::Vector<MirLocalDeclaration> locals;
            locals.add(MirLocalDeclaration{localId(1), MirLocalKind::Parameter,
                                           parameterReborrow.sourceType, scopeId(1),
                                           declaration.parameters[0].sourceSpan.clone()});
            locals.add(MirLocalDeclaration{localId(2), MirLocalKind::Temporary,
                                           parameterReborrow.type, scopeId(1),
                                           parameterReborrow.sourceSpan.clone()});
            zc::Vector<MirStatement> statements;
            statements.add(
                MirStatement::storageLive(localId(2), parameterReborrow.sourceSpan.clone()));
            zc::Vector<MirProjection> destinationProjections;
            zc::Vector<MirProjection> sourceProjections;
            sourceProjections.add(
                MirProjection::dereference(parameterReborrow.sourceType, parameterReborrow.type));
            statements.add(MirStatement::borrowCreation(
                MirPlace(localId(2), parameterReborrow.type, zc::mv(destinationProjections),
                         parameterReborrow.type),
                parameterReborrow.mutability == type::semantic::Mutability::Const
                    ? MirBorrowKind::Shared
                    : MirBorrowKind::Mutable,
                MirPlace(localId(1), parameterReborrow.sourceType, zc::mv(sourceProjections),
                         parameterReborrow.type),
                parameterReborrow.sourceSpan.clone()));
            zc::Vector<MirProjection> returnProjections;
            auto returnOperand =
                placeUse(proofs, copy,
                         MirPlace(localId(2), parameterReborrow.type, zc::mv(returnProjections),
                                  parameterReborrow.type));
            if (returnOperand == zc::none) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
              auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
              if (unsafeBlock == zc::none) {
                return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                    ir::IrFailureKind::MissingRequiredFact, module,
                                                    declaration.definition, identities,
                                                    static_cast<uint32_t>(pending.size() + 1));
              }
              ZC_IF_SOME(block, unsafeBlock) {
                auto unsafeSpan = block.sourceSpan.clone();
                zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
                scopes.add(MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
                statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter,
                                                                 scopeId(2), unsafeSpan.clone()));
                statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Exit,
                                                                 scopeId(2), zc::mv(unsafeSpan)));
              }
            }
            zc::Vector<MirBasicBlock> blocks;
            blocks.add(
                MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                              MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                         returnStatement.sourceSpan.clone())});
            MirFunction function{declaration.definition,
                                 MirFunctionKind::Function,
                                 identity::DefinitionKind::Function,
                                 declaration.resultType,
                                 declaration.sourceSpan.clone(),
                                 zc::mv(scopes),
                                 zc::mv(locals),
                                 zc::mv(blocks)};
            zc::Array<uint8_t> ownerKey;
            ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
            pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
            continue;
          }
          ZC_IF_SOME(parameterReference, reference) {
            if (declaration.parameters.size() != 1 ||
                declaration.parameters[0].key != parameterReference.parameter ||
                declaration.parameters[0].type != parameterReference.type ||
                parameterReference.type != declaration.resultType ||
                parameterReference.category != hir::HirValueCategory::Place ||
                definition == zc::none) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            zc::Vector<MirSourceScope> scopes;
            zc::Maybe<MirSourceScopeId> noParent;
            scopes.add(
                MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
            zc::Vector<MirLocalDeclaration> locals;
            locals.add(MirLocalDeclaration{localId(1), MirLocalKind::Parameter,
                                           parameterReference.type, scopeId(1),
                                           declaration.parameters[0].sourceSpan.clone()});
            zc::Vector<MirStatement> statements;
            zc::Vector<MirProjection> returnProjections;
            auto returnOperand =
                placeUse(proofs, copy,
                         MirPlace(localId(1), parameterReference.type, zc::mv(returnProjections),
                                  parameterReference.type));
            if (returnOperand == zc::none) {
              return rejectMir<BuiltMirCandidate>(
                  ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                  declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
            }
            zc::Vector<MirBasicBlock> blocks;
            blocks.add(
                MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                              MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                         returnStatement.sourceSpan.clone())});
            MirFunction function{declaration.definition,
                                 MirFunctionKind::Function,
                                 identity::DefinitionKind::Function,
                                 declaration.resultType,
                                 declaration.sourceSpan.clone(),
                                 zc::mv(scopes),
                                 zc::mv(locals),
                                 zc::mv(blocks)};
            zc::Array<uint8_t> ownerKey;
            ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
            pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
            continue;
          }
          auto conditional = conditionalFor(hirModule, referenceNode);
          ZC_IF_SOME(conditionalValue, conditional) {
            auto conditionRef = parameterReferenceFor(hirModule, conditionalValue.condition);
            // Each arm value resolves to a scalar-literal expression or a bare
            // parameter reference. Exactly one lookup succeeds per arm.
            auto thenExpr = expressionFor(hirModule, conditionalValue.thenReturnValue);
            auto elseExpr = expressionFor(hirModule, conditionalValue.elseReturnValue);
            auto thenParam = parameterReferenceFor(hirModule, conditionalValue.thenReturnValue);
            auto elseParam = parameterReferenceFor(hirModule, conditionalValue.elseReturnValue);
            const bool thenOk = (thenExpr != zc::none) != (thenParam != zc::none);
            const bool elseOk = (elseExpr != zc::none) != (elseParam != zc::none);
            ZC_IF_SOME(condition, conditionRef) {
              if (thenOk && elseOk) {
                // Resolve the local index of a parameter referenced by an arm.
                auto parameterLocalIndex =
                    [&](const hir::HirParameterReferenceExpression& reference,
                        size_t& outIndex) -> bool {
                  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                    if (declaration.parameters[i].key == reference.parameter) {
                      outIndex = i;
                      return true;
                    }
                  }
                  return false;
                };
                size_t conditionIndex = 0;
                bool found = parameterLocalIndex(condition, conditionIndex);
                identity::SemanticTypeId thenType;
                identity::SemanticTypeId elseType;
                ZC_IF_SOME(value, thenExpr) { thenType = value.type; }
                ZC_IF_SOME(value, thenParam) { thenType = value.type; }
                ZC_IF_SOME(value, elseExpr) { elseType = value.type; }
                ZC_IF_SOME(value, elseParam) { elseType = value.type; }
                bool armParametersResolved = true;
                size_t thenParamIndex = 0;
                size_t elseParamIndex = 0;
                ZC_IF_SOME(value, thenParam) {
                  armParametersResolved &= parameterLocalIndex(value, thenParamIndex);
                }
                ZC_IF_SOME(value, elseParam) {
                  armParametersResolved &= parameterLocalIndex(value, elseParamIndex);
                }
                if (!found || !armParametersResolved ||
                    conditionalValue.type != declaration.resultType ||
                    thenType != declaration.resultType || elseType != declaration.resultType ||
                    definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                const auto conditionLocal = localId(static_cast<uint32_t>(conditionIndex + 1));
                const auto resultLocal =
                    localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                  locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                                 MirLocalKind::Parameter,
                                                 declaration.parameters[i].type, scopeId(1),
                                                 declaration.parameters[i].sourceSpan.clone()});
                }
                locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::FunctionResult,
                                               declaration.resultType, scopeId(1),
                                               returnStatement.sourceSpan.clone()});
                zc::Vector<MirProjection> conditionProjections;
                auto discriminant =
                    placeUse(proofs, copy,
                             MirPlace(conditionLocal, condition.type, zc::mv(conditionProjections),
                                      condition.type));
                if (discriminant == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand =
                    placeUse(proofs, copy,
                             MirPlace(resultLocal, declaration.resultType,
                                      zc::mv(returnProjections), declaration.resultType));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                // Materialize an arm operand: constant for a literal arm, or a
                // place-use of the parameter local for a parameter arm.
                auto armOperand =
                    [&](zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                        zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                        size_t parameterIndex) -> zc::Maybe<MirOperand> {
                  ZC_IF_SOME(value, literal) {
                    return MirOperand::constant(value.type, value.value.clone());
                  }
                  ZC_IF_SOME(value, parameter) {
                    zc::Vector<MirProjection> projections;
                    return placeUse(proofs, copy,
                                    MirPlace(localId(static_cast<uint32_t>(parameterIndex + 1)),
                                             value.type, zc::mv(projections), value.type));
                  }
                  return zc::none;
                };
                auto thenOperand = armOperand(thenExpr, thenParam, thenParamIndex);
                auto elseOperand = armOperand(elseExpr, elseParam, elseParamIndex);
                if (thenOperand == zc::none || elseOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                identity::SourceSpan thenSpan = conditionalValue.sourceSpan.clone();
                identity::SourceSpan elseSpan = conditionalValue.sourceSpan.clone();
                ZC_IF_SOME(value, thenExpr) { thenSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, thenParam) { thenSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, elseExpr) { elseSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, elseParam) { elseSpan = value.sourceSpan.clone(); }
                zc::Vector<MirSwitchIntArm> arms;
                arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                                         blockId(2)});
                arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false),
                                         blockId(3)});
                // The result local is allocated at function entry (dominating both
                // branches); each branch initializes it and jumps to the single join
                // block, which performs the one Return.
                zc::Vector<MirStatement> entryStatements;
                entryStatements.add(
                    MirStatement::storageLive(resultLocal, returnStatement.sourceSpan.clone()));
                zc::Vector<MirProjection> thenDestinationProjections;
                zc::Vector<MirStatement> thenStatements;
                thenStatements.add(MirStatement::assign(
                    MirPlace(resultLocal, declaration.resultType,
                             zc::mv(thenDestinationProjections), declaration.resultType),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(thenOperand))),
                    MirInitializationKind::Initialize, thenSpan.clone()));
                zc::Vector<MirProjection> elseDestinationProjections;
                zc::Vector<MirStatement> elseStatements;
                elseStatements.add(MirStatement::assign(
                    MirPlace(resultLocal, declaration.resultType,
                             zc::mv(elseDestinationProjections), declaration.resultType),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(elseOperand))),
                    MirInitializationKind::Initialize, elseSpan.clone()));
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(entryStatements),
                    MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                             blockId(3), conditionalValue.sourceSpan.clone())});
                blocks.add(MirBasicBlock{blockId(2), scopeId(1), zc::mv(thenStatements),
                                         MirTerminator::gotoTarget(blockId(4), thenSpan.clone())});
                blocks.add(MirBasicBlock{blockId(3), scopeId(1), zc::mv(elseStatements),
                                         MirTerminator::gotoTarget(blockId(4), elseSpan.clone())});
                blocks.add(MirBasicBlock{
                    blockId(4), scopeId(1), zc::Vector<MirStatement>{},
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
            // Equality-comparison condition `a == b`: the SwitchInt discriminant
            // is a bool temporary assigned from a Comparison rvalue in the entry
            // block. The two operands are copies of the compared parameter locals.
            auto equality = primitiveBinaryFor(hirModule, conditionalValue.condition);
            ZC_IF_SOME(equalityValue, equality) {
              if (thenOk && elseOk) {
                // Each comparison operand is a scalar-literal expression or a
                // parameter reference; exactly one lookup succeeds per operand,
                // and at least one operand is a parameter.
                auto leftLiteral = expressionFor(hirModule, equalityValue.left);
                auto leftRef = parameterReferenceFor(hirModule, equalityValue.left);
                auto rightLiteral = expressionFor(hirModule, equalityValue.right);
                auto rightRef = parameterReferenceFor(hirModule, equalityValue.right);
                const bool leftOperandOk = (leftLiteral != zc::none) != (leftRef != zc::none);
                const bool rightOperandOk = (rightLiteral != zc::none) != (rightRef != zc::none);
                auto parameterLocalIndex =
                    [&](const hir::HirParameterReferenceExpression& reference,
                        size_t& outIndex) -> bool {
                  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                    if (declaration.parameters[i].key == reference.parameter) {
                      outIndex = i;
                      return true;
                    }
                  }
                  return false;
                };
                identity::SemanticTypeId thenType;
                identity::SemanticTypeId elseType;
                ZC_IF_SOME(value, thenExpr) { thenType = value.type; }
                ZC_IF_SOME(value, thenParam) { thenType = value.type; }
                ZC_IF_SOME(value, elseExpr) { elseType = value.type; }
                ZC_IF_SOME(value, elseParam) { elseType = value.type; }
                bool armParametersResolved = true;
                size_t thenParamIndex = 0;
                size_t elseParamIndex = 0;
                ZC_IF_SOME(value, thenParam) {
                  armParametersResolved &= parameterLocalIndex(value, thenParamIndex);
                }
                ZC_IF_SOME(value, elseParam) {
                  armParametersResolved &= parameterLocalIndex(value, elseParamIndex);
                }
                // Derive the shared operand type from a parameter operand; at
                // least one operand is a parameter.
                identity::SemanticTypeId operandType;
                bool operandTypeResolved = false;
                ZC_IF_SOME(value, leftRef) {
                  operandType = value.type;
                  operandTypeResolved = true;
                }
                ZC_IF_SOME(value, rightRef) {
                  operandType = value.type;
                  operandTypeResolved = true;
                }
                size_t leftIndex = 0;
                size_t rightIndex = 0;
                bool operandsResolved = leftOperandOk && rightOperandOk && operandTypeResolved &&
                                        (leftRef != zc::none || rightRef != zc::none);
                ZC_IF_SOME(value, leftRef) {
                  operandsResolved &=
                      parameterLocalIndex(value, leftIndex) && value.type == operandType;
                }
                ZC_IF_SOME(value, rightRef) {
                  operandsResolved &=
                      parameterLocalIndex(value, rightIndex) && value.type == operandType;
                }
                ZC_IF_SOME(value, leftLiteral) { operandsResolved &= value.type == operandType; }
                ZC_IF_SOME(value, rightLiteral) { operandsResolved &= value.type == operandType; }
                if (!operandsResolved || !armParametersResolved ||
                    conditionalValue.type != declaration.resultType ||
                    thenType != declaration.resultType || elseType != declaration.resultType ||
                    definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                const auto resultLocal =
                    localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
                // The comparison result is a bool temporary allocated after the
                // parameters and the function result.
                const auto conditionTemp =
                    localId(static_cast<uint32_t>(declaration.parameters.size() + 2));
                identity::SemanticTypeId boolType = equalityValue.type;
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                  locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                                 MirLocalKind::Parameter,
                                                 declaration.parameters[i].type, scopeId(1),
                                                 declaration.parameters[i].sourceSpan.clone()});
                }
                locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::FunctionResult,
                                               declaration.resultType, scopeId(1),
                                               returnStatement.sourceSpan.clone()});
                locals.add(MirLocalDeclaration{conditionTemp, MirLocalKind::Temporary, boolType,
                                               scopeId(1), equalityValue.sourceSpan.clone()});
                // Build each comparison operand: a literal operand becomes a
                // constant; a parameter operand becomes a copy of its local.
                auto comparisonOperand =
                    [&](zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                        zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                        size_t parameterIndex) -> zc::Maybe<MirOperand> {
                  ZC_IF_SOME(value, literal) {
                    return MirOperand::constant(value.type, value.value.clone());
                  }
                  ZC_IF_SOME(value, parameter) {
                    (void)value;
                    zc::Vector<MirProjection> projections;
                    return placeUse(proofs, copy,
                                    MirPlace(localId(static_cast<uint32_t>(parameterIndex + 1)),
                                             operandType, zc::mv(projections), operandType));
                  }
                  return zc::none;
                };
                auto leftOperand = comparisonOperand(leftLiteral, leftRef, leftIndex);
                auto rightOperand = comparisonOperand(rightLiteral, rightRef, rightIndex);
                zc::Vector<MirProjection> discriminantProjections;
                auto discriminant = placeUse(
                    proofs, copy,
                    MirPlace(conditionTemp, boolType, zc::mv(discriminantProjections), boolType));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand =
                    placeUse(proofs, copy,
                             MirPlace(resultLocal, declaration.resultType,
                                      zc::mv(returnProjections), declaration.resultType));
                if (leftOperand == zc::none || rightOperand == zc::none ||
                    discriminant == zc::none || returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                auto armOperand =
                    [&](zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                        zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                        size_t parameterIndex) -> zc::Maybe<MirOperand> {
                  ZC_IF_SOME(value, literal) {
                    return MirOperand::constant(value.type, value.value.clone());
                  }
                  ZC_IF_SOME(value, parameter) {
                    zc::Vector<MirProjection> projections;
                    return placeUse(proofs, copy,
                                    MirPlace(localId(static_cast<uint32_t>(parameterIndex + 1)),
                                             value.type, zc::mv(projections), value.type));
                  }
                  return zc::none;
                };
                auto thenOperand = armOperand(thenExpr, thenParam, thenParamIndex);
                auto elseOperand = armOperand(elseExpr, elseParam, elseParamIndex);
                if (thenOperand == zc::none || elseOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                identity::SourceSpan thenSpan = conditionalValue.sourceSpan.clone();
                identity::SourceSpan elseSpan = conditionalValue.sourceSpan.clone();
                ZC_IF_SOME(value, thenExpr) { thenSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, thenParam) { thenSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, elseExpr) { elseSpan = value.sourceSpan.clone(); }
                ZC_IF_SOME(value, elseParam) { elseSpan = value.sourceSpan.clone(); }
                zc::Vector<MirSwitchIntArm> arms;
                arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                                         blockId(2)});
                arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false),
                                         blockId(3)});
                // Entry block: StorageLive(result), StorageLive(temp), then the
                // Comparison assignment feeding the SwitchInt discriminant.
                zc::Vector<MirStatement> entryStatements;
                entryStatements.add(
                    MirStatement::storageLive(resultLocal, returnStatement.sourceSpan.clone()));
                entryStatements.add(
                    MirStatement::storageLive(conditionTemp, equalityValue.sourceSpan.clone()));
                zc::Vector<MirProjection> tempDestinationProjections;
                entryStatements.add(MirStatement::assign(
                    MirPlace(conditionTemp, boolType, zc::mv(tempDestinationProjections), boolType),
                    MirRvalue::comparison(
                        ZC_ASSERT_NONNULL(mirComparisonOperatorFor(equalityValue.operation)),
                        zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                        zc::mv(ZC_ASSERT_NONNULL(rightOperand)), boolType),
                    MirInitializationKind::Initialize, equalityValue.sourceSpan.clone()));
                zc::Vector<MirProjection> thenDestinationProjections;
                zc::Vector<MirStatement> thenStatements;
                thenStatements.add(MirStatement::assign(
                    MirPlace(resultLocal, declaration.resultType,
                             zc::mv(thenDestinationProjections), declaration.resultType),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(thenOperand))),
                    MirInitializationKind::Initialize, thenSpan.clone()));
                zc::Vector<MirProjection> elseDestinationProjections;
                zc::Vector<MirStatement> elseStatements;
                elseStatements.add(MirStatement::assign(
                    MirPlace(resultLocal, declaration.resultType,
                             zc::mv(elseDestinationProjections), declaration.resultType),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(elseOperand))),
                    MirInitializationKind::Initialize, elseSpan.clone()));
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(entryStatements),
                    MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                             blockId(3), conditionalValue.sourceSpan.clone())});
                blocks.add(MirBasicBlock{blockId(2), scopeId(1), zc::mv(thenStatements),
                                         MirTerminator::gotoTarget(blockId(4), thenSpan.clone())});
                blocks.add(MirBasicBlock{blockId(3), scopeId(1), zc::mv(elseStatements),
                                         MirTerminator::gotoTarget(blockId(4), elseSpan.clone())});
                blocks.add(MirBasicBlock{
                    blockId(4), scopeId(1), zc::Vector<MirStatement>{},
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
      }
      if (block.statements.size() == 1) {
        // Primitive-binary-return shape: `return <a OP b>`. The operation result
        // is computed into the function-result local in the single entry block
        // and returned directly (no branching). A comparison result is bool; an
        // arithmetic/bitwise result is the operand type. Each operand is a
        // scalar-literal constant or a copy of the operand parameter local.
        auto sourceReturn = returnFor(hirModule, block.statements[0]);
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto comparison = primitiveBinaryFor(hirModule, referenceNode);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(returnStatement, sourceReturn) {
          ZC_IF_SOME(comparisonValue, comparison) {
            auto leftLiteral = expressionFor(hirModule, comparisonValue.left);
            auto leftRef = parameterReferenceFor(hirModule, comparisonValue.left);
            auto rightLiteral = expressionFor(hirModule, comparisonValue.right);
            auto rightRef = parameterReferenceFor(hirModule, comparisonValue.right);
            const bool leftOperandOk = (leftLiteral != zc::none) != (leftRef != zc::none);
            const bool rightOperandOk = (rightLiteral != zc::none) != (rightRef != zc::none);
            auto parameterLocalIndex = [&](const hir::HirParameterReferenceExpression& reference,
                                           size_t& outIndex) -> bool {
              for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                if (declaration.parameters[i].key == reference.parameter) {
                  outIndex = i;
                  return true;
                }
              }
              return false;
            };
            // Derive the shared operand type from a parameter operand; at least
            // one operand is a parameter.
            identity::SemanticTypeId operandType;
            bool operandTypeResolved = false;
            ZC_IF_SOME(value, leftRef) {
              operandType = value.type;
              operandTypeResolved = true;
            }
            ZC_IF_SOME(value, rightRef) {
              operandType = value.type;
              operandTypeResolved = true;
            }
            size_t leftIndex = 0;
            size_t rightIndex = 0;
            bool operandsResolved = leftOperandOk && rightOperandOk && operandTypeResolved &&
                                    (leftRef != zc::none || rightRef != zc::none);
            ZC_IF_SOME(value, leftRef) {
              operandsResolved &=
                  parameterLocalIndex(value, leftIndex) && value.type == operandType;
            }
            ZC_IF_SOME(value, rightRef) {
              operandsResolved &=
                  parameterLocalIndex(value, rightIndex) && value.type == operandType;
            }
            ZC_IF_SOME(value, leftLiteral) { operandsResolved &= value.type == operandType; }
            ZC_IF_SOME(value, rightLiteral) { operandsResolved &= value.type == operandType; }
            // A comparison produces bool; an arithmetic operator produces the
            // operand type (so resultType == operandType). Exactly one operator
            // family maps for the HIR operation.
            const auto comparisonOperator = mirComparisonOperatorFor(comparisonValue.operation);
            const auto arithmeticOperator = mirArithmeticOperatorFor(comparisonValue.operation);
            const bool isArithmetic =
                comparisonOperator == zc::none && arithmeticOperator != zc::none;
            const bool operatorOk =
                comparisonOperator != zc::none ||
                (arithmeticOperator != zc::none && comparisonValue.type == operandType);
            if (operandsResolved && comparisonValue.type == declaration.resultType &&
                comparisonValue.operandType == operandType && definition != zc::none &&
                operatorOk) {
              // The result local holds the operation result: bool for a
              // comparison, the operand type for an arithmetic operation.
              const auto resultLocal =
                  localId(static_cast<uint32_t>(declaration.parameters.size() + 1));
              const identity::SemanticTypeId resultType = comparisonValue.type;
              zc::Vector<MirSourceScope> scopes;
              zc::Maybe<MirSourceScopeId> noParent;
              scopes.add(
                  MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
              zc::Vector<MirLocalDeclaration> locals;
              for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                               MirLocalKind::Parameter,
                                               declaration.parameters[i].type, scopeId(1),
                                               declaration.parameters[i].sourceSpan.clone()});
              }
              locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::FunctionResult,
                                             declaration.resultType, scopeId(1),
                                             returnStatement.sourceSpan.clone()});
              // Build each operand: a literal operand becomes a constant; a
              // parameter operand becomes a copy of its local.
              auto comparisonOperand =
                  [&](zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                      zc::Maybe<const hir::HirParameterReferenceExpression&> parameter,
                      size_t parameterIndex) -> zc::Maybe<MirOperand> {
                ZC_IF_SOME(value, literal) {
                  return MirOperand::constant(value.type, value.value.clone());
                }
                ZC_IF_SOME(value, parameter) {
                  (void)value;
                  zc::Vector<MirProjection> projections;
                  return placeUse(proofs, copy,
                                  MirPlace(localId(static_cast<uint32_t>(parameterIndex + 1)),
                                           operandType, zc::mv(projections), operandType));
                }
                return zc::none;
              };
              auto leftOperand = comparisonOperand(leftLiteral, leftRef, leftIndex);
              auto rightOperand = comparisonOperand(rightLiteral, rightRef, rightIndex);
              zc::Vector<MirProjection> returnProjections;
              auto returnOperand =
                  placeUse(proofs, copy,
                           MirPlace(resultLocal, declaration.resultType, zc::mv(returnProjections),
                                    declaration.resultType));
              if (leftOperand == zc::none || rightOperand == zc::none ||
                  returnOperand == zc::none) {
                return rejectMir<BuiltMirCandidate>(
                    ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                    declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
              }
              zc::Vector<MirStatement> statements;
              statements.add(
                  MirStatement::storageLive(resultLocal, returnStatement.sourceSpan.clone()));
              zc::Vector<MirProjection> destinationProjections;
              // A comparison lowers to a Comparison rvalue (result bool); an
              // arithmetic operator lowers to an Arithmetic rvalue (result the
              // operand type).
              auto rvalue =
                  isArithmetic
                      ? MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmeticOperator),
                                              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                              zc::mv(ZC_ASSERT_NONNULL(rightOperand)), resultType)
                      : MirRvalue::comparison(ZC_ASSERT_NONNULL(comparisonOperator),
                                              zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                              zc::mv(ZC_ASSERT_NONNULL(rightOperand)), resultType);
              statements.add(MirStatement::assign(
                  MirPlace(resultLocal, resultType, zc::mv(destinationProjections), resultType),
                  zc::mv(rvalue), MirInitializationKind::Initialize,
                  comparisonValue.sourceSpan.clone()));
              zc::Vector<MirBasicBlock> blocks;
              blocks.add(
                  MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                                MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                           returnStatement.sourceSpan.clone())});
              MirFunction function{declaration.definition,
                                   MirFunctionKind::Function,
                                   identity::DefinitionKind::Function,
                                   declaration.resultType,
                                   declaration.sourceSpan.clone(),
                                   zc::mv(scopes),
                                   zc::mv(locals),
                                   zc::mv(blocks)};
              zc::Array<uint8_t> ownerKey;
              ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
              pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
              continue;
            }
          }
        }
      }
      if (block.statements.size() == 2) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto sourceReturn = returnFor(hirModule, block.statements[1]);
        hir::HirNodeId referenceNode;
        ZC_IF_SOME(returnStatement, sourceReturn) { referenceNode = returnStatement.value; }
        auto localReference = localReferenceFor(hirModule, referenceNode);
        auto fieldProjection = localFieldProjectionFor(hirModule, referenceNode);
        hir::HirNodeId aggregateNode;
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(initializer, local.initializer) { aggregateNode = initializer; }
        }
        auto aggregate = aggregateFor(hirModule, aggregateNode);
        auto definition = identities.definition(declaration.definition);
        hir::HirNodeId reborrowInitializerNode;
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(initializer, local.initializer) { reborrowInitializerNode = initializer; }
        }
        auto reborrowInitializerParameter =
            parameterReferenceFor(hirModule, reborrowInitializerNode);
        auto localAliasReborrow = parameterReborrowFor(hirModule, referenceNode);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(returnStatement, sourceReturn) {
            ZC_IF_SOME(initializer, reborrowInitializerParameter) {
              ZC_IF_SOME(reborrow, localAliasReborrow) {
                if (declaration.parameters.size() != 1 ||
                    declaration.parameters[0].key != initializer.parameter ||
                    declaration.parameters[0].type != initializer.type ||
                    local.initializer != initializer.node || reborrow.sourceAlias == zc::none ||
                    ZC_ASSERT_NONNULL(reborrow.sourceAlias) != local.local ||
                    reborrow.parameter != initializer.parameter ||
                    reborrow.sourceType != local.type || reborrow.type != declaration.resultType ||
                    definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::Parameter,
                                               initializer.type, scopeId(1),
                                               declaration.parameters[0].sourceSpan.clone()});
                locals.add(MirLocalDeclaration{localId(2), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                locals.add(MirLocalDeclaration{localId(3), MirLocalKind::Temporary, reborrow.type,
                                               scopeId(1), reborrow.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(2), local.sourceSpan.clone()));
                zc::Vector<MirProjection> localProjections;
                zc::Vector<MirProjection> parameterProjections;
                auto initializerOperand =
                    placeUse(proofs, copy,
                             MirPlace(localId(1), initializer.type, zc::mv(parameterProjections),
                                      initializer.type));
                if (initializerOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                statements.add(MirStatement::assign(
                    MirPlace(localId(2), local.type, zc::mv(localProjections), local.type),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(initializerOperand))),
                    MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
                statements.add(MirStatement::storageLive(localId(3), reborrow.sourceSpan.clone()));
                zc::Vector<MirProjection> destinationProjections;
                zc::Vector<MirProjection> sourceProjections;
                sourceProjections.add(
                    MirProjection::dereference(reborrow.sourceType, reborrow.type));
                statements.add(MirStatement::borrowCreation(
                    MirPlace(localId(3), reborrow.type, zc::mv(destinationProjections),
                             reborrow.type),
                    reborrow.mutability == type::semantic::Mutability::Const
                        ? MirBorrowKind::Shared
                        : MirBorrowKind::Mutable,
                    MirPlace(localId(2), reborrow.sourceType, zc::mv(sourceProjections),
                             reborrow.type),
                    reborrow.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(3), reborrow.type, zc::mv(returnProjections), reborrow.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
                  auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
                  if (unsafeBlock == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::MissingRequiredFact,
                                                        module, declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  ZC_IF_SOME(block, unsafeBlock) {
                    auto unsafeSpan = block.sourceSpan.clone();
                    zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
                    scopes.add(
                        MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Enter, scopeId(2), unsafeSpan.clone()));
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Exit, scopeId(2), zc::mv(unsafeSpan)));
                  }
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(returnStatement, sourceReturn) {
            auto borrow = localBorrowFor(hirModule, referenceNode);
            ZC_IF_SOME(localBorrow, borrow) {
              if (local.local != localBorrow.local || local.type != localBorrow.sourceType ||
                  localBorrow.type != declaration.resultType || definition == zc::none) {
                return rejectMir<BuiltMirCandidate>(
                    ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                    declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
              }
              zc::Vector<MirSourceScope> scopes;
              zc::Maybe<MirSourceScopeId> noParent;
              scopes.add(
                  MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
              zc::Vector<MirLocalDeclaration> locals;
              locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                             scopeId(1), local.sourceSpan.clone()});
              locals.add(MirLocalDeclaration{localId(2), MirLocalKind::Temporary, localBorrow.type,
                                             scopeId(1), localBorrow.sourceSpan.clone()});
              zc::Vector<MirStatement> statements;
              statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
              ZC_IF_SOME(initializerNode, local.initializer) {
                auto initializer = expressionFor(hirModule, initializerNode);
                if (initializer == zc::none || ZC_ASSERT_NONNULL(initializer).type != local.type) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::MissingRequiredFact,
                                                      module, declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirProjection> projections;
                statements.add(MirStatement::assign(
                    MirPlace(localId(1), local.type, zc::mv(projections), local.type),
                    MirRvalue::use(MirOperand::constant(
                        local.type, ZC_ASSERT_NONNULL(initializer).value.clone())),
                    MirInitializationKind::Initialize,
                    ZC_ASSERT_NONNULL(initializer).sourceSpan.clone()));
              }
              statements.add(MirStatement::storageLive(localId(2), localBorrow.sourceSpan.clone()));
              zc::Vector<MirProjection> destinationProjections;
              zc::Vector<MirProjection> sourceProjections;
              statements.add(MirStatement::borrowCreation(
                  MirPlace(localId(2), localBorrow.type, zc::mv(destinationProjections),
                           localBorrow.type),
                  localBorrow.mutability == type::semantic::Mutability::Const
                      ? MirBorrowKind::Shared
                      : MirBorrowKind::Mutable,
                  MirPlace(localId(1), localBorrow.sourceType, zc::mv(sourceProjections),
                           localBorrow.sourceType),
                  localBorrow.sourceSpan.clone()));
              zc::Vector<MirProjection> returnProjections;
              auto returnOperand = placeUse(proofs, copy,
                                            MirPlace(localId(2), localBorrow.type,
                                                     zc::mv(returnProjections), localBorrow.type));
              if (returnOperand == zc::none) {
                return rejectMir<BuiltMirCandidate>(
                    ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
                    declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
              }
              ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
                auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
                if (unsafeBlock == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::MissingRequiredFact,
                                                      module, declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                ZC_IF_SOME(block, unsafeBlock) {
                  auto unsafeSpan = block.sourceSpan.clone();
                  zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
                  scopes.add(MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
                  statements.add(MirStatement::unsafeScopeBoundary(
                      MirUnsafeScopeBoundaryKind::Enter, scopeId(2), unsafeSpan.clone()));
                  statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Exit,
                                                                   scopeId(2), zc::mv(unsafeSpan)));
                }
              }
              zc::Vector<MirBasicBlock> blocks;
              blocks.add(
                  MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                                MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                           returnStatement.sourceSpan.clone())});
              MirFunction function{declaration.definition,
                                   MirFunctionKind::Function,
                                   identity::DefinitionKind::Function,
                                   declaration.resultType,
                                   declaration.sourceSpan.clone(),
                                   zc::mv(scopes),
                                   zc::mv(locals),
                                   zc::mv(blocks)};
              zc::Array<uint8_t> ownerKey;
              ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
              pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
              continue;
            }
          }
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(returnStatement, sourceReturn) {
            ZC_IF_SOME(projection, fieldProjection) {
              if (local.initializer == zc::none) {
                if (local.local != projection.local || projection.receiverType != local.type ||
                    projection.type != declaration.resultType ||
                    projection.category != hir::HirValueCategory::Place || definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                returnProjections.add(
                    MirProjection::field(projection.field, local.type, projection.type));
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), projection.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
            ZC_IF_SOME(sourceAggregate, aggregate) {
              ZC_IF_SOME(reference, localReference) {
                if (local.local != reference.local || local.type != sourceAggregate.type ||
                    reference.type != declaration.resultType ||
                    sourceAggregate.category != hir::HirValueCategory::Value ||
                    reference.category != hir::HirValueCategory::Place || definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirNominalAggregateElement> elements;
                for (const auto& element : sourceAggregate.elements) {
                  elements.add(MirNominalAggregateElement{
                      element.field, MirOperand::constant(element.type, element.value.clone())});
                }
                zc::Vector<MirProjection> destinationProjections;
                statements.add(MirStatement::assign(
                    MirPlace(localId(1), local.type, zc::mv(destinationProjections), local.type),
                    MirRvalue::nominalAggregate(sourceAggregate.definition, sourceAggregate.type,
                                                zc::mv(elements)),
                    MirInitializationKind::Initialize, sourceAggregate.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
              ZC_IF_SOME(projection, fieldProjection) {
                if (local.local != projection.local || local.type != sourceAggregate.type ||
                    projection.receiverType != local.type ||
                    projection.type != declaration.resultType ||
                    sourceAggregate.category != hir::HirValueCategory::Value ||
                    projection.category != hir::HirValueCategory::Place || definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirNominalAggregateElement> elements;
                for (const auto& element : sourceAggregate.elements) {
                  elements.add(MirNominalAggregateElement{
                      element.field, MirOperand::constant(element.type, element.value.clone())});
                }
                zc::Vector<MirProjection> destinationProjections;
                statements.add(MirStatement::assign(
                    MirPlace(localId(1), local.type, zc::mv(destinationProjections), local.type),
                    MirRvalue::nominalAggregate(sourceAggregate.definition, sourceAggregate.type,
                                                zc::mv(elements)),
                    MirInitializationKind::Initialize, sourceAggregate.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                returnProjections.add(
                    MirProjection::field(projection.field, local.type, projection.type));
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), projection.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        auto reference = localReferenceFor(hirModule, referenceNode);
        ZC_IF_SOME(local, sourceLocal) {
          if (local.initializer == zc::none) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              ZC_IF_SOME(localReference, reference) {
                if (local.local != localReference.local || local.type != declaration.resultType ||
                    localReference.type != local.type ||
                    localReference.category != hir::HirValueCategory::Place ||
                    definition == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        hir::HirNodeId initializerNode;
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(initializer, local.initializer) { initializerNode = initializer; }
        }
        auto initializer = expressionFor(hirModule, initializerNode);
        auto initializerCall = callFor(hirModule, initializerNode);
        auto initializerParameter = parameterReferenceFor(hirModule, initializerNode);
        if (sourceLocal == zc::none || sourceReturn == zc::none ||
            ((initializer != zc::none) + (initializerCall != zc::none) +
                 (initializerParameter != zc::none) !=
             1) ||
            reference == zc::none || definition == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(literal, initializer) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              ZC_IF_SOME(localReference, reference) {
                if (local.local != localReference.local || local.type != declaration.resultType ||
                    literal.type != local.type || localReference.type != local.type ||
                    localReference.category != hir::HirValueCategory::Place) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirProjection> destinationProjections;
                statements.add(MirStatement::assign(
                    MirPlace(localId(1), local.type, zc::mv(destinationProjections), local.type),
                    MirRvalue::use(MirOperand::constant(local.type, literal.value.clone())),
                    MirInitializationKind::Initialize, literal.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
                  auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
                  if (unsafeBlock == zc::none) {
                    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                        ir::IrFailureKind::MissingRequiredFact,
                                                        module, declaration.definition, identities,
                                                        static_cast<uint32_t>(pending.size() + 1));
                  }
                  ZC_IF_SOME(block, unsafeBlock) {
                    auto unsafeSpan = block.sourceSpan.clone();
                    zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
                    scopes.add(
                        MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Enter, scopeId(2), unsafeSpan.clone()));
                    statements.add(MirStatement::unsafeScopeBoundary(
                        MirUnsafeScopeBoundaryKind::Exit, scopeId(2), zc::mv(unsafeSpan)));
                  }
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(parameter, initializerParameter) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              ZC_IF_SOME(localReference, reference) {
                if (declaration.parameters.size() != 1 ||
                    declaration.parameters[0].key != parameter.parameter ||
                    declaration.parameters[0].type != parameter.type ||
                    local.local != localReference.local || local.type != declaration.resultType ||
                    parameter.type != local.type || localReference.type != local.type ||
                    localReference.category != hir::HirValueCategory::Place) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::Parameter, parameter.type,
                                               scopeId(1),
                                               declaration.parameters[0].sourceSpan.clone()});
                locals.add(MirLocalDeclaration{localId(2), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> statements;
                statements.add(MirStatement::storageLive(localId(2), local.sourceSpan.clone()));
                zc::Vector<MirProjection> destinationProjections;
                zc::Vector<MirProjection> sourceProjections;
                auto initializerOperand =
                    placeUse(proofs, copy,
                             MirPlace(localId(1), parameter.type, zc::mv(sourceProjections),
                                      parameter.type));
                if (initializerOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                statements.add(MirStatement::assign(
                    MirPlace(localId(2), local.type, zc::mv(destinationProjections), local.type),
                    MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(initializerOperand))),
                    MirInitializationKind::Initialize, parameter.sourceSpan.clone()));
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(2), local.type, zc::mv(returnProjections), local.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{
                    blockId(1), scopeId(1), zc::mv(statements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(directCall, initializerCall) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              ZC_IF_SOME(localReference, reference) {
                if (local.local != localReference.local || local.type != declaration.resultType ||
                    directCall.resultType != local.type || localReference.type != local.type ||
                    localReference.category != hir::HirValueCategory::Place) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirSourceScope> scopes;
                zc::Maybe<MirSourceScopeId> noParent;
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> entryStatements;
                entryStatements.add(
                    MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                zc::Vector<MirProjection> destinationProjections;
                zc::Vector<MirOperand> arguments;
                bool constantArguments = true;
                for (const auto& argument : directCall.arguments) {
                  ZC_IF_SOME(value, argument.value) {
                    arguments.add(MirOperand::constant(argument.type, value.clone()));
                  } else {
                    constantArguments = false;
                  }
                }
                if (!constantArguments) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Maybe<MirBlockId> noUnwind;
                auto callTerminator = MirTerminator::call(
                    directCall.callee, zc::mv(arguments), MirCallEffect::noActivation(),
                    MirPlace(localId(1), local.type, zc::mv(destinationProjections), local.type),
                    blockId(2), zc::mv(noUnwind), directCall.sourceSpan.clone());
                zc::Vector<MirStatement> continuationStatements;
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(localId(1), local.type, zc::mv(returnProjections), local.type));
                if (returnOperand == zc::none) {
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Vector<MirBasicBlock> blocks;
                blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                         zc::mv(callTerminator)});
                blocks.add(MirBasicBlock{
                    blockId(2), scopeId(1), zc::mv(continuationStatements),
                    MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                               returnStatement.sourceSpan.clone())});
                MirFunction function{declaration.definition,
                                     MirFunctionKind::Function,
                                     identity::DefinitionKind::Function,
                                     declaration.resultType,
                                     declaration.sourceSpan.clone(),
                                     zc::mv(scopes),
                                     zc::mv(locals),
                                     zc::mv(blocks)};
                zc::Array<uint8_t> ownerKey;
                ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
                pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                continue;
              }
            }
          }
        }
      }
    }
    hir::HirNodeId returnNode;
    ZC_IF_SOME(value, sourceBlock) {
      if (value.statements.size() == 1) returnNode = value.statements[0];
    }
    auto sourceReturn = returnFor(hirModule, returnNode);
    hir::HirNodeId expressionNode;
    ZC_IF_SOME(value, sourceReturn) { expressionNode = value.value; }
    auto expression = expressionFor(hirModule, expressionNode);
    auto call = callFor(hirModule, expressionNode);
    auto definition = identities.definition(declaration.definition);
    auto semanticType = semanticTypes.get(declaration.resultType);
    if (sourceReturn == zc::none || (expression == zc::none) == (call == zc::none) ||
        definition == zc::none || !semanticType.is<type::SemanticTypeLookup>() ||
        !declaration.definition.belongsTo(hirModule.semanticContext()) ||
        !declaration.resultType.belongsTo(hirModule.semanticContext())) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
    }
    auto returnSpan = [&]() {
      ZC_IF_SOME(value, sourceReturn) { return value.sourceSpan.clone(); }
      ZC_UNREACHABLE
    }();
    ZC_IF_SOME(literal, expression) {
      zc::Vector<MirSourceScope> scopes;
      zc::Maybe<MirSourceScopeId> noParent;
      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
      zc::Vector<MirLocalDeclaration> locals;
      zc::Vector<MirStatement> statements;
      ZC_IF_SOME(unsafeNode, declaration.unsafeBlock) {
        auto unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
        if (unsafeBlock == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        ZC_IF_SOME(block, unsafeBlock) {
          auto unsafeSpan = block.sourceSpan.clone();
          zc::Maybe<MirSourceScopeId> functionScope = scopeId(1);
          scopes.add(MirSourceScope{scopeId(2), zc::mv(functionScope), unsafeSpan.clone()});
          statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter,
                                                           scopeId(2), unsafeSpan.clone()));
          statements.add(MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Exit,
                                                           scopeId(2), zc::mv(unsafeSpan)));
        }
      }
      auto returnOperand = MirOperand::constant(declaration.resultType, literal.value.clone());
      zc::Vector<MirBasicBlock> blocks;
      blocks.add(
          MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                        MirTerminator::returnValue(zc::mv(returnOperand), returnSpan.clone())});
      MirFunction function{declaration.definition,
                           MirFunctionKind::Function,
                           identity::DefinitionKind::Function,
                           declaration.resultType,
                           declaration.sourceSpan.clone(),
                           zc::mv(scopes),
                           zc::mv(locals),
                           zc::mv(blocks)};
      zc::Array<uint8_t> ownerKey;
      ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
      continue;
    }
    ZC_IF_SOME(directCall, call) {
      // A direct-call return lowers the caller's parameters to leading parameter
      // locals so a parameter-reference argument can be copied/moved as a place
      // operand. The call-result temporary and the function-result local follow
      // the parameters.
      const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
      const auto temporaryLocal = localId(parameterCount + 1);
      const auto resultLocalId = localId(parameterCount + 2);
      auto parameterLocalIndex = [&](const identity::CallableParameterKey& key,
                                     size_t& outIndex) -> bool {
        for (size_t i = 0; i < declaration.parameters.size(); ++i) {
          if (declaration.parameters[i].key == key) {
            outIndex = i;
            return true;
          }
        }
        return false;
      };
      zc::Vector<MirSourceScope> scopes;
      zc::Maybe<MirSourceScopeId> noParent;
      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
      zc::Vector<MirLocalDeclaration> locals;
      for (size_t i = 0; i < declaration.parameters.size(); ++i) {
        locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                       MirLocalKind::Parameter, declaration.parameters[i].type,
                                       scopeId(1), declaration.parameters[i].sourceSpan.clone()});
      }
      locals.add(MirLocalDeclaration{temporaryLocal, MirLocalKind::Temporary,
                                     declaration.resultType, scopeId(1),
                                     directCall.sourceSpan.clone()});
      locals.add(MirLocalDeclaration{resultLocalId, MirLocalKind::FunctionResult,
                                     declaration.resultType, scopeId(1), returnSpan.clone()});
      zc::Vector<MirStatement> entryStatements;
      entryStatements.add(MirStatement::storageLive(temporaryLocal, directCall.sourceSpan.clone()));
      zc::Vector<MirProjection> destinationProjections;
      zc::Vector<MirOperand> arguments;
      bool argumentsResolved = true;
      for (const auto& argument : directCall.arguments) {
        ZC_IF_SOME(value, argument.value) {
          arguments.add(MirOperand::constant(argument.type, value.clone()));
        }
        ZC_IF_SOME(parameter, argument.parameter) {
          size_t parameterIndex = 0;
          if (!parameterLocalIndex(parameter, parameterIndex)) {
            argumentsResolved = false;
            break;
          }
          zc::Vector<MirProjection> argumentProjections;
          auto operand =
              placeUse(proofs, copy,
                       MirPlace(localId(static_cast<uint32_t>(parameterIndex + 1)), argument.type,
                                zc::mv(argumentProjections), argument.type));
          if (operand == zc::none) {
            argumentsResolved = false;
            break;
          }
          arguments.add(zc::mv(ZC_ASSERT_NONNULL(operand)));
        }
      }
      if (!argumentsResolved) {
        return rejectMir<BuiltMirCandidate>(
            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
            declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
      }
      zc::Maybe<MirBlockId> noUnwind;
      auto callTerminator =
          MirTerminator::call(directCall.callee, zc::mv(arguments), MirCallEffect::noActivation(),
                              MirPlace(temporaryLocal, declaration.resultType,
                                       zc::mv(destinationProjections), declaration.resultType),
                              blockId(2), zc::mv(noUnwind), directCall.sourceSpan.clone());
      zc::Vector<MirStatement> continuationStatements;
      continuationStatements.add(MirStatement::storageLive(resultLocalId, returnSpan.clone()));
      zc::Vector<MirProjection> resultProjections;
      zc::Vector<MirProjection> temporaryProjections;
      continuationStatements.add(MirStatement::assign(
          MirPlace(resultLocalId, declaration.resultType, zc::mv(resultProjections),
                   declaration.resultType),
          MirRvalue::use(
              MirOperand::move(MirPlace(temporaryLocal, declaration.resultType,
                                        zc::mv(temporaryProjections), declaration.resultType))),
          MirInitializationKind::Initialize, returnSpan.clone()));
      continuationStatements.add(MirStatement::storageDead(temporaryLocal, returnSpan.clone()));
      zc::Vector<MirProjection> returnProjections;
      auto returnOperand = placeUse(proofs, copy,
                                    MirPlace(resultLocalId, declaration.resultType,
                                             zc::mv(returnProjections), declaration.resultType));
      if (returnOperand == zc::none) {
        return rejectMir<BuiltMirCandidate>(
            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
            declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
      }
      zc::Vector<MirBasicBlock> blocks;
      blocks.add(
          MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements), zc::mv(callTerminator)});
      blocks.add(MirBasicBlock{blockId(2), scopeId(1), zc::mv(continuationStatements),
                               MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                          returnSpan.clone())});
      MirFunction function{declaration.definition,
                           MirFunctionKind::Function,
                           identity::DefinitionKind::Function,
                           declaration.resultType,
                           declaration.sourceSpan.clone(),
                           zc::mv(scopes),
                           zc::mv(locals),
                           zc::mv(blocks)};
      zc::Array<uint8_t> ownerKey;
      ZC_IF_SOME(key, definition) { ownerKey = key.key().encode(); }
      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
      continue;
    }
    ZC_UNREACHABLE
  }

  sortFunctions(pending);
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  for (auto& item : pending) {
    auto encoded = encodeFunction(item.function, module, identities, semanticTypes);
    if (encoded == zc::none) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::CanonicalCodecMismatch, module,
          item.function.owner, identities, static_cast<uint32_t>(functions.size() + 1));
    }
    ZC_IF_SOME(record, encoded) { canonicalFunctions.add(zc::mv(record)); }
    functions.add(zc::mv(item.function));
  }
  auto moduleKey = identities.module(module);
  if (moduleKey == zc::none) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InvalidFact, module,
                                        firstDefinition(hirModule), identities, 0);
  }
  zc::Maybe<MirRevisionId> revision;
  ZC_IF_SOME(key, moduleKey) {
    auto expanded = key.key().encode();
    revision = MirRevisionCodec::computeBuilt(
        hirModule.contextFingerprint(), expanded.asPtr(), hirModule.checkedFactsRevision(),
        hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(),
        canonicalFunctions.asPtr());
  }
  if (revision == zc::none) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::CanonicalCodecMismatch, module,
                                        firstDefinition(hirModule), identities, 0);
  }
  ZC_IF_SOME(value, revision) {
    return ir::IrOperationResult<BuiltMirCandidate>::verified(
        BuiltMirCandidate(hirModule, zc::mv(functions), zc::mv(canonicalFunctions), value));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedBuiltMir> BuiltMirVerifier::verify(BuiltMirCandidate&& candidate,
                                                                 const BuiltMirInput& input) {
  const auto& hirModule = candidate.sourceHir;
  const auto module = hirModule.module();
  const auto identities = hirModule.retainIdentityAuthority();
  const auto& semanticTypes = hirModule.semanticTypes();
  if (&input.hir != &hirModule || !validBuiltMirInput(hirModule, input.body)) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }
  auto proofInput = checker::marker::MarkerProofInput::from(input.body);
  if (proofInput == zc::none) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }
  checker::marker::MarkerProofEngine proofs(zc::mv(ZC_ASSERT_NONNULL(proofInput)));
  const auto copy = input.body.standardMarkers.copy();
  const auto borrowCapability = hirModule.borrowEvidenceCapability();
  const auto evidence = borrowCapability.lookup(hirModule.borrowEvidenceLease());
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != hirModule.borrowEvidenceRevision().digest() ||
      candidate.functions.size() !=
          hirModule.declarations().size() + hirModule.functions().size() ||
      candidate.canonicalFunctions.size() != candidate.functions.size()) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }

  zc::Vector<zc::Array<uint8_t>> recomputedFunctions;
  zc::Array<uint8_t> previousOwner;
  for (size_t index = 0; index < candidate.functions.size(); ++index) {
    const auto& function = candidate.functions[index];
    if (!validateUnsafeScopeBoundaries(function)) {
      return rejectMir<VerifiedBuiltMir>(
          ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidControlFlow, module,
          function.owner, identities, static_cast<uint32_t>(index + 1));
    }
    if (!validateTerminatorTargets(function)) {
      return rejectMir<VerifiedBuiltMir>(
          ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidControlFlow, module,
          function.owner, identities, static_cast<uint32_t>(index + 1));
    }
    zc::Maybe<const hir::HirValueDeclaration&> declaration;
    for (const auto& value : hirModule.declarations()) {
      if (value.definition != function.owner) continue;
      if (declaration != zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::AdditionalFact, module,
            function.owner, identities, static_cast<uint32_t>(index + 1));
      }
      declaration = value;
    }
    zc::Maybe<const hir::HirFunctionDeclaration&> sourceFunction;
    for (const auto& value : hirModule.functions()) {
      if (value.definition != function.owner) continue;
      if (sourceFunction != zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::AdditionalFact, module,
            function.owner, identities, static_cast<uint32_t>(index + 1));
      }
      sourceFunction = value;
    }
    if ((declaration == zc::none) == (sourceFunction == zc::none)) {
      return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                         ir::IrFailureKind::AdditionalFact, module, function.owner,
                                         identities, static_cast<uint32_t>(index + 1));
    }
    ZC_IF_SOME(sourceDeclaration, declaration) {
      auto expression = expressionFor(hirModule, sourceDeclaration.initializer);
      if (expression == zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::MissingRequiredFact,
            module, function.owner, identities, static_cast<uint32_t>(index + 1));
      }
      ZC_IF_SOME(sourceExpression, expression) {
        if (!validScalarFunction(function, sourceDeclaration, sourceExpression, module, identities,
                                 semanticTypes, proofs, copy)) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        auto owner = identities.definition(function.owner);
        auto record = encodeFunction(function, module, identities, semanticTypes);
        if (owner == zc::none || record == zc::none) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, identities, static_cast<uint32_t>(index + 1));
        }
        zc::Array<uint8_t> ownerBytes;
        ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
        if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        previousOwner = zc::mv(ownerBytes);
        ZC_IF_SOME(value, record) {
          if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
                module, function.owner, identities, static_cast<uint32_t>(index + 1));
          }
          recomputedFunctions.add(zc::mv(value));
        }
        continue;
      }
    }
    ZC_IF_SOME(sourceDeclaration, sourceFunction) {
      auto sourceBlock = blockFor(hirModule, sourceDeclaration.body);
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 2 &&
          loopFor(hirModule, ZC_ASSERT_NONNULL(sourceBlock).statements[0]) != zc::none) {
        bool valid = false;
        ZC_IF_SOME(block, sourceBlock) {
          auto loop = loopFor(hirModule, block.statements[0]);
          auto sourceReturn = returnFor(hirModule, block.statements[1]);
          ZC_IF_SOME(loopValue, loop) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              auto conditionRef = parameterReferenceFor(hirModule, loopValue.condition);
              auto returnExpr = expressionFor(hirModule, returnStatement.value);
              ZC_IF_SOME(condRef, conditionRef) {
                ZC_IF_SOME(returnLiteral, returnExpr) {
                  valid = validLoopReturnFunction(
                      function, sourceDeclaration, block, returnStatement, loopValue, condRef,
                      returnLiteral, proofs, copy, module, identities, semanticTypes);
                }
              }
            }
          }
        }
        if (!valid) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        auto owner = identities.definition(function.owner);
        auto record = encodeFunction(function, module, identities, semanticTypes);
        if (owner == zc::none || record == zc::none) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, identities, static_cast<uint32_t>(index + 1));
        }
        zc::Array<uint8_t> ownerBytes;
        ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
        if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        previousOwner = zc::mv(ownerBytes);
        ZC_IF_SOME(value, record) {
          if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
                module, function.owner, identities, static_cast<uint32_t>(index + 1));
          }
          recomputedFunctions.add(zc::mv(value));
        }
        continue;
      }
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 2) {
        ZC_IF_SOME(block, sourceBlock) {
          auto sourceLocal = localFor(hirModule, block.statements[0]);
          auto sourceReturn = returnFor(hirModule, block.statements[1]);
          ZC_IF_SOME(local, sourceLocal) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              auto receiverCall = receiverCallFor(hirModule, returnStatement.value);
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
              auto aggregate = aggregateFor(hirModule, initializerNode);
              ZC_IF_SOME(call, receiverCall) {
                auto receiver = localReferenceFor(hirModule, call.receiver);
                ZC_IF_SOME(reference, receiver) {
                  ZC_IF_SOME(initializer, aggregate) {
                    if (!validReceiverCallReturnFunction(
                            function, sourceDeclaration, block, local, initializer, returnStatement,
                            reference, call, proofs, copy, module, identities, semanticTypes)) {
                      return rejectMir<VerifiedBuiltMir>(
                          ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact,
                          module, function.owner, identities, static_cast<uint32_t>(index + 1));
                    }
                    auto owner = identities.definition(function.owner);
                    auto record = encodeFunction(function, module, identities, semanticTypes);
                    if (owner == zc::none || record == zc::none) {
                      return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                         ir::IrFailureKind::CanonicalCodecMismatch,
                                                         module, function.owner, identities,
                                                         static_cast<uint32_t>(index + 1));
                    }
                    zc::Array<uint8_t> ownerBytes;
                    ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
                    if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
                      return rejectMir<VerifiedBuiltMir>(
                          ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact,
                          module, function.owner, identities, static_cast<uint32_t>(index + 1));
                    }
                    previousOwner = zc::mv(ownerBytes);
                    ZC_IF_SOME(value, record) {
                      if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
                        return rejectMir<VerifiedBuiltMir>(
                            ir::IrFailurePhase::BuiltMirVerification,
                            ir::IrFailureKind::CanonicalCodecMismatch, module, function.owner,
                            identities, static_cast<uint32_t>(index + 1));
                      }
                      recomputedFunctions.add(zc::mv(value));
                    }
                    continue;
                  }
                }
              }
            }
          }
        }
      }
      bool returnsRootLocal = true;
      ZC_IF_SOME(block, sourceBlock) {
        auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
        ZC_IF_SOME(returnStatement, sourceReturn) {
          returnsRootLocal = localFieldProjectionFor(hirModule, returnStatement.value) == zc::none;
        }
      }
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 3) {
        ZC_IF_SOME(block, sourceBlock) {
          bool allLeadingLocals = true;
          for (size_t i = 0; i + 1 < block.statements.size(); ++i) {
            if (localFor(hirModule, block.statements[i]) == zc::none) {
              allLeadingLocals = false;
              break;
            }
          }
          auto sequentialReturn =
              returnFor(hirModule, block.statements[block.statements.size() - 1]);
          if (allLeadingLocals && sequentialReturn != zc::none) {
            bool valid =
                validSequentialLocalReturnFunction(function, hirModule, sourceDeclaration, block,
                                                   module, identities, semanticTypes, proofs, copy);
            if (!valid) {
              return rejectMir<VerifiedBuiltMir>(
                  ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact, module,
                  function.owner, identities, static_cast<uint32_t>(index + 1));
            }
            auto owner = identities.definition(function.owner);
            auto record = encodeFunction(function, module, identities, semanticTypes);
            if (owner == zc::none || record == zc::none) {
              return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                 ir::IrFailureKind::CanonicalCodecMismatch, module,
                                                 function.owner, identities,
                                                 static_cast<uint32_t>(index + 1));
            }
            zc::Array<uint8_t> ownerBytes;
            ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
            if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
              return rejectMir<VerifiedBuiltMir>(
                  ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact, module,
                  function.owner, identities, static_cast<uint32_t>(index + 1));
            }
            previousOwner = zc::mv(ownerBytes);
            ZC_IF_SOME(value, record) {
              if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
                return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                   ir::IrFailureKind::CanonicalCodecMismatch,
                                                   module, function.owner, identities,
                                                   static_cast<uint32_t>(index + 1));
              }
              recomputedFunctions.add(zc::mv(value));
            }
            continue;
          }
        }
      }
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 4 &&
          returnsRootLocal) {
        bool validSequence = false;
        ZC_IF_SOME(block, sourceBlock) {
          auto sourceLocal = localFor(hirModule, block.statements[0]);
          auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
          ZC_IF_SOME(local, sourceLocal) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              auto reference = localReferenceFor(hirModule, returnStatement.value);
              const size_t writeCount = block.statements.size() - 2;
              const size_t initializerCount = local.initializer == zc::none ? 0 : 1;
              zc::Maybe<const hir::HirUnsafeBlockExpression&> unsafeBlock;
              ZC_IF_SOME(unsafeNode, sourceDeclaration.unsafeBlock) {
                unsafeBlock = unsafeBlockFor(hirModule, unsafeNode);
                if (unsafeBlock == zc::none) { validSequence = false; }
              }
              const bool hasUnsafeBlock = unsafeBlock != zc::none;
              validSequence = function.owner == sourceDeclaration.definition &&
                              function.kind == MirFunctionKind::Function &&
                              function.sourceDefinitionKind == identity::DefinitionKind::Function &&
                              function.resultType == sourceDeclaration.resultType &&
                              sourceDeclaration.body == block.node &&
                              function.sourceScopes.size() == (hasUnsafeBlock ? 2 : 1) &&
                              function.locals.size() == 1 && function.blocks.size() == 1 &&
                              local.local.ordinal() == 1 && reference != zc::none &&
                              ZC_ASSERT_NONNULL(reference).local == local.local &&
                              ZC_ASSERT_NONNULL(reference).type == local.type &&
                              ZC_ASSERT_NONNULL(reference).category == hir::HirValueCategory::Place;
              const auto& scope = function.sourceScopes[0];
              const auto& mirLocal = function.locals[0];
              const auto& mirBlock = function.blocks[0];
              if (validSequence &&
                  (scope.id != scopeId(1) || scope.parent != zc::none ||
                   !sameSpan(scope.sourceSpan, sourceDeclaration.sourceSpan) ||
                   mirLocal.id != localId(1) || mirLocal.kind != MirLocalKind::UserLocal ||
                   mirLocal.type != local.type || mirLocal.sourceScope != scope.id ||
                   !sameSpan(mirLocal.sourceSpan, local.sourceSpan) || mirBlock.id != blockId(1) ||
                   mirBlock.sourceScope != scope.id ||
                   mirBlock.statements.size() !=
                       1 + initializerCount + writeCount + (hasUnsafeBlock ? 2 : 0) ||
                   mirBlock.statements[0].kind() != MirStatementKind::StorageLive ||
                   mirBlock.statements[0].storageLocal() != mirLocal.id ||
                   !sameSpan(mirBlock.statements[0].sourceSpan(), local.sourceSpan) ||
                   mirBlock.terminator.kind() != MirTerminatorKind::Return ||
                   mirBlock.terminator.returnValue().value == zc::none ||
                   !sameSpan(mirBlock.terminator.sourceSpan(), returnStatement.sourceSpan))) {
                validSequence = false;
              }
              size_t statementIndex = 1;
              ZC_IF_SOME(initializerNode, local.initializer) {
                auto initializer = expressionFor(hirModule, initializerNode);
                if (!validSequence || initializer == zc::none ||
                    ZC_ASSERT_NONNULL(initializer).type != local.type ||
                    mirBlock.statements[statementIndex].kind() != MirStatementKind::Assign ||
                    mirBlock.statements[statementIndex].assignmentValue().initialization !=
                        MirInitializationKind::Initialize ||
                    !sameSpan(mirBlock.statements[statementIndex].sourceSpan(),
                              ZC_ASSERT_NONNULL(initializer).sourceSpan)) {
                  validSequence = false;
                } else {
                  const auto& assignment = mirBlock.statements[statementIndex].assignmentValue();
                  if (assignment.destination.local() != mirLocal.id ||
                      assignment.destination.rootType() != local.type ||
                      assignment.destination.resultType() != local.type ||
                      assignment.destination.projections().size() != 0 ||
                      assignment.value.kind() != MirRvalueKind::Use ||
                      assignment.value.useValue().operand.kind() != MirOperandKind::Constant ||
                      assignment.value.useValue().operand.constantValue().type != local.type ||
                      !sameConstant(assignment.value.useValue().operand.constantValue().value,
                                    ZC_ASSERT_NONNULL(initializer).value, module, identities,
                                    semanticTypes)) {
                    validSequence = false;
                  }
                }
                ++statementIndex;
              }
              for (size_t writeIndex = 0; writeIndex < writeCount; ++writeIndex) {
                auto write = localWriteFor(hirModule, block.statements[writeIndex + 1]);
                if (write == zc::none) {
                  validSequence = false;
                  break;
                }
                auto value = expressionFor(hirModule, ZC_ASSERT_NONNULL(write).value);
                const auto expectedKind = statementIndex == 1 ? MirInitializationKind::Initialize
                                                              : MirInitializationKind::Overwrite;
                if (!validSequence || value == zc::none ||
                    ZC_ASSERT_NONNULL(write).local != local.local ||
                    ZC_ASSERT_NONNULL(write).type != local.type ||
                    ZC_ASSERT_NONNULL(value).type != local.type ||
                    ZC_ASSERT_NONNULL(write).kind !=
                        (expectedKind == MirInitializationKind::Initialize
                             ? hir::HirLocalWriteKind::Initialize
                             : hir::HirLocalWriteKind::Overwrite) ||
                    mirBlock.statements[statementIndex].kind() != MirStatementKind::Assign ||
                    mirBlock.statements[statementIndex].assignmentValue().initialization !=
                        expectedKind ||
                    !sameSpan(mirBlock.statements[statementIndex].sourceSpan(),
                              ZC_ASSERT_NONNULL(write).sourceSpan)) {
                  validSequence = false;
                  break;
                }
                const auto& assignment = mirBlock.statements[statementIndex].assignmentValue();
                if (assignment.destination.local() != mirLocal.id ||
                    assignment.destination.rootType() != local.type ||
                    assignment.destination.resultType() != local.type ||
                    assignment.destination.projections().size() != 0 ||
                    assignment.value.kind() != MirRvalueKind::Use ||
                    assignment.value.useValue().operand.kind() != MirOperandKind::Constant ||
                    assignment.value.useValue().operand.constantValue().type != local.type ||
                    !sameConstant(assignment.value.useValue().operand.constantValue().value,
                                  ZC_ASSERT_NONNULL(value).value, module, identities,
                                  semanticTypes)) {
                  validSequence = false;
                  break;
                }
                ++statementIndex;
              }
              if (validSequence && hasUnsafeBlock) {
                ZC_IF_SOME(unsafeBlockRef, unsafeBlock) {
                  const auto& unsafeScope = function.sourceScopes[1];
                  if (unsafeScope.id != scopeId(2) || unsafeScope.parent != scopeId(1) ||
                      !sameSpan(unsafeScope.sourceSpan, unsafeBlockRef.sourceSpan) ||
                      mirBlock.statements[statementIndex].kind() !=
                          MirStatementKind::UnsafeScopeBoundary ||
                      mirBlock.statements[statementIndex + 1].kind() !=
                          MirStatementKind::UnsafeScopeBoundary) {
                    validSequence = false;
                  } else {
                    const auto& enter =
                        mirBlock.statements[statementIndex].unsafeScopeBoundaryValue();
                    const auto& exit =
                        mirBlock.statements[statementIndex + 1].unsafeScopeBoundaryValue();
                    if (enter.kind != MirUnsafeScopeBoundaryKind::Enter ||
                        enter.scope != scopeId(2) ||
                        exit.kind != MirUnsafeScopeBoundaryKind::Exit || exit.scope != scopeId(2) ||
                        !sameSpan(mirBlock.statements[statementIndex].sourceSpan(),
                                  unsafeBlockRef.sourceSpan) ||
                        !sameSpan(mirBlock.statements[statementIndex + 1].sourceSpan(),
                                  unsafeBlockRef.sourceSpan)) {
                      validSequence = false;
                    }
                  }
                }
              }
              ZC_IF_SOME(returnValue, mirBlock.terminator.returnValue().value) {
                if (!matchesPlaceUse(returnValue, proofs, copy, local.type) ||
                    returnValue.place().local() != mirLocal.id ||
                    returnValue.place().rootType() != local.type ||
                    returnValue.place().resultType() != local.type ||
                    returnValue.place().projections().size() != 0) {
                  validSequence = false;
                }
              }
            }
          }
        }
        if (!validSequence) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        auto owner = identities.definition(function.owner);
        auto record = encodeFunction(function, module, identities, semanticTypes);
        if (owner == zc::none || record == zc::none) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, identities, static_cast<uint32_t>(index + 1));
        }
        zc::Array<uint8_t> ownerBytes;
        ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
        if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             identities, static_cast<uint32_t>(index + 1));
        }
        previousOwner = zc::mv(ownerBytes);
        ZC_IF_SOME(value, record) {
          if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
                module, function.owner, identities, static_cast<uint32_t>(index + 1));
          }
          recomputedFunctions.add(zc::mv(value));
        }
        continue;
      }
      hir::HirNodeId returnNode;
      hir::HirNodeId localNode;
      hir::HirNodeId overwriteNode;
      ZC_IF_SOME(value, sourceBlock) {
        if (value.statements.size() == 1) returnNode = value.statements[0];
        if (value.statements.size() == 2) {
          localNode = value.statements[0];
          returnNode = value.statements[1];
        }
        if (value.statements.size() == 3) {
          localNode = value.statements[0];
          overwriteNode = value.statements[1];
          returnNode = value.statements[2];
        }
        if (value.statements.size() >= 4) {
          localNode = value.statements[0];
          overwriteNode = value.statements[1];
          returnNode = value.statements[value.statements.size() - 1];
        }
      }
      auto sourceReturn = returnFor(hirModule, returnNode);
      hir::HirNodeId expressionNode;
      ZC_IF_SOME(value, sourceReturn) { expressionNode = value.value; }
      auto expression = expressionFor(hirModule, expressionNode);
      auto call = callFor(hirModule, expressionNode);
      auto parameterReference = parameterReferenceFor(hirModule, expressionNode);
      auto parameterReborrow = parameterReborrowFor(hirModule, expressionNode);
      auto conditional = conditionalFor(hirModule, expressionNode);
      auto comparisonReturn = primitiveBinaryFor(hirModule, expressionNode);
      auto sourceLocal = localFor(hirModule, localNode);
      auto sourceOverwrite = localWriteFor(hirModule, overwriteNode);
      auto localReference = localReferenceFor(hirModule, expressionNode);
      auto localFieldProjection = localFieldProjectionFor(hirModule, expressionNode);
      auto localBorrow = localBorrowFor(hirModule, expressionNode);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(value, sourceLocal) {
        ZC_IF_SOME(initializer, value.initializer) { initializerNode = initializer; }
      }
      auto initializer = expressionFor(hirModule, initializerNode);
      auto initializerAggregate = aggregateFor(hirModule, initializerNode);
      auto initializerCall = callFor(hirModule, initializerNode);
      auto initializerParameter = parameterReferenceFor(hirModule, initializerNode);
      hir::HirNodeId overwriteValueNode;
      ZC_IF_SOME(value, sourceOverwrite) { overwriteValueNode = value.value; }
      auto overwriteValue = expressionFor(hirModule, overwriteValueNode);
      auto overwriteParameter = parameterReferenceFor(hirModule, overwriteValueNode);
      auto overwriteBinary = primitiveBinaryFor(hirModule, overwriteValueNode);
      const bool isLocalFieldReturn = localFieldProjection != zc::none;
      const bool isParameterReturn = parameterReference != zc::none;
      const bool isParameterReborrow = parameterReborrow != zc::none;
      const bool isLocalBorrow = localBorrow != zc::none;
      const bool isConditionalReturn = conditional != zc::none;
      const bool isComparisonReturn = comparisonReturn != zc::none;
      bool isLocalAliasReborrow = false;
      ZC_IF_SOME(local, sourceLocal) {
        ZC_IF_SOME(reborrow, parameterReborrow) {
          isLocalAliasReborrow = reborrow.sourceAlias != zc::none &&
                                 ZC_ASSERT_NONNULL(reborrow.sourceAlias) == local.local;
        }
      }
      const bool isLocalReturn = !isLocalFieldReturn && !isLocalAliasReborrow && !isLocalBorrow &&
                                 (sourceLocal != zc::none || localReference != zc::none);
      const bool hasLocalWrites =
          sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 3;
      bool uninitializedLocal = false;
      bool initializedByWrite = false;
      ZC_IF_SOME(value, sourceLocal) {
        uninitializedLocal = value.initializer == zc::none && sourceOverwrite == zc::none;
      }
      ZC_IF_SOME(local, sourceLocal) {
        ZC_IF_SOME(write, sourceOverwrite) {
          initializedByWrite =
              local.initializer == zc::none && write.kind == hir::HirLocalWriteKind::Initialize;
        }
      }
      if (sourceBlock == zc::none || sourceReturn == zc::none ||
          (!isLocalFieldReturn && !isLocalReturn && !isParameterReturn && !isParameterReborrow &&
           !isLocalBorrow && !isConditionalReturn && !isComparisonReturn &&
           (expression == zc::none) == (call == zc::none)) ||
          (!isLocalFieldReturn && isParameterReturn &&
           (isLocalReturn || isParameterReborrow || expression != zc::none || call != zc::none)) ||
          (!isLocalFieldReturn && isParameterReborrow &&
           (isLocalReturn || expression != zc::none || call != zc::none)) ||
          (!isLocalFieldReturn && isLocalReturn &&
           (sourceLocal == zc::none || localReference == zc::none ||
            (!uninitializedLocal && !initializedByWrite &&
             ((initializer != zc::none) + (initializerAggregate != zc::none) +
                  (initializerCall != zc::none) + (initializerParameter != zc::none) !=
              1)) ||
            (uninitializedLocal &&
             (initializer != zc::none || initializerAggregate != zc::none ||
              initializerCall != zc::none || initializerParameter != zc::none)) ||
            (initializedByWrite &&
             (initializer != zc::none || initializerAggregate != zc::none ||
              initializerCall != zc::none || initializerParameter != zc::none ||
              overwriteValue == zc::none)) ||
            expression != zc::none || call != zc::none))) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::MissingRequiredFact,
            module, function.owner, identities, static_cast<uint32_t>(index + 1));
      }
      bool valid = false;
      ZC_IF_SOME(block, sourceBlock) {
        ZC_IF_SOME(returnStatement, sourceReturn) {
          ZC_IF_SOME(local, sourceLocal) {
            ZC_IF_SOME(projection, localFieldProjection) {
              if (local.initializer == zc::none && sourceOverwrite == zc::none) {
                valid = validUninitializedLocalFieldReturnFunction(function, sourceDeclaration,
                                                                   block, local, returnStatement,
                                                                   projection, proofs, copy);
              }
              ZC_IF_SOME(write, sourceOverwrite) {
                if (local.initializer == zc::none &&
                    write.kind == hir::HirLocalWriteKind::Initialize) {
                  valid = validInitializedLocalFieldSequenceReturnFunction(
                      function, sourceDeclaration, block, local, hirModule, returnStatement,
                      projection, module, identities, semanticTypes, proofs, copy);
                }
              }
            }
            ZC_IF_SOME(sourceAggregate, initializerAggregate) {
              ZC_IF_SOME(reference, localReference) {
                if (sourceOverwrite == zc::none) {
                  valid = validLocalAggregateReturnFunction(
                      function, sourceDeclaration, block, local, sourceAggregate, returnStatement,
                      reference, module, identities, semanticTypes, proofs, copy);
                }
              }
              ZC_IF_SOME(projection, localFieldProjection) {
                if (!hasLocalWrites) {
                  valid = validLocalAggregateFieldReturnFunction(
                      function, sourceDeclaration, block, local, sourceAggregate, returnStatement,
                      projection, module, identities, semanticTypes, proofs, copy);
                } else {
                  valid = validLocalAggregateFieldOverwriteReturnFunction(
                      function, sourceDeclaration, block, local, sourceAggregate, hirModule,
                      returnStatement, projection, module, identities, semanticTypes, proofs, copy);
                }
              }
            }
            ZC_IF_SOME(overwrite, sourceOverwrite) {
              ZC_IF_SOME(overwriteLiteral, overwriteValue) {
                ZC_IF_SOME(reference, localReference) {
                  if (local.initializer == zc::none &&
                      overwrite.kind == hir::HirLocalWriteKind::Initialize) {
                    valid = validLocalWriteInitializationReturnFunction(
                        function, sourceDeclaration, block, local, overwrite, overwriteLiteral,
                        returnStatement, reference, module, identities, semanticTypes, proofs,
                        copy);
                  }
                }
              }
              ZC_IF_SOME(localInitializer, initializer) {
                ZC_IF_SOME(overwriteLiteral, overwriteValue) {
                  ZC_IF_SOME(reference, localReference) {
                    valid = validLocalOverwriteReturnFunction(
                        function, sourceDeclaration, block, local, localInitializer, overwrite,
                        overwriteLiteral, returnStatement, reference, module, identities,
                        semanticTypes, proofs, copy);
                  }
                }
              }
              // A `mut x = <lit>; x = <param>; return x;` body: the overwrite
              // value is a parameter reference lowered to a place-use of the
              // declared parameter local.
              ZC_IF_SOME(localInitializer, initializer) {
                ZC_IF_SOME(overwriteParameterValue, overwriteParameter) {
                  ZC_IF_SOME(reference, localReference) {
                    valid = validLocalParameterOverwriteReturnFunction(
                        function, sourceDeclaration, block, local, localInitializer, overwrite,
                        overwriteParameterValue, returnStatement, reference, module, identities,
                        semanticTypes, proofs, copy);
                  }
                }
              }
              // A `mut x = <lit>; x = a <op> b; return x;` body: the overwrite
              // value is a primitive binary lowered to an Arithmetic/Comparison
              // rvalue whose operands are constants or place-uses of parameter
              // locals.
              ZC_IF_SOME(localInitializer, initializer) {
                ZC_IF_SOME(overwriteBinaryValue, overwriteBinary) {
                  ZC_IF_SOME(reference, localReference) {
                    valid = validLocalBinaryOverwriteReturnFunction(
                        function, sourceDeclaration, block, local, localInitializer, overwrite,
                        overwriteBinaryValue, returnStatement, reference, hirModule, module,
                        identities, semanticTypes, proofs, copy);
                  }
                }
              }
            }
            ZC_IF_SOME(reference, localReference) {
              if (sourceOverwrite == zc::none && local.initializer == zc::none) {
                valid =
                    validUninitializedLocalReturnFunction(function, sourceDeclaration, block, local,
                                                          returnStatement, reference, proofs, copy);
              }
            }
            ZC_IF_SOME(localInitializer, initializer) {
              if (sourceOverwrite == zc::none) {
                ZC_IF_SOME(reference, localReference) {
                  valid = validLocalReturnFunction(
                      function, hirModule, sourceDeclaration, block, local, localInitializer,
                      returnStatement, reference, module, identities, semanticTypes, proofs, copy);
                }
              }
            }
            ZC_IF_SOME(localCall, initializerCall) {
              ZC_IF_SOME(reference, localReference) {
                valid = validLocalCallReturnFunction(function, sourceDeclaration, block, local,
                                                     localCall, returnStatement, reference, proofs,
                                                     copy, module, identities, semanticTypes);
              }
            }
            ZC_IF_SOME(localParameter, initializerParameter) {
              ZC_IF_SOME(reference, localReference) {
                valid = validParameterLocalReturnFunction(function, sourceDeclaration, block, local,
                                                          localParameter, returnStatement,
                                                          reference, proofs, copy);
              }
              ZC_IF_SOME(reborrow, parameterReborrow) {
                if (isLocalAliasReborrow) {
                  valid = validLocalAliasReborrowReturnFunction(
                      function, hirModule, sourceDeclaration, block, local, localParameter,
                      returnStatement, reborrow, proofs, copy);
                }
              }
            }
            ZC_IF_SOME(borrow, localBorrow) {
              valid = validLocalBorrowReturnFunction(function, hirModule, sourceDeclaration, block,
                                                     local, returnStatement, borrow, proofs, copy);
            }
          }
          ZC_IF_SOME(sourceExpression, expression) {
            valid = validScalarReturnFunction(function, hirModule, sourceDeclaration, block,
                                              returnStatement, sourceExpression, module, identities,
                                              semanticTypes);
          }
          ZC_IF_SOME(sourceCall, call) {
            valid = validDirectCallReturnFunction(function, sourceDeclaration, block,
                                                  returnStatement, sourceCall, proofs, copy, module,
                                                  identities, semanticTypes);
          }
          ZC_IF_SOME(sourceParameter, parameterReference) {
            valid = validParameterReturnFunction(function, sourceDeclaration, block,
                                                 returnStatement, sourceParameter, proofs, copy);
          }
          ZC_IF_SOME(sourceReborrow, parameterReborrow) {
            if (!isLocalAliasReborrow) {
              valid = validParameterReborrowReturnFunction(function, hirModule, sourceDeclaration,
                                                           block, returnStatement, sourceReborrow,
                                                           proofs, copy);
            }
          }
          ZC_IF_SOME(sourceConditional, conditional) {
            // Each arm resolves to a scalar-literal expression or a parameter
            // reference; exactly one lookup succeeds per arm.
            ConditionalArmView thenArm{
                expressionFor(hirModule, sourceConditional.thenReturnValue),
                parameterReferenceFor(hirModule, sourceConditional.thenReturnValue)};
            ConditionalArmView elseArm{
                expressionFor(hirModule, sourceConditional.elseReturnValue),
                parameterReferenceFor(hirModule, sourceConditional.elseReturnValue)};
            const bool thenOk = (thenArm.literal != zc::none) != (thenArm.parameter != zc::none);
            const bool elseOk = (elseArm.literal != zc::none) != (elseArm.parameter != zc::none);
            // The condition node resolves to either a bare parameter reference or
            // an equality comparison; dispatch to the matching verifier shape.
            auto conditionRef = parameterReferenceFor(hirModule, sourceConditional.condition);
            auto equality = primitiveBinaryFor(hirModule, sourceConditional.condition);
            ZC_IF_SOME(condRef, conditionRef) {
              if (thenOk && elseOk) {
                valid = validConditionalReturnFunction(
                    function, sourceDeclaration, block, returnStatement, sourceConditional, condRef,
                    thenArm, elseArm, proofs, copy, module, identities, semanticTypes);
              }
            }
            ZC_IF_SOME(equalityValue, equality) {
              if (thenOk && elseOk) {
                valid = validEqualityConditionalReturnFunction(
                    function, hirModule, sourceDeclaration, block, returnStatement,
                    sourceConditional, equalityValue, thenArm, elseArm, proofs, copy, module,
                    identities, semanticTypes);
              }
            }
          }
          ZC_IF_SOME(sourceComparison, comparisonReturn) {
            valid = validComparisonReturnFunction(function, hirModule, sourceDeclaration, block,
                                                  returnStatement, sourceComparison, proofs, copy,
                                                  module, identities, semanticTypes);
          }
        }
      }
      if (!valid) {
        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                           ir::IrFailureKind::InvalidFact, module, function.owner,
                                           identities, static_cast<uint32_t>(index + 1));
      }
      auto owner = identities.definition(function.owner);
      auto record = encodeFunction(function, module, identities, semanticTypes);
      if (owner == zc::none || record == zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
            module, function.owner, identities, static_cast<uint32_t>(index + 1));
      }
      zc::Array<uint8_t> ownerBytes;
      ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
      if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                           ir::IrFailureKind::InvalidFact, module, function.owner,
                                           identities, static_cast<uint32_t>(index + 1));
      }
      previousOwner = zc::mv(ownerBytes);
      ZC_IF_SOME(value, record) {
        if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, identities, static_cast<uint32_t>(index + 1));
        }
        recomputedFunctions.add(zc::mv(value));
      }
      continue;
    }
    ZC_UNREACHABLE
  }

  auto moduleKey = identities.module(module);
  zc::Maybe<MirRevisionId> recomputedRevision;
  if (moduleKey != zc::none) {
    ZC_IF_SOME(key, moduleKey) {
      auto expanded = key.key().encode();
      recomputedRevision = MirRevisionCodec::computeBuilt(
          hirModule.contextFingerprint(), expanded.asPtr(), hirModule.checkedFactsRevision(),
          hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(),
          recomputedFunctions.asPtr());
    }
  }
  if (recomputedRevision == zc::none) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::CanonicalCodecMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }
  bool revisionMatches = false;
  ZC_IF_SOME(value, recomputedRevision) {
    revisionMatches = value.digest() == candidate.revision.digest();
  }
  if (!revisionMatches) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }
  const auto resolvedEvidence = borrowCapability.lookup(hirModule.borrowEvidenceLease());
  if (!resolvedEvidence.isResolved() || resolvedEvidence.evidence().revision().digest() !=
                                            hirModule.borrowEvidenceRevision().digest()) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), identities, 0);
  }
  auto impl = zc::heap<VerifiedBuiltMir::Impl>(
      hirModule.semanticContext(), hirModule.contextFingerprint().clone(),
      hirModule.compilationUnit(), hirModule.crate(), module, hirModule.checkedFactsRevision(),
      hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(),
      hirModule.retainAdmittedBoundModule(), hirModule.retainIdentityAuthority(),
      hirModule.borrowEvidenceLease().clone(), hirModule.borrowEvidenceCapability(),
      zc::mv(candidate.functions), zc::mv(recomputedFunctions), candidate.revision);
  return ir::IrOperationResult<VerifiedBuiltMir>::verified(VerifiedBuiltMir(zc::mv(impl)));
}

}  // namespace zomlang::compiler::mir
