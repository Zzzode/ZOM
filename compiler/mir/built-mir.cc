// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/built-mir.h"

#include "compiler/checker/body/marker-proof.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/driver/core/marker-authority.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/mir/build/mir-builder.h"
#include "compiler/ownership/admission/surface-admission.h"
#include "compiler/type/semantic-type-store.h"

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

// Fails Mir construction as a per-definition *capability* rejection projected
// to a user diagnostic, rather than as an internal invariant. Used for
// type-checked source constructs the current MIR lowering slice cannot emit
// (RFC 0048); definition owner with no site is legal at MirConstruction.
template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectMirCapability(
    ir::IrFailureKind kind, identity::DefId definition,
    const checker::CheckerIdentityAuthority& identities, zc::Maybe<identity::SourceSpan> span) {
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::MirConstruction,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Mir capability failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::CapabilityRejected, ir::IrFailurePhase::MirConstruction, kind,
      ir::IrFailureOwner::definition(definition), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(span), zc::Vector<uint32_t>(), 0);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    ZC_IREQUIRE(admitted.is<ir::AcceptedIrFailureDescriptor>(),
                "Mir capability rejection must admit without identity expansion");
    zc::Vector<ir::IrFailureFact> facts;
    facts.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedCapabilityFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::capabilityRejected(zc::mv(values));
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

zc::Maybe<const hir::HirParameterFieldProjectionExpression&> parameterFieldProjectionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirParameterFieldProjectionExpression&> result;
  for (const auto& projection : module.parameterFieldProjections()) {
    if (projection.node != node) continue;
    if (result != zc::none) return zc::none;
    result = projection;
  }
  return result;
}

zc::Maybe<const hir::HirParameterFieldWriteStatement&> parameterFieldWriteFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  zc::Maybe<const hir::HirParameterFieldWriteStatement&> result;
  for (const auto& write : module.parameterFieldWrites()) {
    if (write.node != node) continue;
    if (result != zc::none) return zc::none;
    result = write;
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

// True when a HIR block is a leading-`let`-then-`return` body lowered by the
// sequential N-local rail: N leading local bindings followed by a single return.
// N>=2 bodies always qualify. An N==1 (two-statement) body qualifies only when
// its single binding's initializer is a primitive binary, which is the only
// single-local initializer owned by the sequential rail; a literal, aggregate,
// or reference single-local keeps the dedicated single-local shape.
bool isSequentialLocalReturnBlock(const hir::VerifiedHirModule& module,
                                  const hir::HirBlockStatement& block) {
  if (block.statements.size() < 2) return false;
  const size_t bindingCount = block.statements.size() - 1;
  for (size_t i = 0; i < bindingCount; ++i) {
    if (localFor(module, block.statements[i]) == zc::none) return false;
  }
  if (returnFor(module, block.statements[bindingCount]) == zc::none) return false;
  // A trailing conditional return is the leading-local comparison shape, not a
  // sequential-local return. Both have K leading locals, but the leading-local
  // shape pools its comparison into the shared conditional term and reads its
  // operands as the comparison's leaves, so it is not a sequential body.
  {
    auto tailReturn = returnFor(module, block.statements[bindingCount]);
    ZC_IF_SOME(statement, tailReturn) {
      if (conditionalFor(module, statement.value) != zc::none) return false;
    }
  }
  if (bindingCount >= 2) return true;
  auto localBinding = localFor(module, block.statements[0]);
  bool binaryInitializer = false;
  ZC_IF_SOME(local, localBinding) {
    ZC_IF_SOME(initializer, local.initializer) {
      binaryInitializer = primitiveBinaryFor(module, initializer) != zc::none ||
                          conditionalFor(module, initializer) != zc::none;
    }
  }
  return binaryInitializer;
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
// arithmetic operator. The twelve arithmetic and bitwise binary operators of
// same-typed scalars are lowerable, as are the two logical short-circuit
// operators (they lower to bitwise And/Or because the admitted slice only
// accepts side-effect-free operands); the six relational comparisons return
// none so their existing handling stands.
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
    case checker::PrimitiveOperation::LogicalAnd:
      return MirArithmeticOperator::BitAnd;
    case checker::PrimitiveOperation::LogicalOr:
      return MirArithmeticOperator::BitOr;
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
  const bool isMethod = declaration.receiver != zc::none;
  const auto expectedSourceKind =
      isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != expectedSourceKind ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) ||
      function.sourceScopes.size() != (hasUnsafeBlock ? 2 : 1) ||
      function.locals.size() != (isMethod ? 1 : 0) || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != expression.node ||
      sourceReturn.resultType != declaration.resultType ||
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
  // An inherent method carries its implicit `this` receiver as the single
  // leading parameter local; the admitted scalar body never reads it.
  if (isMethod) {
    const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
    const auto& receiverLocal = function.locals[0];
    if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
        receiverLocal.type != receiver.type || receiverLocal.sourceScope != scope.id ||
        !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
      return false;
    }
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

bool validReceiverFieldReturnFunction(const MirFunction& function,
                                      const hir::HirFunctionDeclaration& declaration,
                                      const hir::HirBlockStatement& sourceBlock,
                                      const hir::HirReturnStatement& sourceReturn,
                                      const hir::HirParameterFieldProjectionExpression& projection,
                                      checker::marker::MarkerProofEngine& proofs,
                                      identity::DefId copy) {
  if (declaration.receiver == zc::none) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != projection.node ||
      sourceReturn.resultType != declaration.resultType ||
      projection.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 0 ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& receiverLocal = function.locals[0];
  if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
      receiverLocal.type != receiver.type || receiverLocal.sourceScope != scope.id ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn =
        matchesPlaceUse(value, proofs, copy, projection.type) &&
        value.place().local() == localId(1) && value.place().rootType() == receiver.type &&
        value.place().resultType() == projection.type && value.place().projections().size() == 2 &&
        value.place().projections()[0].kind() == MirProjectionKind::Dereference &&
        value.place().projections()[0].inputType() == receiver.type &&
        value.place().projections()[0].resultType() == projection.receiverType &&
        value.place().projections()[1].kind() == MirProjectionKind::Field &&
        value.place().projections()[1].fieldValue().field == projection.field &&
        value.place().projections()[1].inputType() == projection.receiverType &&
        value.place().projections()[1].resultType() == projection.type;
  }
  return validReturn;
}

bool validByValueParameterFieldReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterFieldProjectionExpression& projection,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy) {
  if (declaration.receiver != zc::none) return false;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.parameters.size() != 1 || declaration.parameters[0].key != projection.parameter ||
      declaration.parameters[0].type != projection.receiverType ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != projection.node ||
      sourceReturn.resultType != declaration.resultType ||
      projection.type != declaration.resultType) {
    return false;
  }
  const auto& parameter = declaration.parameters[0];
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 0 ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& parameterLocal = function.locals[0];
  if (parameterLocal.id != localId(1) || parameterLocal.kind != MirLocalKind::Parameter ||
      parameterLocal.type != parameter.type || parameterLocal.sourceScope != scope.id ||
      !sameSpan(parameterLocal.sourceSpan, parameter.sourceSpan)) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn =
        matchesPlaceUse(value, proofs, copy, projection.type) &&
        value.place().local() == localId(1) && value.place().rootType() == parameter.type &&
        value.place().resultType() == projection.type && value.place().projections().size() == 1 &&
        value.place().projections()[0].kind() == MirProjectionKind::Field &&
        value.place().projections()[0].fieldValue().field == projection.field &&
        value.place().projections()[0].inputType() == projection.receiverType &&
        value.place().projections()[0].resultType() == projection.type;
  }
  return validReturn;
}

bool validByValueAggregateCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn, const hir::HirDirectCallExpression& call,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (declaration.receiver != zc::none) return false;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 2 ||
      declaration.parameters.size() != 0 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 2 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != aggregate.node ||
      sourceReturn.value != call.node || sourceLocal.local.ordinal() != 1 ||
      sourceLocal.type != aggregate.type || aggregate.category != hir::HirValueCategory::Value ||
      call.resultType != declaration.resultType || call.arguments.size() != 1 ||
      call.arguments[0].value != zc::none || call.arguments[0].parameter != zc::none ||
      call.arguments[0].local != sourceLocal.local || call.arguments[0].type != sourceLocal.type) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& userLocal = function.locals[0];
  const auto& resultLocal = function.locals[1];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || userLocal.id != localId(1) ||
      userLocal.kind != MirLocalKind::UserLocal || userLocal.type != sourceLocal.type ||
      userLocal.sourceScope != scope.id ||
      !sameSpan(userLocal.sourceSpan, sourceLocal.sourceSpan) || resultLocal.id != localId(2) ||
      resultLocal.kind != MirLocalKind::Temporary || resultLocal.type != call.resultType ||
      resultLocal.sourceScope != scope.id || !sameSpan(resultLocal.sourceSpan, call.sourceSpan) ||
      entry.id != blockId(1) || entry.sourceScope != scope.id || entry.statements.size() != 3 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != userLocal.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      entry.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      !sameSpan(entry.statements[1].sourceSpan(), aggregate.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != resultLocal.id ||
      !sameSpan(entry.statements[2].sourceSpan(), call.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = entry.statements[1].assignmentValue();
  if (assignment.destination.local() != userLocal.id ||
      assignment.destination.rootType() != userLocal.type ||
      assignment.destination.resultType() != userLocal.type ||
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
  const auto& terminator = entry.terminator.callValue();
  if (terminator.callee != call.callee || terminator.arguments.size() != 1 ||
      terminator.destination.local() != resultLocal.id ||
      terminator.destination.rootType() != resultLocal.type ||
      terminator.destination.resultType() != resultLocal.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::NoActivation ||
      terminator.normalTarget != continuation.id || terminator.unwindTarget != zc::none) {
    return false;
  }
  const auto& actualArgument = terminator.arguments[0];
  if (!matchesPlaceUse(actualArgument, proofs, copy, sourceLocal.type) ||
      actualArgument.place().local() != userLocal.id ||
      actualArgument.place().rootType() != userLocal.type ||
      actualArgument.place().resultType() != userLocal.type ||
      actualArgument.place().projections().size() != 0) {
    return false;
  }
  ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, resultLocal.type) &&
           value.place().local() == resultLocal.id &&
           value.place().rootType() == resultLocal.type &&
           value.place().resultType() == resultLocal.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validScalarLocalCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer, const hir::HirReturnStatement& sourceReturn,
    const hir::HirDirectCallExpression& call, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (declaration.receiver != zc::none) return false;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 2 ||
      declaration.parameters.size() != 0 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 2 || sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node || sourceReturn.value != call.node ||
      sourceLocal.local.ordinal() != 1 || sourceLocal.type != initializer.type ||
      initializer.category != hir::HirValueCategory::Value ||
      call.resultType != declaration.resultType || call.arguments.size() != 1 ||
      call.arguments[0].value != zc::none || call.arguments[0].parameter != zc::none ||
      call.arguments[0].local != sourceLocal.local || call.arguments[0].type != sourceLocal.type) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& userLocal = function.locals[0];
  const auto& resultLocal = function.locals[1];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || userLocal.id != localId(1) ||
      userLocal.kind != MirLocalKind::UserLocal || userLocal.type != sourceLocal.type ||
      userLocal.sourceScope != scope.id ||
      !sameSpan(userLocal.sourceSpan, sourceLocal.sourceSpan) || resultLocal.id != localId(2) ||
      resultLocal.kind != MirLocalKind::Temporary || resultLocal.type != call.resultType ||
      resultLocal.sourceScope != scope.id || !sameSpan(resultLocal.sourceSpan, call.sourceSpan) ||
      entry.id != blockId(1) || entry.sourceScope != scope.id || entry.statements.size() != 3 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != userLocal.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      entry.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      !sameSpan(entry.statements[1].sourceSpan(), initializer.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != resultLocal.id ||
      !sameSpan(entry.statements[2].sourceSpan(), call.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = entry.statements[1].assignmentValue();
  if (assignment.destination.local() != userLocal.id ||
      assignment.destination.rootType() != userLocal.type ||
      assignment.destination.resultType() != userLocal.type ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::Use) {
    return false;
  }
  const auto& use = assignment.value.useValue();
  if (use.operand.kind() != MirOperandKind::Constant ||
      use.operand.constantValue().type != sourceLocal.type ||
      !sameConstant(use.operand.constantValue().value, initializer.value, module, identities,
                    semanticTypes)) {
    return false;
  }
  const auto& terminator = entry.terminator.callValue();
  if (terminator.callee != call.callee || terminator.arguments.size() != 1 ||
      terminator.destination.local() != resultLocal.id ||
      terminator.destination.rootType() != resultLocal.type ||
      terminator.destination.resultType() != resultLocal.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::NoActivation ||
      terminator.normalTarget != continuation.id || terminator.unwindTarget != zc::none) {
    return false;
  }
  const auto& actualArgument = terminator.arguments[0];
  if (!matchesPlaceUse(actualArgument, proofs, copy, sourceLocal.type) ||
      actualArgument.place().local() != userLocal.id ||
      actualArgument.place().rootType() != userLocal.type ||
      actualArgument.place().resultType() != userLocal.type ||
      actualArgument.place().projections().size() != 0) {
    return false;
  }
  ZC_IF_SOME(value, continuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, resultLocal.type) &&
           value.place().local() == resultLocal.id &&
           value.place().rootType() == resultLocal.type &&
           value.place().resultType() == resultLocal.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validReceiverFieldWriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock,
    const hir::HirParameterFieldWriteStatement& sourceWrite,
    const hir::HirScalarLiteralExpression& writeLiteral,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterFieldProjectionExpression& projection,
    const hir::VerifiedHirModule& hirModule, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (declaration.receiver == zc::none) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 1 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceWrite.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceReturn.value != projection.node ||
      sourceReturn.resultType != declaration.resultType || sourceWrite.value != writeLiteral.node ||
      sourceWrite.type != declaration.resultType || sourceWrite.parameter != receiver.key ||
      sourceWrite.field != projection.field || writeLiteral.type != declaration.resultType ||
      projection.type != declaration.resultType || projection.parameter != receiver.key) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 1 ||
      block.statements[0].kind() != MirStatementKind::Assign ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.statements[0].sourceSpan(), sourceWrite.sourceSpan) ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& receiverLocal = function.locals[0];
  if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
      receiverLocal.type != receiver.type || receiverLocal.sourceScope != scope.id ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
    return false;
  }
  const auto& assign = block.statements[0].assignmentValue();
  if (assign.initialization != MirInitializationKind::Overwrite ||
      assign.destination.local() != localId(1) || assign.destination.rootType() != receiver.type ||
      assign.destination.resultType() != projection.type ||
      assign.destination.projections().size() != 2 ||
      assign.destination.projections()[0].kind() != MirProjectionKind::Dereference ||
      assign.destination.projections()[0].inputType() != receiver.type ||
      assign.destination.projections()[0].resultType() != projection.receiverType ||
      assign.destination.projections()[1].kind() != MirProjectionKind::Field ||
      assign.destination.projections()[1].fieldValue().field != projection.field ||
      assign.destination.projections()[1].inputType() != projection.receiverType ||
      assign.destination.projections()[1].resultType() != projection.type ||
      assign.value.kind() != MirRvalueKind::Use ||
      assign.value.useValue().operand.kind() != MirOperandKind::Constant ||
      assign.value.useValue().operand.constantValue().type != writeLiteral.type ||
      !sameConstant(assign.value.useValue().operand.constantValue().value, writeLiteral.value,
                    module, identities, semanticTypes)) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn =
        matchesPlaceUse(value, proofs, copy, projection.type) &&
        value.place().local() == localId(1) && value.place().rootType() == receiver.type &&
        value.place().resultType() == projection.type && value.place().projections().size() == 2 &&
        value.place().projections()[0].kind() == MirProjectionKind::Dereference &&
        value.place().projections()[0].inputType() == receiver.type &&
        value.place().projections()[0].resultType() == projection.receiverType &&
        value.place().projections()[1].kind() == MirProjectionKind::Field &&
        value.place().projections()[1].fieldValue().field == projection.field &&
        value.place().projections()[1].inputType() == projection.receiverType &&
        value.place().projections()[1].resultType() == projection.type;
  }
  return validReturn;
}

/// \brief Validates the void mutating-receiver write shape
/// `mutating fn m(this, x: T) { this.field = x; }` with a Unit result. Two
/// parameter locals (receiver then ordinary parameter), one Overwrite Assign to
/// [Dereference, Field] with a copy/move place-use of the parameter local, and a
/// Return terminator carrying no value. The terminator span is the body block
/// span (no HirReturnStatement exists for a void body).
bool validReceiverFieldWriteVoidFunction(const MirFunction& function,
                                         const hir::HirFunctionDeclaration& declaration,
                                         const hir::HirBlockStatement& sourceBlock,
                                         const hir::HirParameterFieldWriteStatement& sourceWrite,
                                         const hir::HirParameterReferenceExpression& writeParameter,
                                         const type::SemanticTypeStore& semanticTypes,
                                         checker::marker::MarkerProofEngine& proofs,
                                         identity::DefId copy) {
  if (declaration.receiver == zc::none || declaration.parameters.size() != 1) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  auto unitLookup = semanticTypes.get(declaration.resultType);
  if (!unitLookup.is<type::SemanticTypeLookup>()) return false;
  auto unitKind = unitLookup.get<type::SemanticTypeLookup>().data().primitiveKind();
  if (unitKind == zc::none || ZC_ASSERT_NONNULL(unitKind) != type::semantic::PrimitiveKind::Unit) {
    return false;
  }
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceWrite.node || sourceWrite.value != writeParameter.node ||
      sourceWrite.parameter != receiver.key ||
      writeParameter.parameter != declaration.parameters[0].key ||
      sourceWrite.type != writeParameter.type ||
      writeParameter.type != declaration.parameters[0].type ||
      writeParameter.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || block.id != blockId(1) ||
      block.sourceScope != scope.id || block.statements.size() != 1 ||
      block.statements[0].kind() != MirStatementKind::Assign ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value != zc::none ||
      !sameSpan(block.statements[0].sourceSpan(), sourceWrite.sourceSpan) ||
      !sameSpan(block.terminator.sourceSpan(), sourceBlock.sourceSpan)) {
    return false;
  }
  const auto& receiverLocal = function.locals[0];
  const auto& parameterLocal = function.locals[1];
  if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
      receiverLocal.type != receiver.type || receiverLocal.sourceScope != scope.id ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan) || parameterLocal.id != localId(2) ||
      parameterLocal.kind != MirLocalKind::Parameter ||
      parameterLocal.type != declaration.parameters[0].type ||
      parameterLocal.sourceScope != scope.id ||
      !sameSpan(parameterLocal.sourceSpan, declaration.parameters[0].sourceSpan)) {
    return false;
  }
  const auto& assign = block.statements[0].assignmentValue();
  if (assign.initialization != MirInitializationKind::Overwrite ||
      assign.destination.local() != localId(1) || assign.destination.rootType() != receiver.type ||
      assign.destination.resultType() != sourceWrite.type ||
      assign.destination.projections().size() != 2 ||
      assign.destination.projections()[0].kind() != MirProjectionKind::Dereference ||
      assign.destination.projections()[0].inputType() != receiver.type ||
      assign.destination.projections()[0].resultType() != sourceWrite.receiverType ||
      assign.destination.projections()[1].kind() != MirProjectionKind::Field ||
      assign.destination.projections()[1].fieldValue().field != sourceWrite.field ||
      assign.destination.projections()[1].inputType() != sourceWrite.receiverType ||
      assign.destination.projections()[1].resultType() != sourceWrite.type ||
      assign.value.kind() != MirRvalueKind::Use) {
    return false;
  }
  const auto& operand = assign.value.useValue().operand;
  return matchesPlaceUse(operand, proofs, copy, sourceWrite.type) &&
         operand.place().local() == localId(2) && operand.place().rootType() == sourceWrite.type &&
         operand.place().resultType() == sourceWrite.type &&
         operand.place().projections().size() == 0;
}

// One conditional arm as seen by the MIR verifier: a scalar-literal
// expression, a parameter reference, or a primitive binary operation. Exactly
// one Maybe is populated.
struct ConditionalArmView final {
  zc::Maybe<const hir::HirScalarLiteralExpression&> literal;
  zc::Maybe<const hir::HirParameterReferenceExpression&> parameter;
  zc::Maybe<const hir::HirPrimitiveBinaryExpression&> binary;
};

// Resolves one arm view to its value node id: the literal/parameter node, or
// the binary node for a binary arm.
zc::Maybe<hir::HirNodeId> conditionalArmNode(const ConditionalArmView& arm) {
  ZC_IF_SOME(value, arm.literal) { return value.node; }
  ZC_IF_SOME(value, arm.parameter) { return value.node; }
  ZC_IF_SOME(value, arm.binary) { return value.node; }
  return zc::none;
}

// Resolves one arm view to its semantic type: the literal/parameter type, or
// the binary result type for a binary arm.
zc::Maybe<identity::SemanticTypeId> conditionalArmType(const ConditionalArmView& arm) {
  ZC_IF_SOME(value, arm.literal) { return value.type; }
  ZC_IF_SOME(value, arm.parameter) { return value.type; }
  ZC_IF_SOME(value, arm.binary) { return value.type; }
  return zc::none;
}

// Resolves a parameter reference to its zero-based index in the function's
// parameter list. Returns false if the reference does not match any parameter.
bool parameterLocalIndexFor(const hir::HirFunctionDeclaration& declaration,
                            const hir::HirParameterReferenceExpression& reference,
                            size_t& outIndex) {
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == reference.parameter) {
      outIndex = i;
      return true;
    }
  }
  return false;
}

// Validates that one arm block initializes the result local from the arm value:
// a literal arm assigns a constant, a parameter arm assigns a place-use of the
// parameter local, and a binary arm assigns an Arithmetic/Comparison rvalue
// whose operator and operands match the HIR binary.
bool branchArmInitializesResult(const MirBasicBlock& branch, const ConditionalArmView& arm,
                                const hir::VerifiedHirModule& hirModule,
                                const hir::HirFunctionDeclaration& declaration,
                                MirLocalId resultLocal, checker::marker::MarkerProofEngine& proofs,
                                identity::DefId copy, identity::ModuleId module,
                                const checker::CheckerIdentityAuthority& identities,
                                const type::SemanticTypeStore& semanticTypes) {
  if (branch.statements[0].kind() != MirStatementKind::Assign) { return false; }
  const auto& assignment = branch.statements[0].assignmentValue();
  if (assignment.initialization != MirInitializationKind::Initialize ||
      assignment.destination.local() != resultLocal ||
      assignment.destination.rootType() != declaration.resultType ||
      assignment.destination.resultType() != declaration.resultType ||
      assignment.destination.projections().size() != 0) {
    return false;
  }
  ZC_IF_SOME(literal, arm.literal) {
    if (assignment.value.kind() != MirRvalueKind::Use) return false;
    const auto& operand = assignment.value.useValue().operand;
    return operand.kind() == MirOperandKind::Constant &&
           operand.constantValue().type == literal.type &&
           sameConstant(operand.constantValue().value, literal.value, module, identities,
                        semanticTypes);
  }
  ZC_IF_SOME(parameter, arm.parameter) {
    if (assignment.value.kind() != MirRvalueKind::Use) return false;
    const auto& operand = assignment.value.useValue().operand;
    size_t parameterIndex = 0;
    if (!parameterLocalIndexFor(declaration, parameter, parameterIndex)) return false;
    return matchesPlaceUse(operand, proofs, copy, parameter.type) &&
           operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
           operand.place().rootType() == parameter.type &&
           operand.place().resultType() == parameter.type &&
           operand.place().projections().size() == 0;
  }
  ZC_IF_SOME(binary, arm.binary) {
    // The MIR rvalue kind follows the HIR operation: arithmetic operators lower
    // to Arithmetic, comparison operators to Comparison. Exactly one mapping
    // must succeed.
    const auto expectedArithmetic = mirArithmeticOperatorFor(binary.operation);
    const auto expectedComparison = mirComparisonOperatorFor(binary.operation);
    const bool isArithmetic = expectedArithmetic != zc::none;
    const bool isComparison = expectedComparison != zc::none;
    if (isArithmetic == isComparison) return false;
    const MirOperand* left = nullptr;
    const MirOperand* right = nullptr;
    if (isArithmetic) {
      if (assignment.value.kind() != MirRvalueKind::Arithmetic) return false;
      const auto& arithmetic = assignment.value.arithmeticValue();
      if (arithmetic.op != ZC_ASSERT_NONNULL(expectedArithmetic) ||
          arithmetic.resultType != binary.type) {
        return false;
      }
      left = &arithmetic.left;
      right = &arithmetic.right;
    } else {
      if (assignment.value.kind() != MirRvalueKind::Comparison) return false;
      const auto& comparison = assignment.value.comparisonValue();
      if (comparison.op != ZC_ASSERT_NONNULL(expectedComparison) ||
          comparison.resultType != binary.type) {
        return false;
      }
      left = &comparison.left;
      right = &comparison.right;
    }
    // Each operand matches its HIR leaf: a literal is a Constant of the operand
    // type and value; a parameter is a copy of the parameter local.
    auto operandMatches = [&](const MirOperand& operand, hir::HirNodeId node) -> bool {
      auto literal = expressionFor(hirModule, node);
      auto parameter = parameterReferenceFor(hirModule, node);
      if ((literal != zc::none) == (parameter != zc::none)) return false;
      ZC_IF_SOME(value, literal) {
        return operand.kind() == MirOperandKind::Constant &&
               operand.constantValue().type == binary.operandType &&
               sameConstant(operand.constantValue().value, value.value, module, identities,
                            semanticTypes);
      }
      ZC_IF_SOME(value, parameter) {
        size_t parameterIndex = 0;
        if (!parameterLocalIndexFor(declaration, value, parameterIndex)) return false;
        return matchesPlaceUse(operand, proofs, copy, binary.operandType) &&
               operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
               operand.place().rootType() == binary.operandType &&
               operand.place().resultType() == binary.operandType &&
               operand.place().projections().size() == 0;
      }
      return false;
    };
    return operandMatches(*left, binary.left) && operandMatches(*right, binary.right);
  }
  return false;
}

bool validConditionalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirReturnStatement& sourceReturn, const hir::HirConditionalExpression& conditional,
    const hir::HirParameterReferenceExpression& conditionRef, const ConditionalArmView& thenArm,
    const ConditionalArmView& elseArm, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  // Resolve each arm to its node id and semantic type independent of arm kind.
  auto armNode = [](const ConditionalArmView& arm) -> zc::Maybe<hir::HirNodeId> {
    return conditionalArmNode(arm);
  };
  auto armType = [](const ConditionalArmView& arm) -> zc::Maybe<identity::SemanticTypeId> {
    return conditionalArmType(arm);
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
  size_t conditionIndex = 0;
  if (!parameterLocalIndexFor(declaration, conditionRef, conditionIndex)) return false;
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() !=
          localId(static_cast<uint32_t>(conditionIndex + 1)) ||
      switchInt.discriminant.place().rootType() != conditionRef.type ||
      switchInt.discriminant.place().resultType() != conditionRef.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Each branch initializes the result local with the arm value, then jumps to
  // the join block. The single Return in the join reads that result local.
  if (!branchArmInitializesResult(thenBlock, thenArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes) ||
      !branchArmInitializesResult(elseBlock, elseArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes)) {
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

bool validMethodConditionalReturnFunction(const MirFunction& function,
                                          const hir::HirFunctionDeclaration& declaration,
                                          const hir::HirBlockStatement& sourceBlock,
                                          const hir::HirReturnStatement& sourceReturn,
                                          const hir::HirConditionalExpression& conditional,
                                          const hir::HirParameterReferenceExpression& conditionRef,
                                          const hir::HirScalarLiteralExpression& thenLiteral,
                                          const hir::HirScalarLiteralExpression& elseLiteral,
                                          checker::marker::MarkerProofEngine& proofs,
                                          identity::DefId copy, identity::ModuleId module,
                                          const checker::CheckerIdentityAuthority& identities,
                                          const type::SemanticTypeStore& semanticTypes) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 2 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != conditional.node ||
      sourceReturn.resultType != declaration.resultType ||
      conditional.condition != conditionRef.node ||
      conditional.thenReturnValue != thenLiteral.node ||
      conditional.elseReturnValue != elseLiteral.node ||
      conditional.type != declaration.resultType || thenLiteral.type != declaration.resultType ||
      elseLiteral.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  // Local 1 is the receiver parameter; ordinary parameters are locals 2..N+1.
  const auto& receiverLocal = function.locals[0];
  if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
      receiverLocal.type != receiver.type || receiverLocal.sourceScope != scopeId(1) ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i + 1];
    if (local.id != localId(static_cast<uint32_t>(i + 2)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal = localId(static_cast<uint32_t>(declaration.parameters.size() + 2));
  const auto& result = function.locals[declaration.parameters.size() + 1];
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
  if (switchInt.arms[0].target != blockId(2) || switchInt.arms[1].target != blockId(3)) {
    return false;
  }
  auto trueValue = switchInt.arms[0].value.booleanValue();
  auto falseValue = switchInt.arms[1].value.booleanValue();
  if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
      ZC_ASSERT_NONNULL(falseValue)) {
    return false;
  }
  // The condition resolves to an ordinary parameter at local index + 2.
  size_t conditionIndex = 0;
  bool conditionResolved = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == conditionRef.parameter) {
      conditionIndex = i;
      conditionResolved = true;
      break;
    }
  }
  if (!conditionResolved || conditionRef.type != declaration.parameters[conditionIndex].type) {
    return false;
  }
  const MirLocalId conditionLocal = localId(static_cast<uint32_t>(conditionIndex + 2));
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionLocal ||
      switchInt.discriminant.place().rootType() != conditionRef.type ||
      switchInt.discriminant.place().resultType() != conditionRef.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Each arm Initialize-assigns its literal constant to the result local.
  auto branchInitializesResult = [&](const MirBasicBlock& branch,
                                     const hir::HirScalarLiteralExpression& literal) -> bool {
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
    return operand.kind() == MirOperandKind::Constant &&
           operand.constantValue().type == literal.type &&
           sameConstant(operand.constantValue().value, literal.value, module, identities,
                        semanticTypes);
  };
  if (!branchInitializesResult(thenBlock, thenLiteral) ||
      !branchInitializesResult(elseBlock, elseLiteral)) {
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
    return conditionalArmNode(arm);
  };
  auto armType = [](const ConditionalArmView& arm) -> zc::Maybe<identity::SemanticTypeId> {
    return conditionalArmType(arm);
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
    refsOk &= parameterLocalIndexFor(declaration, value, leftIndex);
  }
  ZC_IF_SOME(value, rightRef) {
    operandType = value.type;
    refsOk &= parameterLocalIndexFor(declaration, value, rightIndex);
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
  // The condition assignment initializes the bool temporary from a Comparison
  // (relational operators) or an Arithmetic (logical short-circuit operators
  // lowered to BitAnd/BitOr) of the two parameter locals of the shared operand
  // type.
  const auto& tempAssign = entry.statements[2].assignmentValue();
  const bool isLogicalCondition = equality.operation == checker::PrimitiveOperation::LogicalAnd ||
                                  equality.operation == checker::PrimitiveOperation::LogicalOr;
  if (tempAssign.initialization != MirInitializationKind::Initialize ||
      tempAssign.destination.local() != conditionTemp ||
      tempAssign.destination.rootType() != equality.type ||
      tempAssign.destination.resultType() != equality.type ||
      tempAssign.destination.projections().size() != 0 ||
      tempAssign.value.kind() !=
          (isLogicalCondition ? MirRvalueKind::Arithmetic : MirRvalueKind::Comparison)) {
    return false;
  }
  // The MIR operator must be the one mapped from the HIR-carried operation;
  // any other byte is a lowering defect.
  const MirOperand* conditionLeft = nullptr;
  const MirOperand* conditionRight = nullptr;
  if (isLogicalCondition) {
    const auto& arithmetic = tempAssign.value.arithmeticValue();
    auto expectedOperator = mirArithmeticOperatorFor(equality.operation);
    if (expectedOperator == zc::none || arithmetic.op != ZC_ASSERT_NONNULL(expectedOperator) ||
        arithmetic.resultType != equality.type) {
      return false;
    }
    conditionLeft = &arithmetic.left;
    conditionRight = &arithmetic.right;
  } else {
    const auto& comparison = tempAssign.value.comparisonValue();
    auto expectedOperator = mirComparisonOperatorFor(equality.operation);
    if (expectedOperator == zc::none || comparison.op != ZC_ASSERT_NONNULL(expectedOperator) ||
        comparison.resultType != equality.type) {
      return false;
    }
    conditionLeft = &comparison.left;
    conditionRight = &comparison.right;
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
  if (!operandMatches(*conditionLeft, leftLiteral, leftRef, leftIndex) ||
      !operandMatches(*conditionRight, rightLiteral, rightRef, rightIndex)) {
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
  if (!branchArmInitializesResult(thenBlock, thenArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes) ||
      !branchArmInitializesResult(elseBlock, elseArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes)) {
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

// Verifies a chained conditional return: a nested chain of N equality
// conditionals with literal then-arms and one literal else-arm, lowering to
// 2N+2 blocks (N entry/then-arm pairs, one else arm, one join). Each entry
// block evaluates one equality comparison into a bool temporary and switches
// on it; each then-arm block initializes the result local from the arm literal
// and jumps to the join; the else block does the same for the default value.
bool validChainedConditionalReturnFunction(const MirFunction& function,
                                           const hir::VerifiedHirModule& hirModule,
                                           const hir::HirFunctionDeclaration& declaration,
                                           const hir::HirBlockStatement& sourceBlock,
                                           const hir::HirReturnStatement& sourceReturn,
                                           const hir::HirConditionalExpression& outerConditional,
                                           checker::marker::MarkerProofEngine& proofs,
                                           identity::DefId copy, identity::ModuleId module,
                                           const checker::CheckerIdentityAuthority& identities,
                                           const type::SemanticTypeStore& semanticTypes) {
  // Walk the nested conditional chain to extract N entries (equality + then
  // literal) and one else literal. The chain is outer-to-inner.
  struct ChainedEntry {
    const hir::HirPrimitiveBinaryExpression* equality;
    const hir::HirScalarLiteralExpression* thenLiteral;
  };
  zc::Vector<ChainedEntry> entries;
  const hir::HirScalarLiteralExpression* elseLiteral = nullptr;
  hir::HirNodeId currentNode = outerConditional.node;
  while (true) {
    auto conditional = conditionalFor(hirModule, currentNode);
    if (conditional == zc::none) return false;
    const auto& cond = ZC_ASSERT_NONNULL(conditional);
    if (cond.type != declaration.resultType || cond.category != hir::HirValueCategory::Value) {
      return false;
    }
    auto equality = primitiveBinaryFor(hirModule, cond.condition);
    if (equality == zc::none) return false;
    const auto& cmp = ZC_ASSERT_NONNULL(equality);
    auto cmpOp = mirComparisonOperatorFor(cmp.operation);
    if (cmpOp == zc::none || ZC_ASSERT_NONNULL(cmpOp) != MirComparisonOperator::Eq) {
      return false;
    }
    auto thenLit = expressionFor(hirModule, cond.thenReturnValue);
    if (thenLit == zc::none) return false;
    if (ZC_ASSERT_NONNULL(thenLit).type != declaration.resultType) return false;
    entries.add(ChainedEntry{&cmp, &ZC_ASSERT_NONNULL(thenLit)});
    auto elseExpr = expressionFor(hirModule, cond.elseReturnValue);
    if (elseExpr != zc::none) {
      if (ZC_ASSERT_NONNULL(elseExpr).type != declaration.resultType) return false;
      elseLiteral = &ZC_ASSERT_NONNULL(elseExpr);
      break;
    }
    currentNode = cond.elseReturnValue;
  }
  if (entries.size() < 2 || elseLiteral == nullptr) return false;
  const size_t armCount = entries.size();

  // Function header and local layout: parameters + result + N condition temps.
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 1 + armCount ||
      function.blocks.size() != 2 * armCount + 2 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 1 || sourceBlock.statements[0] != sourceReturn.node ||
      sourceReturn.value != outerConditional.node ||
      sourceReturn.resultType != declaration.resultType) {
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
  for (size_t i = 0; i < armCount; ++i) {
    const auto& temp = function.locals[declaration.parameters.size() + 1 + i];
    const auto tempId = localId(static_cast<uint32_t>(declaration.parameters.size() + 2 + i));
    if (temp.id != tempId || temp.kind != MirLocalKind::Temporary ||
        temp.type != entries[i].equality->type || temp.sourceScope != scopeId(1) ||
        !sameSpan(temp.sourceSpan, entries[i].equality->sourceSpan)) {
      return false;
    }
  }

  // Resolve the scrutinee parameter index from the first equality's left
  // operand (all arms share the same scrutinee; the right operand is the
  // pattern literal).
  auto leftRef = parameterReferenceFor(hirModule, entries[0].equality->left);
  auto rightLiteral = expressionFor(hirModule, entries[0].equality->right);
  if (leftRef == zc::none || rightLiteral == zc::none) return false;
  const auto& scrutineeRef = ZC_ASSERT_NONNULL(leftRef);
  size_t scrutineeIndex = 0;
  bool scrutineeFound = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == scrutineeRef.parameter) {
      scrutineeIndex = i;
      scrutineeFound = true;
      break;
    }
  }
  if (!scrutineeFound) return false;
  const auto scrutineeLocal = localId(static_cast<uint32_t>(scrutineeIndex + 1));
  const identity::SemanticTypeId operandType = scrutineeRef.type;

  const uint32_t joinBlockOrdinal = static_cast<uint32_t>(2 * armCount + 2);

  for (size_t k = 0; k < armCount; ++k) {
    const auto& entry = entries[k];
    const auto& entryBlock = function.blocks[2 * k];
    const auto& thenBlock = function.blocks[2 * k + 1];

    // Entry block: k=0 has StorageLive(result) + StorageLive for every
    // condition temp + Assign(temp_0, ...); k>0 has just Assign(temp_k, ...).
    const size_t expectedStmtCount = (k == 0) ? 2 + armCount : 1;
    const size_t assignOffset = (k == 0) ? 1 + armCount : 0;
    if (entryBlock.id != blockId(static_cast<uint32_t>(2 * k + 1)) ||
        entryBlock.sourceScope != scopeId(1) || entryBlock.statements.size() != expectedStmtCount ||
        entryBlock.terminator.kind() != MirTerminatorKind::SwitchInt) {
      return false;
    }
    if (k == 0) {
      if (entryBlock.statements[0].kind() != MirStatementKind::StorageLive ||
          entryBlock.statements[0].storageLocal() != resultLocal ||
          !sameSpan(entryBlock.statements[0].sourceSpan(), sourceReturn.sourceSpan)) {
        return false;
      }
      for (size_t t = 0; t < armCount; ++t) {
        const auto tempId = localId(static_cast<uint32_t>(declaration.parameters.size() + 2 + t));
        if (entryBlock.statements[1 + t].kind() != MirStatementKind::StorageLive ||
            entryBlock.statements[1 + t].storageLocal() != tempId ||
            !sameSpan(entryBlock.statements[1 + t].sourceSpan(), entries[t].equality->sourceSpan)) {
          return false;
        }
      }
    }
    const auto tempId = localId(static_cast<uint32_t>(declaration.parameters.size() + 2 + k));
    const auto& assign = entryBlock.statements[assignOffset];
    if (assign.kind() != MirStatementKind::Assign ||
        !sameSpan(assign.sourceSpan(), entry.equality->sourceSpan)) {
      return false;
    }
    const auto& assignValue = assign.assignmentValue();
    if (assignValue.initialization != MirInitializationKind::Initialize ||
        assignValue.destination.local() != tempId ||
        assignValue.destination.rootType() != entry.equality->type ||
        assignValue.destination.resultType() != entry.equality->type ||
        assignValue.destination.projections().size() != 0 ||
        assignValue.value.kind() != MirRvalueKind::Comparison) {
      return false;
    }
    const auto& comparison = assignValue.value.comparisonValue();
    if (comparison.op != MirComparisonOperator::Eq ||
        comparison.resultType != entry.equality->type) {
      return false;
    }
    // Left operand is the scrutinee parameter; right is the pattern literal.
    if (comparison.left.kind() != MirOperandKind::Copy ||
        comparison.left.place().local() != scrutineeLocal ||
        comparison.left.place().rootType() != operandType ||
        comparison.left.place().resultType() != operandType ||
        comparison.left.place().projections().size() != 0 ||
        !matchesPlaceUse(comparison.left, proofs, copy, operandType)) {
      return false;
    }
    // The pattern literal must match the HIR-carried literal.
    auto patternLiteral = expressionFor(hirModule, entry.equality->right);
    if (patternLiteral == zc::none) return false;
    if (comparison.right.kind() != MirOperandKind::Constant ||
        comparison.right.constantValue().type != operandType ||
        !sameConstant(comparison.right.constantValue().value,
                      ZC_ASSERT_NONNULL(patternLiteral).value, module, identities, semanticTypes)) {
      return false;
    }

    // SwitchInt: true -> then block, false -> next entry or else.
    const auto& switchInt = entryBlock.terminator.switchIntValue();
    const uint32_t thenBlockOrdinal = static_cast<uint32_t>(2 * k + 2);
    const uint32_t elseBlockOrdinal = (k + 1 < armCount) ? static_cast<uint32_t>(2 * (k + 1) + 1)
                                                         : static_cast<uint32_t>(2 * armCount + 1);
    if (switchInt.arms.size() != 2 || switchInt.defaultTarget != blockId(elseBlockOrdinal)) {
      return false;
    }
    const auto& trueArm = switchInt.arms[0];
    const auto& falseArm = switchInt.arms[1];
    if (trueArm.target != blockId(thenBlockOrdinal) ||
        falseArm.target != blockId(elseBlockOrdinal)) {
      return false;
    }
    auto trueValue = trueArm.value.booleanValue();
    auto falseValue = falseArm.value.booleanValue();
    if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
        ZC_ASSERT_NONNULL(falseValue)) {
      return false;
    }
    if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
        switchInt.discriminant.place().local() != tempId ||
        switchInt.discriminant.place().rootType() != entry.equality->type ||
        switchInt.discriminant.place().resultType() != entry.equality->type ||
        switchInt.discriminant.place().projections().size() != 0) {
      return false;
    }

    // Then arm block: Assign(result = literal_k), Goto join.
    if (thenBlock.id != blockId(thenBlockOrdinal) || thenBlock.sourceScope != scopeId(1) ||
        thenBlock.statements.size() != 1 ||
        thenBlock.terminator.kind() != MirTerminatorKind::Goto ||
        thenBlock.terminator.gotoValue().target != blockId(joinBlockOrdinal)) {
      return false;
    }
    const auto& thenAssign = thenBlock.statements[0];
    if (thenAssign.kind() != MirStatementKind::Assign ||
        !sameSpan(thenAssign.sourceSpan(), entry.thenLiteral->sourceSpan)) {
      return false;
    }
    const auto& thenAssignValue = thenAssign.assignmentValue();
    if (thenAssignValue.initialization != MirInitializationKind::Initialize ||
        thenAssignValue.destination.local() != resultLocal ||
        thenAssignValue.destination.rootType() != declaration.resultType ||
        thenAssignValue.destination.resultType() != declaration.resultType ||
        thenAssignValue.destination.projections().size() != 0 ||
        thenAssignValue.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& thenOperand = thenAssignValue.value.useValue().operand;
    if (thenOperand.kind() != MirOperandKind::Constant ||
        thenOperand.constantValue().type != entry.thenLiteral->type ||
        !sameConstant(thenOperand.constantValue().value, entry.thenLiteral->value, module,
                      identities, semanticTypes)) {
      return false;
    }
  }

  // Else arm block: Assign(result = else_literal), Goto join.
  const auto& elseBlock = function.blocks[2 * armCount];
  if (elseBlock.id != blockId(static_cast<uint32_t>(2 * armCount + 1)) ||
      elseBlock.sourceScope != scopeId(1) || elseBlock.statements.size() != 1 ||
      elseBlock.terminator.kind() != MirTerminatorKind::Goto ||
      elseBlock.terminator.gotoValue().target != blockId(joinBlockOrdinal)) {
    return false;
  }
  const auto& elseAssign = elseBlock.statements[0];
  if (elseAssign.kind() != MirStatementKind::Assign ||
      !sameSpan(elseAssign.sourceSpan(), elseLiteral->sourceSpan)) {
    return false;
  }
  const auto& elseAssignValue = elseAssign.assignmentValue();
  if (elseAssignValue.initialization != MirInitializationKind::Initialize ||
      elseAssignValue.destination.local() != resultLocal ||
      elseAssignValue.destination.rootType() != declaration.resultType ||
      elseAssignValue.destination.resultType() != declaration.resultType ||
      elseAssignValue.destination.projections().size() != 0 ||
      elseAssignValue.value.kind() != MirRvalueKind::Use) {
    return false;
  }
  const auto& elseOperand = elseAssignValue.value.useValue().operand;
  if (elseOperand.kind() != MirOperandKind::Constant ||
      elseOperand.constantValue().type != elseLiteral->type ||
      !sameConstant(elseOperand.constantValue().value, elseLiteral->value, module, identities,
                    semanticTypes)) {
    return false;
  }

  // Join block: Return(result).
  const auto& joinBlock = function.blocks[2 * armCount + 1];
  if (joinBlock.id != blockId(joinBlockOrdinal) || joinBlock.sourceScope != scopeId(1) ||
      joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  ZC_IF_SOME(value, joinBlock.terminator.returnValue().value) {
    bool result = matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
                  value.place().local() == resultLocal &&
                  value.place().rootType() == declaration.resultType &&
                  value.place().resultType() == declaration.resultType &&
                  value.place().projections().size() == 0;
    return result;
  }
  return false;
}

// Verifies a match-guard conditional return: the condition is a LogicalAnd
// conjunction of the scrutinee bool parameter and a guard comparison
// (one parameter reference and one scalar literal). The entry block evaluates
// the guard comparison into a temporary, conjuncts it with the scrutinee into
// a second temporary, and switches on the conjunction. The local count is
// parameters + 3 (result + guardTemp + conjTemp).
bool validConjunctiveConditionalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirReturnStatement& sourceReturn, const hir::HirConditionalExpression& conditional,
    const hir::HirPrimitiveBinaryExpression& conjunction, const ConditionalArmView& thenArm,
    const ConditionalArmView& elseArm, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (conjunction.operation != checker::PrimitiveOperation::LogicalAnd) return false;

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

  // The conjunctive condition allocates two extra bool temporaries after the
  // function result local, so the local count is parameters + 3.
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 3 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != conditional.node ||
      sourceReturn.resultType != declaration.resultType ||
      conditional.condition != conjunction.node ||
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
  const auto guardTemp = localId(static_cast<uint32_t>(declaration.parameters.size() + 2));
  const auto conjTemp = localId(static_cast<uint32_t>(declaration.parameters.size() + 3));
  const auto& result = function.locals[declaration.parameters.size()];
  const auto& guard = function.locals[declaration.parameters.size() + 1];
  const auto& conj = function.locals[declaration.parameters.size() + 2];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan) || guard.id != guardTemp ||
      guard.kind != MirLocalKind::Temporary || guard.type != conjunction.type ||
      guard.sourceScope != scopeId(1) || !sameSpan(guard.sourceSpan, conjunction.sourceSpan) ||
      conj.id != conjTemp || conj.kind != MirLocalKind::Temporary ||
      conj.type != conjunction.type || conj.sourceScope != scopeId(1) ||
      !sameSpan(conj.sourceSpan, conjunction.sourceSpan)) {
    return false;
  }

  // The conjunction's left operand is the scrutinee bool parameter reference.
  auto scrutineeRef = parameterReferenceFor(hirModule, conjunction.left);
  if (scrutineeRef == zc::none) return false;
  const auto& scrutinee = ZC_ASSERT_NONNULL(scrutineeRef);
  size_t scrutineeIndex = 0;
  {
    bool found = false;
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == scrutinee.parameter) {
        scrutineeIndex = i;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  if (scrutinee.type != declaration.parameters[scrutineeIndex].type) return false;

  // The conjunction's right operand is the guard comparison: one parameter
  // reference and one scalar literal.
  auto guardComparison = primitiveBinaryFor(hirModule, conjunction.right);
  if (guardComparison == zc::none) return false;
  const auto& guardValue = ZC_ASSERT_NONNULL(guardComparison);
  auto guardComparisonOp = mirComparisonOperatorFor(guardValue.operation);
  if (guardComparisonOp == zc::none) return false;
  if (guardValue.type != conjunction.type) return false;

  auto guardLeftParam = parameterReferenceFor(hirModule, guardValue.left);
  auto guardRightParam = parameterReferenceFor(hirModule, guardValue.right);
  auto guardLeftLiteral = expressionFor(hirModule, guardValue.left);
  auto guardRightLiteral = expressionFor(hirModule, guardValue.right);
  const bool guardLeftIsParam = guardLeftParam != zc::none;
  const bool guardRightIsParam = guardRightParam != zc::none;
  if (guardLeftIsParam == guardRightIsParam) return false;
  const bool guardLeftIsLiteral = guardLeftLiteral != zc::none;
  const bool guardRightIsLiteral = guardRightLiteral != zc::none;
  if (guardLeftIsLiteral == guardRightIsLiteral) return false;
  if (guardLeftIsParam != guardRightIsLiteral) return false;

  const auto& guardParam =
      guardLeftIsParam ? ZC_ASSERT_NONNULL(guardLeftParam) : ZC_ASSERT_NONNULL(guardRightParam);
  const auto& guardLiteral = guardLeftIsLiteral ? ZC_ASSERT_NONNULL(guardLeftLiteral)
                                                : ZC_ASSERT_NONNULL(guardRightLiteral);
  size_t guardParamIndex = 0;
  {
    bool found = false;
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
      if (declaration.parameters[i].key == guardParam.parameter) {
        guardParamIndex = i;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  if (guardParam.type != guardValue.operandType) return false;
  if (guardLiteral.type != guardValue.operandType) return false;

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];
  // Entry block layout: StorageLive(result), StorageLive(guardTemp),
  // Assign(guardTemp = Comparison{op, copy(param), const(lit)}),
  // StorageLive(conjTemp), Assign(conjTemp = BitAnd(copy(scrutinee),
  // copy(guardTemp))), then SwitchInt(copy(conjTemp)).
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 5 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::StorageLive ||
      entry.statements[1].storageLocal() != guardTemp ||
      !sameSpan(entry.statements[1].sourceSpan(), guardValue.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[2].sourceSpan(), guardValue.sourceSpan) ||
      entry.statements[3].kind() != MirStatementKind::StorageLive ||
      entry.statements[3].storageLocal() != conjTemp ||
      !sameSpan(entry.statements[3].sourceSpan(), conjunction.sourceSpan) ||
      entry.statements[4].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[4].sourceSpan(), conjunction.sourceSpan) ||
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

  // Guard comparison assignment.
  const auto& guardAssign = entry.statements[2].assignmentValue();
  if (guardAssign.initialization != MirInitializationKind::Initialize ||
      guardAssign.destination.local() != guardTemp ||
      guardAssign.destination.rootType() != guardValue.type ||
      guardAssign.destination.resultType() != guardValue.type ||
      guardAssign.destination.projections().size() != 0 ||
      guardAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& guardCmp = guardAssign.value.comparisonValue();
  if (guardCmp.op != ZC_ASSERT_NONNULL(guardComparisonOp) ||
      guardCmp.resultType != guardValue.type) {
    return false;
  }
  // Guard comparison operands: parameter place-use and scalar constant.
  auto operandMatches = [&](const MirOperand& operand,
                            zc::Maybe<const hir::HirScalarLiteralExpression&> literal,
                            size_t parameterIndex) -> bool {
    ZC_IF_SOME(value, literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == guardValue.operandType &&
             sameConstant(operand.constantValue().value, value.value, module, identities,
                          semanticTypes);
    }
    const auto local = localId(static_cast<uint32_t>(parameterIndex + 1));
    return matchesPlaceUse(operand, proofs, copy, guardValue.operandType) &&
           operand.place().local() == local &&
           operand.place().rootType() == guardValue.operandType &&
           operand.place().resultType() == guardValue.operandType &&
           operand.place().projections().size() == 0;
  };
  const MirOperand& guardLeft = guardLeftIsParam ? guardCmp.left : guardCmp.right;
  const MirOperand& guardRight = guardLeftIsParam ? guardCmp.right : guardCmp.left;
  if (!operandMatches(guardLeft, guardLeftIsLiteral ? guardLeftLiteral : zc::none,
                      guardLeftIsParam ? guardParamIndex : 0) ||
      !operandMatches(guardRight, guardRightIsLiteral ? guardRightLiteral : zc::none,
                      guardRightIsParam ? guardParamIndex : 0)) {
    return false;
  }

  // Conjunction assignment: BitAnd(copy(scrutinee), copy(guardTemp)).
  const auto& conjAssign = entry.statements[4].assignmentValue();
  if (conjAssign.initialization != MirInitializationKind::Initialize ||
      conjAssign.destination.local() != conjTemp ||
      conjAssign.destination.rootType() != conjunction.type ||
      conjAssign.destination.resultType() != conjunction.type ||
      conjAssign.destination.projections().size() != 0 ||
      conjAssign.value.kind() != MirRvalueKind::Arithmetic) {
    return false;
  }
  const auto& conjArith = conjAssign.value.arithmeticValue();
  if (conjArith.op != MirArithmeticOperator::BitAnd || conjArith.resultType != conjunction.type) {
    return false;
  }
  // Left: scrutinee parameter copy.
  {
    const auto local = localId(static_cast<uint32_t>(scrutineeIndex + 1));
    if (!matchesPlaceUse(conjArith.left, proofs, copy, scrutinee.type) ||
        conjArith.left.place().local() != local ||
        conjArith.left.place().rootType() != scrutinee.type ||
        conjArith.left.place().resultType() != scrutinee.type ||
        conjArith.left.place().projections().size() != 0) {
      return false;
    }
  }
  // Right: guardTemp copy.
  if (!matchesPlaceUse(conjArith.right, proofs, copy, conjunction.type) ||
      conjArith.right.kind() != MirOperandKind::Copy ||
      conjArith.right.place().local() != guardTemp ||
      conjArith.right.place().rootType() != conjunction.type ||
      conjArith.right.place().resultType() != conjunction.type ||
      conjArith.right.place().projections().size() != 0) {
    return false;
  }

  // SwitchInt on the conjunction temporary.
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
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conjTemp ||
      switchInt.discriminant.place().rootType() != conjunction.type ||
      switchInt.discriminant.place().resultType() != conjunction.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }

  // Branch blocks: each initializes the result local from the arm value.
  if (!branchArmInitializesResult(thenBlock, thenArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes) ||
      !branchArmInitializesResult(elseBlock, elseArm, hirModule, declaration, resultLocal, proofs,
                                  copy, module, identities, semanticTypes)) {
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

// Verifies a function with K leading scalar user locals followed by one
// comparison conditional whose arms return constants. Mirrors
// validEqualityConditionalReturnFunction with a dense K UserLocal run between
// the parameters and the function result: the entry block holds a StorageLive
// plus an initializing Assign per leading local (2K statements) before the
// result/temp StorageLive pair and the Comparison assign, and the result and
// bool temporary ordinals shift by K.
bool validLeadingLocalConditionalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirReturnStatement& sourceReturn, const hir::HirConditionalExpression& conditional,
    const hir::HirPrimitiveBinaryExpression& equality,
    const hir::HirScalarLiteralExpression& thenLiteral,
    const hir::HirScalarLiteralExpression& elseLiteral, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const size_t bindingCount = sourceBlock.statements.size() - 1;
  if (bindingCount < 1 || declaration.receiver != zc::none) return false;
  const size_t parameterCount = declaration.parameters.size();
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + bindingCount + 2 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != bindingCount + 1 ||
      sourceBlock.statements[bindingCount] != sourceReturn.node ||
      sourceReturn.value != conditional.node || sourceReturn.resultType != declaration.resultType ||
      conditional.condition != equality.node || conditional.thenReturnValue != thenLiteral.node ||
      conditional.elseReturnValue != elseLiteral.node ||
      conditional.type != declaration.resultType || thenLiteral.type != declaration.resultType ||
      elseLiteral.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  // Resolve the K HIR bindings and cross-check the user locals.
  zc::Vector<const hir::HirLocalBinding*> bindings;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto binding = localFor(hirModule, sourceBlock.statements[i]);
    if (binding == zc::none) return false;
    const auto& value = ZC_ASSERT_NONNULL(binding);
    if (value.local.ordinal() != static_cast<uint32_t>(i + 1) || value.initializer == zc::none) {
      return false;
    }
    const auto& local = function.locals[parameterCount + i];
    if (local.id != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        local.kind != MirLocalKind::UserLocal || local.type != value.type ||
        local.sourceScope != scopeId(1) || !sameSpan(local.sourceSpan, value.sourceSpan)) {
      return false;
    }
    bindings.add(&value);
  }
  const auto resultLocal = localId(static_cast<uint32_t>(parameterCount + bindingCount + 1));
  const auto conditionTemp = localId(static_cast<uint32_t>(parameterCount + bindingCount + 2));
  const auto& result = function.locals[parameterCount + bindingCount];
  const auto& temp = function.locals[parameterCount + bindingCount + 1];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan) || temp.id != conditionTemp ||
      temp.kind != MirLocalKind::Temporary || temp.type != equality.type ||
      temp.sourceScope != scopeId(1) || !sameSpan(temp.sourceSpan, equality.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) ||
      entry.statements.size() != bindingCount * 2 + 3 ||
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
  // One leading-local preamble pair: StorageLive(userLocal) then an Initialize
  // Assign from a constant, a copy of a parameter or an earlier user local, or
  // a one-level arithmetic binary over literal/parameter leaves.
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& binding = *bindings[i];
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
    const auto& liveStatement = entry.statements[i * 2];
    const auto& assignStatement = entry.statements[i * 2 + 1];
    if (liveStatement.kind() != MirStatementKind::StorageLive ||
        liveStatement.storageLocal() != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        !sameSpan(liveStatement.sourceSpan(), binding.sourceSpan) ||
        assignStatement.kind() != MirStatementKind::Assign) {
      return false;
    }
    const auto& assignment = assignStatement.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        assignment.destination.rootType() != binding.type ||
        assignment.destination.resultType() != binding.type ||
        assignment.destination.projections().size() != 0) {
      return false;
    }
    // An arithmetic binding initializer lowers to an Arithmetic rvalue whose
    // two leaf operands are each a constant or a parameter place-use.
    if (auto initBinary = primitiveBinaryFor(hirModule, initializerNode); initBinary != zc::none) {
      const auto& binary = ZC_ASSERT_NONNULL(initBinary);
      if (assignment.value.kind() != MirRvalueKind::Arithmetic) return false;
      const auto& arithmetic = assignment.value.arithmeticValue();
      auto expectedArithmetic = mirArithmeticOperatorFor(binary.operation);
      if (expectedArithmetic == zc::none ||
          arithmetic.op != ZC_ASSERT_NONNULL(expectedArithmetic) ||
          arithmetic.resultType != binding.type || binding.initializerSpan == zc::none ||
          !sameSpan(assignStatement.sourceSpan(), ZC_ASSERT_NONNULL(binding.initializerSpan))) {
        return false;
      }
      auto leafOk = [&](const MirOperand& operand, hir::HirNodeId leafNode) -> bool {
        if (auto leafLiteral = expressionFor(hirModule, leafNode); leafLiteral != zc::none) {
          return operand.kind() == MirOperandKind::Constant &&
                 operand.constantValue().type == binding.type &&
                 sameConstant(operand.constantValue().value, ZC_ASSERT_NONNULL(leafLiteral).value,
                              module, identities, semanticTypes);
        }
        if (auto leafParameter = parameterReferenceFor(hirModule, leafNode);
            leafParameter != zc::none) {
          size_t parameterIndex = 0;
          bool resolved = false;
          for (size_t candidate = 0; candidate < parameterCount; ++candidate) {
            if (declaration.parameters[candidate].key ==
                ZC_ASSERT_NONNULL(leafParameter).parameter) {
              parameterIndex = candidate;
              resolved = true;
              break;
            }
          }
          return resolved && matchesPlaceUse(operand, proofs, copy, binding.type) &&
                 operand.kind() != MirOperandKind::Constant &&
                 operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
                 operand.place().rootType() == binding.type &&
                 operand.place().resultType() == binding.type &&
                 operand.place().projections().size() == 0;
        }
        if (auto leafLocal = localReferenceFor(hirModule, leafNode); leafLocal != zc::none) {
          const auto& value = ZC_ASSERT_NONNULL(leafLocal);
          return value.local.ordinal() >= 1 && value.local.ordinal() <= static_cast<uint32_t>(i) &&
                 value.type == binding.type &&
                 matchesPlaceUse(operand, proofs, copy, binding.type) &&
                 operand.kind() != MirOperandKind::Constant &&
                 operand.place().local() ==
                     localId(static_cast<uint32_t>(parameterCount + value.local.ordinal())) &&
                 operand.place().rootType() == binding.type &&
                 operand.place().resultType() == binding.type &&
                 operand.place().projections().size() == 0;
        }
        return false;
      };
      if (!leafOk(arithmetic.left, binary.left) || !leafOk(arithmetic.right, binary.right)) {
        return false;
      }
      continue;
    }
    if (assignment.value.kind() != MirRvalueKind::Use) return false;
    const auto& operand = assignment.value.useValue().operand;
    auto initLiteral = expressionFor(hirModule, initializerNode);
    auto initParameter = parameterReferenceFor(hirModule, initializerNode);
    auto initLocal = localReferenceFor(hirModule, initializerNode);
    const int present = (initLiteral != zc::none ? 1 : 0) + (initParameter != zc::none ? 1 : 0) +
                        (initLocal != zc::none ? 1 : 0);
    if (present != 1 || binding.initializerSpan == zc::none ||
        !sameSpan(assignStatement.sourceSpan(), ZC_ASSERT_NONNULL(binding.initializerSpan))) {
      return false;
    }
    bool operandOk = false;
    ZC_IF_SOME(value, initLiteral) {
      operandOk = operand.kind() == MirOperandKind::Constant &&
                  operand.constantValue().type == binding.type &&
                  sameConstant(operand.constantValue().value, value.value, module, identities,
                               semanticTypes);
    }
    ZC_IF_SOME(value, initParameter) {
      size_t parameterIndex = 0;
      bool resolved = false;
      for (size_t candidate = 0; candidate < parameterCount; ++candidate) {
        if (declaration.parameters[candidate].key == value.parameter) {
          parameterIndex = candidate;
          resolved = true;
          break;
        }
      }
      operandOk = resolved && value.type == binding.type &&
                  matchesPlaceUse(operand, proofs, copy, binding.type) &&
                  operand.kind() != MirOperandKind::Constant &&
                  operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
                  operand.place().rootType() == binding.type &&
                  operand.place().resultType() == binding.type &&
                  operand.place().projections().size() == 0;
    }
    ZC_IF_SOME(value, initLocal) {
      operandOk =
          value.local.ordinal() >= 1 && value.local.ordinal() <= static_cast<uint32_t>(i) &&
          value.type == binding.type && matchesPlaceUse(operand, proofs, copy, binding.type) &&
          operand.kind() != MirOperandKind::Constant &&
          operand.place().local() ==
              localId(static_cast<uint32_t>(parameterCount + value.local.ordinal())) &&
          operand.place().rootType() == binding.type &&
          operand.place().resultType() == binding.type && operand.place().projections().size() == 0;
    }
    if (!operandOk) return false;
  }
  // Result/temp StorageLive pair and the Comparison assign.
  const auto& resultLive = entry.statements[bindingCount * 2];
  const auto& tempLive = entry.statements[bindingCount * 2 + 1];
  const auto& tempAssignStatement = entry.statements[bindingCount * 2 + 2];
  if (resultLive.kind() != MirStatementKind::StorageLive ||
      resultLive.storageLocal() != resultLocal ||
      !sameSpan(resultLive.sourceSpan(), sourceReturn.sourceSpan) ||
      tempLive.kind() != MirStatementKind::StorageLive ||
      tempLive.storageLocal() != conditionTemp ||
      !sameSpan(tempLive.sourceSpan(), equality.sourceSpan)) {
    return false;
  }
  if (tempAssignStatement.kind() != MirStatementKind::Assign ||
      !sameSpan(tempAssignStatement.sourceSpan(), equality.sourceSpan)) {
    return false;
  }
  const auto& tempAssign = tempAssignStatement.assignmentValue();
  const bool isLogicalCondition = equality.operation == checker::PrimitiveOperation::LogicalAnd ||
                                  equality.operation == checker::PrimitiveOperation::LogicalOr;
  if (tempAssign.initialization != MirInitializationKind::Initialize ||
      tempAssign.destination.local() != conditionTemp ||
      tempAssign.destination.rootType() != equality.type ||
      tempAssign.destination.resultType() != equality.type ||
      tempAssign.destination.projections().size() != 0 ||
      tempAssign.value.kind() !=
          (isLogicalCondition ? MirRvalueKind::Arithmetic : MirRvalueKind::Comparison)) {
    return false;
  }
  const MirOperand* conditionLeft = nullptr;
  const MirOperand* conditionRight = nullptr;
  if (isLogicalCondition) {
    const auto& arithmetic = tempAssign.value.arithmeticValue();
    auto expectedOperator = mirArithmeticOperatorFor(equality.operation);
    if (expectedOperator == zc::none || arithmetic.op != ZC_ASSERT_NONNULL(expectedOperator) ||
        arithmetic.resultType != equality.type) {
      return false;
    }
    conditionLeft = &arithmetic.left;
    conditionRight = &arithmetic.right;
  } else {
    const auto& comparison = tempAssign.value.comparisonValue();
    auto expectedOperator = mirComparisonOperatorFor(equality.operation);
    if (expectedOperator == zc::none || comparison.op != ZC_ASSERT_NONNULL(expectedOperator) ||
        comparison.resultType != equality.type) {
      return false;
    }
    conditionLeft = &comparison.left;
    conditionRight = &comparison.right;
  }
  // Comparison operand: a constant, a parameter copy, or a leading-local copy.
  auto comparisonOperandOk = [&](const MirOperand& operand, hir::HirNodeId operandNode) -> bool {
    auto literal = expressionFor(hirModule, operandNode);
    ZC_IF_SOME(value, literal) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == equality.operandType &&
             sameConstant(operand.constantValue().value, value.value, module, identities,
                          semanticTypes);
    }
    auto parameter = parameterReferenceFor(hirModule, operandNode);
    ZC_IF_SOME(value, parameter) {
      size_t parameterIndex = 0;
      bool resolved = false;
      for (size_t candidate = 0; candidate < parameterCount; ++candidate) {
        if (declaration.parameters[candidate].key == value.parameter) {
          parameterIndex = candidate;
          resolved = true;
          break;
        }
      }
      return resolved && matchesPlaceUse(operand, proofs, copy, equality.operandType) &&
             operand.kind() != MirOperandKind::Constant &&
             operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
             operand.place().rootType() == equality.operandType &&
             operand.place().resultType() == equality.operandType &&
             operand.place().projections().size() == 0;
    }
    auto local = localReferenceFor(hirModule, operandNode);
    ZC_IF_SOME(value, local) {
      return value.local.ordinal() >= 1 &&
             value.local.ordinal() <= static_cast<uint32_t>(bindingCount) &&
             matchesPlaceUse(operand, proofs, copy, equality.operandType) &&
             operand.kind() != MirOperandKind::Constant &&
             operand.place().local() ==
                 localId(static_cast<uint32_t>(parameterCount + value.local.ordinal())) &&
             operand.place().rootType() == equality.operandType &&
             operand.place().resultType() == equality.operandType &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  if (!comparisonOperandOk(*conditionLeft, equality.left) ||
      !comparisonOperandOk(*conditionRight, equality.right)) {
    return false;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.arms.size() != 2 || switchInt.defaultTarget != blockId(3)) return false;
  if (switchInt.arms[0].target != blockId(2) || switchInt.arms[1].target != blockId(3)) {
    return false;
  }
  auto trueValue = switchInt.arms[0].value.booleanValue();
  auto falseValue = switchInt.arms[1].value.booleanValue();
  if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
      ZC_ASSERT_NONNULL(falseValue)) {
    return false;
  }
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionTemp ||
      switchInt.discriminant.place().rootType() != equality.type ||
      switchInt.discriminant.place().resultType() != equality.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  auto branchInitializesResult = [&](const MirBasicBlock& branch,
                                     const hir::HirScalarLiteralExpression& literal) -> bool {
    if (branch.statements[0].kind() != MirStatementKind::Assign) return false;
    const auto& assignment = branch.statements[0].assignmentValue();
    return assignment.initialization == MirInitializationKind::Initialize &&
           assignment.destination.local() == resultLocal &&
           assignment.destination.rootType() == declaration.resultType &&
           assignment.destination.resultType() == declaration.resultType &&
           assignment.destination.projections().size() == 0 &&
           assignment.value.kind() == MirRvalueKind::Use &&
           assignment.value.useValue().operand.kind() == MirOperandKind::Constant &&
           assignment.value.useValue().operand.constantValue().type == literal.type &&
           sameConstant(assignment.value.useValue().operand.constantValue().value, literal.value,
                        module, identities, semanticTypes) &&
           sameSpan(branch.statements[0].sourceSpan(), literal.sourceSpan);
  };
  if (!branchInitializesResult(thenBlock, thenLiteral) ||
      !branchInitializesResult(elseBlock, elseLiteral)) {
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
  // The comparison allocates one result local after the parameters. For a
  // method the implicit receiver leads the parameter locals, so every local
  // ordinal shifts by one and the definition kind is Method.
  const bool isMethod = declaration.receiver != zc::none;
  const size_t receiverCount = isMethod ? 1 : 0;
  const auto expectedDefinitionKind =
      isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != expectedDefinitionKind ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + receiverCount + 1 ||
      function.blocks.size() != 1 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 1 || sourceBlock.statements[0] != sourceReturn.node ||
      sourceReturn.value != comparison.node || sourceReturn.resultType != declaration.resultType ||
      comparison.type != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  if (isMethod) {
    const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
    const auto& receiverLocal = function.locals[0];
    if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
        receiverLocal.type != receiver.type || receiverLocal.sourceScope != scopeId(1) ||
        !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
      return false;
    }
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& local = function.locals[i + receiverCount];
    if (local.id != localId(static_cast<uint32_t>(i + receiverCount + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto resultLocal =
      localId(static_cast<uint32_t>(declaration.parameters.size() + receiverCount + 1));
  const auto& result = function.locals[declaration.parameters.size() + receiverCount];
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
      const auto local = localId(static_cast<uint32_t>(parameterIndex + receiverCount + 1));
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

bool validReceiverFieldBinaryReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirReturnStatement& sourceReturn,
    const hir::HirPrimitiveBinaryExpression& binary,
    const hir::HirParameterFieldProjectionExpression& projection,
    const hir::HirScalarLiteralExpression& literalExpression,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (declaration.receiver == zc::none) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 1 ||
      declaration.parameters.size() != 0 || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != 1 || sourceBlock.statements[0] != sourceReturn.node ||
      sourceReturn.value != binary.node || sourceReturn.resultType != declaration.resultType ||
      binary.type != declaration.resultType || binary.type != binary.operandType ||
      projection.type != binary.operandType || literalExpression.type != binary.operandType) {
    return false;
  }
  // One HIR operand is the field projection and the other is the scalar literal.
  const bool fieldOnLeft = binary.left == projection.node;
  const bool fieldOnRight = binary.right == projection.node;
  if (fieldOnLeft == fieldOnRight) return false;
  const hir::HirNodeId expectedLiteralNode = fieldOnLeft ? binary.right : binary.left;
  if (expectedLiteralNode != literalExpression.node) return false;
  const auto expectedArithmetic = mirArithmeticOperatorFor(binary.operation);
  if (expectedArithmetic == zc::none) return false;
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  const auto& receiverLocal = function.locals[0];
  if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
      receiverLocal.type != receiver.type || receiverLocal.sourceScope != scopeId(1) ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
    return false;
  }
  const auto resultLocal = localId(2);
  const auto& result = function.locals[1];
  if (result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 2 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[1].sourceSpan(), binary.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Return ||
      !sameSpan(entry.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& assignment = entry.statements[1].assignmentValue();
  if (assignment.initialization != MirInitializationKind::Initialize ||
      assignment.destination.local() != resultLocal ||
      assignment.destination.rootType() != declaration.resultType ||
      assignment.destination.resultType() != declaration.resultType ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != MirRvalueKind::Arithmetic) {
    return false;
  }
  const auto& arithmetic = assignment.value.arithmeticValue();
  if (arithmetic.op != ZC_ASSERT_NONNULL(expectedArithmetic) ||
      arithmetic.resultType != binary.type) {
    return false;
  }
  const MirOperand& fieldOperand = fieldOnLeft ? arithmetic.left : arithmetic.right;
  const MirOperand& literalOperand = fieldOnLeft ? arithmetic.right : arithmetic.left;
  const bool fieldOperandOk =
      matchesPlaceUse(fieldOperand, proofs, copy, projection.type) &&
      fieldOperand.place().local() == localId(1) &&
      fieldOperand.place().rootType() == receiver.type &&
      fieldOperand.place().resultType() == projection.type &&
      fieldOperand.place().projections().size() == 2 &&
      fieldOperand.place().projections()[0].kind() == MirProjectionKind::Dereference &&
      fieldOperand.place().projections()[0].inputType() == receiver.type &&
      fieldOperand.place().projections()[0].resultType() == projection.receiverType &&
      fieldOperand.place().projections()[1].kind() == MirProjectionKind::Field &&
      fieldOperand.place().projections()[1].fieldValue().field == projection.field &&
      fieldOperand.place().projections()[1].inputType() == projection.receiverType &&
      fieldOperand.place().projections()[1].resultType() == projection.type;
  const bool literalOperandOk =
      literalOperand.kind() == MirOperandKind::Constant &&
      literalOperand.constantValue().type == projection.type &&
      sameConstant(literalOperand.constantValue().value, literalExpression.value, module,
                   identities, semanticTypes);
  if (!fieldOperandOk || !literalOperandOk) return false;
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

// Validates the loop-body composite function: `mut x = <lit>; while (cond) {
// <writes> } return x;` lowered to a reducible four-block CFG. Parameters occupy
// localId(1..P); the user local x is localId(P+1). Entry initializes x and jumps
// to the header; the header switches on the bool condition parameter into the
// body (true) or the exit (default); the body carries the write assignments then
// jumps back to the header (the reducible back-edge); the exit returns a
// place-use of x. The per-write value rvalue kinds are validated by the shared
// canonical-encoding comparison in the caller; this checks the CFG shape,
// locals, terminators, and that each body statement is an Overwrite of x.
bool validLoopBodyReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& local,
    const hir::HirLoopStatement& loop, const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalReferenceExpression& reference,
    const hir::HirParameterReferenceExpression& conditionRef,
    const hir::VerifiedHirModule& hirModule, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + 1 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != local.node || sourceBlock.statements[1] != loop.node ||
      sourceBlock.statements[2] != sourceReturn.node || sourceReturn.value != reference.node ||
      sourceReturn.resultType != declaration.resultType || loop.condition != conditionRef.node ||
      loop.type != conditionRef.type || local.type != declaration.resultType ||
      reference.type != declaration.resultType ||
      reference.category != hir::HirValueCategory::Place || local.local != reference.local ||
      loop.body.size() == 0) {
    return false;
  }
  const auto resultLocal = localId(parameterCount + 1);
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (uint32_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    if (parameterLocal.id != localId(i + 1) || parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scopeId(1) ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto& result = function.locals[parameterCount];
  if (result.id != resultLocal || result.kind != MirLocalKind::UserLocal ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, local.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];
  // A trailing break exits the body to the loop exit (bb4); a trailing
  // continue or a write-only body jumps back to the header (bb2).
  const auto expectedBodyTarget = loop.breakSpan != zc::none ? blockId(4) : blockId(2);
  // Entry: StorageLive(x) ; Assign(x = initializer, Initialize) ; Goto(bb2).
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
  auto initializer = expressionFor(hirModule, initializerNode);
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 2 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), local.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      entry.terminator.kind() != MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != blockId(2) || header.id != blockId(2) ||
      header.sourceScope != scopeId(1) || header.statements.size() != 0 ||
      header.terminator.kind() != MirTerminatorKind::SwitchInt || body.id != blockId(3) ||
      body.sourceScope != scopeId(1) || body.statements.size() != loop.body.size() ||
      body.terminator.kind() != MirTerminatorKind::Goto ||
      body.terminator.gotoValue().target != expectedBodyTarget || exit.id != blockId(4) ||
      exit.sourceScope != scopeId(1) || exit.statements.size() != 0 ||
      exit.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  const auto& initAssign = entry.statements[1].assignmentValue();
  if (initializer == zc::none || initAssign.initialization != MirInitializationKind::Initialize ||
      initAssign.destination.local() != resultLocal ||
      initAssign.destination.rootType() != declaration.resultType ||
      initAssign.destination.resultType() != declaration.resultType ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.value.kind() != MirRvalueKind::Use ||
      initAssign.value.useValue().operand.kind() != MirOperandKind::Constant ||
      initAssign.value.useValue().operand.constantValue().type !=
          ZC_ASSERT_NONNULL(initializer).type ||
      !sameConstant(initAssign.value.useValue().operand.constantValue().value,
                    ZC_ASSERT_NONNULL(initializer).value, module, identities, semanticTypes)) {
    return false;
  }
  // Header SwitchInt on the condition parameter: [true -> bb3], default = bb4.
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.arms.size() != 1 || switchInt.defaultTarget != blockId(4)) return false;
  const auto& trueArm = switchInt.arms[0];
  if (trueArm.target != blockId(3)) return false;
  auto trueValue = trueArm.value.booleanValue();
  if (trueValue == zc::none || !ZC_ASSERT_NONNULL(trueValue)) return false;
  size_t conditionIndex = 0;
  bool found = false;
  for (uint32_t i = 0; i < parameterCount; ++i) {
    if (declaration.parameters[i].key == conditionRef.parameter) {
      conditionIndex = i;
      found = true;
      break;
    }
  }
  if (!found || switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() !=
          localId(static_cast<uint32_t>(conditionIndex) + 1) ||
      switchInt.discriminant.place().rootType() != conditionRef.type ||
      switchInt.discriminant.place().resultType() != conditionRef.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Body: one Overwrite of x per loop-body write, in order.
  for (size_t writeIndex = 0; writeIndex < loop.body.size(); ++writeIndex) {
    auto write = localWriteFor(hirModule, loop.body[writeIndex]);
    if (write == zc::none || ZC_ASSERT_NONNULL(write).local != local.local ||
        ZC_ASSERT_NONNULL(write).field != zc::none ||
        ZC_ASSERT_NONNULL(write).type != declaration.resultType ||
        ZC_ASSERT_NONNULL(write).kind != hir::HirLocalWriteKind::Overwrite) {
      return false;
    }
    const auto& statement = body.statements[writeIndex];
    if (statement.kind() != MirStatementKind::Assign ||
        !sameSpan(statement.sourceSpan(), ZC_ASSERT_NONNULL(write).sourceSpan)) {
      return false;
    }
    const auto& assignment = statement.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Overwrite ||
        assignment.destination.local() != resultLocal ||
        assignment.destination.rootType() != declaration.resultType ||
        assignment.destination.resultType() != declaration.resultType ||
        assignment.destination.projections().size() != 0) {
      return false;
    }
  }
  // Exit: Return(placeUse(x)).
  ZC_IF_SOME(value, exit.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

// Validates the for-loop composite function: `for (let i = <lit>; i < <lit>;
// i = i <bin> <lit>) {} return <lit>;` lowered to a reducible four-block CFG.
// Parameters occupy localId(1..P); the init local i is localId(P+1); the
// comparison temp is localId(P+2); the result local is localId(P+3). The entry
// block declares the result and init locals and initializes i then jumps to the
// header; the header evaluates the comparison into the temp and switches on it
// into the body (true) or the exit (default); the body carries the update write
// (Overwrite) then jumps back to the header (the reducible back-edge); the exit
// initializes the result with the return literal and returns it.
bool validForLoopReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& local,
    const hir::HirLoopStatement& loop, const hir::HirReturnStatement& sourceReturn,
    const hir::HirPrimitiveBinaryExpression& condition,
    const hir::HirScalarLiteralExpression& returnValue, const hir::VerifiedHirModule& hirModule,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + 3 || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != local.node || sourceBlock.statements[1] != loop.node ||
      sourceBlock.statements[2] != sourceReturn.node || sourceReturn.value != returnValue.node ||
      sourceReturn.resultType != declaration.resultType || loop.condition != condition.node ||
      loop.type != condition.type || loop.body.size() != 1 || local.initializer == zc::none ||
      local.local.ordinal() != 1 || local.type != condition.operandType ||
      returnValue.type != declaration.resultType) {
    return false;
  }
  // Resolve the condition operands, the update write, and the write value.
  auto conditionLeft = localReferenceFor(hirModule, condition.left);
  auto conditionRight = expressionFor(hirModule, condition.right);
  auto write = localWriteFor(hirModule, loop.body[0]);
  if (conditionLeft == zc::none || conditionRight == zc::none || write == zc::none) {
    return false;
  }
  const auto& condLeft = ZC_ASSERT_NONNULL(conditionLeft);
  const auto& condRight = ZC_ASSERT_NONNULL(conditionRight);
  const auto& updateWrite = ZC_ASSERT_NONNULL(write);
  auto writeValue = primitiveBinaryFor(hirModule, updateWrite.value);
  if (writeValue == zc::none) return false;
  const auto& updateValue = ZC_ASSERT_NONNULL(writeValue);
  auto writeValueLeft = localReferenceFor(hirModule, updateValue.left);
  auto writeValueRight = expressionFor(hirModule, updateValue.right);
  if (writeValueLeft == zc::none || writeValueRight == zc::none) return false;
  const auto& updateLeft = ZC_ASSERT_NONNULL(writeValueLeft);
  const auto& updateRight = ZC_ASSERT_NONNULL(writeValueRight);
  const auto comparisonOperator = mirComparisonOperatorFor(condition.operation);
  const auto arithmeticOperator = mirArithmeticOperatorFor(updateValue.operation);
  if (comparisonOperator == zc::none || arithmeticOperator == zc::none ||
      condLeft.local != local.local || condLeft.type != local.type ||
      condRight.type != local.type || updateWrite.local != local.local ||
      updateWrite.field != zc::none || updateWrite.type != local.type ||
      updateWrite.kind != hir::HirLocalWriteKind::Overwrite ||
      updateValue.operandType != local.type || updateValue.type != local.type ||
      updateLeft.local != local.local || updateLeft.type != local.type ||
      updateRight.type != local.type) {
    return false;
  }
  const auto resultLocal = localId(parameterCount + 3);
  const auto initLocal = localId(parameterCount + 1);
  const auto conditionTemp = localId(parameterCount + 2);
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (uint32_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    if (parameterLocal.id != localId(i + 1) || parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scopeId(1) ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto& init = function.locals[parameterCount];
  const auto& temp = function.locals[parameterCount + 1];
  const auto& result = function.locals[parameterCount + 2];
  if (init.id != initLocal || init.kind != MirLocalKind::UserLocal || init.type != local.type ||
      init.sourceScope != scopeId(1) || !sameSpan(init.sourceSpan, local.sourceSpan) ||
      temp.id != conditionTemp || temp.kind != MirLocalKind::Temporary ||
      temp.type != condition.type || temp.sourceScope != scopeId(1) ||
      !sameSpan(temp.sourceSpan, condition.sourceSpan) || result.id != resultLocal ||
      result.kind != MirLocalKind::FunctionResult || result.type != declaration.resultType ||
      result.sourceScope != scopeId(1) || !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];
  // A trailing break exits the body to the loop exit (bb4); a trailing
  // continue or a write-only body jumps back to the header (bb2).
  const auto expectedBodyTarget = loop.breakSpan != zc::none ? blockId(4) : blockId(2);
  // Reducible four-block loop CFG:
  //   bb1 entry:  StorageLive(result) ; StorageLive(i) ; StorageLive(temp) ;
  //               Assign(i = init, Initialize) ;
  //               Assign(temp = Comparison(op, copy(i), <lit>), Initialize) ; Goto(bb2)
  //   bb2 header: SwitchInt(copy(temp), [true -> bb3], default = bb4)
  //   bb3 body:   Assign(i = Arithmetic(op, copy(i), <lit>), Overwrite) ;
  //               Assign(temp = Comparison(op, copy(i), <lit>), Overwrite) ; Goto(bb2)
  //   bb4 exit:   Assign(result = <lit>, Initialize) ; Return(placeUse(result))
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) || entry.statements.size() != 5 ||
      entry.terminator.kind() != MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != blockId(2) || header.id != blockId(2) ||
      header.sourceScope != scopeId(1) || header.statements.size() != 0 ||
      header.terminator.kind() != MirTerminatorKind::SwitchInt || body.id != blockId(3) ||
      body.sourceScope != scopeId(1) || body.statements.size() != 2 ||
      body.terminator.kind() != MirTerminatorKind::Goto ||
      body.terminator.gotoValue().target != expectedBodyTarget || exit.id != blockId(4) ||
      exit.sourceScope != scopeId(1) || exit.statements.size() != 1 ||
      exit.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  // Entry: StorageLive(result), StorageLive(i), StorageLive(temp),
  // Assign(i = init, Initialize), Assign(temp = Comparison, Initialize).
  if (entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::StorageLive ||
      entry.statements[1].storageLocal() != initLocal ||
      !sameSpan(entry.statements[1].sourceSpan(), local.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != conditionTemp ||
      !sameSpan(entry.statements[2].sourceSpan(), condition.sourceSpan) ||
      entry.statements[3].kind() != MirStatementKind::Assign ||
      entry.statements[4].kind() != MirStatementKind::Assign) {
    return false;
  }
  const auto& initAssign = entry.statements[3].assignmentValue();
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
  auto initLiteral = expressionFor(hirModule, initializerNode);
  if (initLiteral == zc::none || initAssign.initialization != MirInitializationKind::Initialize ||
      initAssign.destination.local() != initLocal ||
      initAssign.destination.rootType() != local.type ||
      initAssign.destination.resultType() != local.type ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.value.kind() != MirRvalueKind::Use ||
      initAssign.value.useValue().operand.kind() != MirOperandKind::Constant ||
      initAssign.value.useValue().operand.constantValue().type !=
          ZC_ASSERT_NONNULL(initLiteral).type ||
      !sameConstant(initAssign.value.useValue().operand.constantValue().value,
                    ZC_ASSERT_NONNULL(initLiteral).value, module, identities, semanticTypes)) {
    return false;
  }
  // Entry condition: Assign(temp = Comparison(op, copy(i), <lit>), Initialize).
  const auto& entryCondAssign = entry.statements[4].assignmentValue();
  if (entryCondAssign.initialization != MirInitializationKind::Initialize ||
      entryCondAssign.destination.local() != conditionTemp ||
      entryCondAssign.destination.rootType() != condition.type ||
      entryCondAssign.destination.resultType() != condition.type ||
      entryCondAssign.destination.projections().size() != 0 ||
      entryCondAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& entryComparison = entryCondAssign.value.comparisonValue();
  if (entryComparison.op != ZC_ASSERT_NONNULL(comparisonOperator) ||
      entryComparison.resultType != condition.type ||
      entryComparison.left.kind() != MirOperandKind::Copy ||
      entryComparison.left.place().local() != initLocal ||
      entryComparison.left.place().rootType() != local.type ||
      entryComparison.left.place().resultType() != local.type ||
      entryComparison.left.place().projections().size() != 0 ||
      entryComparison.right.kind() != MirOperandKind::Constant ||
      entryComparison.right.constantValue().type != condRight.type ||
      !sameConstant(entryComparison.right.constantValue().value, condRight.value, module,
                    identities, semanticTypes)) {
    return false;
  }
  // Header SwitchInt: [true -> bb3], default = bb4.
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.arms.size() != 1 || switchInt.defaultTarget != blockId(4)) return false;
  const auto& trueArm = switchInt.arms[0];
  if (trueArm.target != blockId(3)) return false;
  auto trueValue = trueArm.value.booleanValue();
  if (trueValue == zc::none || !ZC_ASSERT_NONNULL(trueValue)) return false;
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionTemp ||
      switchInt.discriminant.place().rootType() != condition.type ||
      switchInt.discriminant.place().resultType() != condition.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Body: Assign(i = Arithmetic(op, copy(i), <lit>), Overwrite),
  // Assign(temp = Comparison(op, copy(i), <lit>), Overwrite).
  const auto& bodyAssign = body.statements[0].assignmentValue();
  if (bodyAssign.initialization != MirInitializationKind::Overwrite ||
      bodyAssign.destination.local() != initLocal ||
      bodyAssign.destination.rootType() != local.type ||
      bodyAssign.destination.resultType() != local.type ||
      bodyAssign.destination.projections().size() != 0 ||
      bodyAssign.value.kind() != MirRvalueKind::Arithmetic) {
    return false;
  }
  const auto& arithmetic = bodyAssign.value.arithmeticValue();
  if (arithmetic.op != ZC_ASSERT_NONNULL(arithmeticOperator) ||
      arithmetic.resultType != updateValue.type || arithmetic.left.kind() != MirOperandKind::Copy ||
      arithmetic.left.place().local() != initLocal ||
      arithmetic.left.place().rootType() != local.type ||
      arithmetic.left.place().resultType() != local.type ||
      arithmetic.left.place().projections().size() != 0 ||
      arithmetic.right.kind() != MirOperandKind::Constant ||
      arithmetic.right.constantValue().type != updateRight.type ||
      !sameConstant(arithmetic.right.constantValue().value, updateRight.value, module, identities,
                    semanticTypes)) {
    return false;
  }
  // Body condition recompute: Assign(temp = Comparison(op, copy(i), <lit>), Overwrite).
  const auto& bodyCondAssign = body.statements[1].assignmentValue();
  if (bodyCondAssign.initialization != MirInitializationKind::Overwrite ||
      bodyCondAssign.destination.local() != conditionTemp ||
      bodyCondAssign.destination.rootType() != condition.type ||
      bodyCondAssign.destination.resultType() != condition.type ||
      bodyCondAssign.destination.projections().size() != 0 ||
      bodyCondAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& bodyComparison = bodyCondAssign.value.comparisonValue();
  if (bodyComparison.op != ZC_ASSERT_NONNULL(comparisonOperator) ||
      bodyComparison.resultType != condition.type ||
      bodyComparison.left.kind() != MirOperandKind::Copy ||
      bodyComparison.left.place().local() != initLocal ||
      bodyComparison.left.place().rootType() != local.type ||
      bodyComparison.left.place().resultType() != local.type ||
      bodyComparison.left.place().projections().size() != 0 ||
      bodyComparison.right.kind() != MirOperandKind::Constant ||
      bodyComparison.right.constantValue().type != condRight.type ||
      !sameConstant(bodyComparison.right.constantValue().value, condRight.value, module, identities,
                    semanticTypes)) {
    return false;
  }
  // Exit: Assign(result = <lit>, Initialize) ; Return(placeUse(result)).
  const auto& exitAssign = exit.statements[0].assignmentValue();
  if (exitAssign.initialization != MirInitializationKind::Initialize ||
      exitAssign.destination.local() != resultLocal ||
      exitAssign.destination.rootType() != declaration.resultType ||
      exitAssign.destination.resultType() != declaration.resultType ||
      exitAssign.destination.projections().size() != 0 ||
      exitAssign.value.kind() != MirRvalueKind::Use ||
      exitAssign.value.useValue().operand.kind() != MirOperandKind::Constant ||
      exitAssign.value.useValue().operand.constantValue().type != returnValue.type ||
      !sameConstant(exitAssign.value.useValue().operand.constantValue().value, returnValue.value,
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

bool validNestedForLoopAccumulatorReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLoopStatement& outerLoop,
    const hir::HirReturnStatement& sourceReturn, const hir::HirPrimitiveBinaryExpression& outerCond,
    const hir::HirLocalReferenceExpression& returnRef, const hir::VerifiedHirModule& hirModule,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  const size_t accumulatorCount = sourceBlock.statements.size() - 3;
  if (accumulatorCount == 0) return false;
  // The outer loop body must have exactly three statements: inner-init local,
  // inner loop, outer update write.
  if (outerLoop.body.size() != 3) return false;
  auto innerInitMaybe = localFor(hirModule, outerLoop.body[0]);
  auto innerLoopMaybe = loopFor(hirModule, outerLoop.body[1]);
  auto outerWriteMaybe = localWriteFor(hirModule, outerLoop.body[2]);
  if (innerInitMaybe == zc::none || innerLoopMaybe == zc::none || outerWriteMaybe == zc::none)
    return false;
  const auto& innerInit = ZC_ASSERT_NONNULL(innerInitMaybe);
  const auto& innerLoop = ZC_ASSERT_NONNULL(innerLoopMaybe);
  const auto& outerWrite = ZC_ASSERT_NONNULL(outerWriteMaybe);
  // The inner loop body must have N+1 statements: N accumulator writes and the
  // inner update write.
  if (innerLoop.body.size() != accumulatorCount + 1) return false;
  // Resolve the N accumulator locals and the outer init local.
  zc::Vector<const hir::HirLocalBinding*> accLocals;
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto local = localFor(hirModule, sourceBlock.statements[k]);
    if (local == zc::none) return false;
    accLocals.add(&ZC_ASSERT_NONNULL(local));
  }
  auto outerInitMaybe = localFor(hirModule, sourceBlock.statements[accumulatorCount]);
  if (outerInitMaybe == zc::none) return false;
  const auto& outerInit = ZC_ASSERT_NONNULL(outerInitMaybe);
  const auto& firstAccLocal = *accLocals[0];
  // Validate function-level properties.
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + accumulatorCount + 5 ||
      function.blocks.size() != 7 || declaration.body != sourceBlock.node ||
      sourceBlock.statements[accumulatorCount] != outerInit.node ||
      sourceBlock.statements[accumulatorCount + 1] != outerLoop.node ||
      sourceBlock.statements[accumulatorCount + 2] != sourceReturn.node ||
      sourceReturn.value != returnRef.node || sourceReturn.resultType != declaration.resultType ||
      outerLoop.condition != outerCond.node || outerLoop.type != outerCond.type ||
      outerInit.initializer == zc::none ||
      outerInit.local.ordinal() != static_cast<uint32_t>(accumulatorCount + 1) ||
      outerInit.type != outerCond.operandType || innerInit.initializer == zc::none ||
      innerInit.local.ordinal() != static_cast<uint32_t>(accumulatorCount + 2) ||
      returnRef.local != firstAccLocal.local || returnRef.type != firstAccLocal.type) {
    return false;
  }
  // Validate each accumulator local's ordinal and initializer.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    if (accLocals[k]->initializer == zc::none ||
        accLocals[k]->local.ordinal() != static_cast<uint32_t>(k + 1) ||
        accLocals[k]->type != declaration.resultType) {
      return false;
    }
  }
  // Resolve the inner condition.
  auto innerCondBinary = primitiveBinaryFor(hirModule, innerLoop.condition);
  if (innerCondBinary == zc::none) return false;
  const auto& innerCond = ZC_ASSERT_NONNULL(innerCondBinary);
  // Validate inner init type matches inner condition operand type.
  if (innerInit.type != innerCond.operandType) return false;
  // Validate outer and inner update writes.
  auto outerWriteBin = primitiveBinaryFor(hirModule, outerWrite.value);
  if (outerWriteBin == zc::none) return false;
  const auto& outerWriteBinRef = ZC_ASSERT_NONNULL(outerWriteBin);
  auto innerWriteMaybe = localWriteFor(hirModule, innerLoop.body[accumulatorCount]);
  if (innerWriteMaybe == zc::none) return false;
  const auto& innerWrite = ZC_ASSERT_NONNULL(innerWriteMaybe);
  auto innerWriteBin = primitiveBinaryFor(hirModule, innerWrite.value);
  if (innerWriteBin == zc::none) return false;
  const auto& innerWriteBinRef = ZC_ASSERT_NONNULL(innerWriteBin);
  if (outerWrite.local != outerInit.local || outerWrite.field != zc::none ||
      outerWrite.type != outerInit.type || outerWrite.kind != hir::HirLocalWriteKind::Overwrite ||
      outerWriteBinRef.operandType != outerInit.type || outerWriteBinRef.type != outerInit.type ||
      innerWrite.local != innerInit.local || innerWrite.field != zc::none ||
      innerWrite.type != innerInit.type || innerWrite.kind != hir::HirLocalWriteKind::Overwrite ||
      innerWriteBinRef.operandType != innerInit.type || innerWriteBinRef.type != innerInit.type) {
    return false;
  }
  // Validate N accumulator body writes.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto bw = localWriteFor(hirModule, innerLoop.body[k]);
    if (bw == zc::none) return false;
    const auto& bwRef = ZC_ASSERT_NONNULL(bw);
    auto bwBin = primitiveBinaryFor(hirModule, bwRef.value);
    if (bwBin == zc::none) return false;
    const auto& bwBinRef = ZC_ASSERT_NONNULL(bwBin);
    auto bwLhs = localReferenceFor(hirModule, bwBinRef.left);
    if (bwLhs == zc::none) return false;
    const auto& bwLhsRef = ZC_ASSERT_NONNULL(bwLhs);
    if (bwRef.local != accLocals[k]->local || bwRef.field != zc::none ||
        bwRef.type != accLocals[k]->type || bwRef.kind != hir::HirLocalWriteKind::Overwrite ||
        bwBinRef.operandType != accLocals[k]->type || bwBinRef.type != accLocals[k]->type ||
        bwLhsRef.local != accLocals[k]->local || bwLhsRef.type != accLocals[k]->type) {
      return false;
    }
  }
  // Local layout: parameters (1..P), accumulators (P+1..P+N), outer init
  // (P+N+1), outer cond temp (P+N+2), inner init (P+N+3), inner cond temp
  // (P+N+4), result (P+N+5).
  const auto outerInitMirLocal =
      localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 1);
  const auto outerCondTemp = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 2);
  const auto innerInitMirLocal =
      localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 3);
  const auto innerCondTemp = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 4);
  const auto resultLocal = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 5);
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  // Validate parameter locals.
  for (uint32_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    if (parameterLocal.id != localId(i + 1) || parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scopeId(1) ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  // Validate accumulator locals.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto& acc = function.locals[parameterCount + k];
    if (acc.id != localId(parameterCount + static_cast<uint32_t>(k) + 1) ||
        acc.kind != MirLocalKind::UserLocal || acc.type != accLocals[k]->type ||
        acc.sourceScope != scopeId(1) || !sameSpan(acc.sourceSpan, accLocals[k]->sourceSpan)) {
      return false;
    }
  }
  // Validate outer init, outer cond temp, inner init, inner cond temp, result.
  const auto& outerInitLocal = function.locals[parameterCount + accumulatorCount];
  const auto& outerCondTempLocal = function.locals[parameterCount + accumulatorCount + 1];
  const auto& innerInitLocal = function.locals[parameterCount + accumulatorCount + 2];
  const auto& innerCondTempLocal = function.locals[parameterCount + accumulatorCount + 3];
  const auto& resultLocalDecl = function.locals[parameterCount + accumulatorCount + 4];
  if (outerInitLocal.id != outerInitMirLocal || outerInitLocal.kind != MirLocalKind::UserLocal ||
      outerInitLocal.type != outerInit.type || outerInitLocal.sourceScope != scopeId(1) ||
      !sameSpan(outerInitLocal.sourceSpan, outerInit.sourceSpan) ||
      outerCondTempLocal.id != outerCondTemp ||
      outerCondTempLocal.kind != MirLocalKind::Temporary ||
      outerCondTempLocal.type != outerCond.type || outerCondTempLocal.sourceScope != scopeId(1) ||
      !sameSpan(outerCondTempLocal.sourceSpan, outerCond.sourceSpan) ||
      innerInitLocal.id != innerInitMirLocal || innerInitLocal.kind != MirLocalKind::UserLocal ||
      innerInitLocal.type != innerInit.type || innerInitLocal.sourceScope != scopeId(1) ||
      !sameSpan(innerInitLocal.sourceSpan, innerInit.sourceSpan) ||
      innerCondTempLocal.id != innerCondTemp ||
      innerCondTempLocal.kind != MirLocalKind::Temporary ||
      innerCondTempLocal.type != innerCond.type || innerCondTempLocal.sourceScope != scopeId(1) ||
      !sameSpan(innerCondTempLocal.sourceSpan, innerCond.sourceSpan) ||
      resultLocalDecl.id != resultLocal || resultLocalDecl.kind != MirLocalKind::FunctionResult ||
      resultLocalDecl.type != declaration.resultType || resultLocalDecl.sourceScope != scopeId(1) ||
      !sameSpan(resultLocalDecl.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  // Validate the seven-block CFG.
  // bb1 outer entry: StorageLive all locals, Assign accumulators, Assign outer
  //   init, Assign outer cond temp, Goto(bb2).
  // bb2 outer header: SwitchInt(copy(outer cond temp), [true -> bb3], default=bb7).
  // bb3 inner entry: Assign(inner init, Overwrite), Assign(inner cond temp,
  //   Overwrite), Goto(bb4). Overwrite because the inner entry is inside the
  //   outer loop and re-executed on subsequent outer iterations.
  // bb4 inner header: SwitchInt(copy(inner cond temp), [true -> bb5], default=bb6).
  // bb5 inner body: N accumulator writes, inner update write, re-compare, Goto(bb4).
  // bb6 inner exit=outer continuation: outer update write, re-compare, Goto(bb2).
  // bb7 outer exit: Assign(result = copy(acc_0), Initialize), Return(placeUse(result)).
  const auto& entry = function.blocks[0];
  const auto& outerHeader = function.blocks[1];
  const auto& innerEntry = function.blocks[2];
  const auto& innerHeader = function.blocks[3];
  const auto& innerBody = function.blocks[4];
  const auto& outerCont = function.blocks[5];
  const auto& exit = function.blocks[6];
  // Entry block: StorageLive(result), StorageLive(acc_k)..., StorageLive(outer
  // init), StorageLive(outer cond temp), StorageLive(inner init), StorageLive(inner
  // cond temp), Assign(acc_k = accInit_k)..., Assign(outer init = init), Assign(outer
  // cond temp = comparison).
  const size_t expectedEntrySize = 7 + 2 * accumulatorCount;
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) ||
      entry.statements.size() != expectedEntrySize ||
      entry.terminator.kind() != MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != blockId(2) || outerHeader.id != blockId(2) ||
      outerHeader.sourceScope != scopeId(1) || outerHeader.statements.size() != 0 ||
      outerHeader.terminator.kind() != MirTerminatorKind::SwitchInt ||
      innerEntry.id != blockId(3) || innerEntry.sourceScope != scopeId(1) ||
      innerEntry.statements.size() != 2 ||
      innerEntry.terminator.kind() != MirTerminatorKind::Goto ||
      innerEntry.terminator.gotoValue().target != blockId(4) || innerHeader.id != blockId(4) ||
      innerHeader.sourceScope != scopeId(1) || innerHeader.statements.size() != 0 ||
      innerHeader.terminator.kind() != MirTerminatorKind::SwitchInt || innerBody.id != blockId(5) ||
      innerBody.sourceScope != scopeId(1) || innerBody.statements.size() != 2 + accumulatorCount ||
      innerBody.terminator.kind() != MirTerminatorKind::Goto ||
      innerBody.terminator.gotoValue().target != blockId(4) || outerCont.id != blockId(6) ||
      outerCont.sourceScope != scopeId(1) || outerCont.statements.size() != 2 ||
      outerCont.terminator.kind() != MirTerminatorKind::Goto ||
      outerCont.terminator.gotoValue().target != blockId(2) || exit.id != blockId(7) ||
      exit.sourceScope != scopeId(1) || exit.statements.size() != 1 ||
      exit.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  // Validate outer header SwitchInt.
  const auto& outerSwitch = outerHeader.terminator.switchIntValue();
  if (outerSwitch.arms.size() != 1 || outerSwitch.defaultTarget != blockId(7)) return false;
  if (outerSwitch.arms[0].target != blockId(3)) return false;
  auto outerTrueValue = outerSwitch.arms[0].value.booleanValue();
  if (outerTrueValue == zc::none || !ZC_ASSERT_NONNULL(outerTrueValue)) return false;
  if (outerSwitch.discriminant.kind() != MirOperandKind::Copy ||
      outerSwitch.discriminant.place().local() != outerCondTemp ||
      outerSwitch.discriminant.place().rootType() != outerCond.type ||
      outerSwitch.discriminant.place().resultType() != outerCond.type ||
      outerSwitch.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Validate inner header SwitchInt.
  const auto& innerSwitch = innerHeader.terminator.switchIntValue();
  if (innerSwitch.arms.size() != 1 || innerSwitch.defaultTarget != blockId(6)) return false;
  if (innerSwitch.arms[0].target != blockId(5)) return false;
  auto innerTrueValue = innerSwitch.arms[0].value.booleanValue();
  if (innerTrueValue == zc::none || !ZC_ASSERT_NONNULL(innerTrueValue)) return false;
  if (innerSwitch.discriminant.kind() != MirOperandKind::Copy ||
      innerSwitch.discriminant.place().local() != innerCondTemp ||
      innerSwitch.discriminant.place().rootType() != innerCond.type ||
      innerSwitch.discriminant.place().resultType() != innerCond.type ||
      innerSwitch.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Validate exit block: Assign(result = copy(acc_0), Initialize), Return.
  const auto& exitAssign = exit.statements[0].assignmentValue();
  if (exitAssign.initialization != MirInitializationKind::Initialize ||
      exitAssign.destination.local() != resultLocal ||
      exitAssign.destination.rootType() != declaration.resultType ||
      exitAssign.destination.resultType() != declaration.resultType ||
      exitAssign.destination.projections().size() != 0 ||
      exitAssign.value.kind() != MirRvalueKind::Use ||
      exitAssign.value.useValue().operand.kind() != MirOperandKind::Copy ||
      exitAssign.value.useValue().operand.place().local() != localId(parameterCount + 1) ||
      exitAssign.value.useValue().operand.place().rootType() != firstAccLocal.type ||
      exitAssign.value.useValue().operand.place().resultType() != firstAccLocal.type ||
      exitAssign.value.useValue().operand.place().projections().size() != 0) {
    return false;
  }
  if (exit.terminator.kind() != MirTerminatorKind::Return) return false;
  ZC_IF_SOME(value, exit.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, declaration.resultType) &&
           value.place().local() == resultLocal &&
           value.place().rootType() == declaration.resultType &&
           value.place().resultType() == declaration.resultType &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validForLoopAccumulatorReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLoopStatement& loop,
    const hir::HirReturnStatement& sourceReturn, const hir::HirPrimitiveBinaryExpression& condition,
    const hir::HirLocalReferenceExpression& returnRef, const hir::VerifiedHirModule& hirModule,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  const bool hasBreakCondition = loop.breakCondition.isValid();
  // The source block has N+3 statements: N accumulator locals, the loop-init
  // local, the loop, and the return. N = statements.size() - 3.
  const size_t accumulatorCount = sourceBlock.statements.size() - 3;
  if (accumulatorCount == 0) return false;
  // The loop body has N+1 statements: N accumulator writes and the update
  // write. The if-guarded break condition is wired to the loop statement's
  // breakCondition field, not listed as a body statement.
  const size_t expectedBodyCount = accumulatorCount + 1;
  // The guarded-break CFG has one extra local (break temp) and one extra
  // block (guard).
  const size_t expectedLocalCount = parameterCount + accumulatorCount + (hasBreakCondition ? 4 : 3);
  const size_t expectedBlockCount = hasBreakCondition ? 5 : 4;
  // Resolve the N accumulator locals and the init local from the source block.
  zc::Vector<const hir::HirLocalBinding*> accLocals;
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto local = localFor(hirModule, sourceBlock.statements[k]);
    if (local == zc::none) return false;
    accLocals.add(&ZC_ASSERT_NONNULL(local));
  }
  auto initLocalMaybe = localFor(hirModule, sourceBlock.statements[accumulatorCount]);
  if (initLocalMaybe == zc::none) return false;
  const auto& initLocal = ZC_ASSERT_NONNULL(initLocalMaybe);
  const auto& accLocal = *accLocals[0];
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != expectedLocalCount ||
      function.blocks.size() != expectedBlockCount || declaration.body != sourceBlock.node ||
      sourceBlock.statements.size() != accumulatorCount + 3 ||
      sourceBlock.statements[accumulatorCount] != initLocal.node ||
      sourceBlock.statements[accumulatorCount + 1] != loop.node ||
      sourceBlock.statements[accumulatorCount + 2] != sourceReturn.node ||
      sourceReturn.value != returnRef.node || sourceReturn.resultType != declaration.resultType ||
      loop.condition != condition.node || loop.type != condition.type ||
      loop.body.size() != expectedBodyCount || initLocal.initializer == zc::none ||
      initLocal.local.ordinal() != static_cast<uint32_t>(accumulatorCount + 1) ||
      initLocal.type != condition.operandType || returnRef.local != accLocal.local ||
      returnRef.type != accLocal.type) {
    return false;
  }
  // Validate each accumulator local's ordinal and initializer.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    if (accLocals[k]->initializer == zc::none ||
        accLocals[k]->local.ordinal() != static_cast<uint32_t>(k + 1) ||
        accLocals[k]->type != declaration.resultType) {
      return false;
    }
  }
  // Resolve the condition operands.
  auto conditionLeft = localReferenceFor(hirModule, condition.left);
  auto conditionRight = expressionFor(hirModule, condition.right);
  if (conditionLeft == zc::none || conditionRight == zc::none) return false;
  const auto& condLeft = ZC_ASSERT_NONNULL(conditionLeft);
  const auto& condRight = ZC_ASSERT_NONNULL(conditionRight);
  // Resolve the N body writes and the update write.
  zc::Vector<const hir::HirLocalWriteStatement*> bodyWrites;
  zc::Vector<const hir::HirPrimitiveBinaryExpression*> bodyWriteBinaries;
  zc::Vector<const hir::HirLocalReferenceExpression*> bodyWriteLhss;
  zc::Vector<zc::Maybe<const hir::HirLocalReferenceExpression&>> bodyWriteRhss;
  zc::Vector<zc::Maybe<const hir::HirScalarLiteralExpression&>> bodyWriteRhsLits;
  zc::Vector<MirArithmeticOperator> bodyWriteOperators;
  zc::Vector<bool> bodyWriteRhsIsLiteral;
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto bw = localWriteFor(hirModule, loop.body[k]);
    if (bw == zc::none) return false;
    const auto& bwRef = ZC_ASSERT_NONNULL(bw);
    auto bwBin = primitiveBinaryFor(hirModule, bwRef.value);
    if (bwBin == zc::none) return false;
    const auto& bwBinRef = ZC_ASSERT_NONNULL(bwBin);
    auto bwLhs = localReferenceFor(hirModule, bwBinRef.left);
    if (bwLhs == zc::none) return false;
    // The right operand is either a local reference to the init local or a
    // scalar literal.
    auto bwRhsRef = localReferenceFor(hirModule, bwBinRef.right);
    auto bwRhsLit = expressionFor(hirModule, bwBinRef.right);
    const bool rhsIsLit = bwRhsRef == zc::none && bwRhsLit != zc::none;
    if (bwRhsRef == zc::none && bwRhsLit == zc::none) return false;
    auto bwOp = mirArithmeticOperatorFor(bwBinRef.operation);
    if (bwOp == zc::none) return false;
    bodyWrites.add(&bwRef);
    bodyWriteBinaries.add(&bwBinRef);
    bodyWriteLhss.add(&ZC_ASSERT_NONNULL(bwLhs));
    bodyWriteRhss.add(zc::mv(bwRhsRef));
    bodyWriteRhsLits.add(zc::mv(bwRhsLit));
    bodyWriteOperators.add(ZC_ASSERT_NONNULL(bwOp));
    bodyWriteRhsIsLiteral.add(rhsIsLit);
  }
  // Resolve the update write (i = i <bin> <lit>) and its arithmetic value.
  auto updateWrite = localWriteFor(hirModule, loop.body[accumulatorCount]);
  if (updateWrite == zc::none) return false;
  const auto& uw = ZC_ASSERT_NONNULL(updateWrite);
  auto uwValue = primitiveBinaryFor(hirModule, uw.value);
  if (uwValue == zc::none) return false;
  const auto& uwBin = ZC_ASSERT_NONNULL(uwValue);
  auto uwLeft = localReferenceFor(hirModule, uwBin.left);
  auto uwRight = expressionFor(hirModule, uwBin.right);
  if (uwLeft == zc::none || uwRight == zc::none) return false;
  const auto& uwLhs = ZC_ASSERT_NONNULL(uwLeft);
  const auto& uwRhs = ZC_ASSERT_NONNULL(uwRight);
  const auto comparisonOperator = mirComparisonOperatorFor(condition.operation);
  const auto uwOperator = mirArithmeticOperatorFor(uwBin.operation);
  if (comparisonOperator == zc::none || uwOperator == zc::none ||
      condLeft.local != initLocal.local || condLeft.type != initLocal.type ||
      condRight.type != initLocal.type || uw.local != initLocal.local || uw.field != zc::none ||
      uw.type != initLocal.type || uw.kind != hir::HirLocalWriteKind::Overwrite ||
      uwBin.operandType != initLocal.type || uwBin.type != initLocal.type ||
      uwLhs.local != initLocal.local || uwLhs.type != initLocal.type ||
      uwRhs.type != initLocal.type) {
    return false;
  }
  // Validate each body write.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto& bw = *bodyWrites[k];
    const auto& bwBin = *bodyWriteBinaries[k];
    const auto& bwLhs = *bodyWriteLhss[k];
    if (bw.local != accLocals[k]->local || bw.field != zc::none || bw.type != accLocals[k]->type ||
        bw.kind != hir::HirLocalWriteKind::Overwrite || bwBin.operandType != accLocals[k]->type ||
        bwBin.type != accLocals[k]->type || bwLhs.local != accLocals[k]->local ||
        bwLhs.type != accLocals[k]->type) {
      return false;
    }
    if (bodyWriteRhsIsLiteral[k]) {
      const auto& bwRhsLit = ZC_ASSERT_NONNULL(bodyWriteRhsLits[k]);
      if (bwRhsLit.type != accLocals[k]->type) return false;
    } else {
      const auto& bwRhs = ZC_ASSERT_NONNULL(bodyWriteRhss[k]);
      if (bwRhs.local != initLocal.local || bwRhs.type != initLocal.type) return false;
    }
  }
  const auto resultLocal = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) +
                                   (hasBreakCondition ? 4 : 3));
  const auto initMirLocal = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 1);
  const auto conditionTemp = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 2);
  const auto breakTemp = localId(parameterCount + static_cast<uint32_t>(accumulatorCount) + 3);
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (uint32_t i = 0; i < parameterCount; ++i) {
    const auto& parameterLocal = function.locals[i];
    if (parameterLocal.id != localId(i + 1) || parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scopeId(1) ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  // Validate accumulator locals.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto& acc = function.locals[parameterCount + k];
    if (acc.id != localId(parameterCount + static_cast<uint32_t>(k) + 1) ||
        acc.kind != MirLocalKind::UserLocal || acc.type != accLocals[k]->type ||
        acc.sourceScope != scopeId(1) || !sameSpan(acc.sourceSpan, accLocals[k]->sourceSpan)) {
      return false;
    }
  }
  // Resolve the if-guarded break condition when present.
  zc::Maybe<const hir::HirPrimitiveBinaryExpression&> breakBinary;
  zc::Maybe<const hir::HirLocalReferenceExpression&> breakLeft;
  zc::Maybe<const hir::HirScalarLiteralExpression&> breakRight;
  zc::Maybe<MirComparisonOperator> breakOp;
  if (hasBreakCondition) {
    breakBinary = primitiveBinaryFor(hirModule, loop.breakCondition);
    if (breakBinary == zc::none) return false;
    breakLeft = localReferenceFor(hirModule, ZC_ASSERT_NONNULL(breakBinary).left);
    breakRight = expressionFor(hirModule, ZC_ASSERT_NONNULL(breakBinary).right);
    breakOp = mirComparisonOperatorFor(ZC_ASSERT_NONNULL(breakBinary).operation);
    if (breakLeft == zc::none || breakRight == zc::none || breakOp == zc::none) return false;
    const auto& bl = ZC_ASSERT_NONNULL(breakLeft);
    if (bl.local != initLocal.local || bl.type != initLocal.type) return false;
  }
  const auto& init = function.locals[parameterCount + accumulatorCount];
  const auto& temp = function.locals[parameterCount + accumulatorCount + 1];
  const auto& result =
      function.locals[parameterCount + accumulatorCount + (hasBreakCondition ? 3 : 2)];
  if (init.id != initMirLocal || init.kind != MirLocalKind::UserLocal ||
      init.type != initLocal.type || init.sourceScope != scopeId(1) ||
      !sameSpan(init.sourceSpan, initLocal.sourceSpan) || temp.id != conditionTemp ||
      temp.kind != MirLocalKind::Temporary || temp.type != condition.type ||
      temp.sourceScope != scopeId(1) || !sameSpan(temp.sourceSpan, condition.sourceSpan) ||
      result.id != resultLocal || result.kind != MirLocalKind::FunctionResult ||
      result.type != declaration.resultType || result.sourceScope != scopeId(1) ||
      !sameSpan(result.sourceSpan, sourceReturn.sourceSpan)) {
    return false;
  }
  if (hasBreakCondition) {
    const auto& breakTempLocal = function.locals[parameterCount + accumulatorCount + 2];
    const auto& bb = ZC_ASSERT_NONNULL(breakBinary);
    if (breakTempLocal.id != breakTemp || breakTempLocal.kind != MirLocalKind::Temporary ||
        breakTempLocal.type != bb.type || breakTempLocal.sourceScope != scopeId(1) ||
        !sameSpan(breakTempLocal.sourceSpan, bb.sourceSpan)) {
      return false;
    }
  }
  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[hasBreakCondition ? 3 : 2];
  const auto& exit = function.blocks[hasBreakCondition ? 4 : 3];
  // A trailing break exits the body to the loop exit; a trailing
  // continue or a write-only body jumps back to the header (bb2).
  const auto exitBlockId = hasBreakCondition ? blockId(5) : blockId(4);
  const auto expectedBodyTarget = loop.breakSpan != zc::none ? exitBlockId : blockId(2);
  // Reducible loop CFG. Without a guarded break: entry/header/body/exit.
  // With a guarded break: entry/header/guard/continuation/exit, where the
  // guard block evaluates the break condition and routes to the exit (break
  // taken) or the continuation (break not taken).
  //   bb1 entry:  StorageLive(result) ; StorageLive(acc_k)... ; StorageLive(i) ;
  //               StorageLive(temp) ; [StorageLive(breakTemp) ;]
  //               Assign(acc_k = accInit_k, Initialize)... ;
  //               Assign(i = init, Initialize) ;
  //               Assign(temp = Comparison(op, copy(i), <lit>), Initialize) ; Goto(bb2)
  //   bb2 header: SwitchInt(copy(temp), [true -> bb3], default = exit)
  //   [bb3 guard: Assign(breakTemp = Comparison(op, copy(i), <lit>), Overwrite) ;
  //               SwitchInt(copy(breakTemp), [true -> exit], default = bb4)]
  //   bbN body:   Assign(acc_k = Arithmetic(op, copy(acc_k), copy(i)|<lit>), Overwrite)... ;
  //               Assign(i = Arithmetic(op, copy(i), <lit>), Overwrite) ;
  //               Assign(temp = Comparison(op, copy(i), <lit>), Overwrite) ; Goto(bb2)
  //   bbX exit:   Assign(result = copy(acc_0), Initialize) ; Return(placeUse(result))
  const size_t expectedEntrySize = (hasBreakCondition ? 6 : 5) + 2 * accumulatorCount;
  const size_t expectedBodySize = 2 + accumulatorCount;
  const auto bodyBlockId = hasBreakCondition ? blockId(4) : blockId(3);
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) ||
      entry.statements.size() != expectedEntrySize ||
      entry.terminator.kind() != MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != blockId(2) || header.id != blockId(2) ||
      header.sourceScope != scopeId(1) || header.statements.size() != 0 ||
      header.terminator.kind() != MirTerminatorKind::SwitchInt || body.id != bodyBlockId ||
      body.sourceScope != scopeId(1) || body.statements.size() != expectedBodySize ||
      body.terminator.kind() != MirTerminatorKind::Goto ||
      body.terminator.gotoValue().target != expectedBodyTarget || exit.id != exitBlockId ||
      exit.sourceScope != scopeId(1) || exit.statements.size() != 1 ||
      exit.terminator.kind() != MirTerminatorKind::Return) {
    return false;
  }
  // Validate the guard block when present.
  if (hasBreakCondition) {
    const auto& guard = function.blocks[2];
    if (guard.id != blockId(3) || guard.sourceScope != scopeId(1) || guard.statements.size() != 1 ||
        guard.terminator.kind() != MirTerminatorKind::SwitchInt) {
      return false;
    }
    const auto& guardAssign = guard.statements[0].assignmentValue();
    const auto& bb = ZC_ASSERT_NONNULL(breakBinary);
    const auto& br = ZC_ASSERT_NONNULL(breakRight);
    if (guardAssign.initialization != MirInitializationKind::Overwrite ||
        guardAssign.destination.local() != breakTemp ||
        guardAssign.destination.rootType() != bb.type ||
        guardAssign.destination.resultType() != bb.type ||
        guardAssign.destination.projections().size() != 0 ||
        guardAssign.value.kind() != MirRvalueKind::Comparison) {
      return false;
    }
    const auto& guardComparison = guardAssign.value.comparisonValue();
    if (guardComparison.op != ZC_ASSERT_NONNULL(breakOp) || guardComparison.resultType != bb.type ||
        guardComparison.left.kind() != MirOperandKind::Copy ||
        guardComparison.left.place().local() != initMirLocal ||
        guardComparison.left.place().rootType() != initLocal.type ||
        guardComparison.left.place().resultType() != initLocal.type ||
        guardComparison.left.place().projections().size() != 0 ||
        guardComparison.right.kind() != MirOperandKind::Constant ||
        guardComparison.right.constantValue().type != br.type ||
        !sameConstant(guardComparison.right.constantValue().value, br.value, module, identities,
                      semanticTypes)) {
      return false;
    }
    const auto& guardSwitch = guard.terminator.switchIntValue();
    if (guardSwitch.arms.size() != 1 || guardSwitch.defaultTarget != bodyBlockId) return false;
    const auto& guardTrueArm = guardSwitch.arms[0];
    if (guardTrueArm.target != exitBlockId) return false;
    auto guardTrueValue = guardTrueArm.value.booleanValue();
    if (guardTrueValue == zc::none || !ZC_ASSERT_NONNULL(guardTrueValue)) return false;
    if (guardSwitch.discriminant.kind() != MirOperandKind::Copy ||
        guardSwitch.discriminant.place().local() != breakTemp ||
        guardSwitch.discriminant.place().rootType() != bb.type ||
        guardSwitch.discriminant.place().resultType() != bb.type ||
        guardSwitch.discriminant.place().projections().size() != 0) {
      return false;
    }
  }
  // Entry: StorageLive(result), StorageLive(acc_k)..., StorageLive(i),
  // StorageLive(temp), [StorageLive(breakTemp)].
  if (entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocal ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto accMirLocal = localId(parameterCount + static_cast<uint32_t>(k) + 1);
    if (entry.statements[1 + k].kind() != MirStatementKind::StorageLive ||
        entry.statements[1 + k].storageLocal() != accMirLocal ||
        !sameSpan(entry.statements[1 + k].sourceSpan(), accLocals[k]->sourceSpan)) {
      return false;
    }
  }
  const size_t initStorageLiveIndex = 1 + accumulatorCount;
  const size_t tempStorageLiveIndex = 2 + accumulatorCount;
  if (entry.statements[initStorageLiveIndex].kind() != MirStatementKind::StorageLive ||
      entry.statements[initStorageLiveIndex].storageLocal() != initMirLocal ||
      !sameSpan(entry.statements[initStorageLiveIndex].sourceSpan(), initLocal.sourceSpan) ||
      entry.statements[tempStorageLiveIndex].kind() != MirStatementKind::StorageLive ||
      entry.statements[tempStorageLiveIndex].storageLocal() != conditionTemp ||
      !sameSpan(entry.statements[tempStorageLiveIndex].sourceSpan(), condition.sourceSpan)) {
    return false;
  }
  if (hasBreakCondition) {
    const size_t breakStorageLiveIndex = 3 + accumulatorCount;
    const auto& bb = ZC_ASSERT_NONNULL(breakBinary);
    if (entry.statements[breakStorageLiveIndex].kind() != MirStatementKind::StorageLive ||
        entry.statements[breakStorageLiveIndex].storageLocal() != breakTemp ||
        !sameSpan(entry.statements[breakStorageLiveIndex].sourceSpan(), bb.sourceSpan)) {
      return false;
    }
  }
  // The break-condition StorageLive shifts all subsequent entry statements by
  // one when present.
  const size_t entryOffset = hasBreakCondition ? 1 : 0;
  // Entry: Assign(acc_k = accInit_k, Initialize)...
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto accMirLocal = localId(parameterCount + static_cast<uint32_t>(k) + 1);
    const size_t assignIndex = 3 + accumulatorCount + entryOffset + k;
    if (entry.statements[assignIndex].kind() != MirStatementKind::Assign) return false;
    const auto& accInitAssign = entry.statements[assignIndex].assignmentValue();
    hir::HirNodeId accInitializerNode;
    ZC_IF_SOME(value, accLocals[k]->initializer) { accInitializerNode = value; }
    auto accInitLiteral = expressionFor(hirModule, accInitializerNode);
    if (accInitLiteral == zc::none ||
        accInitAssign.initialization != MirInitializationKind::Initialize ||
        accInitAssign.destination.local() != accMirLocal ||
        accInitAssign.destination.rootType() != accLocals[k]->type ||
        accInitAssign.destination.resultType() != accLocals[k]->type ||
        accInitAssign.destination.projections().size() != 0 ||
        accInitAssign.value.kind() != MirRvalueKind::Use ||
        accInitAssign.value.useValue().operand.kind() != MirOperandKind::Constant ||
        accInitAssign.value.useValue().operand.constantValue().type !=
            ZC_ASSERT_NONNULL(accInitLiteral).type ||
        !sameConstant(accInitAssign.value.useValue().operand.constantValue().value,
                      ZC_ASSERT_NONNULL(accInitLiteral).value, module, identities, semanticTypes)) {
      return false;
    }
  }
  // Entry: Assign(i = init, Initialize).
  const size_t initAssignIndex = 3 + 2 * accumulatorCount + entryOffset;
  if (entry.statements[initAssignIndex].kind() != MirStatementKind::Assign) return false;
  const auto& initAssign = entry.statements[initAssignIndex].assignmentValue();
  hir::HirNodeId initInitializerNode;
  ZC_IF_SOME(value, initLocal.initializer) { initInitializerNode = value; }
  auto initLiteral = expressionFor(hirModule, initInitializerNode);
  if (initLiteral == zc::none || initAssign.initialization != MirInitializationKind::Initialize ||
      initAssign.destination.local() != initMirLocal ||
      initAssign.destination.rootType() != initLocal.type ||
      initAssign.destination.resultType() != initLocal.type ||
      initAssign.destination.projections().size() != 0 ||
      initAssign.value.kind() != MirRvalueKind::Use ||
      initAssign.value.useValue().operand.kind() != MirOperandKind::Constant ||
      initAssign.value.useValue().operand.constantValue().type !=
          ZC_ASSERT_NONNULL(initLiteral).type ||
      !sameConstant(initAssign.value.useValue().operand.constantValue().value,
                    ZC_ASSERT_NONNULL(initLiteral).value, module, identities, semanticTypes)) {
    return false;
  }
  // Entry: Assign(temp = Comparison(op, copy(i), <lit>), Initialize).
  const size_t entryCondIndex = 4 + 2 * accumulatorCount + entryOffset;
  if (entry.statements[entryCondIndex].kind() != MirStatementKind::Assign) return false;
  const auto& entryCondAssign = entry.statements[entryCondIndex].assignmentValue();
  if (entryCondAssign.initialization != MirInitializationKind::Initialize ||
      entryCondAssign.destination.local() != conditionTemp ||
      entryCondAssign.destination.rootType() != condition.type ||
      entryCondAssign.destination.resultType() != condition.type ||
      entryCondAssign.destination.projections().size() != 0 ||
      entryCondAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& entryComparison = entryCondAssign.value.comparisonValue();
  if (entryComparison.op != ZC_ASSERT_NONNULL(comparisonOperator) ||
      entryComparison.resultType != condition.type ||
      entryComparison.left.kind() != MirOperandKind::Copy ||
      entryComparison.left.place().local() != initMirLocal ||
      entryComparison.left.place().rootType() != initLocal.type ||
      entryComparison.left.place().resultType() != initLocal.type ||
      entryComparison.left.place().projections().size() != 0 ||
      entryComparison.right.kind() != MirOperandKind::Constant ||
      entryComparison.right.constantValue().type != condRight.type ||
      !sameConstant(entryComparison.right.constantValue().value, condRight.value, module,
                    identities, semanticTypes)) {
    return false;
  }
  // Header SwitchInt: [true -> bb3], default = exit.
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.arms.size() != 1 || switchInt.defaultTarget != exitBlockId) return false;
  const auto& trueArm = switchInt.arms[0];
  if (trueArm.target != blockId(3)) return false;
  auto trueValue = trueArm.value.booleanValue();
  if (trueValue == zc::none || !ZC_ASSERT_NONNULL(trueValue)) return false;
  if (switchInt.discriminant.kind() != MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != conditionTemp ||
      switchInt.discriminant.place().rootType() != condition.type ||
      switchInt.discriminant.place().resultType() != condition.type ||
      switchInt.discriminant.place().projections().size() != 0) {
    return false;
  }
  // Body: Assign(acc_k = Arithmetic(op, copy(acc_k), copy(i)|<lit>), Overwrite)...
  for (size_t k = 0; k < accumulatorCount; ++k) {
    const auto accMirLocal = localId(parameterCount + static_cast<uint32_t>(k) + 1);
    if (body.statements[k].kind() != MirStatementKind::Assign) return false;
    const auto& bodyAccAssign = body.statements[k].assignmentValue();
    if (bodyAccAssign.initialization != MirInitializationKind::Overwrite ||
        bodyAccAssign.destination.local() != accMirLocal ||
        bodyAccAssign.destination.rootType() != accLocals[k]->type ||
        bodyAccAssign.destination.resultType() != accLocals[k]->type ||
        bodyAccAssign.destination.projections().size() != 0 ||
        bodyAccAssign.value.kind() != MirRvalueKind::Arithmetic) {
      return false;
    }
    const auto& accArithmetic = bodyAccAssign.value.arithmeticValue();
    if (accArithmetic.op != bodyWriteOperators[k] ||
        accArithmetic.resultType != bodyWriteBinaries[k]->type ||
        accArithmetic.left.kind() != MirOperandKind::Copy ||
        accArithmetic.left.place().local() != accMirLocal ||
        accArithmetic.left.place().rootType() != accLocals[k]->type ||
        accArithmetic.left.place().resultType() != accLocals[k]->type ||
        accArithmetic.left.place().projections().size() != 0) {
      return false;
    }
    if (bodyWriteRhsIsLiteral[k]) {
      const auto& bwRhsLit = ZC_ASSERT_NONNULL(bodyWriteRhsLits[k]);
      if (accArithmetic.right.kind() != MirOperandKind::Constant ||
          accArithmetic.right.constantValue().type != bwRhsLit.type ||
          !sameConstant(accArithmetic.right.constantValue().value, bwRhsLit.value, module,
                        identities, semanticTypes)) {
        return false;
      }
    } else {
      if (accArithmetic.right.kind() != MirOperandKind::Copy ||
          accArithmetic.right.place().local() != initMirLocal ||
          accArithmetic.right.place().rootType() != initLocal.type ||
          accArithmetic.right.place().resultType() != initLocal.type ||
          accArithmetic.right.place().projections().size() != 0) {
        return false;
      }
    }
  }
  // Body[N]: Assign(i = Arithmetic(op, copy(i), <lit>), Overwrite).
  const size_t bodyUpdateIndex = accumulatorCount;
  if (body.statements[bodyUpdateIndex].kind() != MirStatementKind::Assign) return false;
  const auto& bodyUpdateAssign = body.statements[bodyUpdateIndex].assignmentValue();
  if (bodyUpdateAssign.initialization != MirInitializationKind::Overwrite ||
      bodyUpdateAssign.destination.local() != initMirLocal ||
      bodyUpdateAssign.destination.rootType() != initLocal.type ||
      bodyUpdateAssign.destination.resultType() != initLocal.type ||
      bodyUpdateAssign.destination.projections().size() != 0 ||
      bodyUpdateAssign.value.kind() != MirRvalueKind::Arithmetic) {
    return false;
  }
  const auto& updateArithmetic = bodyUpdateAssign.value.arithmeticValue();
  if (updateArithmetic.op != ZC_ASSERT_NONNULL(uwOperator) ||
      updateArithmetic.resultType != uwBin.type ||
      updateArithmetic.left.kind() != MirOperandKind::Copy ||
      updateArithmetic.left.place().local() != initMirLocal ||
      updateArithmetic.left.place().rootType() != initLocal.type ||
      updateArithmetic.left.place().resultType() != initLocal.type ||
      updateArithmetic.left.place().projections().size() != 0 ||
      updateArithmetic.right.kind() != MirOperandKind::Constant ||
      updateArithmetic.right.constantValue().type != uwRhs.type ||
      !sameConstant(updateArithmetic.right.constantValue().value, uwRhs.value, module, identities,
                    semanticTypes)) {
    return false;
  }
  // Body[N+1]: Assign(temp = Comparison(op, copy(i), <lit>), Overwrite).
  const size_t bodyCondIndex = accumulatorCount + 1;
  if (body.statements[bodyCondIndex].kind() != MirStatementKind::Assign) return false;
  const auto& bodyCondAssign = body.statements[bodyCondIndex].assignmentValue();
  if (bodyCondAssign.initialization != MirInitializationKind::Overwrite ||
      bodyCondAssign.destination.local() != conditionTemp ||
      bodyCondAssign.destination.rootType() != condition.type ||
      bodyCondAssign.destination.resultType() != condition.type ||
      bodyCondAssign.destination.projections().size() != 0 ||
      bodyCondAssign.value.kind() != MirRvalueKind::Comparison) {
    return false;
  }
  const auto& bodyComparison = bodyCondAssign.value.comparisonValue();
  if (bodyComparison.op != ZC_ASSERT_NONNULL(comparisonOperator) ||
      bodyComparison.resultType != condition.type ||
      bodyComparison.left.kind() != MirOperandKind::Copy ||
      bodyComparison.left.place().local() != initMirLocal ||
      bodyComparison.left.place().rootType() != initLocal.type ||
      bodyComparison.left.place().resultType() != initLocal.type ||
      bodyComparison.left.place().projections().size() != 0 ||
      bodyComparison.right.kind() != MirOperandKind::Constant ||
      bodyComparison.right.constantValue().type != condRight.type ||
      !sameConstant(bodyComparison.right.constantValue().value, condRight.value, module, identities,
                    semanticTypes)) {
    return false;
  }
  // Exit: Assign(result = copy(acc_0), Initialize) ; Return(placeUse(result)).
  const auto& exitAssign = exit.statements[0].assignmentValue();
  const auto firstAccMirLocal = localId(parameterCount + 1);
  if (exitAssign.initialization != MirInitializationKind::Initialize ||
      exitAssign.destination.local() != resultLocal ||
      exitAssign.destination.rootType() != declaration.resultType ||
      exitAssign.destination.resultType() != declaration.resultType ||
      exitAssign.destination.projections().size() != 0 ||
      exitAssign.value.kind() != MirRvalueKind::Use ||
      exitAssign.value.useValue().operand.kind() != MirOperandKind::Copy ||
      exitAssign.value.useValue().operand.place().local() != firstAccMirLocal ||
      exitAssign.value.useValue().operand.place().rootType() != accLocal.type ||
      exitAssign.value.useValue().operand.place().resultType() != accLocal.type ||
      exitAssign.value.useValue().operand.place().projections().size() != 0) {
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
  // The returned parameter must be one of the declared parameters; for a method
  // the implicit receiver leads as local 1, so ordinary parameter locals start
  // at local 2. N == 1, K == 0 is the byte-identical single-parameter special
  // case.
  size_t referencedIndex = 0;
  bool referencedFound = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == reference.parameter &&
        declaration.parameters[i].type == reference.type) {
      referencedIndex = i;
      referencedFound = true;
      break;
    }
  }
  const bool isMethod = declaration.receiver != zc::none;
  const size_t leadingReceiverCount = isMethod ? 1 : 0;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind !=
          (isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function) ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + leadingReceiverCount ||
      function.blocks.size() != 1 || declaration.parameters.size() == 0 || !referencedFound ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != reference.node ||
      reference.type != declaration.resultType ||
      reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  size_t localSlot = 0;
  if (isMethod) {
    const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
    const auto& receiverLocal = function.locals[0];
    if (receiverLocal.id != localId(1) || receiverLocal.kind != MirLocalKind::Parameter ||
        receiverLocal.type != receiver.type || receiverLocal.sourceScope != scope.id ||
        !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan)) {
      return false;
    }
    localSlot = 1;
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    const auto& parameterLocal = function.locals[localSlot + i];
    if (parameterLocal.id != localId(static_cast<uint32_t>(i + 1 + leadingReceiverCount)) ||
        parameterLocal.kind != MirLocalKind::Parameter ||
        parameterLocal.type != declaration.parameters[i].type ||
        parameterLocal.sourceScope != scope.id ||
        !sameSpan(parameterLocal.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  const auto referencedLocal =
      localId(static_cast<uint32_t>(referencedIndex + 1 + leadingReceiverCount));
  if (block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 0 ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, reference.type) &&
           value.place().local() == referencedLocal && value.place().rootType() == reference.type &&
           value.place().resultType() == reference.type && value.place().projections().size() == 0;
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

bool validMethodScalarLocalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    const hir::HirLocalBinding& sourceLocal, const hir::HirScalarLiteralExpression& initializer,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) return false;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializer.node || sourceReturn.value != reference.node ||
      sourceLocal.local != reference.local || sourceLocal.type != declaration.resultType ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& receiverLocal = function.locals[0];
  const auto& local = function.locals[1];
  const auto& block = function.blocks[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || receiverLocal.id != localId(1) ||
      receiverLocal.kind != MirLocalKind::Parameter || receiverLocal.type != receiver.type ||
      receiverLocal.sourceScope != scope.id ||
      !sameSpan(receiverLocal.sourceSpan, receiver.sourceSpan) || local.id != localId(2) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      block.id != blockId(1) || block.sourceScope != scope.id || block.statements.size() != 2 ||
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
  // constant, a parameter reference to a copy place-use of its parameter local,
  // and a reference to the written user local (`x = x + 1`) to a copy
  // place-use of that same local.
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
    auto operandLocal = localReferenceFor(hirModule, operandNode);
    ZC_IF_SOME(localRef, operandLocal) {
      return localRef.type == overwriteBinary.operandType && localRef.local == reference.local &&
             matchesPlaceUse(operand, proofs, copy, overwriteBinary.operandType) &&
             operand.place().local() == userLocalId &&
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

/// \brief Validates a single user-local body with one or more overwrite writes.
///
/// `mut x = <lit/param>; x = <lit/param/binary>; ...; return x;`. The MIR body
/// has one user local, one entry block with StorageLive, an Initialize Assign,
/// one Overwrite Assign per write, and a Return terminator. A binary write value
/// lowers to an Arithmetic rvalue whose operands are constants, parameter
/// place-uses, or a place-use of the user local.
bool validLocalWriteReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& reference,
    const hir::VerifiedHirModule& hirModule, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  const auto userLocalId = localId(parameterCount + 1);
  const size_t writeCount = sourceBlock.statements.size() - 2;
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != parameterCount + 1u || function.blocks.size() != 1 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() < 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[sourceBlock.statements.size() - 1] != sourceReturn.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != declaration.resultType || reference.type != sourceLocal.type ||
      reference.category != hir::HirValueCategory::Place) {
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
      block.sourceScope != scope.id || block.statements.size() != 2 + writeCount ||
      block.statements[0].kind() != MirStatementKind::StorageLive ||
      block.statements[0].storageLocal() != userLocalId ||
      !sameSpan(block.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      block.statements[1].kind() != MirStatementKind::Assign ||
      block.statements[1].assignmentValue().initialization != MirInitializationKind::Initialize ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none ||
      !sameSpan(block.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  // The initialize assignment writes the initializer into the user local.
  const auto& initializeAssign = block.statements[1].assignmentValue();
  if (initializeAssign.destination.local() != userLocalId ||
      initializeAssign.destination.rootType() != local.type ||
      initializeAssign.destination.resultType() != local.type ||
      initializeAssign.destination.projections().size() != 0 ||
      initializeAssign.value.kind() != MirRvalueKind::Use) {
    return false;
  }
  const auto& initializeOperand = initializeAssign.value.useValue().operand;
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(initializer, sourceLocal.initializer) { initializerNode = initializer; }
  auto initializerLiteral = expressionFor(hirModule, initializerNode);
  auto initializerParameter = parameterReferenceFor(hirModule, initializerNode);
  if (initializerLiteral != zc::none) {
    if (initializeOperand.kind() != MirOperandKind::Constant ||
        initializeOperand.constantValue().type != sourceLocal.type ||
        !sameConstant(initializeOperand.constantValue().value,
                      ZC_ASSERT_NONNULL(initializerLiteral).value, module, identities,
                      semanticTypes) ||
        !sameSpan(block.statements[1].sourceSpan(),
                  ZC_ASSERT_NONNULL(initializerLiteral).sourceSpan)) {
      return false;
    }
  } else if (initializerParameter != zc::none) {
    const auto& param = ZC_ASSERT_NONNULL(initializerParameter);
    size_t parameterIndex = 0;
    bool found = false;
    for (size_t p = 0; p < declaration.parameters.size(); ++p) {
      if (declaration.parameters[p].key == param.parameter) {
        parameterIndex = p;
        found = true;
        break;
      }
    }
    if (!found || param.type != sourceLocal.type ||
        !matchesPlaceUse(initializeOperand, proofs, copy, sourceLocal.type) ||
        initializeOperand.place().local() != localId(static_cast<uint32_t>(parameterIndex) + 1) ||
        initializeOperand.place().rootType() != sourceLocal.type ||
        initializeOperand.place().resultType() != sourceLocal.type ||
        initializeOperand.place().projections().size() != 0) {
      return false;
    }
  } else {
    return false;
  }
  // Validate each overwrite write.
  auto operandMatches = [&](const MirOperand& operand, hir::HirNodeId operandNode,
                            identity::SemanticTypeId operandType) -> bool {
    auto operandLiteral = expressionFor(hirModule, operandNode);
    ZC_IF_SOME(literalValue, operandLiteral) {
      return operand.kind() == MirOperandKind::Constant &&
             operand.constantValue().type == operandType && literalValue.type == operandType &&
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
      return found && parameter.type == operandType &&
             matchesPlaceUse(operand, proofs, copy, operandType) &&
             operand.place().local() == localId(static_cast<uint32_t>(parameterIndex) + 1) &&
             operand.place().rootType() == operandType &&
             operand.place().resultType() == operandType &&
             operand.place().projections().size() == 0;
    }
    auto operandLocal = localReferenceFor(hirModule, operandNode);
    ZC_IF_SOME(localRef, operandLocal) {
      return localRef.type == operandType && localRef.local == reference.local &&
             matchesPlaceUse(operand, proofs, copy, operandType) &&
             operand.place().local() == userLocalId && operand.place().rootType() == operandType &&
             operand.place().resultType() == operandType &&
             operand.place().projections().size() == 0;
    }
    return false;
  };
  for (size_t i = 0; i < writeCount; ++i) {
    auto sourceWrite = localWriteFor(hirModule, sourceBlock.statements[1 + i]);
    if (sourceWrite == zc::none) { return false; }
    const auto& write = ZC_ASSERT_NONNULL(sourceWrite);
    if (write.kind != hir::HirLocalWriteKind::Overwrite || write.field != zc::none ||
        write.local != sourceLocal.local || write.type != sourceLocal.type) {
      return false;
    }
    const auto& assign = block.statements[2 + i].assignmentValue();
    if (block.statements[2 + i].kind() != MirStatementKind::Assign ||
        assign.initialization != MirInitializationKind::Overwrite ||
        assign.destination.local() != userLocalId || assign.destination.rootType() != local.type ||
        assign.destination.resultType() != local.type ||
        assign.destination.projections().size() != 0 ||
        !sameSpan(block.statements[2 + i].sourceSpan(), write.sourceSpan)) {
      return false;
    }
    auto writeLiteral = expressionFor(hirModule, write.value);
    auto writeParameter = parameterReferenceFor(hirModule, write.value);
    auto writeBinary = primitiveBinaryFor(hirModule, write.value);
    if (writeLiteral != zc::none) {
      if (assign.value.kind() != MirRvalueKind::Use ||
          assign.value.useValue().operand.kind() != MirOperandKind::Constant ||
          assign.value.useValue().operand.constantValue().type != sourceLocal.type ||
          !sameConstant(assign.value.useValue().operand.constantValue().value,
                        ZC_ASSERT_NONNULL(writeLiteral).value, module, identities, semanticTypes)) {
        return false;
      }
    } else if (writeParameter != zc::none) {
      const auto& param = ZC_ASSERT_NONNULL(writeParameter);
      size_t parameterIndex = 0;
      bool found = false;
      for (size_t p = 0; p < declaration.parameters.size(); ++p) {
        if (declaration.parameters[p].key == param.parameter) {
          parameterIndex = p;
          found = true;
          break;
        }
      }
      if (!found || param.type != sourceLocal.type || assign.value.kind() != MirRvalueKind::Use ||
          !matchesPlaceUse(assign.value.useValue().operand, proofs, copy, sourceLocal.type) ||
          assign.value.useValue().operand.place().local() !=
              localId(static_cast<uint32_t>(parameterIndex) + 1) ||
          assign.value.useValue().operand.place().rootType() != sourceLocal.type ||
          assign.value.useValue().operand.place().resultType() != sourceLocal.type ||
          assign.value.useValue().operand.place().projections().size() != 0) {
        return false;
      }
    } else if (writeBinary != zc::none) {
      const auto& binary = ZC_ASSERT_NONNULL(writeBinary);
      auto arithmetic = mirArithmeticOperatorFor(binary.operation);
      if (arithmetic == zc::none) { return false; }
      if (binary.type != sourceLocal.type || binary.operandType != sourceLocal.type) {
        return false;
      }
      if (assign.value.kind() != MirRvalueKind::Arithmetic) { return false; }
      const auto& rvalue = assign.value.arithmeticValue();
      if (rvalue.op != ZC_ASSERT_NONNULL(arithmetic) || rvalue.resultType != binary.type ||
          !operandMatches(rvalue.left, binary.left, binary.operandType) ||
          !operandMatches(rvalue.right, binary.right, binary.operandType)) {
        return false;
      }
    } else {
      return false;
    }
  }
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    bool result = matchesPlaceUse(value, proofs, copy, local.type) &&
                  value.place().local() == userLocalId && value.place().rootType() == local.type &&
                  value.place().resultType() == local.type &&
                  value.place().projections().size() == 0;
    return result;
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
  if (!isSequentialLocalReturnBlock(hirModule, sourceBlock)) return false;
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
      // A leading local carries its own binding's type; mixed-type locals
      // (e.g. a bool local in an i32 function) are legal. The returned local
      // is separately checked against the function result type at the return.
      if (local.node != sourceBlock.statements[i] ||
          local.local.ordinal() != static_cast<uint32_t>(i + 1) || local.initializer == zc::none) {
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

/// \brief Validates the four-block diamond for a ternary-initialized local:
/// `let a: T = ...; ...; let r: T = cond ? lt : le; return r;`. The entry
/// block initializes the K leading locals, storage-lives the ternary local, and
/// switches on the bool condition; the two arm blocks assign the branch
/// literals; the join block returns the ternary local.
bool validTernaryLocalReturnFunction(
    const MirFunction& function, const hir::VerifiedHirModule& hirModule,
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& sourceBlock,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy) {
  const size_t bindingCount = sourceBlock.statements.size() - 1;
  if (bindingCount < 1 || declaration.receiver != zc::none) return false;
  const size_t parameterCount = declaration.parameters.size();
  const size_t ternaryIndex = bindingCount - 1;
  // Resolve the HIR bindings.
  zc::Vector<const hir::HirLocalBinding*> bindings;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto binding = localFor(hirModule, sourceBlock.statements[i]);
    if (binding == zc::none) return false;
    const auto& value = ZC_ASSERT_NONNULL(binding);
    if (value.local.ordinal() != static_cast<uint32_t>(i + 1) || value.initializer == zc::none) {
      return false;
    }
    bindings.add(&value);
  }
  // The last binding's initializer is the conditional.
  hir::HirNodeId ternaryInitializer;
  ZC_IF_SOME(initializer, bindings[ternaryIndex]->initializer) { ternaryInitializer = initializer; }
  auto conditional = conditionalFor(hirModule, ternaryInitializer);
  if (conditional == zc::none) return false;
  const auto& cond = ZC_ASSERT_NONNULL(conditional);
  if (cond.type != declaration.resultType || cond.category != hir::HirValueCategory::Value) {
    return false;
  }
  auto conditionParameter = parameterReferenceFor(hirModule, cond.condition);
  auto conditionLocal = localReferenceFor(hirModule, cond.condition);
  auto conditionLiteral = expressionFor(hirModule, cond.condition);
  if (conditionParameter == zc::none && conditionLocal == zc::none &&
      conditionLiteral == zc::none) {
    return false;
  }
  const bool conditionIsLiteral = conditionLiteral != zc::none;
  auto thenLiteral = expressionFor(hirModule, cond.thenReturnValue);
  auto elseLiteral = expressionFor(hirModule, cond.elseReturnValue);
  if (thenLiteral == zc::none || elseLiteral == zc::none) return false;
  if (ZC_ASSERT_NONNULL(thenLiteral).type != declaration.resultType ||
      ZC_ASSERT_NONNULL(elseLiteral).type != declaration.resultType) {
    return false;
  }
  // The return names the ternary local.
  auto sourceReturn = returnFor(hirModule, sourceBlock.statements[bindingCount]);
  if (sourceReturn == zc::none) return false;
  auto returnReference = localReferenceFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (returnReference == zc::none || ZC_ASSERT_NONNULL(returnReference).local.ordinal() !=
                                         static_cast<uint32_t>(ternaryIndex + 1)) {
    return false;
  }
  // Function-level shape. A bool-literal condition adds one Temporary local
  // holding the condition value for the SwitchInt discriminant.
  const size_t expectedLocalCount = parameterCount + bindingCount + (conditionIsLiteral ? 1 : 0);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != expectedLocalCount || function.blocks.size() != 4 ||
      declaration.body != sourceBlock.node) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan)) {
    return false;
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    const auto& local = function.locals[i];
    if (local.id != localId(static_cast<uint32_t>(i + 1)) ||
        local.kind != MirLocalKind::Parameter || local.type != declaration.parameters[i].type ||
        local.sourceScope != scopeId(1) ||
        !sameSpan(local.sourceSpan, declaration.parameters[i].sourceSpan)) {
      return false;
    }
  }
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& local = function.locals[parameterCount + i];
    if (local.id != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        local.kind != MirLocalKind::UserLocal || local.type != bindings[i]->type ||
        local.sourceScope != scopeId(1) || !sameSpan(local.sourceSpan, bindings[i]->sourceSpan)) {
      return false;
    }
  }
  // A bool-literal condition adds one Temporary local after the user locals.
  if (conditionIsLiteral) {
    const auto& temp = function.locals[parameterCount + bindingCount];
    if (temp.id != localId(static_cast<uint32_t>(parameterCount + bindingCount + 1)) ||
        temp.kind != MirLocalKind::Temporary ||
        temp.type != ZC_ASSERT_NONNULL(conditionLiteral).type || temp.sourceScope != scopeId(1) ||
        !sameSpan(temp.sourceSpan, ZC_ASSERT_NONNULL(conditionLiteral).sourceSpan)) {
      return false;
    }
  }
  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];
  // Entry: K*2 leading-local statements + StorageLive(ternary) + 2 temp
  // statements (StorageLive + Assign) when the condition is a bool literal.
  const size_t expectedEntryStatements = ternaryIndex * 2 + 1 + (conditionIsLiteral ? 2 : 0);
  if (entry.id != blockId(1) || entry.sourceScope != scopeId(1) ||
      entry.statements.size() != expectedEntryStatements ||
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
  // Leading local preamble: StorageLive + Initialize Assign per binding.
  for (size_t i = 0; i < ternaryIndex; ++i) {
    const auto& binding = *bindings[i];
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
    const auto& liveStatement = entry.statements[i * 2];
    const auto& assignStatement = entry.statements[i * 2 + 1];
    if (liveStatement.kind() != MirStatementKind::StorageLive ||
        liveStatement.storageLocal() != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        !sameSpan(liveStatement.sourceSpan(), binding.sourceSpan) ||
        assignStatement.kind() != MirStatementKind::Assign) {
      return false;
    }
    const auto& assignment = assignStatement.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != localId(static_cast<uint32_t>(parameterCount + i + 1)) ||
        assignment.destination.rootType() != binding.type ||
        assignment.destination.resultType() != binding.type ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    auto initLiteral = expressionFor(hirModule, initializerNode);
    auto initParameter = parameterReferenceFor(hirModule, initializerNode);
    auto initLocal = localReferenceFor(hirModule, initializerNode);
    const int present = (initLiteral != zc::none ? 1 : 0) + (initParameter != zc::none ? 1 : 0) +
                        (initLocal != zc::none ? 1 : 0);
    if (present != 1) return false;
    bool operandOk = false;
    ZC_IF_SOME(value, initLiteral) {
      operandOk = operand.kind() == MirOperandKind::Constant &&
                  operand.constantValue().type == binding.type &&
                  sameConstant(operand.constantValue().value, value.value, module, identities,
                               semanticTypes);
    }
    ZC_IF_SOME(value, initParameter) {
      size_t parameterIndex = 0;
      bool resolved = false;
      for (size_t candidate = 0; candidate < parameterCount; ++candidate) {
        if (declaration.parameters[candidate].key == value.parameter) {
          parameterIndex = candidate;
          resolved = true;
          break;
        }
      }
      operandOk = resolved && value.type == binding.type &&
                  matchesPlaceUse(operand, proofs, copy, binding.type) &&
                  operand.kind() != MirOperandKind::Constant &&
                  operand.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
                  operand.place().rootType() == binding.type &&
                  operand.place().resultType() == binding.type &&
                  operand.place().projections().size() == 0;
    }
    ZC_IF_SOME(value, initLocal) {
      operandOk =
          value.local.ordinal() >= 1 && value.local.ordinal() <= static_cast<uint32_t>(i) &&
          value.type == binding.type && matchesPlaceUse(operand, proofs, copy, binding.type) &&
          operand.kind() != MirOperandKind::Constant &&
          operand.place().local() ==
              localId(static_cast<uint32_t>(parameterCount + value.local.ordinal())) &&
          operand.place().rootType() == binding.type &&
          operand.place().resultType() == binding.type && operand.place().projections().size() == 0;
    }
    if (!operandOk) return false;
  }
  // Ternary local StorageLive.
  const auto& ternaryLive = entry.statements[ternaryIndex * 2];
  if (ternaryLive.kind() != MirStatementKind::StorageLive ||
      ternaryLive.storageLocal() !=
          localId(static_cast<uint32_t>(parameterCount + ternaryIndex + 1)) ||
      !sameSpan(ternaryLive.sourceSpan(), bindings[ternaryIndex]->sourceSpan)) {
    return false;
  }
  // A bool-literal condition adds StorageLive(temp) + Assign(temp = literal)
  // after the ternary local StorageLive.
  if (conditionIsLiteral) {
    const auto& tempLive = entry.statements[ternaryIndex * 2 + 1];
    const auto& tempAssign = entry.statements[ternaryIndex * 2 + 2];
    const auto& literal = ZC_ASSERT_NONNULL(conditionLiteral);
    const auto tempId = localId(static_cast<uint32_t>(parameterCount + bindingCount + 1));
    if (tempLive.kind() != MirStatementKind::StorageLive || tempLive.storageLocal() != tempId ||
        !sameSpan(tempLive.sourceSpan(), literal.sourceSpan) ||
        tempAssign.kind() != MirStatementKind::Assign) {
      return false;
    }
    const auto& assignment = tempAssign.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() != tempId ||
        assignment.destination.rootType() != literal.type ||
        assignment.destination.resultType() != literal.type ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    if (operand.kind() != MirOperandKind::Constant ||
        operand.constantValue().type != literal.type ||
        !sameConstant(operand.constantValue().value, literal.value, module, identities,
                      semanticTypes) ||
        !sameSpan(tempAssign.sourceSpan(), literal.sourceSpan)) {
      return false;
    }
  }
  // SwitchInt terminator on the condition.
  const auto& switchTerminator = entry.terminator.switchIntValue();
  if (switchTerminator.arms.size() != 2 || switchTerminator.defaultTarget != blockId(3)) {
    return false;
  }
  auto trueValue = switchTerminator.arms[0].value.booleanValue();
  auto falseValue = switchTerminator.arms[1].value.booleanValue();
  if (trueValue == zc::none || falseValue == zc::none || !ZC_ASSERT_NONNULL(trueValue) ||
      ZC_ASSERT_NONNULL(falseValue) || switchTerminator.arms[0].target != blockId(2) ||
      switchTerminator.arms[1].target != blockId(3)) {
    return false;
  }
  // The discriminant is a place-use of the condition temporary, parameter, or
  // local.
  const auto& discriminant = switchTerminator.discriminant;
  bool discriminantOk = false;
  if (conditionIsLiteral) {
    const auto& literal = ZC_ASSERT_NONNULL(conditionLiteral);
    const auto tempId = localId(static_cast<uint32_t>(parameterCount + bindingCount + 1));
    discriminantOk = matchesPlaceUse(discriminant, proofs, copy, literal.type) &&
                     discriminant.kind() != MirOperandKind::Constant &&
                     discriminant.place().local() == tempId &&
                     discriminant.place().rootType() == literal.type &&
                     discriminant.place().resultType() == literal.type &&
                     discriminant.place().projections().size() == 0;
  }
  ZC_IF_SOME(param, conditionParameter) {
    size_t parameterIndex = 0;
    bool resolved = false;
    for (size_t candidate = 0; candidate < parameterCount; ++candidate) {
      if (declaration.parameters[candidate].key == param.parameter) {
        parameterIndex = candidate;
        resolved = true;
        break;
      }
    }
    discriminantOk =
        resolved && matchesPlaceUse(discriminant, proofs, copy, param.type) &&
        discriminant.kind() != MirOperandKind::Constant &&
        discriminant.place().local() == localId(static_cast<uint32_t>(parameterIndex + 1)) &&
        discriminant.place().rootType() == param.type &&
        discriminant.place().resultType() == param.type &&
        discriminant.place().projections().size() == 0;
  }
  ZC_IF_SOME(local, conditionLocal) {
    discriminantOk = local.local.ordinal() >= 1 &&
                     local.local.ordinal() <= static_cast<uint32_t>(ternaryIndex) &&
                     matchesPlaceUse(discriminant, proofs, copy, local.type) &&
                     discriminant.kind() != MirOperandKind::Constant &&
                     discriminant.place().local() ==
                         localId(static_cast<uint32_t>(parameterCount + local.local.ordinal())) &&
                     discriminant.place().rootType() == local.type &&
                     discriminant.place().resultType() == local.type &&
                     discriminant.place().projections().size() == 0;
  }
  if (!discriminantOk) return false;
  // Arm blocks: one Initialize Assign of the branch literal.
  const auto armBlock = [&](const MirBasicBlock& arm,
                            const hir::HirScalarLiteralExpression& literal) -> bool {
    const auto& assignStatement = arm.statements[0];
    if (assignStatement.kind() != MirStatementKind::Assign) return false;
    const auto& assignment = assignStatement.assignmentValue();
    if (assignment.initialization != MirInitializationKind::Initialize ||
        assignment.destination.local() !=
            localId(static_cast<uint32_t>(parameterCount + ternaryIndex + 1)) ||
        assignment.destination.rootType() != declaration.resultType ||
        assignment.destination.resultType() != declaration.resultType ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    return operand.kind() == MirOperandKind::Constant &&
           operand.constantValue().type == declaration.resultType &&
           sameConstant(operand.constantValue().value, literal.value, module, identities,
                        semanticTypes) &&
           sameSpan(assignStatement.sourceSpan(), literal.sourceSpan);
  };
  if (!armBlock(thenBlock, ZC_ASSERT_NONNULL(thenLiteral)) ||
      !armBlock(elseBlock, ZC_ASSERT_NONNULL(elseLiteral))) {
    return false;
  }
  // Join block: return the ternary local.
  ZC_IF_SOME(returnOperand, joinBlock.terminator.returnValue().value) {
    if (!matchesPlaceUse(returnOperand, proofs, copy, declaration.resultType) ||
        returnOperand.place().local() !=
            localId(static_cast<uint32_t>(parameterCount + ternaryIndex + 1)) ||
        returnOperand.place().rootType() != declaration.resultType ||
        returnOperand.place().resultType() != declaration.resultType ||
        returnOperand.place().projections().size() != 0 ||
        !sameSpan(joinBlock.terminator.sourceSpan(), ZC_ASSERT_NONNULL(sourceReturn).sourceSpan)) {
      return false;
    }
    return true;
  }
  return false;
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
  const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != declaration.parameters.size() + 1 || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node || sourceLocal.initializer != call.node ||
      sourceReturn.value != reference.node || sourceLocal.local != reference.local ||
      sourceLocal.type != declaration.resultType || call.resultType != sourceLocal.type ||
      reference.type != sourceLocal.type || reference.category != hir::HirValueCategory::Place) {
    return false;
  }
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
  const auto resultLocalId = localId(parameterCount + 1);
  const auto& local = function.locals[parameterCount];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != resultLocalId ||
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
    return matchesPlaceUse(value, proofs, copy, local.type) && value.place().local() == local.id &&
           value.place().rootType() == local.type && value.place().resultType() == local.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

bool validReceiverCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    zc::Maybe<const hir::HirNominalAggregateExpression&> aggregate,
    zc::Maybe<const hir::HirScalarLiteralExpression&> scalarInitializer,
    const hir::HirReturnStatement& sourceReturn, const hir::HirLocalReferenceExpression& receiver,
    const hir::HirReceiverCallExpression& call, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  const bool hasAggregate = aggregate != zc::none;
  const bool hasScalar = !hasAggregate && scalarInitializer != zc::none;
  if (!hasAggregate && !hasScalar) return false;
  const hir::HirNodeId initializerNode =
      hasAggregate ? ZC_ASSERT_NONNULL(aggregate).node : ZC_ASSERT_NONNULL(scalarInitializer).node;
  const identity::SemanticTypeId initializerType =
      hasAggregate ? ZC_ASSERT_NONNULL(aggregate).type : ZC_ASSERT_NONNULL(scalarInitializer).type;
  const hir::HirValueCategory initializerCategory =
      hasAggregate ? ZC_ASSERT_NONNULL(aggregate).category
                   : ZC_ASSERT_NONNULL(scalarInitializer).category;
  const identity::SourceSpan& initializerSourceSpan =
      hasAggregate ? ZC_ASSERT_NONNULL(aggregate).sourceSpan
                   : ZC_ASSERT_NONNULL(scalarInitializer).sourceSpan;
  const bool sharedCall = call.receiverMode == checker::checked::ReceiverMode::Shared;
  const auto expectedReceiverMode =
      sharedCall ? checker::checked::ReceiverMode::Shared : checker::checked::ReceiverMode::Mutable;
  const auto expectedReceiverStep = sharedCall
                                        ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                                        : checker::checked::ReceiverAdjustmentStep::BorrowMutable;
  // A field-comparison argument adds one bool temporary (localId(3)) holding
  // the comparison result, shifting the result temporary to localId(4) and
  // adding two entry statements (StorageLive + Comparison Assign).
  bool hasComparisonArgument = false;
  for (const auto& argument : call.arguments) {
    if (argument.comparisonOperation != zc::none) {
      hasComparisonArgument = true;
      break;
    }
  }
  const size_t expectedLocalCount = hasComparisonArgument ? 4 : 3;
  const size_t expectedEntryStatementCount = hasComparisonArgument ? 7 : 5;
  const MirLocalId expectedResultLocal = hasComparisonArgument ? localId(4) : localId(3);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != expectedLocalCount || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 2 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != sourceReturn.node ||
      sourceLocal.initializer != initializerNode || sourceReturn.value != call.node ||
      call.receiver != receiver.node || sourceLocal.local != receiver.local ||
      sourceLocal.type != initializerType || sourceLocal.type != receiver.type ||
      sourceLocal.type != call.receiverSourceType ||
      initializerCategory != hir::HirValueCategory::Value ||
      receiver.category != hir::HirValueCategory::Place ||
      call.resultType != declaration.resultType || call.receiverMode != expectedReceiverMode ||
      call.receiverAdjustments.size() != 1 || call.receiverAdjustments[0] != expectedReceiverStep) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& local = function.locals[0];
  const auto& receiverTemporary = function.locals[1];
  const auto& result = function.locals[expectedLocalCount - 1];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || local.id != localId(1) ||
      local.kind != MirLocalKind::UserLocal || local.type != sourceLocal.type ||
      local.sourceScope != scope.id || !sameSpan(local.sourceSpan, sourceLocal.sourceSpan) ||
      receiverTemporary.id != localId(2) || receiverTemporary.kind != MirLocalKind::Temporary ||
      receiverTemporary.type != call.receiverType || receiverTemporary.sourceScope != scope.id ||
      !sameSpan(receiverTemporary.sourceSpan, receiver.sourceSpan) ||
      result.id != expectedResultLocal || result.kind != MirLocalKind::Temporary ||
      result.type != call.resultType || result.sourceScope != scope.id ||
      !sameSpan(result.sourceSpan, call.sourceSpan) || entry.id != blockId(1) ||
      entry.sourceScope != scope.id || entry.statements.size() != expectedEntryStatementCount ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != local.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != receiverTemporary.id ||
      !sameSpan(entry.statements[2].sourceSpan(), receiver.sourceSpan) ||
      entry.statements[3].kind() != MirStatementKind::BorrowCreation ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  // With a comparison argument, statements 4 and 5 are the comparison
  // computation (StorageLive + Comparison Assign) and statement 6 is the
  // result StorageLive. Without one, statement 4 is the result StorageLive.
  const size_t resultStorageLiveIndex = expectedEntryStatementCount - 1;
  if (entry.statements[resultStorageLiveIndex].kind() != MirStatementKind::StorageLive ||
      entry.statements[resultStorageLiveIndex].storageLocal() != result.id ||
      !sameSpan(entry.statements[resultStorageLiveIndex].sourceSpan(), call.sourceSpan)) {
    return false;
  }
  if (hasComparisonArgument) {
    const auto& comparisonTemporary = function.locals[2];
    if (comparisonTemporary.id != localId(3) ||
        comparisonTemporary.kind != MirLocalKind::Temporary ||
        comparisonTemporary.sourceScope != scope.id ||
        !sameSpan(comparisonTemporary.sourceSpan, call.sourceSpan)) {
      return false;
    }
    if (entry.statements[4].kind() != MirStatementKind::StorageLive ||
        entry.statements[4].storageLocal() != comparisonTemporary.id ||
        !sameSpan(entry.statements[4].sourceSpan(), call.sourceSpan) ||
        entry.statements[5].kind() != MirStatementKind::Assign) {
      return false;
    }
    const auto& comparisonAssignment = entry.statements[5].assignmentValue();
    if (comparisonAssignment.initialization != MirInitializationKind::Initialize ||
        comparisonAssignment.destination.local() != comparisonTemporary.id ||
        comparisonAssignment.destination.projections().size() != 0 ||
        comparisonAssignment.value.kind() != MirRvalueKind::Comparison) {
      return false;
    }
  }
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != MirInitializationKind::Initialize ||
      initialization.destination.local() != local.id ||
      initialization.destination.rootType() != local.type ||
      initialization.destination.resultType() != local.type ||
      initialization.destination.projections().size() != 0 ||
      (hasAggregate ? initialization.value.kind() != MirRvalueKind::NominalAggregate
                    : initialization.value.kind() != MirRvalueKind::Use) ||
      !sameSpan(entry.statements[1].sourceSpan(), initializerSourceSpan)) {
    return false;
  }
  if (hasAggregate) {
    const auto& loweredAggregate = initialization.value.nominalAggregateValue();
    if (loweredAggregate.definition != ZC_ASSERT_NONNULL(aggregate).definition ||
        loweredAggregate.type != ZC_ASSERT_NONNULL(aggregate).type ||
        loweredAggregate.elements.size() != ZC_ASSERT_NONNULL(aggregate).elements.size()) {
      return false;
    }
    for (size_t index = 0; index < ZC_ASSERT_NONNULL(aggregate).elements.size(); ++index) {
      const auto& actual = loweredAggregate.elements[index];
      const auto& expected = ZC_ASSERT_NONNULL(aggregate).elements[index];
      if (actual.field != expected.field || actual.operand.kind() != MirOperandKind::Constant ||
          actual.operand.constantValue().type != expected.type ||
          !sameConstant(actual.operand.constantValue().value, expected.value, module, identities,
                        semanticTypes)) {
        return false;
      }
    }
  } else {
    // A scalar literal initializer (enum variant discriminant) lowers to a
    // constant-use rvalue whose type and value match the HIR literal.
    const auto& useValue = initialization.value.useValue();
    if (useValue.operand.kind() != MirOperandKind::Constant ||
        useValue.operand.constantValue().type != initializerType ||
        !sameConstant(useValue.operand.constantValue().value,
                      ZC_ASSERT_NONNULL(scalarInitializer).value, module, identities,
                      semanticTypes)) {
      return false;
    }
  }
  const auto expectedBorrowKind = sharedCall ? MirBorrowKind::Shared : MirBorrowKind::Mutable;
  const auto& borrow = entry.statements[3].borrowCreationValue();
  if (borrow.kind != expectedBorrowKind ||
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
  const auto expectedEffectKind =
      sharedCall ? MirCallEffectKind::NoActivation : MirCallEffectKind::ActivateMutableReceiver;
  if (terminator.callee != call.callee ||
      terminator.arguments.size() != call.arguments.size() + 1 ||
      terminator.destination.local() != result.id ||
      terminator.destination.rootType() != result.type ||
      terminator.destination.resultType() != result.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != expectedEffectKind ||
      (sharedCall ? activatedReceiver != zc::none
                  : activatedReceiver == zc::none ||
                        ZC_ASSERT_NONNULL(activatedReceiver) != receiverTemporary.id) ||
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
    if (expected.comparisonOperation != zc::none) {
      // Field-comparison argument: the operand is a place-use of the bool
      // comparison temporary (localId(3)).
      if (!matchesPlaceUse(actual, proofs, copy, expected.type)) { return false; }
      const auto& argumentPlace = actual.place();
      if (argumentPlace.local() != localId(3) || argumentPlace.rootType() != expected.type ||
          argumentPlace.resultType() != expected.type || argumentPlace.projections().size() != 0) {
        return false;
      }
    } else if (expected.value == zc::none) {
      // Field-projection argument on the receiver local: the operand is a
      // place-use of the receiver local with a single field projection.
      if (expected.field == zc::none || !matchesPlaceUse(actual, proofs, copy, expected.type)) {
        return false;
      }
      const auto& argumentPlace = actual.place();
      const auto field = ZC_ASSERT_NONNULL(expected.field);
      if (argumentPlace.local() != local.id || argumentPlace.rootType() != local.type ||
          argumentPlace.resultType() != expected.type || argumentPlace.projections().size() != 1 ||
          argumentPlace.projections()[0].kind() != MirProjectionKind::Field ||
          argumentPlace.projections()[0].fieldValue().field != field ||
          argumentPlace.projections()[0].fieldValue().inputType != local.type ||
          argumentPlace.projections()[0].fieldValue().resultType != expected.type) {
        return false;
      }
    } else {
      ZC_IF_SOME(value, expected.value) {
        if (actual.kind() != MirOperandKind::Constant ||
            actual.constantValue().type != expected.type ||
            !sameConstant(actual.constantValue().value, value, module, identities, semanticTypes)) {
          return false;
        }
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

/// \brief Validates the discarded mutable-call then shared trailing-call caller
/// `fn f() -> T { let o = S{..}; o.set(c); return o.get(); }`. Five dense
/// locals (owner, the setter's mutable borrow temporary, the unread Unit call
/// destination, the getter's shared borrow temporary, the getter result) span
/// three blocks: owner initialization plus the setter call, a distinct second
/// borrow of the same owner plus the getter call, and a valued return.
bool validVoidCallThenReceiverCallReturnFunction(
    const MirFunction& function, const hir::HirFunctionDeclaration& declaration,
    const hir::HirBlockStatement& sourceBlock, const hir::HirLocalBinding& sourceLocal,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalReferenceExpression& setReceiver,
    const hir::HirReceiverCallExpression& discardedCall,
    const hir::HirLocalReferenceExpression& getReceiver,
    const hir::HirReceiverCallExpression& trailingCall, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copy, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, bool sharedDevirt) {
  if (!sharedDevirt) {
    auto unitLookup = semanticTypes.get(discardedCall.resultType);
    if (!unitLookup.is<type::SemanticTypeLookup>()) return false;
    auto unitKind = unitLookup.get<type::SemanticTypeLookup>().data().primitiveKind();
    if (unitKind == zc::none ||
        ZC_ASSERT_NONNULL(unitKind) != type::semantic::PrimitiveKind::Unit) {
      return false;
    }
  }
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 5 || function.blocks.size() != 3 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 3 ||
      sourceBlock.statements[0] != sourceLocal.node ||
      sourceBlock.statements[1] != discardedCall.node ||
      sourceBlock.statements[2] != sourceReturn.node || sourceLocal.initializer != aggregate.node ||
      sourceReturn.value != trailingCall.node || discardedCall.receiver != setReceiver.node ||
      trailingCall.receiver != getReceiver.node || setReceiver.local != sourceLocal.local ||
      getReceiver.local != sourceLocal.local || sourceLocal.type != aggregate.type ||
      sourceLocal.type != discardedCall.receiverSourceType ||
      sourceLocal.type != trailingCall.receiverSourceType ||
      aggregate.category != hir::HirValueCategory::Value ||
      setReceiver.category != hir::HirValueCategory::Place ||
      getReceiver.category != hir::HirValueCategory::Place ||
      discardedCall.receiverMode != (sharedDevirt ? checker::checked::ReceiverMode::Shared
                                                  : checker::checked::ReceiverMode::Mutable) ||
      discardedCall.receiverAdjustments.size() != 1 ||
      discardedCall.receiverAdjustments[0] !=
          (sharedDevirt ? checker::checked::ReceiverAdjustmentStep::BorrowShared
                        : checker::checked::ReceiverAdjustmentStep::BorrowMutable) ||
      discardedCall.arguments.size() != (sharedDevirt ? 0 : 1) ||
      (!sharedDevirt && discardedCall.arguments[0].value == zc::none) ||
      trailingCall.receiverMode != checker::checked::ReceiverMode::Shared ||
      trailingCall.receiverAdjustments.size() != 1 ||
      trailingCall.receiverAdjustments[0] !=
          checker::checked::ReceiverAdjustmentStep::BorrowShared ||
      trailingCall.arguments.size() != 0 || trailingCall.resultType != declaration.resultType) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& owner = function.locals[0];
  const auto& setterBorrow = function.locals[1];
  const auto& unitResult = function.locals[2];
  const auto& getterBorrow = function.locals[3];
  const auto& getterResult = function.locals[4];
  const auto& entry = function.blocks[0];
  const auto& setterContinuation = function.blocks[1];
  const auto& getterContinuation = function.blocks[2];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || owner.id != localId(1) ||
      owner.kind != MirLocalKind::UserLocal || owner.type != sourceLocal.type ||
      owner.sourceScope != scope.id || !sameSpan(owner.sourceSpan, sourceLocal.sourceSpan) ||
      setterBorrow.id != localId(2) || setterBorrow.kind != MirLocalKind::Temporary ||
      setterBorrow.type != discardedCall.receiverType || setterBorrow.sourceScope != scope.id ||
      !sameSpan(setterBorrow.sourceSpan, setReceiver.sourceSpan) || unitResult.id != localId(3) ||
      unitResult.kind != MirLocalKind::Temporary || unitResult.type != discardedCall.resultType ||
      unitResult.sourceScope != scope.id ||
      !sameSpan(unitResult.sourceSpan, discardedCall.sourceSpan) || getterBorrow.id != localId(4) ||
      getterBorrow.kind != MirLocalKind::Temporary ||
      getterBorrow.type != trailingCall.receiverType || getterBorrow.sourceScope != scope.id ||
      !sameSpan(getterBorrow.sourceSpan, getReceiver.sourceSpan) || getterResult.id != localId(5) ||
      getterResult.kind != MirLocalKind::Temporary ||
      getterResult.type != trailingCall.resultType || getterResult.sourceScope != scope.id ||
      !sameSpan(getterResult.sourceSpan, trailingCall.sourceSpan) || entry.id != blockId(1) ||
      entry.sourceScope != scope.id || entry.statements.size() != 5 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != owner.id ||
      !sameSpan(entry.statements[0].sourceSpan(), sourceLocal.sourceSpan) ||
      entry.statements[1].kind() != MirStatementKind::Assign ||
      !sameSpan(entry.statements[1].sourceSpan(), aggregate.sourceSpan) ||
      entry.statements[2].kind() != MirStatementKind::StorageLive ||
      entry.statements[2].storageLocal() != setterBorrow.id ||
      !sameSpan(entry.statements[2].sourceSpan(), setReceiver.sourceSpan) ||
      entry.statements[3].kind() != MirStatementKind::BorrowCreation ||
      !sameSpan(entry.statements[3].sourceSpan(), setReceiver.sourceSpan) ||
      entry.statements[4].kind() != MirStatementKind::StorageLive ||
      entry.statements[4].storageLocal() != unitResult.id ||
      !sameSpan(entry.statements[4].sourceSpan(), discardedCall.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call ||
      !sameSpan(entry.terminator.sourceSpan(), discardedCall.sourceSpan) ||
      setterContinuation.id != blockId(2) || setterContinuation.sourceScope != scope.id ||
      setterContinuation.statements.size() != 3 ||
      setterContinuation.statements[0].kind() != MirStatementKind::StorageLive ||
      setterContinuation.statements[0].storageLocal() != getterBorrow.id ||
      !sameSpan(setterContinuation.statements[0].sourceSpan(), getReceiver.sourceSpan) ||
      setterContinuation.statements[1].kind() != MirStatementKind::BorrowCreation ||
      !sameSpan(setterContinuation.statements[1].sourceSpan(), getReceiver.sourceSpan) ||
      setterContinuation.statements[2].kind() != MirStatementKind::StorageLive ||
      setterContinuation.statements[2].storageLocal() != getterResult.id ||
      !sameSpan(setterContinuation.statements[2].sourceSpan(), trailingCall.sourceSpan) ||
      setterContinuation.terminator.kind() != MirTerminatorKind::Call ||
      !sameSpan(setterContinuation.terminator.sourceSpan(), trailingCall.sourceSpan) ||
      getterContinuation.id != blockId(3) || getterContinuation.sourceScope != scope.id ||
      getterContinuation.statements.size() != 0 ||
      getterContinuation.terminator.kind() != MirTerminatorKind::Return ||
      getterContinuation.terminator.returnValue().value == zc::none ||
      !sameSpan(getterContinuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& initialization = entry.statements[1].assignmentValue();
  if (initialization.initialization != MirInitializationKind::Initialize ||
      initialization.destination.local() != owner.id ||
      initialization.destination.rootType() != owner.type ||
      initialization.destination.resultType() != owner.type ||
      initialization.destination.projections().size() != 0 ||
      initialization.value.kind() != MirRvalueKind::NominalAggregate) {
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
  const auto& mutableBorrow = entry.statements[3].borrowCreationValue();
  if (mutableBorrow.kind != (sharedDevirt ? MirBorrowKind::Shared : MirBorrowKind::Mutable) ||
      mutableBorrow.destination.local() != setterBorrow.id ||
      mutableBorrow.destination.rootType() != setterBorrow.type ||
      mutableBorrow.destination.resultType() != setterBorrow.type ||
      mutableBorrow.destination.projections().size() != 0 ||
      mutableBorrow.source.local() != owner.id || mutableBorrow.source.rootType() != owner.type ||
      mutableBorrow.source.resultType() != owner.type ||
      mutableBorrow.source.projections().size() != 0) {
    return false;
  }
  const auto& setterCall = entry.terminator.callValue();
  auto setterActivation = setterCall.effect.activatedMutableReceiver();
  if (setterCall.callee != discardedCall.callee ||
      setterCall.arguments.size() != (sharedDevirt ? 1 : 2) ||
      setterCall.destination.local() != unitResult.id ||
      setterCall.destination.rootType() != unitResult.type ||
      setterCall.destination.resultType() != unitResult.type ||
      setterCall.destination.projections().size() != 0 ||
      (!sharedDevirt &&
       (setterCall.effect.kind() != MirCallEffectKind::ActivateMutableReceiver ||
        setterActivation == zc::none || ZC_ASSERT_NONNULL(setterActivation) != setterBorrow.id)) ||
      (sharedDevirt && setterCall.effect.kind() != MirCallEffectKind::NoActivation) ||
      setterCall.normalTarget != setterContinuation.id || setterCall.unwindTarget != zc::none ||
      !matchesPlaceUse(setterCall.arguments[0], proofs, copy, setterBorrow.type)) {
    return false;
  }
  const auto& setterReceiverOperand = setterCall.arguments[0].place();
  if (setterReceiverOperand.local() != setterBorrow.id ||
      setterReceiverOperand.rootType() != setterBorrow.type ||
      setterReceiverOperand.resultType() != setterBorrow.type ||
      setterReceiverOperand.projections().size() != 0) {
    return false;
  }
  if (!sharedDevirt) {
    const auto& setterConstant = setterCall.arguments[1];
    ZC_IF_SOME(expectedSetterConstant, discardedCall.arguments[0].value) {
      if (setterConstant.kind() != MirOperandKind::Constant ||
          setterConstant.constantValue().type != discardedCall.arguments[0].type ||
          !sameConstant(setterConstant.constantValue().value, expectedSetterConstant, module,
                        identities, semanticTypes)) {
        return false;
      }
    }
  }
  const auto& sharedBorrow = setterContinuation.statements[1].borrowCreationValue();
  if (sharedBorrow.kind != MirBorrowKind::Shared ||
      sharedBorrow.destination.local() != getterBorrow.id ||
      sharedBorrow.destination.rootType() != getterBorrow.type ||
      sharedBorrow.destination.resultType() != getterBorrow.type ||
      sharedBorrow.destination.projections().size() != 0 ||
      sharedBorrow.source.local() != owner.id || sharedBorrow.source.rootType() != owner.type ||
      sharedBorrow.source.resultType() != owner.type ||
      sharedBorrow.source.projections().size() != 0) {
    return false;
  }
  const auto& getterCall = setterContinuation.terminator.callValue();
  if (getterCall.callee != trailingCall.callee || getterCall.arguments.size() != 1 ||
      getterCall.destination.local() != getterResult.id ||
      getterCall.destination.rootType() != getterResult.type ||
      getterCall.destination.resultType() != getterResult.type ||
      getterCall.destination.projections().size() != 0 ||
      getterCall.effect.kind() != MirCallEffectKind::NoActivation ||
      getterCall.effect.activatedMutableReceiver() != zc::none ||
      getterCall.normalTarget != getterContinuation.id || getterCall.unwindTarget != zc::none ||
      !matchesPlaceUse(getterCall.arguments[0], proofs, copy, getterBorrow.type)) {
    return false;
  }
  const auto& getterReceiverOperand = getterCall.arguments[0].place();
  if (getterReceiverOperand.local() != getterBorrow.id ||
      getterReceiverOperand.rootType() != getterBorrow.type ||
      getterReceiverOperand.resultType() != getterBorrow.type ||
      getterReceiverOperand.projections().size() != 0) {
    return false;
  }
  ZC_IF_SOME(value, getterContinuation.terminator.returnValue().value) {
    return matchesPlaceUse(value, proofs, copy, getterResult.type) &&
           value.place().local() == getterResult.id &&
           value.place().rootType() == getterResult.type &&
           value.place().resultType() == getterResult.type &&
           value.place().projections().size() == 0;
  }
  return false;
}

/// \brief Validates the shared-receiver self-call shape
/// `fn m(this) -> T { return this.n(); }`. The receiver is the leading
/// Parameter local and is forwarded directly as the call's sole receiver
/// argument; no BorrowCreation temporary exists. One Temporary result local is
/// storage-live in the entry block, the call targets block 2, which returns it.
bool validMethodReceiverSelfCallReturnFunction(const MirFunction& function,
                                               const hir::HirFunctionDeclaration& declaration,
                                               const hir::HirBlockStatement& sourceBlock,
                                               const hir::HirReturnStatement& sourceReturn,
                                               const hir::HirReceiverCallExpression& call,
                                               checker::marker::MarkerProofEngine& proofs,
                                               identity::DefId copy, identity::ModuleId module,
                                               const checker::CheckerIdentityAuthority& identities,
                                               const type::SemanticTypeStore& semanticTypes) {
  (void)module;
  (void)identities;
  (void)semanticTypes;
  if (declaration.receiver == zc::none) { return false; }
  const auto& headerReceiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Method ||
      function.resultType != declaration.resultType || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 2 ||
      declaration.body != sourceBlock.node || sourceBlock.statements.size() != 1 ||
      sourceBlock.statements[0] != sourceReturn.node || sourceReturn.value != call.node ||
      call.receiver != hir::HirNodeId() ||
      call.receiverMode != checker::checked::ReceiverMode::Shared ||
      call.receiverAdjustments.size() != 1 ||
      call.receiverAdjustments[0] != checker::checked::ReceiverAdjustmentStep::ReborrowShared ||
      call.arguments.size() != 0 || call.resultType != declaration.resultType ||
      call.receiverType != headerReceiver.type || call.receiverSourceType != headerReceiver.type) {
    return false;
  }
  const auto& scope = function.sourceScopes[0];
  const auto& receiver = function.locals[0];
  const auto& result = function.locals[1];
  const auto& entry = function.blocks[0];
  const auto& continuation = function.blocks[1];
  if (scope.id != scopeId(1) || scope.parent != zc::none ||
      !sameSpan(scope.sourceSpan, declaration.sourceSpan) || receiver.id != localId(1) ||
      receiver.kind != MirLocalKind::Parameter || receiver.type != headerReceiver.type ||
      receiver.sourceScope != scope.id ||
      !sameSpan(receiver.sourceSpan, headerReceiver.sourceSpan) || result.id != localId(2) ||
      result.kind != MirLocalKind::Temporary || result.type != call.resultType ||
      result.sourceScope != scope.id || !sameSpan(result.sourceSpan, call.sourceSpan) ||
      entry.id != blockId(1) || entry.sourceScope != scope.id || entry.statements.size() != 1 ||
      entry.statements[0].kind() != MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != result.id ||
      !sameSpan(entry.statements[0].sourceSpan(), call.sourceSpan) ||
      entry.terminator.kind() != MirTerminatorKind::Call || continuation.id != blockId(2) ||
      continuation.sourceScope != scope.id || continuation.statements.size() != 0 ||
      continuation.terminator.kind() != MirTerminatorKind::Return ||
      continuation.terminator.returnValue().value == zc::none ||
      !sameSpan(entry.terminator.sourceSpan(), call.sourceSpan) ||
      !sameSpan(continuation.terminator.sourceSpan(), sourceReturn.sourceSpan)) {
    return false;
  }
  const auto& terminator = entry.terminator.callValue();
  auto activatedReceiver = terminator.effect.activatedMutableReceiver();
  if (terminator.callee != call.callee || terminator.arguments.size() != 1 ||
      terminator.destination.local() != result.id ||
      terminator.destination.rootType() != result.type ||
      terminator.destination.resultType() != result.type ||
      terminator.destination.projections().size() != 0 ||
      terminator.effect.kind() != MirCallEffectKind::NoActivation ||
      activatedReceiver != zc::none || terminator.normalTarget != continuation.id ||
      terminator.unwindTarget != zc::none ||
      !matchesPlaceUse(terminator.arguments[0], proofs, copy, receiver.type)) {
    return false;
  }
  const auto& receiverOperand = terminator.arguments[0].place();
  if (receiverOperand.local() != receiver.id || receiverOperand.rootType() != receiver.type ||
      receiverOperand.resultType() != receiver.type || receiverOperand.projections().size() != 0) {
    return false;
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
       ownership::AdmittedBoundModule&& boundModule, checker::CheckerIdentityAuthority&& identities,
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

  ownership::AdmittedBoundModule boundModule;
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

ownership::AdmittedBoundModule VerifiedBuiltMir::retainAdmittedBoundModule() const {
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
  // A shared-receiver self-call (`return this.method();`) owns one value node
  // (the receiver-call record) but no aggregate to pair against its function,
  // unlike the owner-local receiver call whose aggregate entry balances the
  // equation. Each header-forwarded self-call therefore adds one RHS term.
  int64_t receiverSelfCallValueNodes = 0;
  for (const auto& call : hirModule.receiverCalls()) {
    if (call.receiver == hir::HirNodeId()) ++receiverSelfCallValueNodes;
  }
  // A by-value aggregate call function owns both a direct-call value node and
  // an aggregate-initializer value node while it is still one function, so the
  // value-node checksum counts an extra node per such function.
  int64_t directAggregateCallValueNodes = 0;
  for (const auto& call : hirModule.calls()) {
    if (call.arguments.size() == 1 && call.arguments[0].value == zc::none &&
        call.arguments[0].parameter == zc::none && call.arguments[0].local != zc::none) {
      ++directAggregateCallValueNodes;
    }
  }
  // An admitted void function materializes no HirReturnStatement: its sole body
  // statement is the mutating receiver-field write and it terminates with
  // Return(void). Its parameter RHS still contributes a parameter reference and
  // its receiver-field write subtracts a value node, so the checksum is off by
  // one per such function; this count restores both the checksum and the returns
  // identity. Count by the STRUCTURAL absence of a trailing return record, not by
  // the Unit result type: a function that declares `-> unit` and still writes
  // `return unit;` is an ordinary valued-return function and keeps its return, so
  // counting it here would make the identity expect one return too few. The HIR
  // verifier admits a no-return body only for the Unit void-method shape.
  int64_t voidFunctionCount = 0;
  for (const auto& voidFunction : hirModule.functions()) {
    auto voidBlock = blockFor(hirModule, voidFunction.body);
    if (voidBlock == zc::none || ZC_ASSERT_NONNULL(voidBlock).statements.size() == 0) continue;
    const hir::HirNodeId voidTrailing =
        ZC_ASSERT_NONNULL(voidBlock).statements[ZC_ASSERT_NONNULL(voidBlock).statements.size() - 1];
    if (returnFor(hirModule, voidTrailing) == zc::none) ++voidFunctionCount;
  }
  int64_t sequentialValueNodeExcess = 0;
  for (const auto& sequentialFunction : hirModule.functions()) {
    auto sequentialBlock = blockFor(hirModule, sequentialFunction.body);
    ZC_IF_SOME(block, sequentialBlock) {
      if (!isSequentialLocalReturnBlock(hirModule, block)) continue;
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
            // A ternary conditional initializer: the two arm literals are
            // covered by conditionals*2 on the LHS; a bool-literal or
            // parameter-reference condition is an additional value node on
            // the RHS that needs its own credit. A local-reference condition
            // is not a value node here, matching the sequential rule.
            if (binary == zc::none) {
              auto conditional = conditionalFor(hirModule, initializer);
              ZC_IF_SOME(cond, conditional) {
                if (expressionFor(hirModule, cond.condition) != zc::none ||
                    parameterReferenceFor(hirModule, cond.condition) != zc::none) {
                  ++valueNodes;
                }
              }
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
  // A leading-local conditional function has K leading let bindings and a
  // trailing return whose value is a conditional expression. Its let binding
  // initializers materialize value nodes (expressions, aggregates, parameter
  // references) on the RHS of the checksum, but the shared equation credits
  // only the function, the conditional arms, and the comparison operation on
  // the LHS. This term carries the per-function excess
  // (valueNodes - binaryBindings - 1) so the balance holds for any K, mirroring
  // the sequential-local-return computation. The -1 adjusts for the function
  // node already counted in the LHS functions term.
  int64_t leadingLocalConditionalValueNodeExcess = 0;
  for (const auto& conditionalFunction : hirModule.functions()) {
    auto conditionalBlock = blockFor(hirModule, conditionalFunction.body);
    ZC_IF_SOME(block, conditionalBlock) {
      if (block.statements.size() < 2) continue;
      const size_t bindingCount = block.statements.size() - 1;
      bool allLeadingLocals = true;
      for (size_t i = 0; i < bindingCount; ++i) {
        if (localFor(hirModule, block.statements[i]) == zc::none) {
          allLeadingLocals = false;
          break;
        }
      }
      if (!allLeadingLocals) continue;
      auto trailingReturn = returnFor(hirModule, block.statements[bindingCount]);
      if (trailingReturn == zc::none) continue;
      auto conditional = conditionalFor(hirModule, ZC_ASSERT_NONNULL(trailingReturn).value);
      if (conditional == zc::none) continue;
      int64_t valueNodes = 0;
      int64_t binaryBindings = 0;
      for (size_t i = 0; i < bindingCount; ++i) {
        auto localBinding = localFor(hirModule, block.statements[i]);
        ZC_IF_SOME(local, localBinding) {
          ZC_IF_SOME(initializer, local.initializer) {
            auto binary = primitiveBinaryFor(hirModule, initializer);
            ZC_IF_SOME(value, binary) {
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
      leadingLocalConditionalValueNodeExcess += valueNodes - binaryBindings - 1;
    }
  }
  // Binary-write local operands: each local reference that is an operand of a
  // binary write value (`x = x + 1`) materializes a localReference, not a value
  // node, so it joins the RHS checksum as a credit like parameterReturnCount.
  int64_t binaryWriteLocalOperandCount = 0;
  for (const auto& write : hirModule.localWrites()) {
    auto binary = primitiveBinaryFor(hirModule, write.value);
    ZC_IF_SOME(binaryValue, binary) {
      for (const auto operand : {binaryValue.left, binaryValue.right}) {
        if (localReferenceFor(hirModule, operand) != zc::none) { ++binaryWriteLocalOperandCount; }
      }
    }
  }
  // For-loop accumulator functions with N>1 accumulators carry an N-1
  // checksum imbalance: each additional accumulator beyond the first
  // contributes one extra expression (its literal initializer) to the rhs
  // without a matching lhs node, because the accumulator write's binary
  // operation is already counted.
  int64_t forLoopAccumulatorCorrection = 0;
  for (const auto& accumulatorFunction : hirModule.functions()) {
    auto accumulatorBlock = blockFor(hirModule, accumulatorFunction.body);
    ZC_IF_SOME(block, accumulatorBlock) {
      if (block.statements.size() < 4) continue;
      const size_t accCount = block.statements.size() - 3;
      if (accCount <= 1) continue;
      auto initLocal = localFor(hirModule, block.statements[accCount]);
      if (initLocal == zc::none) continue;
      auto loop = loopFor(hirModule, block.statements[accCount + 1]);
      if (loop == zc::none) continue;
      auto sourceReturn = returnFor(hirModule, block.statements[accCount + 2]);
      if (sourceReturn == zc::none) continue;
      ZC_IF_SOME(loopValue, loop) {
        if (loopValue.body.size() != accCount + 1) continue;
      }
      forLoopAccumulatorCorrection += static_cast<int64_t>(accCount) - 1;
    }
  }
  const int64_t lhsChecksum =
      static_cast<int64_t>(hirModule.declarations().size() + hirModule.functions().size() +
                           hirModule.conditionals().size() * 2 +
                           hirModule.primitiveBinaryOperations().size() +
                           hirModule.loops().size()) +
      sequentialValueNodeExcess + directAggregateCallValueNodes +
      leadingLocalConditionalValueNodeExcess + forLoopAccumulatorCorrection;
  const int64_t rhsChecksum =
      static_cast<int64_t>(
          hirModule.expressions().size() + hirModule.calls().size() +
          hirModule.aggregates().size() + uninitializedLocalReturnCount + parameterReturnCount +
          parameterReborrowCount + hirModule.parameterFieldProjections().size() +
          receiverSelfCallValueNodes + voidFunctionCount - localAliasReborrowCount -
          hirModule.localWrites().size() - hirModule.parameterFieldWrites().size()) +
      binaryWriteLocalOperandCount;
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != hirModule.borrowEvidenceRevision().digest() ||
      hirModule.borrowEvidenceLease().key().revision.digest() !=
          hirModule.borrowEvidenceRevision().digest() ||
      lhsChecksum != rhsChecksum || hirModule.functions().size() != hirModule.blocks().size() ||
      static_cast<int64_t>(hirModule.functions().size()) - voidFunctionCount !=
          static_cast<int64_t>(hirModule.returns().size())) {
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
    // An inherent method reaches this loop only in the verified flat
    // scalar-literal-return shape (the HIR builder drains every other method
    // body as ZOM4099 before verification). Lower it through the same strict
    // recursive rail, which declares the implicit `this` receiver as the
    // leading parameter local that the scalar body never reads. Any shape the
    // rail does not admit stays a per-definition capability rejection rather
    // than falling through to module-function construction.
    if (declaration.receiver != zc::none) {
      bool lowered = false;
      ZC_IF_SOME(block, sourceBlock) {
        auto recursive = tryBuildRecursiveFunction(declaration, block, hirModule, identities,
                                                   semanticTypes, proofs, copy);
        ZC_IF_SOME(product, recursive) {
          pending.add(PendingMirFunction{zc::mv(product.function), zc::mv(product.ownerKey)});
          lowered = true;
        }
      }
      if (lowered) continue;
      return rejectMirCapability<BuiltMirCandidate>(ir::IrFailureKind::UnsupportedSourceConstruct,
                                                    declaration.definition, identities,
                                                    declaration.sourceSpan.clone());
    }
    if (sourceBlock == zc::none) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
    }
    ZC_IF_SOME(block, sourceBlock) {
      // RFC 0048 Phase 3: the recursive destination-driven FunctionBuilder owns
      // the bare scalar-literal and parameter single-block returns plus the
      // single scalar-initialized user local and one literal-overwrite returns.
      // Its predicate is strict; every other shape falls through to the legacy
      // construction below unchanged (including the unsafe-tail variants of the
      // local shapes, which share the legacy construction blocks).
      auto recursive = tryBuildRecursiveFunction(declaration, block, hirModule, identities,
                                                 semanticTypes, proofs, copy);
      ZC_IF_SOME(product, recursive) {
        pending.add(PendingMirFunction{zc::mv(product.function), zc::mv(product.ownerKey)});
        continue;
      }
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
            auto scalarInitializer = expressionFor(hirModule, initializerNode);
            ZC_IF_SOME(call, receiverCall) {
              // The receiver-call initializer is either a nominal aggregate
              // (struct literal) or a scalar literal (enum variant discriminant).
              // Exactly one is present at the initializer node.
              const bool hasAggregateInitializer = aggregate != zc::none;
              const bool hasScalarInitializer =
                  !hasAggregateInitializer && scalarInitializer != zc::none;
              if (hasAggregateInitializer || hasScalarInitializer) {
                auto receiver = localReferenceFor(hirModule, call.receiver);
                ZC_IF_SOME(reference, receiver) {
                  const identity::SemanticTypeId initializerType =
                      hasAggregateInitializer ? ZC_ASSERT_NONNULL(aggregate).type
                                              : ZC_ASSERT_NONNULL(scalarInitializer).type;
                  if (local.initializer != initializerNode || local.local != reference.local ||
                      local.type != initializerType || local.type != call.receiverSourceType ||
                      reference.type != call.receiverSourceType ||
                      reference.category != hir::HirValueCategory::Place ||
                      call.resultType != declaration.resultType ||
                      (call.receiverMode != checker::checked::ReceiverMode::Mutable &&
                       call.receiverMode != checker::checked::ReceiverMode::Shared) ||
                      call.receiverAdjustments.size() != 1 ||
                      call.receiverAdjustments[0] !=
                          (call.receiverMode == checker::checked::ReceiverMode::Mutable
                               ? checker::checked::ReceiverAdjustmentStep::BorrowMutable
                               : checker::checked::ReceiverAdjustmentStep::BorrowShared) ||
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
                  // A field-comparison argument (`cell.compare(cell.value > 0)`)
                  // needs one extra bool temporary to hold the comparison result
                  // before the call. Detect it before constructing locals so the
                  // local numbering and statement layout stay consistent.
                  bool hasComparisonArgument = false;
                  for (const auto& argument : call.arguments) {
                    if (argument.comparisonOperation != zc::none) {
                      hasComparisonArgument = true;
                      break;
                    }
                  }
                  const MirLocalId resultLocal = hasComparisonArgument ? localId(4) : localId(3);
                  zc::Vector<MirLocalDeclaration> locals;
                  locals.add(MirLocalDeclaration{localId(1), MirLocalKind::UserLocal, local.type,
                                                 scopeId(1), local.sourceSpan.clone()});
                  locals.add(MirLocalDeclaration{localId(2), MirLocalKind::Temporary,
                                                 call.receiverType, scopeId(1),
                                                 reference.sourceSpan.clone()});
                  if (hasComparisonArgument) {
                    locals.add(MirLocalDeclaration{localId(3), MirLocalKind::Temporary,
                                                   call.arguments[0].type, scopeId(1),
                                                   call.sourceSpan.clone()});
                  }
                  locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::Temporary,
                                                 call.resultType, scopeId(1),
                                                 call.sourceSpan.clone()});
                  zc::Vector<MirStatement> entryStatements;
                  entryStatements.add(
                      MirStatement::storageLive(localId(1), local.sourceSpan.clone()));
                  zc::Vector<MirProjection> initializerProjections;
                  MirRvalue initializerRvalue = [&]() {
                    if (hasAggregateInitializer) {
                      zc::Vector<MirNominalAggregateElement> elements;
                      for (const auto& element : ZC_ASSERT_NONNULL(aggregate).elements) {
                        elements.add(MirNominalAggregateElement{
                            element.field,
                            MirOperand::constant(element.type, element.value.clone())});
                      }
                      return MirRvalue::nominalAggregate(ZC_ASSERT_NONNULL(aggregate).definition,
                                                         ZC_ASSERT_NONNULL(aggregate).type,
                                                         zc::mv(elements));
                    }
                    const auto& lit = ZC_ASSERT_NONNULL(scalarInitializer);
                    return MirRvalue::use(MirOperand::constant(lit.type, lit.value.clone()));
                  }();
                  const identity::SourceSpan& initializerSourceSpan =
                      hasAggregateInitializer ? ZC_ASSERT_NONNULL(aggregate).sourceSpan
                                              : ZC_ASSERT_NONNULL(scalarInitializer).sourceSpan;
                  entryStatements.add(MirStatement::assign(
                      MirPlace(localId(1), local.type, zc::mv(initializerProjections), local.type),
                      zc::mv(initializerRvalue), MirInitializationKind::Initialize,
                      initializerSourceSpan.clone()));
                  entryStatements.add(
                      MirStatement::storageLive(localId(2), reference.sourceSpan.clone()));
                  const bool sharedCall =
                      call.receiverMode == checker::checked::ReceiverMode::Shared;
                  const auto receiverBorrowKind =
                      sharedCall ? MirBorrowKind::Shared : MirBorrowKind::Mutable;
                  zc::Vector<MirProjection> receiverDestinationProjections;
                  zc::Vector<MirProjection> receiverSourceProjections;
                  entryStatements.add(MirStatement::borrowCreation(
                      MirPlace(localId(2), call.receiverType,
                               zc::mv(receiverDestinationProjections), call.receiverType),
                      receiverBorrowKind,
                      MirPlace(localId(1), local.type, zc::mv(receiverSourceProjections),
                               local.type),
                      reference.sourceSpan.clone()));
                  if (hasComparisonArgument) {
                    // Compute the field comparison into the bool temporary at
                    // localId(3): load the field value through a place-use and
                    // compare it against the literal right operand.
                    const auto& comparisonArgument = call.arguments[0];
                    const auto comparisonOp = mirComparisonOperatorFor(
                        ZC_ASSERT_NONNULL(comparisonArgument.comparisonOperation));
                    if (comparisonOp == zc::none) {
                      return rejectMirCapability<BuiltMirCandidate>(
                          ir::IrFailureKind::UnsupportedSourceConstruct, declaration.definition,
                          identities, call.sourceSpan.clone());
                    }
                    entryStatements.add(
                        MirStatement::storageLive(localId(3), call.sourceSpan.clone()));
                    zc::Vector<MirProjection> fieldProjections;
                    fieldProjections.add(
                        MirProjection::field(ZC_ASSERT_NONNULL(comparisonArgument.field),
                                             local.type, comparisonArgument.type));
                    auto fieldOperand =
                        placeUse(proofs, copy,
                                 MirPlace(localId(1), local.type, zc::mv(fieldProjections),
                                          comparisonArgument.type));
                    if (fieldOperand == zc::none) {
                      return rejectMir<BuiltMirCandidate>(
                          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                          module, declaration.definition, identities,
                          static_cast<uint32_t>(pending.size() + 1));
                    }
                    auto literalOperand =
                        MirOperand::constant(comparisonArgument.type,
                                             ZC_ASSERT_NONNULL(comparisonArgument.value).clone());
                    zc::Vector<MirProjection> comparisonDestProjections;
                    entryStatements.add(MirStatement::assign(
                        MirPlace(localId(3), comparisonArgument.type,
                                 zc::mv(comparisonDestProjections), comparisonArgument.type),
                        MirRvalue::comparison(ZC_ASSERT_NONNULL(comparisonOp),
                                              zc::mv(ZC_ASSERT_NONNULL(fieldOperand)),
                                              zc::mv(literalOperand), comparisonArgument.type),
                        MirInitializationKind::Initialize, call.sourceSpan.clone()));
                  }
                  entryStatements.add(
                      MirStatement::storageLive(resultLocal, call.sourceSpan.clone()));
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
                    if (argument.comparisonOperation != zc::none) {
                      // A field-comparison argument: pass the bool temporary
                      // holding the comparison result (localId(3)).
                      zc::Vector<MirProjection> comparisonArgProjections;
                      auto comparisonArgOperand =
                          placeUse(proofs, copy,
                                   MirPlace(localId(3), argument.type,
                                            zc::mv(comparisonArgProjections), argument.type));
                      if (comparisonArgOperand == zc::none) {
                        return rejectMir<BuiltMirCandidate>(
                            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                            module, declaration.definition, identities,
                            static_cast<uint32_t>(pending.size() + 1));
                      }
                      arguments.add(zc::mv(ZC_ASSERT_NONNULL(comparisonArgOperand)));
                      continue;
                    }
                    ZC_IF_SOME(value, argument.value) {
                      arguments.add(MirOperand::constant(argument.type, value.clone()));
                    } else {
                      ZC_IF_SOME(argumentLocal, argument.local) {
                        ZC_IF_SOME(field, argument.field) {
                          // A field-projection argument on the receiver local:
                          // copy the field value through a place-use.
                          if (argumentLocal != local.local) {
                            return rejectMir<BuiltMirCandidate>(
                                ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                                module, declaration.definition, identities,
                                static_cast<uint32_t>(pending.size() + 1));
                          }
                          zc::Vector<MirProjection> argumentProjections;
                          argumentProjections.add(
                              MirProjection::field(field, local.type, argument.type));
                          auto argumentOperand =
                              placeUse(proofs, copy,
                                       MirPlace(localId(1), local.type, zc::mv(argumentProjections),
                                                argument.type));
                          if (argumentOperand == zc::none) {
                            return rejectMir<BuiltMirCandidate>(
                                ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                                module, declaration.definition, identities,
                                static_cast<uint32_t>(pending.size() + 1));
                          }
                          arguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentOperand)));
                        } else {
                          constantArguments = false;
                        }
                      } else {
                        constantArguments = false;
                      }
                    }
                  }
                  if (!constantArguments) {
                    return rejectMirCapability<BuiltMirCandidate>(
                        ir::IrFailureKind::UnsupportedSourceConstruct, declaration.definition,
                        identities, call.sourceSpan.clone());
                  }
                  zc::Vector<MirProjection> resultProjections;
                  zc::Maybe<MirBlockId> noUnwind;
                  auto receiverEffect = sharedCall
                                            ? MirCallEffect::noActivation()
                                            : MirCallEffect::activateMutableReceiver(localId(2));
                  auto callTerminator =
                      MirTerminator::call(call.callee, zc::mv(arguments), zc::mv(receiverEffect),
                                          MirPlace(resultLocal, call.resultType,
                                                   zc::mv(resultProjections), call.resultType),
                                          blockId(2), zc::mv(noUnwind), call.sourceSpan.clone());
                  zc::Vector<MirProjection> returnProjections;
                  auto returnOperand =
                      placeUse(proofs, copy,
                               MirPlace(resultLocal, call.resultType, zc::mv(returnProjections),
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
      // Loop-body composite body: `mut x = <lit>; while (cond) { <writes to x> }
      // return x;`. Lowers to a reducible four-block CFG. Parameters occupy
      // localId(1..P); the user local x is localId(P+1). The entry block declares
      // and initializes x then jumps to the header; the header switches on the
      // bool condition parameter into the body (true) or the exit (default); the
      // body carries the write assignments (Overwrite) then jumps back to the
      // header (the reducible back-edge); the exit returns a place-use of x.
      if (block.statements.size() == 3) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto loop = loopFor(hirModule, block.statements[1]);
        auto sourceReturn = returnFor(hirModule, block.statements[2]);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(loopValue, loop) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
              auto initializer = expressionFor(hirModule, initializerNode);
              auto reference = localReferenceFor(hirModule, returnStatement.value);
              auto conditionRef = parameterReferenceFor(hirModule, loopValue.condition);
              const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
              const auto userLocalId = localId(parameterCount + 1);
              ZC_IF_SOME(initialValue, initializer) {
                ZC_IF_SOME(localReference, reference) {
                  ZC_IF_SOME(condition, conditionRef) {
                    size_t conditionIndex = 0;
                    bool conditionFound = false;
                    for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                      if (declaration.parameters[p].key == condition.parameter) {
                        conditionIndex = p;
                        conditionFound = true;
                        break;
                      }
                    }
                    if (conditionFound && local.initializer != zc::none &&
                        local.local == localReference.local &&
                        local.type == declaration.resultType && initialValue.type == local.type &&
                        localReference.type == local.type &&
                        localReference.category == hir::HirValueCategory::Place &&
                        condition.type == declaration.parameters[conditionIndex].type &&
                        definition != zc::none) {
                      // Build the loop-body write assignments. Each write is an
                      // Overwrite of x whose value is a literal constant, a copy
                      // place-use of a parameter, or an Arithmetic/Comparison
                      // rvalue, reusing the flat mut-local write lowering.
                      zc::Vector<MirStatement> bodyStatements;
                      bool built = true;
                      for (size_t writeIndex = 0; built && writeIndex < loopValue.body.size();
                           ++writeIndex) {
                        auto write = localWriteFor(hirModule, loopValue.body[writeIndex]);
                        if (write == zc::none || ZC_ASSERT_NONNULL(write).local != local.local ||
                            ZC_ASSERT_NONNULL(write).field != zc::none ||
                            ZC_ASSERT_NONNULL(write).type != local.type ||
                            ZC_ASSERT_NONNULL(write).kind != hir::HirLocalWriteKind::Overwrite) {
                          built = false;
                          break;
                        }
                        const auto valueNode = ZC_ASSERT_NONNULL(write).value;
                        auto writeLiteral = expressionFor(hirModule, valueNode);
                        auto writeParameter = parameterReferenceFor(hirModule, valueNode);
                        auto writeBinary = primitiveBinaryFor(hirModule, valueNode);
                        zc::Maybe<MirRvalue> rvalue;
                        ZC_IF_SOME(literalValue, writeLiteral) {
                          if (literalValue.type != local.type) {
                            built = false;
                          } else {
                            rvalue = MirRvalue::use(
                                MirOperand::constant(local.type, literalValue.value.clone()));
                          }
                        }
                        ZC_IF_SOME(parameterValue, writeParameter) {
                          size_t parameterIndex = 0;
                          bool found = false;
                          for (size_t p = 0; p < declaration.parameters.size(); ++p) {
                            if (declaration.parameters[p].key == parameterValue.parameter) {
                              parameterIndex = p;
                              found = true;
                              break;
                            }
                          }
                          if (!found || parameterValue.type != local.type ||
                              parameterValue.category != hir::HirValueCategory::Place) {
                            built = false;
                          } else {
                            zc::Vector<MirProjection> projections;
                            auto operand = placeUse(
                                proofs, copy,
                                MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                         local.type, zc::mv(projections), local.type));
                            if (operand == zc::none) {
                              built = false;
                            } else {
                              rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
                            }
                          }
                        }
                        ZC_IF_SOME(binaryValue, writeBinary) {
                          const auto comparisonOperator =
                              mirComparisonOperatorFor(binaryValue.operation);
                          const auto arithmeticOperator =
                              mirArithmeticOperatorFor(binaryValue.operation);
                          const bool isArithmeticBinary =
                              comparisonOperator == zc::none && arithmeticOperator != zc::none;
                          if (binaryValue.type != local.type ||
                              binaryValue.category != hir::HirValueCategory::Value ||
                              (comparisonOperator == zc::none && arithmeticOperator == zc::none) ||
                              (isArithmeticBinary && binaryValue.operandType != local.type)) {
                            built = false;
                          } else {
                            auto buildOperand =
                                [&](hir::HirNodeId operandNode) -> zc::Maybe<MirOperand> {
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
                                return placeUse(
                                    proofs, copy,
                                    MirPlace(localId(static_cast<uint32_t>(parameterIndex) + 1),
                                             binaryValue.operandType, zc::mv(projections),
                                             binaryValue.operandType));
                              }
                              // A local operand reads the written user local
                              // (`x = x + 1`): a copy place-use of that local.
                              auto operandLocal = localReferenceFor(hirModule, operandNode);
                              ZC_IF_SOME(localRef, operandLocal) {
                                if (localRef.type != binaryValue.operandType ||
                                    localRef.local != local.local) {
                                  return zc::none;
                                }
                                zc::Vector<MirProjection> projections;
                                return placeUse(
                                    proofs, copy,
                                    MirPlace(userLocalId, binaryValue.operandType,
                                             zc::mv(projections), binaryValue.operandType));
                              }
                              return zc::none;
                            };
                            auto leftOperand = buildOperand(binaryValue.left);
                            auto rightOperand = buildOperand(binaryValue.right);
                            if (leftOperand == zc::none || rightOperand == zc::none) {
                              built = false;
                            } else {
                              rvalue = isArithmeticBinary
                                           ? MirRvalue::arithmetic(
                                                 ZC_ASSERT_NONNULL(arithmeticOperator),
                                                 zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                 zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                 binaryValue.type)
                                           : MirRvalue::comparison(
                                                 ZC_ASSERT_NONNULL(comparisonOperator),
                                                 zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                 zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                 binaryValue.type);
                            }
                          }
                        }
                        if (!built || rvalue == zc::none) {
                          built = false;
                          break;
                        }
                        zc::Vector<MirProjection> overwriteProjections;
                        bodyStatements.add(MirStatement::assign(
                            MirPlace(userLocalId, local.type, zc::mv(overwriteProjections),
                                     local.type),
                            zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Overwrite,
                            ZC_ASSERT_NONNULL(write).sourceSpan.clone()));
                      }
                      if (!built) {
                        return rejectMir<BuiltMirCandidate>(
                            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                            module, declaration.definition, identities,
                            static_cast<uint32_t>(pending.size() + 1));
                      }
                      const auto conditionLocal =
                          localId(static_cast<uint32_t>(conditionIndex) + 1);
                      zc::Vector<MirSourceScope> scopes;
                      zc::Maybe<MirSourceScopeId> noParent;
                      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                                declaration.sourceSpan.clone()});
                      zc::Vector<MirLocalDeclaration> locals;
                      for (uint32_t p = 0; p < parameterCount; ++p) {
                        locals.add(MirLocalDeclaration{
                            localId(p + 1), MirLocalKind::Parameter, declaration.parameters[p].type,
                            scopeId(1), declaration.parameters[p].sourceSpan.clone()});
                      }
                      locals.add(MirLocalDeclaration{userLocalId, MirLocalKind::UserLocal,
                                                     local.type, scopeId(1),
                                                     local.sourceSpan.clone()});
                      zc::Vector<MirProjection> conditionProjections;
                      auto discriminant =
                          placeUse(proofs, copy,
                                   MirPlace(conditionLocal, condition.type,
                                            zc::mv(conditionProjections), condition.type));
                      zc::Vector<MirProjection> returnProjections;
                      auto returnOperand = placeUse(
                          proofs, copy,
                          MirPlace(userLocalId, local.type, zc::mv(returnProjections), local.type));
                      if (discriminant == zc::none || returnOperand == zc::none) {
                        return rejectMir<BuiltMirCandidate>(
                            ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact,
                            module, declaration.definition, identities,
                            static_cast<uint32_t>(pending.size() + 1));
                      }
                      zc::Vector<MirStatement> entryStatements;
                      entryStatements.add(
                          MirStatement::storageLive(userLocalId, local.sourceSpan.clone()));
                      zc::Vector<MirProjection> initializeProjections;
                      entryStatements.add(MirStatement::assign(
                          MirPlace(userLocalId, local.type, zc::mv(initializeProjections),
                                   local.type),
                          MirRvalue::use(
                              MirOperand::constant(local.type, initialValue.value.clone())),
                          MirInitializationKind::Initialize, initialValue.sourceSpan.clone()));
                      zc::Vector<MirSwitchIntArm> arms;
                      arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                                               blockId(3)});
                      zc::Vector<MirStatement> exitStatements;
                      // A trailing break exits the body to the loop exit (bb4); a
                      // trailing continue or a write-only body jumps back to the
                      // header (bb2, the reducible back-edge).
                      const auto bodyTerminatorTarget =
                          loopValue.breakSpan != zc::none ? blockId(4) : blockId(2);
                      const auto& bodyTerminatorSpan =
                          loopValue.breakSpan != zc::none
                              ? ZC_ASSERT_NONNULL(loopValue.breakSpan)
                              : (loopValue.continueSpan != zc::none
                                     ? ZC_ASSERT_NONNULL(loopValue.continueSpan)
                                     : loopValue.sourceSpan);
                      zc::Vector<MirBasicBlock> blocks;
                      blocks.add(MirBasicBlock{
                          blockId(1), scopeId(1), zc::mv(entryStatements),
                          MirTerminator::gotoTarget(blockId(2), loopValue.sourceSpan.clone())});
                      blocks.add(MirBasicBlock{
                          blockId(2), scopeId(1), zc::Vector<MirStatement>{},
                          MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                   zc::mv(arms), blockId(4),
                                                   loopValue.sourceSpan.clone())});
                      blocks.add(
                          MirBasicBlock{blockId(3), scopeId(1), zc::mv(bodyStatements),
                                        MirTerminator::gotoTarget(bodyTerminatorTarget,
                                                                  bodyTerminatorSpan.clone())});
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
            }
          }
        }
      }
      // For-loop composite body: `for (let i = <lit>; i < <lit>; i = i <bin>
      // <lit>) {} return <lit>;`. Lowers to a reducible four-block CFG.
      // Parameters occupy localId(1..P); the init local i is localId(P+1); the
      // comparison temp is localId(P+2); the result local is localId(P+3). The
      // entry block declares the result and init locals and initializes i then
      // jumps to the header; the header evaluates the comparison into the temp
      // and switches on it into the body (true) or the exit (default); the body
      // carries the update write (Overwrite) then jumps back to the header (the
      // reducible back-edge); the exit initializes the result with the return
      // literal and returns it.
      if (block.statements.size() == 3) {
        auto sourceLocal = localFor(hirModule, block.statements[0]);
        auto loop = loopFor(hirModule, block.statements[1]);
        auto sourceReturn = returnFor(hirModule, block.statements[2]);
        auto definition = identities.definition(declaration.definition);
        ZC_IF_SOME(local, sourceLocal) {
          ZC_IF_SOME(loopValue, loop) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(value, local.initializer) { initializerNode = value; }
              auto initializer = expressionFor(hirModule, initializerNode);
              auto conditionBinary = primitiveBinaryFor(hirModule, loopValue.condition);
              auto returnLiteral = expressionFor(hirModule, returnStatement.value);
              const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
              const auto initLocalId = localId(parameterCount + 1);
              const auto conditionTempId = localId(parameterCount + 2);
              const auto resultLocalId = localId(parameterCount + 3);
              ZC_IF_SOME(initValue, initializer) {
                ZC_IF_SOME(condition, conditionBinary) {
                  ZC_IF_SOME(returnValue, returnLiteral) {
                    const auto comparisonOperator = mirComparisonOperatorFor(condition.operation);
                    if (local.initializer == zc::none || local.local.ordinal() != 1 ||
                        initValue.type != local.type || comparisonOperator == zc::none ||
                        condition.operandType != local.type || loopValue.body.size() != 1 ||
                        returnValue.type != declaration.resultType || definition == zc::none) {
                      // Not a for-loop shape; fall through to the next arm.
                    } else {
                      // Resolve the condition operands.
                      auto conditionLeft = localReferenceFor(hirModule, condition.left);
                      auto conditionRight = expressionFor(hirModule, condition.right);
                      // Resolve the update write and its arithmetic value.
                      auto write = localWriteFor(hirModule, loopValue.body[0]);
                      ZC_IF_SOME(condLeft, conditionLeft) {
                        ZC_IF_SOME(condRight, conditionRight) {
                          ZC_IF_SOME(updateWrite, write) {
                            auto writeValueBinary =
                                primitiveBinaryFor(hirModule, updateWrite.value);
                            ZC_IF_SOME(writeValue, writeValueBinary) {
                              const auto arithmeticOperator =
                                  mirArithmeticOperatorFor(writeValue.operation);
                              if (condLeft.local != local.local || condLeft.type != local.type ||
                                  condRight.type != local.type ||
                                  updateWrite.local != local.local ||
                                  updateWrite.field != zc::none || updateWrite.type != local.type ||
                                  updateWrite.kind != hir::HirLocalWriteKind::Overwrite ||
                                  arithmeticOperator == zc::none ||
                                  writeValue.operandType != local.type ||
                                  writeValue.type != local.type) {
                                // Not a for-loop shape; fall through.
                              } else {
                                auto writeValueLeft = localReferenceFor(hirModule, writeValue.left);
                                auto writeValueRight = expressionFor(hirModule, writeValue.right);
                                ZC_IF_SOME(updateLeft, writeValueLeft) {
                                  ZC_IF_SOME(updateRight, writeValueRight) {
                                    if (updateLeft.local != local.local ||
                                        updateLeft.type != local.type ||
                                        updateRight.type != local.type) {
                                      // Not a for-loop shape; fall through.
                                    } else {
                                      // Build the reducible four-block CFG.
                                      zc::Vector<MirSourceScope> scopes;
                                      zc::Maybe<MirSourceScopeId> noParent;
                                      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent),
                                                                declaration.sourceSpan.clone()});
                                      zc::Vector<MirLocalDeclaration> locals;
                                      for (uint32_t p = 0; p < parameterCount; ++p) {
                                        locals.add(MirLocalDeclaration{
                                            localId(p + 1), MirLocalKind::Parameter,
                                            declaration.parameters[p].type, scopeId(1),
                                            declaration.parameters[p].sourceSpan.clone()});
                                      }
                                      locals.add(MirLocalDeclaration{
                                          initLocalId, MirLocalKind::UserLocal, local.type,
                                          scopeId(1), local.sourceSpan.clone()});
                                      locals.add(MirLocalDeclaration{
                                          conditionTempId, MirLocalKind::Temporary, condition.type,
                                          scopeId(1), condition.sourceSpan.clone()});
                                      locals.add(MirLocalDeclaration{
                                          resultLocalId, MirLocalKind::FunctionResult,
                                          declaration.resultType, scopeId(1),
                                          returnStatement.sourceSpan.clone()});
                                      // Entry: StorageLive(result), StorageLive(i),
                                      // StorageLive(temp), Assign(i = init, Initialize),
                                      // Assign(temp = Comparison(op, copy(i), <lit>),
                                      // Initialize), Goto(bb2).
                                      zc::Vector<MirStatement> entryStatements;
                                      entryStatements.add(MirStatement::storageLive(
                                          resultLocalId, returnStatement.sourceSpan.clone()));
                                      entryStatements.add(MirStatement::storageLive(
                                          initLocalId, local.sourceSpan.clone()));
                                      entryStatements.add(MirStatement::storageLive(
                                          conditionTempId, condition.sourceSpan.clone()));
                                      zc::Vector<MirProjection> initProjections;
                                      entryStatements.add(MirStatement::assign(
                                          MirPlace(initLocalId, local.type, zc::mv(initProjections),
                                                   local.type),
                                          MirRvalue::use(MirOperand::constant(
                                              local.type, initValue.value.clone())),
                                          MirInitializationKind::Initialize,
                                          initValue.sourceSpan.clone()));
                                      // Condition comparison in the entry block.
                                      zc::Vector<MirProjection> entryCondLeftProjections;
                                      auto entryCondLeftOperand = placeUse(
                                          proofs, copy,
                                          MirPlace(initLocalId, local.type,
                                                   zc::mv(entryCondLeftProjections), local.type));
                                      if (entryCondLeftOperand == zc::none) {
                                        return rejectMir<BuiltMirCandidate>(
                                            ir::IrFailurePhase::MirConstruction,
                                            ir::IrFailureKind::InvalidFact, module,
                                            declaration.definition, identities,
                                            static_cast<uint32_t>(pending.size() + 1));
                                      }
                                      zc::Vector<MirProjection> entryTempProjections;
                                      entryStatements.add(MirStatement::assign(
                                          MirPlace(conditionTempId, condition.type,
                                                   zc::mv(entryTempProjections), condition.type),
                                          MirRvalue::comparison(
                                              ZC_ASSERT_NONNULL(comparisonOperator),
                                              zc::mv(ZC_ASSERT_NONNULL(entryCondLeftOperand)),
                                              MirOperand::constant(condRight.type,
                                                                   condRight.value.clone()),
                                              condition.type),
                                          MirInitializationKind::Initialize,
                                          condition.sourceSpan.clone()));
                                      // Header: SwitchInt(copy(temp), [true -> bb3],
                                      // default = bb4).
                                      zc::Vector<MirProjection> discriminantProjections;
                                      auto discriminant =
                                          placeUse(proofs, copy,
                                                   MirPlace(conditionTempId, condition.type,
                                                            zc::mv(discriminantProjections),
                                                            condition.type));
                                      if (discriminant == zc::none) {
                                        return rejectMir<BuiltMirCandidate>(
                                            ir::IrFailurePhase::MirConstruction,
                                            ir::IrFailureKind::InvalidFact, module,
                                            declaration.definition, identities,
                                            static_cast<uint32_t>(pending.size() + 1));
                                      }
                                      zc::Vector<MirSwitchIntArm> arms;
                                      arms.add(MirSwitchIntArm{
                                          checker::checked::CanonicalConstValue::boolean(true),
                                          blockId(3)});
                                      // Body: Assign(i = Arithmetic(op, copy(i),
                                      // <lit>), Overwrite), Assign(temp =
                                      // Comparison(op, copy(i), <lit>), Overwrite),
                                      // Goto(bb2).
                                      zc::Vector<MirProjection> updateLeftProjections;
                                      auto updateLeftOperand = placeUse(
                                          proofs, copy,
                                          MirPlace(initLocalId, local.type,
                                                   zc::mv(updateLeftProjections), local.type));
                                      if (updateLeftOperand == zc::none) {
                                        return rejectMir<BuiltMirCandidate>(
                                            ir::IrFailurePhase::MirConstruction,
                                            ir::IrFailureKind::InvalidFact, module,
                                            declaration.definition, identities,
                                            static_cast<uint32_t>(pending.size() + 1));
                                      }
                                      zc::Vector<MirStatement> bodyStatements;
                                      zc::Vector<MirProjection> overwriteProjections;
                                      bodyStatements.add(MirStatement::assign(
                                          MirPlace(initLocalId, local.type,
                                                   zc::mv(overwriteProjections), local.type),
                                          MirRvalue::arithmetic(
                                              ZC_ASSERT_NONNULL(arithmeticOperator),
                                              zc::mv(ZC_ASSERT_NONNULL(updateLeftOperand)),
                                              MirOperand::constant(updateRight.type,
                                                                   updateRight.value.clone()),
                                              writeValue.type),
                                          MirInitializationKind::Overwrite,
                                          updateWrite.sourceSpan.clone()));
                                      // Recompute the condition at the end of the
                                      // body for the next iteration.
                                      zc::Vector<MirProjection> bodyCondLeftProjections;
                                      auto bodyCondLeftOperand = placeUse(
                                          proofs, copy,
                                          MirPlace(initLocalId, local.type,
                                                   zc::mv(bodyCondLeftProjections), local.type));
                                      if (bodyCondLeftOperand == zc::none) {
                                        return rejectMir<BuiltMirCandidate>(
                                            ir::IrFailurePhase::MirConstruction,
                                            ir::IrFailureKind::InvalidFact, module,
                                            declaration.definition, identities,
                                            static_cast<uint32_t>(pending.size() + 1));
                                      }
                                      zc::Vector<MirProjection> bodyTempProjections;
                                      bodyStatements.add(MirStatement::assign(
                                          MirPlace(conditionTempId, condition.type,
                                                   zc::mv(bodyTempProjections), condition.type),
                                          MirRvalue::comparison(
                                              ZC_ASSERT_NONNULL(comparisonOperator),
                                              zc::mv(ZC_ASSERT_NONNULL(bodyCondLeftOperand)),
                                              MirOperand::constant(condRight.type,
                                                                   condRight.value.clone()),
                                              condition.type),
                                          MirInitializationKind::Overwrite,
                                          condition.sourceSpan.clone()));
                                      // Exit: Assign(result = <lit>, Initialize),
                                      // Return(placeUse(result)).
                                      zc::Vector<MirProjection> returnProjections;
                                      auto returnOperand =
                                          placeUse(proofs, copy,
                                                   MirPlace(resultLocalId, declaration.resultType,
                                                            zc::mv(returnProjections),
                                                            declaration.resultType));
                                      if (returnOperand == zc::none) {
                                        return rejectMir<BuiltMirCandidate>(
                                            ir::IrFailurePhase::MirConstruction,
                                            ir::IrFailureKind::InvalidFact, module,
                                            declaration.definition, identities,
                                            static_cast<uint32_t>(pending.size() + 1));
                                      }
                                      zc::Vector<MirStatement> exitStatements;
                                      zc::Vector<MirProjection> exitProjections;
                                      exitStatements.add(MirStatement::assign(
                                          MirPlace(resultLocalId, declaration.resultType,
                                                   zc::mv(exitProjections), declaration.resultType),
                                          MirRvalue::use(MirOperand::constant(
                                              declaration.resultType, returnValue.value.clone())),
                                          MirInitializationKind::Initialize,
                                          returnValue.sourceSpan.clone()));
                                      // A trailing break exits the body to the
                                      // loop exit (bb4); a trailing continue or
                                      // a write-only body jumps back to the
                                      // header (bb2, the reducible back-edge).
                                      const auto bodyTerminatorTarget =
                                          loopValue.breakSpan != zc::none ? blockId(4) : blockId(2);
                                      const auto& bodyTerminatorSpan =
                                          loopValue.breakSpan != zc::none
                                              ? ZC_ASSERT_NONNULL(loopValue.breakSpan)
                                              : (loopValue.continueSpan != zc::none
                                                     ? ZC_ASSERT_NONNULL(loopValue.continueSpan)
                                                     : loopValue.sourceSpan);
                                      zc::Vector<MirBasicBlock> blocks;
                                      blocks.add(MirBasicBlock{
                                          blockId(1), scopeId(1), zc::mv(entryStatements),
                                          MirTerminator::gotoTarget(blockId(2),
                                                                    loopValue.sourceSpan.clone())});
                                      blocks.add(MirBasicBlock{
                                          blockId(2), scopeId(1), zc::Vector<MirStatement>{},
                                          MirTerminator::switchInt(
                                              zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                              blockId(4), loopValue.sourceSpan.clone())});
                                      blocks.add(MirBasicBlock{
                                          blockId(3), scopeId(1), zc::mv(bodyStatements),
                                          MirTerminator::gotoTarget(bodyTerminatorTarget,
                                                                    bodyTerminatorSpan.clone())});
                                      blocks.add(MirBasicBlock{
                                          blockId(4), scopeId(1), zc::mv(exitStatements),
                                          MirTerminator::returnValue(
                                              zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
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
                                      pending.add(
                                          PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
                                      continue;
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      // Nested for-loop accumulator composite body: N leading `mut` accumulator
      // locals, an outer `for (let i = <lit>; i < <lit>; i = i <bin> <lit>)`
      // whose sole body statements are an inner `let j = <lit>` binding and an
      // inner `for (let j = <lit>; j < <lit>; j = j <bin> <lit>) { acc_k =
      // acc_k <bin> j|<lit>; ... }` loop followed by the outer update write,
      // and a trailing `return acc_0;`. Lowers to a reducible seven-block CFG.
      // Parameters occupy localId(1..P); accumulator k is localId(P+1+k); the
      // outer init local i is localId(P+1+N); the outer comparison temp is
      // localId(P+2+N); the inner init local j is localId(P+3+N); the inner
      // comparison temp is localId(P+4+N); the result local is localId(P+5+N).
      if (block.statements.size() >= 4) {
        const size_t accCount = block.statements.size() - 3;
        auto sourceInitLocal = localFor(hirModule, block.statements[accCount]);
        auto loop = loopFor(hirModule, block.statements[accCount + 1]);
        auto sourceReturn = returnFor(hirModule, block.statements[accCount + 2]);
        auto definition = identities.definition(declaration.definition);
        zc::Vector<zc::Maybe<const hir::HirLocalBinding&>> sourceAccLocals;
        bool accLocalsOk = true;
        for (size_t k = 0; k < accCount; ++k) {
          auto accLocal = localFor(hirModule, block.statements[k]);
          if (accLocal == zc::none) {
            accLocalsOk = false;
            break;
          }
          sourceAccLocals.add(zc::mv(accLocal));
        }
        if (accLocalsOk) {
          ZC_IF_SOME(initLocal, sourceInitLocal) {
            ZC_IF_SOME(loopValue, loop) {
              ZC_IF_SOME(returnStatement, sourceReturn) {
                const uint32_t parameterCount =
                    static_cast<uint32_t>(declaration.parameters.size());
                // The outer loop body must have exactly three statements:
                // inner-init local, inner loop, outer update write.
                if (loopValue.body.size() == 3 && definition != zc::none) {
                  auto innerInitLocal = localFor(hirModule, loopValue.body[0]);
                  auto innerLoop = loopFor(hirModule, loopValue.body[1]);
                  auto outerUpdateWrite = localWriteFor(hirModule, loopValue.body[2]);
                  ZC_IF_SOME(innerInit, innerInitLocal) {
                    ZC_IF_SOME(innerLoopValue, innerLoop) {
                      ZC_IF_SOME(outerWrite, outerUpdateWrite) {
                        // The inner loop body must have N+1 statements:
                        // N accumulator writes followed by the inner update.
                        if (innerLoopValue.body.size() == accCount + 1) {
                          const auto outerInitLocalId =
                              localId(parameterCount + static_cast<uint32_t>(accCount) + 1);
                          const auto outerCondTempId =
                              localId(parameterCount + static_cast<uint32_t>(accCount) + 2);
                          const auto innerInitLocalId =
                              localId(parameterCount + static_cast<uint32_t>(accCount) + 3);
                          const auto innerCondTempId =
                              localId(parameterCount + static_cast<uint32_t>(accCount) + 4);
                          const auto resultLocalId =
                              localId(parameterCount + static_cast<uint32_t>(accCount) + 5);
                          // Resolve accumulator initializers.
                          zc::Vector<zc::Maybe<const hir::HirScalarLiteralExpression&>>
                              accInitializers;
                          zc::Vector<MirLocalId> accMirLocalIds;
                          bool shapeOk = true;
                          for (size_t k = 0; k < accCount && shapeOk; ++k) {
                            const auto& accLocal = ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                            hir::HirNodeId accInitializerNode;
                            ZC_IF_SOME(value, accLocal.initializer) { accInitializerNode = value; }
                            auto accInitializer = expressionFor(hirModule, accInitializerNode);
                            if (accInitializer == zc::none || accLocal.initializer == zc::none ||
                                accLocal.local.ordinal() != static_cast<uint32_t>(k + 1)) {
                              shapeOk = false;
                              break;
                            }
                            accInitializers.add(zc::mv(accInitializer));
                            accMirLocalIds.add(
                                localId(parameterCount + static_cast<uint32_t>(k) + 1));
                          }
                          // Resolve outer init, outer condition, inner init,
                          // inner condition, return reference.
                          hir::HirNodeId outerInitNode;
                          ZC_IF_SOME(value, initLocal.initializer) { outerInitNode = value; }
                          auto outerInitExpr = expressionFor(hirModule, outerInitNode);
                          auto outerCondBinary = primitiveBinaryFor(hirModule, loopValue.condition);
                          auto returnReference =
                              localReferenceFor(hirModule, returnStatement.value);
                          hir::HirNodeId innerInitNode;
                          ZC_IF_SOME(value, innerInit.initializer) { innerInitNode = value; }
                          auto innerInitExpr = expressionFor(hirModule, innerInitNode);
                          auto innerCondBinary =
                              primitiveBinaryFor(hirModule, innerLoopValue.condition);
                          if (shapeOk && outerInitExpr != zc::none && outerCondBinary != zc::none &&
                              returnReference != zc::none && innerInitExpr != zc::none &&
                              innerCondBinary != zc::none) {
                            ZC_IF_SOME(outerInitValue, outerInitExpr) {
                              ZC_IF_SOME(outerCond, outerCondBinary) {
                                ZC_IF_SOME(returnRef, returnReference) {
                                  ZC_IF_SOME(innerInitValue, innerInitExpr) {
                                    ZC_IF_SOME(innerCond, innerCondBinary) {
                                      const auto outerCmpOp =
                                          mirComparisonOperatorFor(outerCond.operation);
                                      const auto innerCmpOp =
                                          mirComparisonOperatorFor(innerCond.operation);
                                      const auto& firstAccLocal =
                                          ZC_ASSERT_NONNULL(sourceAccLocals[0]);
                                      // Validate outer and inner init locals.
                                      if (initLocal.initializer == zc::none ||
                                          initLocal.local.ordinal() !=
                                              static_cast<uint32_t>(accCount + 1) ||
                                          outerInitValue.type != initLocal.type ||
                                          outerCmpOp == zc::none ||
                                          outerCond.operandType != initLocal.type ||
                                          innerInit.initializer == zc::none ||
                                          innerInit.local.ordinal() !=
                                              static_cast<uint32_t>(accCount + 2) ||
                                          innerInitValue.type != innerInit.type ||
                                          innerCmpOp == zc::none ||
                                          innerCond.operandType != innerInit.type ||
                                          returnRef.local != firstAccLocal.local ||
                                          returnRef.type != firstAccLocal.type ||
                                          firstAccLocal.type != declaration.resultType) {
                                        // Not a nested accumulator shape; fall
                                        // through.
                                      } else {
                                        // Resolve outer condition operands.
                                        auto outerCondLeft =
                                            localReferenceFor(hirModule, outerCond.left);
                                        auto outerCondRight =
                                            expressionFor(hirModule, outerCond.right);
                                        // Resolve inner condition operands.
                                        auto innerCondLeft =
                                            localReferenceFor(hirModule, innerCond.left);
                                        auto innerCondRight =
                                            expressionFor(hirModule, innerCond.right);
                                        // Resolve N accumulator body writes.
                                        zc::Vector<zc::Maybe<const hir::HirLocalWriteStatement&>>
                                            bodyWrites;
                                        zc::Vector<
                                            zc::Maybe<const hir::HirPrimitiveBinaryExpression&>>
                                            bodyWriteBinaries;
                                        zc::Vector<
                                            zc::Maybe<const hir::HirLocalReferenceExpression&>>
                                            bodyWriteLhss;
                                        zc::Vector<
                                            zc::Maybe<const hir::HirLocalReferenceExpression&>>
                                            bodyWriteRhss;
                                        zc::Vector<
                                            zc::Maybe<const hir::HirScalarLiteralExpression&>>
                                            bodyWriteRhsLits;
                                        zc::Vector<MirArithmeticOperator> bodyWriteOps;
                                        zc::Vector<bool> bodyWriteRhsIsLiteral;
                                        bool bodyWritesOk = true;
                                        for (size_t k = 0; k < accCount; ++k) {
                                          auto bw =
                                              localWriteFor(hirModule, innerLoopValue.body[k]);
                                          if (bw == zc::none) {
                                            bodyWritesOk = false;
                                            break;
                                          }
                                          const auto& bwRef = ZC_ASSERT_NONNULL(bw);
                                          auto bwBin = primitiveBinaryFor(hirModule, bwRef.value);
                                          if (bwBin == zc::none) {
                                            bodyWritesOk = false;
                                            break;
                                          }
                                          const auto& bwBinRef = ZC_ASSERT_NONNULL(bwBin);
                                          auto bwLhs = localReferenceFor(hirModule, bwBinRef.left);
                                          if (bwLhs == zc::none) {
                                            bodyWritesOk = false;
                                            break;
                                          }
                                          auto bwRhsRef =
                                              localReferenceFor(hirModule, bwBinRef.right);
                                          auto bwRhsLit = expressionFor(hirModule, bwBinRef.right);
                                          const bool rhsIsLit =
                                              bwRhsRef == zc::none && bwRhsLit != zc::none;
                                          if (bwRhsRef == zc::none && bwRhsLit == zc::none) {
                                            bodyWritesOk = false;
                                            break;
                                          }
                                          auto bwOp = mirArithmeticOperatorFor(bwBinRef.operation);
                                          if (bwOp == zc::none) {
                                            bodyWritesOk = false;
                                            break;
                                          }
                                          bodyWrites.add(zc::mv(bw));
                                          bodyWriteBinaries.add(zc::mv(bwBin));
                                          bodyWriteLhss.add(zc::mv(bwLhs));
                                          bodyWriteRhss.add(zc::mv(bwRhsRef));
                                          bodyWriteRhsLits.add(zc::mv(bwRhsLit));
                                          bodyWriteOps.add(ZC_ASSERT_NONNULL(bwOp));
                                          bodyWriteRhsIsLiteral.add(rhsIsLit);
                                        }
                                        // Resolve inner and outer update writes.
                                        auto innerUpdateWrite =
                                            localWriteFor(hirModule, innerLoopValue.body[accCount]);
                                        if (bodyWritesOk && innerUpdateWrite != zc::none) {
                                          ZC_IF_SOME(outerCondLhs, outerCondLeft) {
                                            ZC_IF_SOME(outerCondRhs, outerCondRight) {
                                              ZC_IF_SOME(innerCondLhs, innerCondLeft) {
                                                ZC_IF_SOME(innerCondRhs, innerCondRight) {
                                                  ZC_IF_SOME(innerWrite, innerUpdateWrite) {
                                                    auto innerWriteBin = primitiveBinaryFor(
                                                        hirModule, innerWrite.value);
                                                    ZC_IF_SOME(innerWriteValue, innerWriteBin) {
                                                      const auto innerWriteOp =
                                                          mirArithmeticOperatorFor(
                                                              innerWriteValue.operation);
                                                      // Validate body writes.
                                                      bool bodyWritesValid =
                                                          outerCondLhs.local == initLocal.local &&
                                                          outerCondLhs.type == initLocal.type &&
                                                          outerCondRhs.type == initLocal.type &&
                                                          innerCondLhs.local == innerInit.local &&
                                                          innerCondLhs.type == innerInit.type &&
                                                          innerCondRhs.type == innerInit.type &&
                                                          innerWrite.local == innerInit.local &&
                                                          innerWrite.field == zc::none &&
                                                          innerWrite.type == innerInit.type &&
                                                          innerWrite.kind ==
                                                              hir::HirLocalWriteKind::Overwrite &&
                                                          innerWriteOp != zc::none &&
                                                          innerWriteValue.operandType ==
                                                              innerInit.type &&
                                                          innerWriteValue.type == innerInit.type &&
                                                          outerWrite.local == initLocal.local &&
                                                          outerWrite.field == zc::none &&
                                                          outerWrite.type == initLocal.type &&
                                                          outerWrite.kind ==
                                                              hir::HirLocalWriteKind::Overwrite;
                                                      for (size_t k = 0;
                                                           k < accCount && bodyWritesValid; ++k) {
                                                        const auto& bw =
                                                            ZC_ASSERT_NONNULL(bodyWrites[k]);
                                                        const auto& bwValue =
                                                            ZC_ASSERT_NONNULL(bodyWriteBinaries[k]);
                                                        const auto& bwLhs =
                                                            ZC_ASSERT_NONNULL(bodyWriteLhss[k]);
                                                        const auto& accLocal =
                                                            ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                                        if (bw.local != accLocal.local ||
                                                            bw.field != zc::none ||
                                                            bw.type != accLocal.type ||
                                                            bw.kind !=
                                                                hir::HirLocalWriteKind::Overwrite ||
                                                            bwValue.operandType != accLocal.type ||
                                                            bwValue.type != accLocal.type ||
                                                            bwLhs.local != accLocal.local ||
                                                            bwLhs.type != accLocal.type) {
                                                          bodyWritesValid = false;
                                                          break;
                                                        }
                                                        if (bodyWriteRhsIsLiteral[k]) {
                                                          const auto& bwRhsLit = ZC_ASSERT_NONNULL(
                                                              bodyWriteRhsLits[k]);
                                                          if (bwRhsLit.type != accLocal.type) {
                                                            bodyWritesValid = false;
                                                            break;
                                                          }
                                                        } else {
                                                          const auto& bwRhs =
                                                              ZC_ASSERT_NONNULL(bodyWriteRhss[k]);
                                                          if (bwRhs.local != innerInit.local ||
                                                              bwRhs.type != innerInit.type) {
                                                            bodyWritesValid = false;
                                                            break;
                                                          }
                                                        }
                                                      }
                                                      // Resolve inner update
                                                      // operands.
                                                      auto innerWriteLeft = localReferenceFor(
                                                          hirModule, innerWriteValue.left);
                                                      auto innerWriteRight = expressionFor(
                                                          hirModule, innerWriteValue.right);
                                                      // Resolve outer update
                                                      // operands.
                                                      auto outerWriteBin = primitiveBinaryFor(
                                                          hirModule, outerWrite.value);
                                                      if (bodyWritesValid &&
                                                          innerWriteLeft != zc::none &&
                                                          innerWriteRight != zc::none &&
                                                          outerWriteBin != zc::none) {
                                                        ZC_IF_SOME(innerWriteLhs, innerWriteLeft) {
                                                          ZC_IF_SOME(innerWriteRhs,
                                                                     innerWriteRight) {
                                                            ZC_IF_SOME(outerWriteValue,
                                                                       outerWriteBin) {
                                                              const auto outerWriteOp =
                                                                  mirArithmeticOperatorFor(
                                                                      outerWriteValue.operation);
                                                              auto outerWriteLeft =
                                                                  localReferenceFor(
                                                                      hirModule,
                                                                      outerWriteValue.left);
                                                              auto outerWriteRight = expressionFor(
                                                                  hirModule, outerWriteValue.right);
                                                              if (innerWriteLhs.local !=
                                                                      innerInit.local ||
                                                                  innerWriteLhs.type !=
                                                                      innerInit.type ||
                                                                  innerWriteRhs.type !=
                                                                      innerInit.type ||
                                                                  outerWriteOp == zc::none ||
                                                                  outerWriteValue.operandType !=
                                                                      initLocal.type ||
                                                                  outerWriteValue.type !=
                                                                      initLocal.type ||
                                                                  outerWriteLeft == zc::none ||
                                                                  outerWriteRight == zc::none) {
                                                                // Not a nested
                                                                // accumulator
                                                                // shape; fall
                                                                // through.
                                                              } else {
                                                                ZC_IF_SOME(outerWriteLhs,
                                                                           outerWriteLeft) {
                                                                  ZC_IF_SOME(outerWriteRhs,
                                                                             outerWriteRight) {
                                                                    if (outerWriteLhs.local !=
                                                                            initLocal.local ||
                                                                        outerWriteLhs.type !=
                                                                            initLocal.type ||
                                                                        outerWriteRhs.type !=
                                                                            initLocal.type) {
                                                                      // Not a
                                                                      // nested
                                                                      // accumulator
                                                                      // shape;
                                                                      // fall
                                                                      // through.
                                                                    } else {
                                                                      // Build the
                                                                      // reducible
                                                                      // seven-block
                                                                      // CFG.
                                                                      zc::Vector<MirSourceScope>
                                                                          scopes;
                                                                      zc::Maybe<MirSourceScopeId>
                                                                          noParent;
                                                                      scopes.add(MirSourceScope{
                                                                          scopeId(1),
                                                                          zc::mv(noParent),
                                                                          declaration.sourceSpan
                                                                              .clone()});
                                                                      zc::Vector<
                                                                          MirLocalDeclaration>
                                                                          locals;
                                                                      for (uint32_t p = 0;
                                                                           p < parameterCount;
                                                                           ++p) {
                                                                        locals.add(
                                                                            MirLocalDeclaration{
                                                                                localId(p + 1),
                                                                                MirLocalKind::
                                                                                    Parameter,
                                                                                declaration
                                                                                    .parameters[p]
                                                                                    .type,
                                                                                scopeId(1),
                                                                                declaration
                                                                                    .parameters[p]
                                                                                    .sourceSpan
                                                                                    .clone()});
                                                                      }
                                                                      for (size_t k = 0;
                                                                           k < accCount; ++k) {
                                                                        const auto& accLocal =
                                                                            ZC_ASSERT_NONNULL(
                                                                                sourceAccLocals[k]);
                                                                        locals.add(
                                                                            MirLocalDeclaration{
                                                                                accMirLocalIds[k],
                                                                                MirLocalKind::
                                                                                    UserLocal,
                                                                                accLocal.type,
                                                                                scopeId(1),
                                                                                accLocal.sourceSpan
                                                                                    .clone()});
                                                                      }
                                                                      locals.add(
                                                                          MirLocalDeclaration{
                                                                              outerInitLocalId,
                                                                              MirLocalKind::
                                                                                  UserLocal,
                                                                              initLocal.type,
                                                                              scopeId(1),
                                                                              initLocal.sourceSpan
                                                                                  .clone()});
                                                                      locals.add(
                                                                          MirLocalDeclaration{
                                                                              outerCondTempId,
                                                                              MirLocalKind::
                                                                                  Temporary,
                                                                              outerCond.type,
                                                                              scopeId(1),
                                                                              outerCond.sourceSpan
                                                                                  .clone()});
                                                                      locals.add(
                                                                          MirLocalDeclaration{
                                                                              innerInitLocalId,
                                                                              MirLocalKind::
                                                                                  UserLocal,
                                                                              innerInit.type,
                                                                              scopeId(1),
                                                                              innerInit.sourceSpan
                                                                                  .clone()});
                                                                      locals.add(
                                                                          MirLocalDeclaration{
                                                                              innerCondTempId,
                                                                              MirLocalKind::
                                                                                  Temporary,
                                                                              innerCond.type,
                                                                              scopeId(1),
                                                                              innerCond.sourceSpan
                                                                                  .clone()});
                                                                      locals.add(
                                                                          MirLocalDeclaration{
                                                                              resultLocalId,
                                                                              MirLocalKind::
                                                                                  FunctionResult,
                                                                              declaration
                                                                                  .resultType,
                                                                              scopeId(1),
                                                                              returnStatement
                                                                                  .sourceSpan
                                                                                  .clone()});
                                                                      // bb1
                                                                      // (outer
                                                                      // entry):
                                                                      // StorageLive
                                                                      // all
                                                                      // locals,
                                                                      // Assign
                                                                      // accumulators,
                                                                      // Assign
                                                                      // outer
                                                                      // init,
                                                                      // Assign
                                                                      // outer
                                                                      // cond
                                                                      // temp,
                                                                      // Goto(bb2).
                                                                      zc::Vector<MirStatement>
                                                                          entryStatements;
                                                                      entryStatements.add(
                                                                          MirStatement::storageLive(
                                                                              resultLocalId,
                                                                              returnStatement
                                                                                  .sourceSpan
                                                                                  .clone()));
                                                                      for (size_t k = 0;
                                                                           k < accCount; ++k) {
                                                                        const auto& accLocal =
                                                                            ZC_ASSERT_NONNULL(
                                                                                sourceAccLocals[k]);
                                                                        entryStatements.add(
                                                                            MirStatement::
                                                                                storageLive(
                                                                                    accMirLocalIds
                                                                                        [k],
                                                                                    accLocal
                                                                                        .sourceSpan
                                                                                        .clone()));
                                                                      }
                                                                      entryStatements.add(
                                                                          MirStatement::storageLive(
                                                                              outerInitLocalId,
                                                                              initLocal.sourceSpan
                                                                                  .clone()));
                                                                      entryStatements.add(
                                                                          MirStatement::storageLive(
                                                                              outerCondTempId,
                                                                              outerCond.sourceSpan
                                                                                  .clone()));
                                                                      entryStatements.add(
                                                                          MirStatement::storageLive(
                                                                              innerInitLocalId,
                                                                              innerInit.sourceSpan
                                                                                  .clone()));
                                                                      entryStatements.add(
                                                                          MirStatement::storageLive(
                                                                              innerCondTempId,
                                                                              innerCond.sourceSpan
                                                                                  .clone()));
                                                                      for (size_t k = 0;
                                                                           k < accCount; ++k) {
                                                                        const auto& accLocal =
                                                                            ZC_ASSERT_NONNULL(
                                                                                sourceAccLocals[k]);
                                                                        const auto& accInitValue =
                                                                            ZC_ASSERT_NONNULL(
                                                                                accInitializers[k]);
                                                                        zc::Vector<MirProjection>
                                                                            accInitProjections;
                                                                        entryStatements.add(
                                                                            MirStatement::assign(
                                                                                MirPlace(
                                                                                    accMirLocalIds
                                                                                        [k],
                                                                                    accLocal.type,
                                                                                    zc::mv(
                                                                                        accInitProjections),
                                                                                    accLocal.type),
                                                                                MirRvalue::use(
                                                                                    MirOperand::constant(
                                                                                        accLocal
                                                                                            .type,
                                                                                        accInitValue
                                                                                            .value
                                                                                            .clone())),
                                                                                MirInitializationKind::
                                                                                    Initialize,
                                                                                accInitValue
                                                                                    .sourceSpan
                                                                                    .clone()));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          outerInitProjections;
                                                                      entryStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  outerInitLocalId,
                                                                                  initLocal.type,
                                                                                  zc::mv(
                                                                                      outerInitProjections),
                                                                                  initLocal.type),
                                                                              MirRvalue::use(
                                                                                  MirOperand::constant(
                                                                                      initLocal
                                                                                          .type,
                                                                                      outerInitValue
                                                                                          .value
                                                                                          .clone())),
                                                                              MirInitializationKind::
                                                                                  Initialize,
                                                                              outerInitValue
                                                                                  .sourceSpan
                                                                                  .clone()));
                                                                      // Outer
                                                                      // condition
                                                                      // comparison
                                                                      // in the
                                                                      // entry
                                                                      // block.
                                                                      zc::Vector<MirProjection>
                                                                          outerCondLeftProjections;
                                                                      auto outerCondLeftOperand =
                                                                          placeUse(
                                                                              proofs, copy,
                                                                              MirPlace(
                                                                                  outerInitLocalId,
                                                                                  initLocal.type,
                                                                                  zc::mv(
                                                                                      outerCondLeftProjections),
                                                                                  initLocal.type));
                                                                      if (outerCondLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          outerTempProjections;
                                                                      entryStatements.add(MirStatement::assign(
                                                                          MirPlace(
                                                                              outerCondTempId,
                                                                              outerCond.type,
                                                                              zc::mv(
                                                                                  outerTempProjections),
                                                                              outerCond.type),
                                                                          MirRvalue::comparison(
                                                                              ZC_ASSERT_NONNULL(
                                                                                  outerCmpOp),
                                                                              zc::mv(ZC_ASSERT_NONNULL(
                                                                                  outerCondLeftOperand)),
                                                                              MirOperand::constant(
                                                                                  outerCondRhs.type,
                                                                                  outerCondRhs.value
                                                                                      .clone()),
                                                                              outerCond.type),
                                                                          MirInitializationKind::
                                                                              Initialize,
                                                                          outerCond.sourceSpan
                                                                              .clone()));
                                                                      // bb2
                                                                      // (outer
                                                                      // header):
                                                                      // SwitchInt(
                                                                      // copy(outer
                                                                      // cond
                                                                      // temp),
                                                                      // [true
                                                                      // -> bb3],
                                                                      // default
                                                                      // = bb7).
                                                                      zc::Vector<MirProjection>
                                                                          outerDiscProjections;
                                                                      auto outerDiscriminant = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              outerCondTempId,
                                                                              outerCond.type,
                                                                              zc::mv(
                                                                                  outerDiscProjections),
                                                                              outerCond.type));
                                                                      if (outerDiscriminant ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirSwitchIntArm>
                                                                          outerArms;
                                                                      outerArms.add(MirSwitchIntArm{
                                                                          checker::checked::
                                                                              CanonicalConstValue::
                                                                                  boolean(true),
                                                                          blockId(3)});
                                                                      // bb3
                                                                      // (inner
                                                                      // entry):
                                                                      // Assign(inner
                                                                      // init,
                                                                      // Overwrite),
                                                                      // Assign(inner
                                                                      // cond
                                                                      // temp,
                                                                      // Overwrite),
                                                                      // Goto(bb4).
                                                                      // Overwrite
                                                                      // (not
                                                                      // Initialize)
                                                                      // because
                                                                      // the
                                                                      // inner
                                                                      // entry
                                                                      // is
                                                                      // inside
                                                                      // the
                                                                      // outer
                                                                      // loop
                                                                      // and
                                                                      // re-executed
                                                                      // on
                                                                      // subsequent
                                                                      // outer
                                                                      // iterations.
                                                                      zc::Vector<MirStatement>
                                                                          innerEntryStatements;
                                                                      zc::Vector<MirProjection>
                                                                          innerInitProjections;
                                                                      innerEntryStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  innerInitLocalId,
                                                                                  innerInit.type,
                                                                                  zc::mv(
                                                                                      innerInitProjections),
                                                                                  innerInit.type),
                                                                              MirRvalue::use(
                                                                                  MirOperand::constant(
                                                                                      innerInit
                                                                                          .type,
                                                                                      innerInitValue
                                                                                          .value
                                                                                          .clone())),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              innerInitValue
                                                                                  .sourceSpan
                                                                                  .clone()));
                                                                      zc::Vector<MirProjection>
                                                                          innerCondLeftProjections;
                                                                      auto innerCondLeftOperand =
                                                                          placeUse(
                                                                              proofs, copy,
                                                                              MirPlace(
                                                                                  innerInitLocalId,
                                                                                  innerInit.type,
                                                                                  zc::mv(
                                                                                      innerCondLeftProjections),
                                                                                  innerInit.type));
                                                                      if (innerCondLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          innerTempProjections;
                                                                      innerEntryStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  innerCondTempId,
                                                                                  innerCond.type,
                                                                                  zc::mv(
                                                                                      innerTempProjections),
                                                                                  innerCond.type),
                                                                              MirRvalue::comparison(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      innerCmpOp),
                                                                                  zc::mv(ZC_ASSERT_NONNULL(
                                                                                      innerCondLeftOperand)),
                                                                                  MirOperand::constant(
                                                                                      innerCondRhs
                                                                                          .type,
                                                                                      innerCondRhs
                                                                                          .value
                                                                                          .clone()),
                                                                                  innerCond.type),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              innerCond.sourceSpan
                                                                                  .clone()));
                                                                      // bb4
                                                                      // (inner
                                                                      // header):
                                                                      // SwitchInt(
                                                                      // copy(inner
                                                                      // cond
                                                                      // temp),
                                                                      // [true
                                                                      // -> bb5],
                                                                      // default
                                                                      // = bb6).
                                                                      zc::Vector<MirProjection>
                                                                          innerDiscProjections;
                                                                      auto innerDiscriminant = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              innerCondTempId,
                                                                              innerCond.type,
                                                                              zc::mv(
                                                                                  innerDiscProjections),
                                                                              innerCond.type));
                                                                      if (innerDiscriminant ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirSwitchIntArm>
                                                                          innerArms;
                                                                      innerArms.add(MirSwitchIntArm{
                                                                          checker::checked::
                                                                              CanonicalConstValue::
                                                                                  boolean(true),
                                                                          blockId(5)});
                                                                      // bb5
                                                                      // (inner
                                                                      // body):
                                                                      // accumulator
                                                                      // writes,
                                                                      // inner
                                                                      // update,
                                                                      // re-compare,
                                                                      // Goto(bb4).
                                                                      zc::Vector<MirStatement>
                                                                          innerBodyStatements;
                                                                      for (size_t k = 0;
                                                                           k < accCount; ++k) {
                                                                        const auto& accLocal =
                                                                            ZC_ASSERT_NONNULL(
                                                                                sourceAccLocals[k]);
                                                                        const auto& bw =
                                                                            ZC_ASSERT_NONNULL(
                                                                                bodyWrites[k]);
                                                                        const auto& bwValue =
                                                                            ZC_ASSERT_NONNULL(
                                                                                bodyWriteBinaries
                                                                                    [k]);
                                                                        zc::Vector<MirProjection>
                                                                            bwLeftProjections;
                                                                        auto bwLeftOperand = placeUse(
                                                                            proofs, copy,
                                                                            MirPlace(
                                                                                accMirLocalIds[k],
                                                                                accLocal.type,
                                                                                zc::mv(
                                                                                    bwLeftProjections),
                                                                                accLocal.type));
                                                                        if (bwLeftOperand ==
                                                                            zc::none) {
                                                                          return rejectMir<
                                                                              BuiltMirCandidate>(
                                                                              ir::IrFailurePhase::
                                                                                  MirConstruction,
                                                                              ir::IrFailureKind::
                                                                                  InvalidFact,
                                                                              module,
                                                                              declaration
                                                                                  .definition,
                                                                              identities,
                                                                              static_cast<uint32_t>(
                                                                                  pending.size() +
                                                                                  1));
                                                                        }
                                                                        zc::Maybe<MirOperand>
                                                                            bwRightOperand;
                                                                        if (bodyWriteRhsIsLiteral
                                                                                [k]) {
                                                                          const auto& bwRhsLit =
                                                                              ZC_ASSERT_NONNULL(
                                                                                  bodyWriteRhsLits
                                                                                      [k]);
                                                                          bwRightOperand =
                                                                              MirOperand::constant(
                                                                                  bwRhsLit.type,
                                                                                  bwRhsLit.value
                                                                                      .clone());
                                                                        } else {
                                                                          zc::Vector<MirProjection>
                                                                              bwRightProjections;
                                                                          auto bwRightPlace = placeUse(
                                                                              proofs, copy,
                                                                              MirPlace(
                                                                                  innerInitLocalId,
                                                                                  innerInit.type,
                                                                                  zc::mv(
                                                                                      bwRightProjections),
                                                                                  innerInit.type));
                                                                          if (bwRightPlace ==
                                                                              zc::none) {
                                                                            return rejectMir<
                                                                                BuiltMirCandidate>(
                                                                                ir::IrFailurePhase::
                                                                                    MirConstruction,
                                                                                ir::IrFailureKind::
                                                                                    InvalidFact,
                                                                                module,
                                                                                declaration
                                                                                    .definition,
                                                                                identities,
                                                                                static_cast<
                                                                                    uint32_t>(
                                                                                    pending.size() +
                                                                                    1));
                                                                          }
                                                                          bwRightOperand = zc::mv(
                                                                              ZC_ASSERT_NONNULL(
                                                                                  bwRightPlace));
                                                                        }
                                                                        zc::Vector<MirProjection>
                                                                            bwOverwriteProjections;
                                                                        innerBodyStatements.add(MirStatement::assign(
                                                                            MirPlace(
                                                                                accMirLocalIds[k],
                                                                                accLocal.type,
                                                                                zc::mv(
                                                                                    bwOverwriteProjections),
                                                                                accLocal.type),
                                                                            MirRvalue::arithmetic(
                                                                                bodyWriteOps[k],
                                                                                zc::mv(ZC_ASSERT_NONNULL(
                                                                                    bwLeftOperand)),
                                                                                zc::mv(ZC_ASSERT_NONNULL(
                                                                                    bwRightOperand)),
                                                                                bwValue.type),
                                                                            MirInitializationKind::
                                                                                Overwrite,
                                                                            bw.sourceSpan.clone()));
                                                                      }
                                                                      // Inner
                                                                      // update
                                                                      // write.
                                                                      zc::Vector<MirProjection>
                                                                          innerUwLeftProjections;
                                                                      auto innerUwLeftOperand = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              innerInitLocalId,
                                                                              innerInit.type,
                                                                              zc::mv(
                                                                                  innerUwLeftProjections),
                                                                              innerInit.type));
                                                                      if (innerUwLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          innerUwOverwriteProjections;
                                                                      innerBodyStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  innerInitLocalId,
                                                                                  innerInit.type,
                                                                                  zc::mv(
                                                                                      innerUwOverwriteProjections),
                                                                                  innerInit.type),
                                                                              MirRvalue::arithmetic(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      innerWriteOp),
                                                                                  zc::mv(ZC_ASSERT_NONNULL(
                                                                                      innerUwLeftOperand)),
                                                                                  MirOperand::constant(
                                                                                      innerWriteRhs
                                                                                          .type,
                                                                                      innerWriteRhs
                                                                                          .value
                                                                                          .clone()),
                                                                                  innerWriteValue
                                                                                      .type),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              innerWrite.sourceSpan
                                                                                  .clone()));
                                                                      // Recompute
                                                                      // inner
                                                                      // condition.
                                                                      zc::Vector<MirProjection>
                                                                          innerBodyCondLeftProjections;
                                                                      auto innerBodyCondLeftOperand =
                                                                          placeUse(
                                                                              proofs, copy,
                                                                              MirPlace(
                                                                                  innerInitLocalId,
                                                                                  innerInit.type,
                                                                                  zc::mv(
                                                                                      innerBodyCondLeftProjections),
                                                                                  innerInit.type));
                                                                      if (innerBodyCondLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          innerBodyTempProjections;
                                                                      innerBodyStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  innerCondTempId,
                                                                                  innerCond.type,
                                                                                  zc::mv(
                                                                                      innerBodyTempProjections),
                                                                                  innerCond.type),
                                                                              MirRvalue::comparison(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      innerCmpOp),
                                                                                  zc::mv(ZC_ASSERT_NONNULL(
                                                                                      innerBodyCondLeftOperand)),
                                                                                  MirOperand::constant(
                                                                                      innerCondRhs
                                                                                          .type,
                                                                                      innerCondRhs
                                                                                          .value
                                                                                          .clone()),
                                                                                  innerCond.type),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              innerCond.sourceSpan
                                                                                  .clone()));
                                                                      // bb6
                                                                      // (inner
                                                                      // exit
                                                                      // =
                                                                      // outer
                                                                      // continuation):
                                                                      // outer
                                                                      // update,
                                                                      // re-compare,
                                                                      // Goto(bb2).
                                                                      zc::Vector<MirStatement>
                                                                          outerContStatements;
                                                                      zc::Vector<MirProjection>
                                                                          outerUwLeftProjections;
                                                                      auto outerUwLeftOperand = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              outerInitLocalId,
                                                                              initLocal.type,
                                                                              zc::mv(
                                                                                  outerUwLeftProjections),
                                                                              initLocal.type));
                                                                      if (outerUwLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          outerUwOverwriteProjections;
                                                                      outerContStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  outerInitLocalId,
                                                                                  initLocal.type,
                                                                                  zc::mv(
                                                                                      outerUwOverwriteProjections),
                                                                                  initLocal.type),
                                                                              MirRvalue::arithmetic(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      outerWriteOp),
                                                                                  zc::mv(ZC_ASSERT_NONNULL(
                                                                                      outerUwLeftOperand)),
                                                                                  MirOperand::constant(
                                                                                      outerWriteRhs
                                                                                          .type,
                                                                                      outerWriteRhs
                                                                                          .value
                                                                                          .clone()),
                                                                                  outerWriteValue
                                                                                      .type),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              outerWrite.sourceSpan
                                                                                  .clone()));
                                                                      // Recompute
                                                                      // outer
                                                                      // condition.
                                                                      zc::Vector<MirProjection>
                                                                          outerBodyCondLeftProjections;
                                                                      auto outerBodyCondLeftOperand =
                                                                          placeUse(
                                                                              proofs, copy,
                                                                              MirPlace(
                                                                                  outerInitLocalId,
                                                                                  initLocal.type,
                                                                                  zc::mv(
                                                                                      outerBodyCondLeftProjections),
                                                                                  initLocal.type));
                                                                      if (outerBodyCondLeftOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          outerBodyTempProjections;
                                                                      outerContStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  outerCondTempId,
                                                                                  outerCond.type,
                                                                                  zc::mv(
                                                                                      outerBodyTempProjections),
                                                                                  outerCond.type),
                                                                              MirRvalue::comparison(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      outerCmpOp),
                                                                                  zc::mv(ZC_ASSERT_NONNULL(
                                                                                      outerBodyCondLeftOperand)),
                                                                                  MirOperand::constant(
                                                                                      outerCondRhs
                                                                                          .type,
                                                                                      outerCondRhs
                                                                                          .value
                                                                                          .clone()),
                                                                                  outerCond.type),
                                                                              MirInitializationKind::
                                                                                  Overwrite,
                                                                              outerCond.sourceSpan
                                                                                  .clone()));
                                                                      // bb7
                                                                      // (outer
                                                                      // exit):
                                                                      // Assign(result
                                                                      // =
                                                                      // copy(acc_0),
                                                                      // Initialize),
                                                                      // Return(placeUse(result)).
                                                                      const auto& firstAccLocal =
                                                                          ZC_ASSERT_NONNULL(
                                                                              sourceAccLocals[0]);
                                                                      zc::Vector<MirProjection>
                                                                          exitCopyProjections;
                                                                      auto exitCopyOperand = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              accMirLocalIds[0],
                                                                              firstAccLocal.type,
                                                                              zc::mv(
                                                                                  exitCopyProjections),
                                                                              firstAccLocal.type));
                                                                      if (exitCopyOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirProjection>
                                                                          returnProjections;
                                                                      auto returnOperand = placeUse(
                                                                          proofs, copy,
                                                                          MirPlace(
                                                                              resultLocalId,
                                                                              declaration
                                                                                  .resultType,
                                                                              zc::mv(
                                                                                  returnProjections),
                                                                              declaration
                                                                                  .resultType));
                                                                      if (returnOperand ==
                                                                          zc::none) {
                                                                        return rejectMir<
                                                                            BuiltMirCandidate>(
                                                                            ir::IrFailurePhase::
                                                                                MirConstruction,
                                                                            ir::IrFailureKind::
                                                                                InvalidFact,
                                                                            module,
                                                                            declaration.definition,
                                                                            identities,
                                                                            static_cast<uint32_t>(
                                                                                pending.size() +
                                                                                1));
                                                                      }
                                                                      zc::Vector<MirStatement>
                                                                          exitStatements;
                                                                      zc::Vector<MirProjection>
                                                                          exitProjections;
                                                                      exitStatements.add(
                                                                          MirStatement::assign(
                                                                              MirPlace(
                                                                                  resultLocalId,
                                                                                  declaration
                                                                                      .resultType,
                                                                                  zc::mv(
                                                                                      exitProjections),
                                                                                  declaration
                                                                                      .resultType),
                                                                              MirRvalue::use(zc::mv(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      exitCopyOperand))),
                                                                              MirInitializationKind::
                                                                                  Initialize,
                                                                              returnRef.sourceSpan
                                                                                  .clone()));
                                                                      zc::Vector<MirBasicBlock>
                                                                          blocks;
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(1), scopeId(1),
                                                                          zc::mv(entryStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              blockId(2),
                                                                              loopValue.sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(2), scopeId(1),
                                                                          zc::Vector<
                                                                              MirStatement>{},
                                                                          MirTerminator::switchInt(
                                                                              zc::mv(ZC_ASSERT_NONNULL(
                                                                                  outerDiscriminant)),
                                                                              zc::mv(outerArms),
                                                                              blockId(7),
                                                                              loopValue.sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(3), scopeId(1),
                                                                          zc::mv(
                                                                              innerEntryStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              blockId(4),
                                                                              innerLoopValue
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(4), scopeId(1),
                                                                          zc::Vector<
                                                                              MirStatement>{},
                                                                          MirTerminator::switchInt(
                                                                              zc::mv(ZC_ASSERT_NONNULL(
                                                                                  innerDiscriminant)),
                                                                              zc::mv(innerArms),
                                                                              blockId(6),
                                                                              innerLoopValue
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(5), scopeId(1),
                                                                          zc::mv(
                                                                              innerBodyStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              blockId(4),
                                                                              innerLoopValue
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(6), scopeId(1),
                                                                          zc::mv(
                                                                              outerContStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              blockId(2),
                                                                              loopValue.sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(7), scopeId(1),
                                                                          zc::mv(exitStatements),
                                                                          MirTerminator::returnValue(
                                                                              zc::mv(
                                                                                  ZC_ASSERT_NONNULL(
                                                                                      returnOperand)),
                                                                              returnStatement
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      MirFunction mirFunction{
                                                                          declaration.definition,
                                                                          MirFunctionKind::Function,
                                                                          identity::DefinitionKind::
                                                                              Function,
                                                                          declaration.resultType,
                                                                          declaration.sourceSpan
                                                                              .clone(),
                                                                          zc::mv(scopes),
                                                                          zc::mv(locals),
                                                                          zc::mv(blocks)};
                                                                      zc::Array<uint8_t> ownerKey;
                                                                      ZC_IF_SOME(key, definition) {
                                                                        ownerKey =
                                                                            key.key().encode();
                                                                      }
                                                                      pending.add(
                                                                          PendingMirFunction{
                                                                              zc::mv(mirFunction),
                                                                              zc::mv(ownerKey)});
                                                                      continue;
                                                                    }
                                                                  }
                                                                }
                                                              }
                                                            }
                                                          }
                                                        }
                                                      }
                                                    }
                                                  }
                                                }
                                              }
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      // For-loop accumulator composite body: N leading `mut` accumulator
      // locals, a `for (let i = <lit>; i < <lit>; i = i <bin> <lit>) { acc_k =
      // acc_k <bin> i|<lit>; ... }` loop, and a trailing `return acc_0;`.
      // Lowers to a reducible four-block CFG. Parameters occupy localId(1..P);
      // accumulator k is localId(P+1+k); the init local i is
      // localId(P+1+N); the comparison temp is localId(P+2+N); the result
      // local is localId(P+3+N). The entry block declares all locals,
      // initializes the accumulators and i, evaluates the first comparison,
      // and jumps to the header; the header switches on the temp into the
      // body (true) or the exit (default); the body carries the accumulator
      // writes, the update write, and the re-comparison, then jumps back to
      // the header (the reducible back-edge); the exit copies the first
      // accumulator into the result and returns it.
      if (block.statements.size() >= 4) {
        const size_t accCount = block.statements.size() - 3;
        auto sourceInitLocal = localFor(hirModule, block.statements[accCount]);
        auto loop = loopFor(hirModule, block.statements[accCount + 1]);
        auto sourceReturn = returnFor(hirModule, block.statements[accCount + 2]);
        auto definition = identities.definition(declaration.definition);
        // Resolve the N accumulator locals.
        zc::Vector<zc::Maybe<const hir::HirLocalBinding&>> sourceAccLocals;
        bool accLocalsOk = true;
        for (size_t k = 0; k < accCount; ++k) {
          auto accLocal = localFor(hirModule, block.statements[k]);
          if (accLocal == zc::none) {
            accLocalsOk = false;
            break;
          }
          sourceAccLocals.add(zc::mv(accLocal));
        }
        if (accLocalsOk) {
          ZC_IF_SOME(initLocal, sourceInitLocal) {
            ZC_IF_SOME(loopValue, loop) {
              ZC_IF_SOME(returnStatement, sourceReturn) {
                const uint32_t parameterCount =
                    static_cast<uint32_t>(declaration.parameters.size());
                const bool hasBreakCondition = loopValue.breakCondition.isValid();
                const auto initLocalId =
                    localId(parameterCount + static_cast<uint32_t>(accCount) + 1);
                const auto conditionTempId =
                    localId(parameterCount + static_cast<uint32_t>(accCount) + 2);
                const auto breakTempId =
                    localId(parameterCount + static_cast<uint32_t>(accCount) + 3);
                const auto resultLocalId = localId(
                    parameterCount + static_cast<uint32_t>(accCount) + (hasBreakCondition ? 4 : 3));
                // Resolve accumulator initializers and validate shape.
                zc::Vector<zc::Maybe<const hir::HirScalarLiteralExpression&>> accInitializers;
                zc::Vector<MirLocalId> accMirLocalIds;
                bool shapeOk = definition != zc::none && loopValue.body.size() == accCount + 1;
                for (size_t k = 0; k < accCount && shapeOk; ++k) {
                  const auto& accLocal = ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                  hir::HirNodeId accInitializerNode;
                  ZC_IF_SOME(value, accLocal.initializer) { accInitializerNode = value; }
                  auto accInitializer = expressionFor(hirModule, accInitializerNode);
                  if (accInitializer == zc::none || accLocal.initializer == zc::none ||
                      accLocal.local.ordinal() != static_cast<uint32_t>(k + 1)) {
                    shapeOk = false;
                    break;
                  }
                  accInitializers.add(zc::mv(accInitializer));
                  accMirLocalIds.add(localId(parameterCount + static_cast<uint32_t>(k) + 1));
                }
                hir::HirNodeId initInitializerNode;
                ZC_IF_SOME(value, initLocal.initializer) { initInitializerNode = value; }
                auto initInitializer = expressionFor(hirModule, initInitializerNode);
                auto conditionBinary = primitiveBinaryFor(hirModule, loopValue.condition);
                auto returnReference = localReferenceFor(hirModule, returnStatement.value);
                // Resolve the if-guarded break condition when present.
                zc::Maybe<const hir::HirPrimitiveBinaryExpression&> breakConditionBinary;
                zc::Maybe<const hir::HirLocalReferenceExpression&> breakConditionLeft;
                zc::Maybe<const hir::HirScalarLiteralExpression&> breakConditionRight;
                zc::Maybe<MirComparisonOperator> breakComparisonOperator;
                if (hasBreakCondition) {
                  breakConditionBinary = primitiveBinaryFor(hirModule, loopValue.breakCondition);
                  ZC_IF_SOME(breakBinary, breakConditionBinary) {
                    breakConditionLeft = localReferenceFor(hirModule, breakBinary.left);
                    breakConditionRight = expressionFor(hirModule, breakBinary.right);
                    breakComparisonOperator = mirComparisonOperatorFor(breakBinary.operation);
                  }
                }
                const bool breakConditionOk =
                    !hasBreakCondition ||
                    (breakConditionBinary != zc::none && breakConditionLeft != zc::none &&
                     breakConditionRight != zc::none && breakComparisonOperator != zc::none);
                if (shapeOk && initInitializer != zc::none && conditionBinary != zc::none &&
                    returnReference != zc::none && breakConditionOk) {
                  ZC_IF_SOME(initValue, initInitializer) {
                    ZC_IF_SOME(condition, conditionBinary) {
                      ZC_IF_SOME(returnRef, returnReference) {
                        const auto comparisonOperator =
                            mirComparisonOperatorFor(condition.operation);
                        const auto& firstAccLocal = ZC_ASSERT_NONNULL(sourceAccLocals[0]);
                        if (initLocal.initializer == zc::none ||
                            initLocal.local.ordinal() != static_cast<uint32_t>(accCount + 1) ||
                            initValue.type != initLocal.type || comparisonOperator == zc::none ||
                            condition.operandType != initLocal.type ||
                            returnRef.local != firstAccLocal.local ||
                            returnRef.type != firstAccLocal.type ||
                            firstAccLocal.type != declaration.resultType) {
                          // Not an accumulator shape; fall through.
                        } else {
                          // Resolve the condition operands.
                          auto conditionLeft = localReferenceFor(hirModule, condition.left);
                          auto conditionRight = expressionFor(hirModule, condition.right);
                          // Resolve the N body writes and the update write.
                          zc::Vector<zc::Maybe<const hir::HirLocalWriteStatement&>> bodyWrites;
                          zc::Vector<zc::Maybe<const hir::HirPrimitiveBinaryExpression&>>
                              bodyWriteBinaries;
                          zc::Vector<zc::Maybe<const hir::HirLocalReferenceExpression&>>
                              bodyWriteLhss;
                          zc::Vector<zc::Maybe<const hir::HirLocalReferenceExpression&>>
                              bodyWriteRhss;
                          zc::Vector<zc::Maybe<const hir::HirScalarLiteralExpression&>>
                              bodyWriteRhsLits;
                          zc::Vector<MirArithmeticOperator> bodyWriteOperators;
                          zc::Vector<bool> bodyWriteRhsIsLiteral;
                          bool bodyWritesOk = true;
                          for (size_t k = 0; k < accCount; ++k) {
                            auto bw = localWriteFor(hirModule, loopValue.body[k]);
                            if (bw == zc::none) {
                              bodyWritesOk = false;
                              break;
                            }
                            const auto& bwRef = ZC_ASSERT_NONNULL(bw);
                            auto bwBin = primitiveBinaryFor(hirModule, bwRef.value);
                            if (bwBin == zc::none) {
                              bodyWritesOk = false;
                              break;
                            }
                            const auto& bwBinRef = ZC_ASSERT_NONNULL(bwBin);
                            auto bwLhs = localReferenceFor(hirModule, bwBinRef.left);
                            if (bwLhs == zc::none) {
                              bodyWritesOk = false;
                              break;
                            }
                            // The right operand is either a local reference to
                            // the init local or a scalar literal.
                            auto bwRhsRef = localReferenceFor(hirModule, bwBinRef.right);
                            auto bwRhsLit = expressionFor(hirModule, bwBinRef.right);
                            const bool rhsIsLit = bwRhsRef == zc::none && bwRhsLit != zc::none;
                            if (bwRhsRef == zc::none && bwRhsLit == zc::none) {
                              bodyWritesOk = false;
                              break;
                            }
                            auto bwOp = mirArithmeticOperatorFor(bwBinRef.operation);
                            if (bwOp == zc::none) {
                              bodyWritesOk = false;
                              break;
                            }
                            bodyWrites.add(zc::mv(bw));
                            bodyWriteBinaries.add(zc::mv(bwBin));
                            bodyWriteLhss.add(zc::mv(bwLhs));
                            bodyWriteRhss.add(zc::mv(bwRhsRef));
                            bodyWriteRhsLits.add(zc::mv(bwRhsLit));
                            bodyWriteOperators.add(ZC_ASSERT_NONNULL(bwOp));
                            bodyWriteRhsIsLiteral.add(rhsIsLit);
                          }
                          auto updateWrite = localWriteFor(hirModule, loopValue.body[accCount]);
                          if (bodyWritesOk && updateWrite != zc::none) {
                            ZC_IF_SOME(condLeft, conditionLeft) {
                              ZC_IF_SOME(condRight, conditionRight) {
                                ZC_IF_SOME(uw, updateWrite) {
                                  auto uwBinary = primitiveBinaryFor(hirModule, uw.value);
                                  ZC_IF_SOME(uwValue, uwBinary) {
                                    const auto uwOperator =
                                        mirArithmeticOperatorFor(uwValue.operation);
                                    // Validate body writes.
                                    bool bodyWritesValid =
                                        condLeft.local == initLocal.local &&
                                        condLeft.type == initLocal.type &&
                                        condRight.type == initLocal.type &&
                                        uw.local == initLocal.local && uw.field == zc::none &&
                                        uw.type == initLocal.type &&
                                        uw.kind == hir::HirLocalWriteKind::Overwrite &&
                                        uwOperator != zc::none &&
                                        uwValue.operandType == initLocal.type &&
                                        uwValue.type == initLocal.type;
                                    for (size_t k = 0; k < accCount && bodyWritesValid; ++k) {
                                      const auto& bw = ZC_ASSERT_NONNULL(bodyWrites[k]);
                                      const auto& bwValue = ZC_ASSERT_NONNULL(bodyWriteBinaries[k]);
                                      const auto& bwLhs = ZC_ASSERT_NONNULL(bodyWriteLhss[k]);
                                      const auto& accLocal = ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                      if (bw.local != accLocal.local || bw.field != zc::none ||
                                          bw.type != accLocal.type ||
                                          bw.kind != hir::HirLocalWriteKind::Overwrite ||
                                          bwValue.operandType != accLocal.type ||
                                          bwValue.type != accLocal.type ||
                                          bwLhs.local != accLocal.local ||
                                          bwLhs.type != accLocal.type) {
                                        bodyWritesValid = false;
                                        break;
                                      }
                                      if (bodyWriteRhsIsLiteral[k]) {
                                        const auto& bwRhsLit =
                                            ZC_ASSERT_NONNULL(bodyWriteRhsLits[k]);
                                        if (bwRhsLit.type != accLocal.type) {
                                          bodyWritesValid = false;
                                          break;
                                        }
                                      } else {
                                        const auto& bwRhs = ZC_ASSERT_NONNULL(bodyWriteRhss[k]);
                                        if (bwRhs.local != initLocal.local ||
                                            bwRhs.type != initLocal.type) {
                                          bodyWritesValid = false;
                                          break;
                                        }
                                      }
                                    }
                                    auto uwLeft = localReferenceFor(hirModule, uwValue.left);
                                    auto uwRight = expressionFor(hirModule, uwValue.right);
                                    if (bodyWritesValid && uwLeft != zc::none &&
                                        uwRight != zc::none) {
                                      ZC_IF_SOME(uwLhs, uwLeft) {
                                        ZC_IF_SOME(uwRhs, uwRight) {
                                          const bool breakConditionValid =
                                              !hasBreakCondition ||
                                              (ZC_ASSERT_NONNULL(breakConditionLeft).local ==
                                                   initLocal.local &&
                                               ZC_ASSERT_NONNULL(breakConditionLeft).type ==
                                                   initLocal.type &&
                                               ZC_ASSERT_NONNULL(breakConditionRight).type ==
                                                   initLocal.type &&
                                               ZC_ASSERT_NONNULL(breakConditionBinary)
                                                       .operandType == initLocal.type);
                                          if (uwLhs.local != initLocal.local ||
                                              uwLhs.type != initLocal.type ||
                                              uwRhs.type != initLocal.type ||
                                              !breakConditionValid) {
                                            // Not an accumulator shape;
                                            // fall through.
                                          } else {
                                            // Build the reducible four-block
                                            // CFG.
                                            zc::Vector<MirSourceScope> scopes;
                                            zc::Maybe<MirSourceScopeId> noParent;
                                            scopes.add(
                                                MirSourceScope{scopeId(1), zc::mv(noParent),
                                                               declaration.sourceSpan.clone()});
                                            zc::Vector<MirLocalDeclaration> locals;
                                            for (uint32_t p = 0; p < parameterCount; ++p) {
                                              locals.add(MirLocalDeclaration{
                                                  localId(p + 1), MirLocalKind::Parameter,
                                                  declaration.parameters[p].type, scopeId(1),
                                                  declaration.parameters[p].sourceSpan.clone()});
                                            }
                                            for (size_t k = 0; k < accCount; ++k) {
                                              const auto& accLocal =
                                                  ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                              locals.add(MirLocalDeclaration{
                                                  accMirLocalIds[k], MirLocalKind::UserLocal,
                                                  accLocal.type, scopeId(1),
                                                  accLocal.sourceSpan.clone()});
                                            }
                                            locals.add(MirLocalDeclaration{
                                                initLocalId, MirLocalKind::UserLocal,
                                                initLocal.type, scopeId(1),
                                                initLocal.sourceSpan.clone()});
                                            locals.add(MirLocalDeclaration{
                                                conditionTempId, MirLocalKind::Temporary,
                                                condition.type, scopeId(1),
                                                condition.sourceSpan.clone()});
                                            if (hasBreakCondition) {
                                              const auto& breakBinary =
                                                  ZC_ASSERT_NONNULL(breakConditionBinary);
                                              locals.add(MirLocalDeclaration{
                                                  breakTempId, MirLocalKind::Temporary,
                                                  breakBinary.type, scopeId(1),
                                                  breakBinary.sourceSpan.clone()});
                                            }
                                            locals.add(MirLocalDeclaration{
                                                resultLocalId, MirLocalKind::FunctionResult,
                                                declaration.resultType, scopeId(1),
                                                returnStatement.sourceSpan.clone()});
                                            // Entry: StorageLive(result),
                                            // StorageLive(acc_k)...,
                                            // StorageLive(i), StorageLive(temp),
                                            // Assign(acc_k = accInit_k,
                                            // Initialize)...,
                                            // Assign(i = init, Initialize),
                                            // Assign(temp = Comparison(op,
                                            // copy(i), <lit>), Initialize),
                                            // Goto(bb2).
                                            zc::Vector<MirStatement> entryStatements;
                                            entryStatements.add(MirStatement::storageLive(
                                                resultLocalId, returnStatement.sourceSpan.clone()));
                                            for (size_t k = 0; k < accCount; ++k) {
                                              const auto& accLocal =
                                                  ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                              entryStatements.add(MirStatement::storageLive(
                                                  accMirLocalIds[k], accLocal.sourceSpan.clone()));
                                            }
                                            entryStatements.add(MirStatement::storageLive(
                                                initLocalId, initLocal.sourceSpan.clone()));
                                            entryStatements.add(MirStatement::storageLive(
                                                conditionTempId, condition.sourceSpan.clone()));
                                            if (hasBreakCondition) {
                                              const auto& breakBinary =
                                                  ZC_ASSERT_NONNULL(breakConditionBinary);
                                              entryStatements.add(MirStatement::storageLive(
                                                  breakTempId, breakBinary.sourceSpan.clone()));
                                            }
                                            for (size_t k = 0; k < accCount; ++k) {
                                              const auto& accLocal =
                                                  ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                              const auto& accInitValue =
                                                  ZC_ASSERT_NONNULL(accInitializers[k]);
                                              zc::Vector<MirProjection> accInitProjections;
                                              entryStatements.add(MirStatement::assign(
                                                  MirPlace(accMirLocalIds[k], accLocal.type,
                                                           zc::mv(accInitProjections),
                                                           accLocal.type),
                                                  MirRvalue::use(MirOperand::constant(
                                                      accLocal.type, accInitValue.value.clone())),
                                                  MirInitializationKind::Initialize,
                                                  accInitValue.sourceSpan.clone()));
                                            }
                                            zc::Vector<MirProjection> initProjections;
                                            entryStatements.add(MirStatement::assign(
                                                MirPlace(initLocalId, initLocal.type,
                                                         zc::mv(initProjections), initLocal.type),
                                                MirRvalue::use(MirOperand::constant(
                                                    initLocal.type, initValue.value.clone())),
                                                MirInitializationKind::Initialize,
                                                initValue.sourceSpan.clone()));
                                            // Condition comparison in the
                                            // entry block.
                                            zc::Vector<MirProjection> entryCondLeftProjections;
                                            auto entryCondLeftOperand =
                                                placeUse(proofs, copy,
                                                         MirPlace(initLocalId, initLocal.type,
                                                                  zc::mv(entryCondLeftProjections),
                                                                  initLocal.type));
                                            if (entryCondLeftOperand == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirProjection> entryTempProjections;
                                            entryStatements.add(MirStatement::assign(
                                                MirPlace(conditionTempId, condition.type,
                                                         zc::mv(entryTempProjections),
                                                         condition.type),
                                                MirRvalue::comparison(
                                                    ZC_ASSERT_NONNULL(comparisonOperator),
                                                    zc::mv(ZC_ASSERT_NONNULL(entryCondLeftOperand)),
                                                    MirOperand::constant(condRight.type,
                                                                         condRight.value.clone()),
                                                    condition.type),
                                                MirInitializationKind::Initialize,
                                                condition.sourceSpan.clone()));
                                            // Header: SwitchInt(copy(temp),
                                            // [true -> bb3], default = bb4).
                                            zc::Vector<MirProjection> discriminantProjections;
                                            auto discriminant =
                                                placeUse(proofs, copy,
                                                         MirPlace(conditionTempId, condition.type,
                                                                  zc::mv(discriminantProjections),
                                                                  condition.type));
                                            if (discriminant == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirSwitchIntArm> arms;
                                            arms.add(MirSwitchIntArm{
                                                checker::checked::CanonicalConstValue::boolean(
                                                    true),
                                                blockId(3)});
                                            // Body: Assign(acc_k =
                                            // Arithmetic(op, copy(acc_k),
                                            // copy(i)|<lit>), Overwrite)...,
                                            // Assign(i = Arithmetic(op, copy(i),
                                            // <lit>), Overwrite),
                                            // Assign(temp = Comparison(op,
                                            // copy(i), <lit>), Overwrite),
                                            // Goto(bb2).
                                            zc::Vector<MirStatement> bodyStatements;
                                            for (size_t k = 0; k < accCount; ++k) {
                                              const auto& accLocal =
                                                  ZC_ASSERT_NONNULL(sourceAccLocals[k]);
                                              const auto& bw = ZC_ASSERT_NONNULL(bodyWrites[k]);
                                              const auto& bwValue =
                                                  ZC_ASSERT_NONNULL(bodyWriteBinaries[k]);
                                              zc::Vector<MirProjection> bwLeftProjections;
                                              auto bwLeftOperand = placeUse(
                                                  proofs, copy,
                                                  MirPlace(accMirLocalIds[k], accLocal.type,
                                                           zc::mv(bwLeftProjections),
                                                           accLocal.type));
                                              if (bwLeftOperand == zc::none) {
                                                return rejectMir<BuiltMirCandidate>(
                                                    ir::IrFailurePhase::MirConstruction,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    declaration.definition, identities,
                                                    static_cast<uint32_t>(pending.size() + 1));
                                              }
                                              zc::Maybe<MirOperand> bwRightOperand;
                                              if (bodyWriteRhsIsLiteral[k]) {
                                                const auto& bwRhsLit =
                                                    ZC_ASSERT_NONNULL(bodyWriteRhsLits[k]);
                                                bwRightOperand = MirOperand::constant(
                                                    bwRhsLit.type, bwRhsLit.value.clone());
                                              } else {
                                                zc::Vector<MirProjection> bwRightProjections;
                                                auto bwRightPlace =
                                                    placeUse(proofs, copy,
                                                             MirPlace(initLocalId, initLocal.type,
                                                                      zc::mv(bwRightProjections),
                                                                      initLocal.type));
                                                if (bwRightPlace == zc::none) {
                                                  return rejectMir<BuiltMirCandidate>(
                                                      ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                                                }
                                                bwRightOperand =
                                                    zc::mv(ZC_ASSERT_NONNULL(bwRightPlace));
                                              }
                                              zc::Vector<MirProjection> bwOverwriteProjections;
                                              bodyStatements.add(MirStatement::assign(
                                                  MirPlace(accMirLocalIds[k], accLocal.type,
                                                           zc::mv(bwOverwriteProjections),
                                                           accLocal.type),
                                                  MirRvalue::arithmetic(
                                                      bodyWriteOperators[k],
                                                      zc::mv(ZC_ASSERT_NONNULL(bwLeftOperand)),
                                                      zc::mv(ZC_ASSERT_NONNULL(bwRightOperand)),
                                                      bwValue.type),
                                                  MirInitializationKind::Overwrite,
                                                  bw.sourceSpan.clone()));
                                            }
                                            // Update write.
                                            zc::Vector<MirProjection> uwLeftProjections;
                                            auto uwLeftOperand =
                                                placeUse(proofs, copy,
                                                         MirPlace(initLocalId, initLocal.type,
                                                                  zc::mv(uwLeftProjections),
                                                                  initLocal.type));
                                            if (uwLeftOperand == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirProjection> uwOverwriteProjections;
                                            bodyStatements.add(MirStatement::assign(
                                                MirPlace(initLocalId, initLocal.type,
                                                         zc::mv(uwOverwriteProjections),
                                                         initLocal.type),
                                                MirRvalue::arithmetic(
                                                    ZC_ASSERT_NONNULL(uwOperator),
                                                    zc::mv(ZC_ASSERT_NONNULL(uwLeftOperand)),
                                                    MirOperand::constant(uwRhs.type,
                                                                         uwRhs.value.clone()),
                                                    uwValue.type),
                                                MirInitializationKind::Overwrite,
                                                uw.sourceSpan.clone()));
                                            // Recompute the condition at
                                            // the end of the body for the
                                            // next iteration.
                                            zc::Vector<MirProjection> bodyCondLeftProjections;
                                            auto bodyCondLeftOperand =
                                                placeUse(proofs, copy,
                                                         MirPlace(initLocalId, initLocal.type,
                                                                  zc::mv(bodyCondLeftProjections),
                                                                  initLocal.type));
                                            if (bodyCondLeftOperand == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirProjection> bodyTempProjections;
                                            bodyStatements.add(MirStatement::assign(
                                                MirPlace(conditionTempId, condition.type,
                                                         zc::mv(bodyTempProjections),
                                                         condition.type),
                                                MirRvalue::comparison(
                                                    ZC_ASSERT_NONNULL(comparisonOperator),
                                                    zc::mv(ZC_ASSERT_NONNULL(bodyCondLeftOperand)),
                                                    MirOperand::constant(condRight.type,
                                                                         condRight.value.clone()),
                                                    condition.type),
                                                MirInitializationKind::Overwrite,
                                                condition.sourceSpan.clone()));
                                            // Exit: Assign(result =
                                            // copy(acc_0), Initialize),
                                            // Return(placeUse(result)).
                                            const auto& firstAccLocal =
                                                ZC_ASSERT_NONNULL(sourceAccLocals[0]);
                                            zc::Vector<MirProjection> exitCopyProjections;
                                            auto exitCopyOperand = placeUse(
                                                proofs, copy,
                                                MirPlace(accMirLocalIds[0], firstAccLocal.type,
                                                         zc::mv(exitCopyProjections),
                                                         firstAccLocal.type));
                                            if (exitCopyOperand == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirProjection> returnProjections;
                                            auto returnOperand = placeUse(
                                                proofs, copy,
                                                MirPlace(resultLocalId, declaration.resultType,
                                                         zc::mv(returnProjections),
                                                         declaration.resultType));
                                            if (returnOperand == zc::none) {
                                              return rejectMir<BuiltMirCandidate>(
                                                  ir::IrFailurePhase::MirConstruction,
                                                  ir::IrFailureKind::InvalidFact, module,
                                                  declaration.definition, identities,
                                                  static_cast<uint32_t>(pending.size() + 1));
                                            }
                                            zc::Vector<MirStatement> exitStatements;
                                            zc::Vector<MirProjection> exitProjections;
                                            exitStatements.add(MirStatement::assign(
                                                MirPlace(resultLocalId, declaration.resultType,
                                                         zc::mv(exitProjections),
                                                         declaration.resultType),
                                                MirRvalue::use(
                                                    zc::mv(ZC_ASSERT_NONNULL(exitCopyOperand))),
                                                MirInitializationKind::Initialize,
                                                returnRef.sourceSpan.clone()));
                                            // Block layout: the four-block
                                            // CFG is entry/header/body/exit.
                                            // When an if-guarded break is
                                            // present, a guard block is
                                            // inserted between the header and
                                            // the body: entry/header/guard/
                                            // continuation/exit. The guard
                                            // block evaluates the break
                                            // condition and routes to the
                                            // exit (break taken) or the
                                            // continuation (break not taken).
                                            const auto exitBlockId =
                                                hasBreakCondition ? blockId(5) : blockId(4);
                                            const auto bodyBlockId =
                                                hasBreakCondition ? blockId(4) : blockId(3);
                                            // A trailing break exits the
                                            // body to the loop exit; a
                                            // trailing continue or a
                                            // write-only body jumps back
                                            // to the header (bb2, the
                                            // reducible back-edge).
                                            const auto bodyTerminatorTarget =
                                                loopValue.breakSpan != zc::none ? exitBlockId
                                                                                : blockId(2);
                                            const auto& bodyTerminatorSpan =
                                                loopValue.breakSpan != zc::none
                                                    ? ZC_ASSERT_NONNULL(loopValue.breakSpan)
                                                    : (loopValue.continueSpan != zc::none
                                                           ? ZC_ASSERT_NONNULL(
                                                                 loopValue.continueSpan)
                                                           : loopValue.sourceSpan);
                                            // Guard block: Assign(breakTemp =
                                            // Comparison(op, copy(i), <lit>),
                                            // Initialize), SwitchInt(copy(
                                            // breakTemp), [true -> exit],
                                            // default = body).
                                            zc::Vector<MirStatement> guardStatements;
                                            zc::Maybe<MirOperand> guardDiscriminant;
                                            if (hasBreakCondition) {
                                              const auto& breakBinary =
                                                  ZC_ASSERT_NONNULL(breakConditionBinary);
                                              const auto& breakRight =
                                                  ZC_ASSERT_NONNULL(breakConditionRight);
                                              zc::Vector<MirProjection> guardLeftProjections;
                                              auto guardLeftOperand =
                                                  placeUse(proofs, copy,
                                                           MirPlace(initLocalId, initLocal.type,
                                                                    zc::mv(guardLeftProjections),
                                                                    initLocal.type));
                                              if (guardLeftOperand == zc::none) {
                                                return rejectMir<BuiltMirCandidate>(
                                                    ir::IrFailurePhase::MirConstruction,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    declaration.definition, identities,
                                                    static_cast<uint32_t>(pending.size() + 1));
                                              }
                                              zc::Vector<MirProjection> guardTempProjections;
                                              guardStatements.add(MirStatement::assign(
                                                  MirPlace(breakTempId, breakBinary.type,
                                                           zc::mv(guardTempProjections),
                                                           breakBinary.type),
                                                  MirRvalue::comparison(
                                                      ZC_ASSERT_NONNULL(breakComparisonOperator),
                                                      zc::mv(ZC_ASSERT_NONNULL(guardLeftOperand)),
                                                      MirOperand::constant(
                                                          breakRight.type,
                                                          breakRight.value.clone()),
                                                      breakBinary.type),
                                                  MirInitializationKind::Overwrite,
                                                  breakBinary.sourceSpan.clone()));
                                              zc::Vector<MirProjection> guardDiscProjections;
                                              guardDiscriminant =
                                                  placeUse(proofs, copy,
                                                           MirPlace(breakTempId, breakBinary.type,
                                                                    zc::mv(guardDiscProjections),
                                                                    breakBinary.type));
                                              if (guardDiscriminant == zc::none) {
                                                return rejectMir<BuiltMirCandidate>(
                                                    ir::IrFailurePhase::MirConstruction,
                                                    ir::IrFailureKind::InvalidFact, module,
                                                    declaration.definition, identities,
                                                    static_cast<uint32_t>(pending.size() + 1));
                                              }
                                            }
                                            zc::Vector<MirBasicBlock> blocks;
                                            blocks.add(MirBasicBlock{
                                                blockId(1), scopeId(1), zc::mv(entryStatements),
                                                MirTerminator::gotoTarget(
                                                    blockId(2), loopValue.sourceSpan.clone())});
                                            blocks.add(MirBasicBlock{
                                                blockId(2), scopeId(1), zc::Vector<MirStatement>{},
                                                MirTerminator::switchInt(
                                                    zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                    zc::mv(arms), exitBlockId,
                                                    loopValue.sourceSpan.clone())});
                                            if (hasBreakCondition) {
                                              zc::Vector<MirSwitchIntArm> guardArms;
                                              guardArms.add(MirSwitchIntArm{
                                                  checker::checked::CanonicalConstValue::boolean(
                                                      true),
                                                  exitBlockId});
                                              blocks.add(MirBasicBlock{
                                                  blockId(3), scopeId(1), zc::mv(guardStatements),
                                                  MirTerminator::switchInt(
                                                      zc::mv(ZC_ASSERT_NONNULL(guardDiscriminant)),
                                                      zc::mv(guardArms), bodyBlockId,
                                                      ZC_ASSERT_NONNULL(breakConditionBinary)
                                                          .sourceSpan.clone())});
                                            }
                                            blocks.add(MirBasicBlock{
                                                bodyBlockId, scopeId(1), zc::mv(bodyStatements),
                                                MirTerminator::gotoTarget(
                                                    bodyTerminatorTarget,
                                                    bodyTerminatorSpan.clone())});
                                            blocks.add(MirBasicBlock{
                                                exitBlockId, scopeId(1), zc::mv(exitStatements),
                                                MirTerminator::returnValue(
                                                    zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                    returnStatement.sourceSpan.clone())});
                                            MirFunction mirFunction{
                                                declaration.definition,
                                                MirFunctionKind::Function,
                                                identity::DefinitionKind::Function,
                                                declaration.resultType,
                                                declaration.sourceSpan.clone(),
                                                zc::mv(scopes),
                                                zc::mv(locals),
                                                zc::mv(blocks)};
                                            zc::Array<uint8_t> ownerKey;
                                            ZC_IF_SOME(key, definition) {
                                              ownerKey = key.key().encode();
                                            }
                                            pending.add(PendingMirFunction{zc::mv(mirFunction),
                                                                           zc::mv(ownerKey)});
                                            continue;
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      // Sequential N-local body: N leading `let` bindings followed by a single
      // `return <local-or-parameter>`. Parameters occupy localId(1..P); user
      // local i occupies localId(P + i + 1). Each binding lowers to StorageLive +
      // Assign of a constant (literal), nominal aggregate, a primitive binary
      // (arithmetic/comparison) rvalue, or a copy/move place-use of the
      // referenced parameter or earlier local. N>=2 always qualifies; an N==1
      // body qualifies only when its binding is a primitive binary.
      {
        bool allLeadingLocals = isSequentialLocalReturnBlock(hirModule, block);
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
              // Each leading local keeps its own binding's type; only the
              // returned local must match the function result type, and that
              // is checked at the return. Mixed-type leading locals are legal.
              if (local.local.ordinal() != static_cast<uint32_t>(i + 1) ||
                  local.initializer == zc::none) {
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
                    // Builds one binary operand: a scalar-literal constant, a
                    // copy place-use of a parameter local, or a copy place-use
                    // of the written user local (`x = x + 1`), of the operand
                    // type.
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
                      auto operandLocal = localReferenceFor(hirModule, operandNode);
                      ZC_IF_SOME(localRef, operandLocal) {
                        if (localRef.type != binaryValue.operandType) return zc::none;
                        if (localRef.local != localReference.local) return zc::none;
                        zc::Vector<MirProjection> projections;
                        return placeUse(proofs, copy,
                                        MirPlace(userLocalId, binaryValue.operandType,
                                                 zc::mv(projections), binaryValue.operandType));
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
                // The caller's parameters lower to leading parameter locals so a
                // parameter-reference call argument is copied as a place operand;
                // the call-initialized user local follows the parameters.
                const uint32_t parameterCount =
                    static_cast<uint32_t>(declaration.parameters.size());
                const auto resultLocal = localId(parameterCount + 1);
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
                scopes.add(
                    MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
                zc::Vector<MirLocalDeclaration> locals;
                for (size_t i = 0; i < declaration.parameters.size(); ++i) {
                  locals.add(MirLocalDeclaration{localId(static_cast<uint32_t>(i + 1)),
                                                 MirLocalKind::Parameter,
                                                 declaration.parameters[i].type, scopeId(1),
                                                 declaration.parameters[i].sourceSpan.clone()});
                }
                locals.add(MirLocalDeclaration{resultLocal, MirLocalKind::UserLocal, local.type,
                                               scopeId(1), local.sourceSpan.clone()});
                zc::Vector<MirStatement> entryStatements;
                entryStatements.add(
                    MirStatement::storageLive(resultLocal, local.sourceSpan.clone()));
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
                    auto operand = placeUse(
                        proofs, copy,
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
                  return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                                      ir::IrFailureKind::InvalidFact, module,
                                                      declaration.definition, identities,
                                                      static_cast<uint32_t>(pending.size() + 1));
                }
                zc::Maybe<MirBlockId> noUnwind;
                auto callTerminator = MirTerminator::call(
                    directCall.callee, zc::mv(arguments), MirCallEffect::noActivation(),
                    MirPlace(resultLocal, local.type, zc::mv(destinationProjections), local.type),
                    blockId(2), zc::mv(noUnwind), directCall.sourceSpan.clone());
                zc::Vector<MirStatement> continuationStatements;
                zc::Vector<MirProjection> returnProjections;
                auto returnOperand = placeUse(
                    proofs, copy,
                    MirPlace(resultLocal, local.type, zc::mv(returnProjections), local.type));
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
      // Void mutating-receiver write method: a sole
      // `this.<field> = <parameter>;` statement with a Unit result and no
      // return. Claimed before the generic chain, which expects every block to
      // end in a return record.
      if (sourceBlock != zc::none && sourceDeclaration.receiver != zc::none &&
          ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 1) {
        const auto& voidBlock = ZC_ASSERT_NONNULL(sourceBlock);
        auto voidWrite = parameterFieldWriteFor(hirModule, voidBlock.statements[0]);
        ZC_IF_SOME(sourceWrite, voidWrite) {
          auto writeParameter = parameterReferenceFor(hirModule, sourceWrite.value);
          bool valid = false;
          ZC_IF_SOME(parameter, writeParameter) {
            valid = validReceiverFieldWriteVoidFunction(function, sourceDeclaration, voidBlock,
                                                        sourceWrite, parameter, semanticTypes,
                                                        proofs, copy);
          }
          if (!valid) {
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact, module,
                function.owner, identities, static_cast<uint32_t>(index + 1));
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
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact, module,
                function.owner, identities, static_cast<uint32_t>(index + 1));
          }
          previousOwner = zc::mv(ownerBytes);
          ZC_IF_SOME(value, record) {
            if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
              return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                 ir::IrFailureKind::CanonicalCodecMismatch, module,
                                                 function.owner, identities,
                                                 static_cast<uint32_t>(index + 1));
            }
            recomputedFunctions.add(zc::mv(value));
          }
          continue;
        }
      }
      // Discarded mutable receiver call followed by a shared trailing receiver
      // call: `fn f() -> T { let o = S{..constants..}; o.set(c); return
      // o.get(); }`. The statement-position receiver call cannot be resolved by
      // the generic chain, so this branch owns the shape once its three leading
      // records resolve.
      if (sourceBlock != zc::none && sourceDeclaration.receiver == zc::none &&
          ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 3) {
        const auto& callerBlock = ZC_ASSERT_NONNULL(sourceBlock);
        auto callerBinding = localFor(hirModule, callerBlock.statements[0]);
        auto discardedCallRecord = receiverCallFor(hirModule, callerBlock.statements[1]);
        auto callerReturn = returnFor(hirModule, callerBlock.statements[2]);
        ZC_IF_SOME(binding, callerBinding) {
          ZC_IF_SOME(discardedCall, discardedCallRecord) {
            ZC_IF_SOME(sourceReturn, callerReturn) {
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
              auto aggregateRecord = aggregateFor(hirModule, initializerNode);
              auto trailingCallRecord = receiverCallFor(hirModule, sourceReturn.value);
              auto setReceiverRecord = localReferenceFor(hirModule, discardedCall.receiver);
              ZC_IF_SOME(aggregate, aggregateRecord) {
                ZC_IF_SOME(trailingCall, trailingCallRecord) {
                  ZC_IF_SOME(setReceiver, setReceiverRecord) {
                    auto getReceiverRecord = localReferenceFor(hirModule, trailingCall.receiver);
                    ZC_IF_SOME(getReceiver, getReceiverRecord) {
                      const bool discardedMutableUnit =
                          discardedCall.receiverMode == checker::checked::ReceiverMode::Mutable &&
                          discardedCall.receiverAdjustments.size() == 1 &&
                          discardedCall.receiverAdjustments[0] ==
                              checker::checked::ReceiverAdjustmentStep::BorrowMutable &&
                          discardedCall.arguments.size() == 1 &&
                          discardedCall.arguments[0].value != zc::none;
                      const bool discardedSharedValue =
                          discardedCall.receiverMode == checker::checked::ReceiverMode::Shared &&
                          discardedCall.receiverAdjustments.size() == 1 &&
                          discardedCall.receiverAdjustments[0] ==
                              checker::checked::ReceiverAdjustmentStep::BorrowShared &&
                          discardedCall.arguments.size() == 0;
                      const bool gate =
                          binding.local.ordinal() == 1 && binding.initializer == aggregate.node &&
                          binding.type == aggregate.type &&
                          binding.type == discardedCall.receiverSourceType &&
                          binding.type == trailingCall.receiverSourceType &&
                          aggregate.category == hir::HirValueCategory::Value &&
                          discardedCall.receiver == setReceiver.node &&
                          trailingCall.receiver == getReceiver.node &&
                          setReceiver.local == binding.local &&
                          getReceiver.local == binding.local &&
                          setReceiver.category == hir::HirValueCategory::Place &&
                          getReceiver.category == hir::HirValueCategory::Place &&
                          (discardedMutableUnit || discardedSharedValue) &&
                          trailingCall.receiverMode == checker::checked::ReceiverMode::Shared &&
                          trailingCall.receiverAdjustments.size() == 1 &&
                          trailingCall.receiverAdjustments[0] ==
                              checker::checked::ReceiverAdjustmentStep::BorrowShared &&
                          trailingCall.arguments.size() == 0 &&
                          trailingCall.resultType == sourceDeclaration.resultType;
                      bool valid = false;
                      if (gate) {
                        valid = validVoidCallThenReceiverCallReturnFunction(
                            function, sourceDeclaration, callerBlock, binding, aggregate,
                            sourceReturn, setReceiver, discardedCall, getReceiver, trailingCall,
                            proofs, copy, module, identities, semanticTypes, discardedSharedValue);
                      }
                      if (!valid) {
                        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                           ir::IrFailureKind::InvalidFact, module,
                                                           function.owner, identities,
                                                           static_cast<uint32_t>(index + 1));
                      }
                      auto owner = identities.definition(function.owner);
                      auto record = encodeFunction(function, module, identities, semanticTypes);
                      if (owner == zc::none || record == zc::none) {
                        return rejectMir<VerifiedBuiltMir>(
                            ir::IrFailurePhase::BuiltMirVerification,
                            ir::IrFailureKind::CanonicalCodecMismatch, module, function.owner,
                            identities, static_cast<uint32_t>(index + 1));
                      }
                      zc::Array<uint8_t> ownerBytes;
                      ZC_IF_SOME(value, owner) { ownerBytes = value.key().encode(); }
                      if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
                        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                           ir::IrFailureKind::InvalidFact, module,
                                                           function.owner, identities,
                                                           static_cast<uint32_t>(index + 1));
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
      }
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
      // Three-statement loop body `[local, loop, return]` whose middle
      // statement is a loop. Two shapes share this skeleton: the for-loop
      // composite (scalar-literal return, comparison-binary condition) and the
      // loop-body composite (local-reference return, parameter-reference
      // condition). Both validate as a reducible four-block CFG. The for-loop
      // validator is tried first; the loop-body composite is the fallback.
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 3 &&
          loopFor(hirModule, ZC_ASSERT_NONNULL(sourceBlock).statements[1]) != zc::none) {
        bool valid = false;
        ZC_IF_SOME(block, sourceBlock) {
          auto sourceLocal = localFor(hirModule, block.statements[0]);
          auto loop = loopFor(hirModule, block.statements[1]);
          auto sourceReturn = returnFor(hirModule, block.statements[2]);
          ZC_IF_SOME(local, sourceLocal) {
            ZC_IF_SOME(loopValue, loop) {
              ZC_IF_SOME(returnStatement, sourceReturn) {
                // For-loop composite: the return is a scalar literal and the
                // condition is a comparison binary.
                auto conditionBinary = primitiveBinaryFor(hirModule, loopValue.condition);
                auto returnLiteral = expressionFor(hirModule, returnStatement.value);
                ZC_IF_SOME(condition, conditionBinary) {
                  ZC_IF_SOME(returnValue, returnLiteral) {
                    valid = validForLoopReturnFunction(function, sourceDeclaration, block, local,
                                                       loopValue, returnStatement, condition,
                                                       returnValue, hirModule, proofs, copy, module,
                                                       identities, semanticTypes);
                  }
                }
                // Loop-body composite: the return is a local reference and the
                // condition is a parameter reference.
                if (!valid) {
                  auto reference = localReferenceFor(hirModule, returnStatement.value);
                  auto conditionRef = parameterReferenceFor(hirModule, loopValue.condition);
                  ZC_IF_SOME(referenceValue, reference) {
                    ZC_IF_SOME(condRef, conditionRef) {
                      valid = validLoopBodyReturnFunction(
                          function, sourceDeclaration, block, local, loopValue, returnStatement,
                          referenceValue, condRef, hirModule, proofs, copy, module, identities,
                          semanticTypes);
                    }
                  }
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
      // Nested for-loop accumulator: same N+3-statement body as the for-loop
      // accumulator, but the outer loop body has exactly three statements
      // (inner-init local, inner loop, outer update write). Validates as a
      // reducible seven-block CFG.
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 4 &&
          loopFor(hirModule,
                  ZC_ASSERT_NONNULL(sourceBlock)
                      .statements[ZC_ASSERT_NONNULL(sourceBlock).statements.size() - 2]) !=
              zc::none) {
        bool nestedValid = false;
        ZC_IF_SOME(block, sourceBlock) {
          auto outerLoop = loopFor(hirModule, block.statements[block.statements.size() - 2]);
          auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
          ZC_IF_SOME(outerLoopValue, outerLoop) {
            if (outerLoopValue.body.size() == 3) {
              auto innerInitMaybe = localFor(hirModule, outerLoopValue.body[0]);
              auto innerLoopMaybe = loopFor(hirModule, outerLoopValue.body[1]);
              if (innerInitMaybe != zc::none && innerLoopMaybe != zc::none) {
                ZC_IF_SOME(returnStatement, sourceReturn) {
                  auto outerCondBinary = primitiveBinaryFor(hirModule, outerLoopValue.condition);
                  auto returnReference = localReferenceFor(hirModule, returnStatement.value);
                  ZC_IF_SOME(outerCond, outerCondBinary) {
                    ZC_IF_SOME(returnRef, returnReference) {
                      nestedValid = validNestedForLoopAccumulatorReturnFunction(
                          function, sourceDeclaration, block, outerLoopValue, returnStatement,
                          outerCond, returnRef, hirModule, proofs, copy, module, identities,
                          semanticTypes);
                    }
                  }
                }
              }
            }
          }
        }
        if (nestedValid) {
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
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::InvalidFact, module,
                function.owner, identities, static_cast<uint32_t>(index + 1));
          }
          previousOwner = zc::mv(ownerBytes);
          ZC_IF_SOME(value, record) {
            if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
              return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                                 ir::IrFailureKind::CanonicalCodecMismatch, module,
                                                 function.owner, identities,
                                                 static_cast<uint32_t>(index + 1));
            }
            recomputedFunctions.add(zc::mv(value));
          }
          continue;
        }
      }
      // N+3-statement loop body `[acc-local..., init-local, loop, return]` whose
      // second-to-last statement is a loop. This is the for-loop accumulator
      // composite: N leading accumulator bindings, a for-loop with a non-empty
      // body, and a local-reference return. Validates as a reducible four-block
      // CFG.
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 4 &&
          loopFor(hirModule,
                  ZC_ASSERT_NONNULL(sourceBlock)
                      .statements[ZC_ASSERT_NONNULL(sourceBlock).statements.size() - 2]) !=
              zc::none) {
        bool valid = false;
        ZC_IF_SOME(block, sourceBlock) {
          auto loop = loopFor(hirModule, block.statements[block.statements.size() - 2]);
          auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
          ZC_IF_SOME(loopValue, loop) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              auto conditionBinary = primitiveBinaryFor(hirModule, loopValue.condition);
              auto returnReference = localReferenceFor(hirModule, returnStatement.value);
              ZC_IF_SOME(condition, conditionBinary) {
                ZC_IF_SOME(returnRef, returnReference) {
                  valid = validForLoopAccumulatorReturnFunction(
                      function, sourceDeclaration, block, loopValue, returnStatement, condition,
                      returnRef, hirModule, proofs, copy, module, identities, semanticTypes);
                  if (!valid) {}
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
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 1 &&
          sourceDeclaration.receiver != zc::none) {
        ZC_IF_SOME(block, sourceBlock) {
          auto sourceReturn = returnFor(hirModule, block.statements[0]);
          ZC_IF_SOME(returnStatement, sourceReturn) {
            auto selfCall = receiverCallFor(hirModule, returnStatement.value);
            if (selfCall != zc::none && ZC_ASSERT_NONNULL(selfCall).receiver == hir::HirNodeId()) {
              ZC_IF_SOME(call, selfCall) {
                if (!validMethodReceiverSelfCallReturnFunction(function, sourceDeclaration, block,
                                                               returnStatement, call, proofs, copy,
                                                               module, identities, semanticTypes)) {
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
        }
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
              auto scalarInitializer = expressionFor(hirModule, initializerNode);
              ZC_IF_SOME(call, receiverCall) {
                auto receiver = localReferenceFor(hirModule, call.receiver);
                ZC_IF_SOME(reference, receiver) {
                  if (aggregate != zc::none || scalarInitializer != zc::none) {
                    if (!validReceiverCallReturnFunction(function, sourceDeclaration, block, local,
                                                         aggregate, scalarInitializer,
                                                         returnStatement, reference, call, proofs,
                                                         copy, module, identities, semanticTypes)) {
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
      bool trailingConditionalReturn = false;
      ZC_IF_SOME(block, sourceBlock) {
        auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
        ZC_IF_SOME(returnStatement, sourceReturn) {
          returnsRootLocal = localFieldProjectionFor(hirModule, returnStatement.value) == zc::none;
          trailingConditionalReturn = conditionalFor(hirModule, returnStatement.value) != zc::none;
        }
      }
      if (sourceBlock != zc::none &&
          isSequentialLocalReturnBlock(hirModule, ZC_ASSERT_NONNULL(sourceBlock))) {
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
            // Ternary-initialized local: the last binding's initializer is a
            // HirConditionalExpression. The MIR is a four-block diamond, not a
            // single-block sequential body.
            bool isTernary = false;
            if (block.statements.size() >= 2) {
              auto lastBinding = localFor(hirModule, block.statements[block.statements.size() - 2]);
              ZC_IF_SOME(local, lastBinding) {
                ZC_IF_SOME(initializer, local.initializer) {
                  isTernary = conditionalFor(hirModule, initializer) != zc::none;
                }
              }
            }
            bool valid = false;
            if (isTernary) {
              valid =
                  validTernaryLocalReturnFunction(function, hirModule, sourceDeclaration, block,
                                                  module, identities, semanticTypes, proofs, copy);
            } else {
              valid = validSequentialLocalReturnFunction(function, hirModule, sourceDeclaration,
                                                         block, module, identities, semanticTypes,
                                                         proofs, copy);
            }
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
      // Single user-local body with one or more overwrite writes where at least
      // one write value is a binary expression (compound assignment desugar):
      // `mut x = <lit/param>; x = <lit/param/binary>; ...; return x;`. The
      // dedicated verifier re-validates every write and operand.
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 4 &&
          returnsRootLocal && !trailingConditionalReturn) {
        ZC_IF_SOME(block, sourceBlock) {
          auto sourceLocal = localFor(hirModule, block.statements[0]);
          auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
          ZC_IF_SOME(local, sourceLocal) {
            ZC_IF_SOME(returnStatement, sourceReturn) {
              auto reference = localReferenceFor(hirModule, returnStatement.value);
              if (reference != zc::none && local.initializer != zc::none) {
                bool hasBinaryWrite = false;
                const size_t writeCount = block.statements.size() - 2;
                for (size_t i = 0; i < writeCount; ++i) {
                  auto write = localWriteFor(hirModule, block.statements[1 + i]);
                  if (write != zc::none) {
                    auto binary = primitiveBinaryFor(hirModule, ZC_ASSERT_NONNULL(write).value);
                    if (binary != zc::none) {
                      hasBinaryWrite = true;
                      break;
                    }
                  }
                }
                if (hasBinaryWrite) {
                  bool valid = validLocalWriteReturnFunction(
                      function, sourceDeclaration, block, local, returnStatement,
                      ZC_ASSERT_NONNULL(reference), hirModule, module, identities, semanticTypes,
                      proofs, copy);
                  if (valid) {
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
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 4 &&
          returnsRootLocal && !trailingConditionalReturn) {
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
              if (validSequence) {
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
      auto receiverFieldProjection = parameterFieldProjectionFor(hirModule, expressionNode);
      // In a two-statement receiver write-read body the leading statement is a
      // parameter field write; resolve it independently.
      hir::HirNodeId leadingNode;
      if (sourceBlock != zc::none && ZC_ASSERT_NONNULL(sourceBlock).statements.size() == 2) {
        leadingNode = ZC_ASSERT_NONNULL(sourceBlock).statements[0];
      }
      auto receiverFieldWrite = parameterFieldWriteFor(hirModule, leadingNode);
      hir::HirNodeId receiverWriteValueNode;
      ZC_IF_SOME(value, receiverFieldWrite) { receiverWriteValueNode = value.value; }
      auto receiverWriteLiteral = expressionFor(hirModule, receiverWriteValueNode);
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
      // K leading scalar user locals followed by one comparison conditional with
      // literal arms. Detected purely from the HIR shape; the dedicated
      // verifier re-validates every local, statement, and operand.
      const bool isLeadingLocalConditional =
          sourceDeclaration.receiver == zc::none && sourceBlock != zc::none &&
          ZC_ASSERT_NONNULL(sourceBlock).statements.size() >= 2 && conditional != zc::none &&
          primitiveBinaryFor(hirModule, ZC_ASSERT_NONNULL(conditional).condition) != zc::none;
      const bool isLocalFieldReturn = localFieldProjection != zc::none;
      const bool isReceiverFieldReturn =
          receiverFieldProjection != zc::none && sourceDeclaration.receiver != zc::none;
      const bool isByValueParameterFieldReturn =
          receiverFieldProjection != zc::none && sourceDeclaration.receiver == zc::none &&
          sourceDeclaration.parameters.size() == 1 &&
          sourceDeclaration.parameters[0].key ==
              ZC_ASSERT_NONNULL(receiverFieldProjection).parameter &&
          sourceDeclaration.parameters[0].type ==
              ZC_ASSERT_NONNULL(receiverFieldProjection).receiverType;
      const bool isByValueAggregateCallReturn =
          receiverFieldProjection == zc::none && localFieldProjection == zc::none &&
          sourceDeclaration.receiver == zc::none && sourceDeclaration.parameters.size() == 0 &&
          sourceLocal != zc::none && localReference == zc::none &&
          initializerAggregate != zc::none && call != zc::none && expression == zc::none &&
          parameterReference == zc::none && ZC_ASSERT_NONNULL(call).arguments.size() == 1 &&
          ZC_ASSERT_NONNULL(call).arguments[0].value == zc::none &&
          ZC_ASSERT_NONNULL(call).arguments[0].parameter == zc::none &&
          ZC_ASSERT_NONNULL(call).arguments[0].local == ZC_ASSERT_NONNULL(sourceLocal).local;
      const bool isScalarLocalCallReturn =
          receiverFieldProjection == zc::none && localFieldProjection == zc::none &&
          sourceDeclaration.receiver == zc::none && sourceDeclaration.parameters.size() == 0 &&
          sourceLocal != zc::none && localReference == zc::none && initializer != zc::none &&
          initializerAggregate == zc::none && call != zc::none && expression == zc::none &&
          parameterReference == zc::none && ZC_ASSERT_NONNULL(call).arguments.size() == 1 &&
          ZC_ASSERT_NONNULL(call).arguments[0].value == zc::none &&
          ZC_ASSERT_NONNULL(call).arguments[0].parameter == zc::none &&
          ZC_ASSERT_NONNULL(call).arguments[0].local == ZC_ASSERT_NONNULL(sourceLocal).local;
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
                                 !isByValueAggregateCallReturn && !isScalarLocalCallReturn &&
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
      if (!isLeadingLocalConditional &&
          (sourceBlock == zc::none || sourceReturn == zc::none ||
           (!isLocalFieldReturn && !isReceiverFieldReturn && !isByValueParameterFieldReturn &&
            !isByValueAggregateCallReturn && !isScalarLocalCallReturn && !isLocalReturn &&
            !isParameterReturn && !isParameterReborrow && !isLocalBorrow && !isConditionalReturn &&
            !isComparisonReturn && (expression == zc::none) == (call == zc::none)) ||
           (isByValueAggregateCallReturn &&
            (isLocalFieldReturn || isReceiverFieldReturn || isByValueParameterFieldReturn ||
             isScalarLocalCallReturn || isLocalReturn || isParameterReturn || isParameterReborrow ||
             isLocalBorrow || isConditionalReturn || isComparisonReturn || expression != zc::none ||
             localReference != zc::none || sourceOverwrite != zc::none)) ||
           (isScalarLocalCallReturn &&
            (isLocalFieldReturn || isReceiverFieldReturn || isByValueParameterFieldReturn ||
             isLocalReturn || isParameterReturn || isParameterReborrow || isLocalBorrow ||
             isConditionalReturn || isComparisonReturn || expression != zc::none ||
             localReference != zc::none || sourceOverwrite != zc::none)) ||
           (isReceiverFieldReturn &&
            (isLocalFieldReturn || isLocalReturn || isParameterReturn || isParameterReborrow ||
             isLocalBorrow || isConditionalReturn || isComparisonReturn || expression != zc::none ||
             call != zc::none)) ||
           (isByValueParameterFieldReturn &&
            (isLocalFieldReturn || isReceiverFieldReturn || isLocalReturn || isParameterReturn ||
             isParameterReborrow || isLocalBorrow || isConditionalReturn || isComparisonReturn ||
             expression != zc::none || call != zc::none)) ||
           (!isLocalFieldReturn && !isReceiverFieldReturn && isParameterReturn &&
            (isLocalReturn || isParameterReborrow || expression != zc::none || call != zc::none)) ||
           (!isLocalFieldReturn && !isReceiverFieldReturn && isParameterReborrow &&
            (isLocalReturn || expression != zc::none || call != zc::none)) ||
           (!isLocalFieldReturn && !isReceiverFieldReturn && isLocalReturn &&
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
             expression != zc::none || call != zc::none)))) {
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
                  if (sourceDeclaration.receiver != zc::none) {
                    valid = validMethodScalarLocalReturnFunction(
                        function, hirModule, sourceDeclaration, block, local, localInitializer,
                        returnStatement, reference, module, identities, semanticTypes, proofs,
                        copy);
                  } else {
                    valid = validLocalReturnFunction(function, hirModule, sourceDeclaration, block,
                                                     local, localInitializer, returnStatement,
                                                     reference, module, identities, semanticTypes,
                                                     proofs, copy);
                  }
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
            // Single user-local body with one or more overwrite writes:
            // `mut x = <lit/param>; x = <lit/param/binary>; ...; return x;`.
            // The dedicated verifier re-validates every write and operand.
            if (!valid && sourceBlock != zc::none &&
                ZC_ASSERT_NONNULL(sourceBlock).statements.size() > 3 &&
                sourceOverwrite != zc::none && local.initializer != zc::none) {
              ZC_IF_SOME(reference, localReference) {
                valid = validLocalWriteReturnFunction(function, sourceDeclaration, block, local,
                                                      returnStatement, reference, hirModule, module,
                                                      identities, semanticTypes, proofs, copy);
              }
            }
          }
          ZC_IF_SOME(sourceExpression, expression) {
            valid = validScalarReturnFunction(function, hirModule, sourceDeclaration, block,
                                              returnStatement, sourceExpression, module, identities,
                                              semanticTypes);
          }
          ZC_IF_SOME(sourceReceiverField, receiverFieldProjection) {
            if (sourceDeclaration.receiver != zc::none && receiverFieldWrite != zc::none &&
                receiverWriteLiteral != zc::none) {
              valid = validReceiverFieldWriteReturnFunction(
                  function, sourceDeclaration, block, ZC_ASSERT_NONNULL(receiverFieldWrite),
                  ZC_ASSERT_NONNULL(receiverWriteLiteral), returnStatement, sourceReceiverField,
                  hirModule, module, identities, semanticTypes, proofs, copy);
            } else if (sourceDeclaration.receiver != zc::none) {
              valid = validReceiverFieldReturnFunction(function, sourceDeclaration, block,
                                                       returnStatement, sourceReceiverField, proofs,
                                                       copy);
            } else if (isByValueParameterFieldReturn) {
              valid = validByValueParameterFieldReturnFunction(function, sourceDeclaration, block,
                                                               returnStatement, sourceReceiverField,
                                                               proofs, copy);
            }
          }
          ZC_IF_SOME(sourceCall, call) {
            if (isByValueAggregateCallReturn) {
              ZC_IF_SOME(aggregateLocal, sourceLocal) {
                ZC_IF_SOME(aggregateRecord, initializerAggregate) {
                  valid = validByValueAggregateCallReturnFunction(
                      function, sourceDeclaration, block, aggregateLocal, aggregateRecord,
                      returnStatement, sourceCall, module, identities, semanticTypes, proofs, copy);
                }
              }
            } else if (isScalarLocalCallReturn) {
              ZC_IF_SOME(scalarLocal, sourceLocal) {
                ZC_IF_SOME(scalarInitializer, initializer) {
                  valid = validScalarLocalCallReturnFunction(
                      function, sourceDeclaration, block, scalarLocal, scalarInitializer,
                      returnStatement, sourceCall, module, identities, semanticTypes, proofs, copy);
                }
              }
            } else {
              valid = validDirectCallReturnFunction(function, sourceDeclaration, block,
                                                    returnStatement, sourceCall, proofs, copy,
                                                    module, identities, semanticTypes);
            }
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
            // K leading scalar locals followed by the comparison conditional:
            // both arms are literal constants and the condition is the
            // comparison whose operands may read the leading locals.
            if (isLeadingLocalConditional) {
              auto leadingThenLiteral = expressionFor(hirModule, sourceConditional.thenReturnValue);
              auto leadingElseLiteral = expressionFor(hirModule, sourceConditional.elseReturnValue);
              auto leadingEquality = primitiveBinaryFor(hirModule, sourceConditional.condition);
              if (leadingThenLiteral != zc::none && leadingElseLiteral != zc::none &&
                  leadingEquality != zc::none) {
                valid = validLeadingLocalConditionalReturnFunction(
                    function, hirModule, sourceDeclaration, block, returnStatement,
                    sourceConditional, ZC_ASSERT_NONNULL(leadingEquality),
                    ZC_ASSERT_NONNULL(leadingThenLiteral), ZC_ASSERT_NONNULL(leadingElseLiteral),
                    proofs, copy, module, identities, semanticTypes);
              }
            }
            // Chained conditional return: a nested chain of equality
            // conditionals with literal then-arms and a literal else-arm.
            // The else branch of each conditional (except the innermost) is
            // another conditional, so the generic arm resolution below cannot
            // match it. Detect the shape by walking the chain and dispatch to
            // the dedicated verifier.
            if (!isLeadingLocalConditional && !valid) {
              auto chainedEquality = primitiveBinaryFor(hirModule, sourceConditional.condition);
              auto chainedThenLiteral = expressionFor(hirModule, sourceConditional.thenReturnValue);
              auto chainedElseConditional =
                  conditionalFor(hirModule, sourceConditional.elseReturnValue);
              if (chainedEquality != zc::none && chainedThenLiteral != zc::none &&
                  chainedElseConditional != zc::none) {
                valid = validChainedConditionalReturnFunction(
                    function, hirModule, sourceDeclaration, block, returnStatement,
                    sourceConditional, proofs, copy, module, identities, semanticTypes);
              }
            }
            // Each arm resolves to a scalar-literal expression, a parameter
            // reference, or a primitive binary operation; exactly one lookup
            // succeeds per arm.
            ConditionalArmView thenArm{
                expressionFor(hirModule, sourceConditional.thenReturnValue),
                parameterReferenceFor(hirModule, sourceConditional.thenReturnValue),
                primitiveBinaryFor(hirModule, sourceConditional.thenReturnValue)};
            ConditionalArmView elseArm{
                expressionFor(hirModule, sourceConditional.elseReturnValue),
                parameterReferenceFor(hirModule, sourceConditional.elseReturnValue),
                primitiveBinaryFor(hirModule, sourceConditional.elseReturnValue)};
            const bool thenOk = (thenArm.literal != zc::none) != (thenArm.parameter != zc::none) !=
                                (thenArm.binary != zc::none);
            const bool elseOk = (elseArm.literal != zc::none) != (elseArm.parameter != zc::none) !=
                                (elseArm.binary != zc::none);
            // The condition node resolves to either a bare parameter reference or
            // an equality comparison; dispatch to the matching verifier shape.
            auto conditionRef = parameterReferenceFor(hirModule, sourceConditional.condition);
            auto equality = primitiveBinaryFor(hirModule, sourceConditional.condition);
            ZC_IF_SOME(condRef, conditionRef) {
              if (!isLeadingLocalConditional && sourceDeclaration.receiver != zc::none &&
                  thenArm.literal != zc::none && elseArm.literal != zc::none &&
                  thenArm.parameter == zc::none && elseArm.parameter == zc::none) {
                // The shared-receiver method conditional: literal arms over a
                // bare ordinary-parameter condition, with the receiver leading
                // the local layout.
                valid = validMethodConditionalReturnFunction(
                    function, sourceDeclaration, block, returnStatement, sourceConditional, condRef,
                    ZC_ASSERT_NONNULL(thenArm.literal), ZC_ASSERT_NONNULL(elseArm.literal), proofs,
                    copy, module, identities, semanticTypes);
              } else if (thenOk && elseOk) {
                valid = validConditionalReturnFunction(function, hirModule, sourceDeclaration,
                                                       block, returnStatement, sourceConditional,
                                                       condRef, thenArm, elseArm, proofs, copy,
                                                       module, identities, semanticTypes);
              }
            }
            ZC_IF_SOME(equalityValue, equality) {
              // A LogicalAnd condition is either a match-guard conjunction
              // (parameters + 3 locals: result + guardTemp + conjTemp) or a
              // bare `&&` operator in condition position (parameters + 2
              // locals: result + temp). Dispatch on the local count so the
              // `&&` operator path reaches the equality verifier.
              if (equalityValue.operation == checker::PrimitiveOperation::LogicalAnd &&
                  !isLeadingLocalConditional && thenOk && elseOk &&
                  function.locals.size() == sourceDeclaration.parameters.size() + 3) {
                valid = validConjunctiveConditionalReturnFunction(
                    function, hirModule, sourceDeclaration, block, returnStatement,
                    sourceConditional, equalityValue, thenArm, elseArm, proofs, copy, module,
                    identities, semanticTypes);
              } else if (!isLeadingLocalConditional && thenOk && elseOk) {
                valid = validEqualityConditionalReturnFunction(
                    function, hirModule, sourceDeclaration, block, returnStatement,
                    sourceConditional, equalityValue, thenArm, elseArm, proofs, copy, module,
                    identities, semanticTypes);
              }
            }
          }
          ZC_IF_SOME(sourceComparison, comparisonReturn) {
            auto fieldBinaryProjection =
                parameterFieldProjectionFor(hirModule, sourceComparison.left);
            hir::HirNodeId fieldBinaryLiteralNode = sourceComparison.right;
            if (fieldBinaryProjection == zc::none) {
              fieldBinaryProjection =
                  parameterFieldProjectionFor(hirModule, sourceComparison.right);
              fieldBinaryLiteralNode = sourceComparison.left;
            }
            auto fieldBinaryLiteral = expressionFor(hirModule, fieldBinaryLiteralNode);
            if (sourceDeclaration.receiver != zc::none && fieldBinaryProjection != zc::none &&
                fieldBinaryLiteral != zc::none) {
              valid = validReceiverFieldBinaryReturnFunction(
                  function, sourceDeclaration, block, returnStatement, sourceComparison,
                  ZC_ASSERT_NONNULL(fieldBinaryProjection), ZC_ASSERT_NONNULL(fieldBinaryLiteral),
                  proofs, copy, module, identities, semanticTypes);
            } else {
              valid = validComparisonReturnFunction(function, hirModule, sourceDeclaration, block,
                                                    returnStatement, sourceComparison, proofs, copy,
                                                    module, identities, semanticTypes);
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
