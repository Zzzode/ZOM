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
#include "compiler/mir/build/loop-context.h"
#include "compiler/mir/build/mir-builder.h"
#include "compiler/mir/cfg-utils.h"
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

MirRvalue::MirRvalue(MirEraseRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue::MirRvalue(MirErrorUnionRvalue&& value) noexcept : value(zc::mv(value)) {}

MirRvalue::MirRvalue(MirExtractFieldRvalue&& value) noexcept : value(zc::mv(value)) {}

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

MirRvalue MirRvalue::erase(MirOperand&& source, identity::SemanticTypeId sourceType,
                           identity::SemanticTypeId targetType) noexcept {
  return MirRvalue(MirEraseRvalue{zc::mv(source), sourceType, targetType});
}

MirRvalue MirRvalue::errorUnion(identity::SemanticTypeId unionType,
                                identity::SemanticTypeId successType,
                                identity::SemanticTypeId residualType, uint8_t tag,
                                MirOperand&& payload) noexcept {
  return MirRvalue(MirErrorUnionRvalue{unionType, successType, residualType, tag, zc::mv(payload)});
}

MirRvalue MirRvalue::extractField(MirOperand&& source, uint32_t fieldIndex,
                                  identity::SemanticTypeId resultType) noexcept {
  return MirRvalue(MirExtractFieldRvalue{zc::mv(source), fieldIndex, resultType});
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
  if (value.is<MirEraseRvalue>()) {
    const auto& erase = value.get<MirEraseRvalue>();
    return MirRvalue::erase(erase.source.clone(), erase.sourceType, erase.targetType);
  }
  if (value.is<MirErrorUnionRvalue>()) {
    const auto& errorUnion = value.get<MirErrorUnionRvalue>();
    return MirRvalue::errorUnion(errorUnion.unionType, errorUnion.successType,
                                 errorUnion.residualType, errorUnion.tag,
                                 errorUnion.payload.clone());
  }
  if (value.is<MirExtractFieldRvalue>()) {
    const auto& extract = value.get<MirExtractFieldRvalue>();
    return MirRvalue::extractField(extract.source.clone(), extract.fieldIndex, extract.resultType);
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
  if (value.is<MirEraseRvalue>()) return MirRvalueKind::Erase;
  if (value.is<MirErrorUnionRvalue>()) return MirRvalueKind::ErrorUnion;
  if (value.is<MirExtractFieldRvalue>()) return MirRvalueKind::ExtractField;
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

const MirEraseRvalue& MirRvalue::eraseValue() const { return value.get<MirEraseRvalue>(); }

const MirErrorUnionRvalue& MirRvalue::errorUnionValue() const {
  return value.get<MirErrorUnionRvalue>();
}

const MirExtractFieldRvalue& MirRvalue::extractFieldValue() const {
  return value.get<MirExtractFieldRvalue>();
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
MirTerminator::MirTerminator(MirIndirectCallTerminator&& value,
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

MirTerminator MirTerminator::indirectCall(identity::DefId interface, identity::DefId method,
                                          MirOperand&& receiver, zc::Vector<MirOperand>&& arguments,
                                          MirCallEffect&& effect, MirPlace&& destination,
                                          MirBlockId normalTarget,
                                          zc::Maybe<MirBlockId>&& unwindTarget,
                                          identity::SourceSpan&& sourceSpan) noexcept {
  return MirTerminator(
      MirIndirectCallTerminator{interface, method, zc::mv(receiver), zc::mv(arguments),
                                zc::mv(effect), zc::mv(destination), normalTarget,
                                zc::mv(unwindTarget)},
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
  if (kind() == MirTerminatorKind::IndirectCall) {
    const auto& source = indirectCallValue();
    zc::Vector<MirOperand> arguments;
    for (const auto& argument : source.arguments) arguments.add(argument.clone());
    zc::Maybe<MirBlockId> unwindTarget;
    ZC_IF_SOME(target, source.unwindTarget) { unwindTarget = target; }
    return indirectCall(source.interface, source.method, source.receiver.clone(), zc::mv(arguments),
                        source.effect.clone(), source.destination.clone(), source.normalTarget,
                        zc::mv(unwindTarget), sourceSpan().clone());
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
  if (value.is<MirIndirectCallTerminator>()) return MirTerminatorKind::IndirectCall;
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

const MirIndirectCallTerminator& MirTerminator::indirectCallValue() const {
  return value.get<MirIndirectCallTerminator>();
}

namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
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
  if (value.kind() == MirRvalueKind::Erase) {
    const auto& erase = value.eraseValue();
    return encodeOperand(encoder, erase.source, module, identities, semanticTypes) &&
           encodeType(encoder, erase.sourceType, semanticTypes) &&
           encodeType(encoder, erase.targetType, semanticTypes);
  }
  if (value.kind() == MirRvalueKind::ErrorUnion) {
    const auto& errorUnion = value.errorUnionValue();
    encoder.encodeUint8(errorUnion.tag);
    return encodeType(encoder, errorUnion.unionType, semanticTypes) &&
           encodeType(encoder, errorUnion.successType, semanticTypes) &&
           encodeType(encoder, errorUnion.residualType, semanticTypes) &&
           encodeOperand(encoder, errorUnion.payload, module, identities, semanticTypes);
  }
  if (value.kind() == MirRvalueKind::ExtractField) {
    const auto& extract = value.extractFieldValue();
    encoder.encodeUint32(extract.fieldIndex);
    return encodeOperand(encoder, extract.source, module, identities, semanticTypes) &&
           encodeType(encoder, extract.resultType, semanticTypes);
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
  if (terminator.kind() == MirTerminatorKind::IndirectCall) {
    const auto& indirect = terminator.indirectCallValue();
    if (!encodeDefinition(encoder, indirect.interface, identities) ||
        !encodeDefinition(encoder, indirect.method, identities) ||
        !encodeOperand(encoder, indirect.receiver, module, identities, semanticTypes) ||
        !encodePlace(encoder, indirect.destination, identities, semanticTypes) ||
        !indirect.normalTarget.isValid()) {
      return false;
    }
    encoder.encodeUint8(static_cast<uint8_t>(indirect.effect.kind()));
    ZC_IF_SOME(temporary, indirect.effect.activatedMutableReceiver()) {
      if (!temporary.isValid()) return false;
      encoder.encodeUint32(temporary.ordinal());
    }
    encoder.encodeSequenceSize(indirect.arguments.size());
    for (const auto& argument : indirect.arguments) {
      if (!encodeOperand(encoder, argument, module, identities, semanticTypes)) return false;
    }
    encoder.encodeUint32(indirect.normalTarget.ordinal());
    ZC_IF_SOME(target, indirect.unwindTarget) {
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

// True when a HIR block is a leading-`let`-then-`return` body lowered by the
// sequential N-local rail: N leading local bindings followed by a single return.
// N>=2 bodies always qualify. An N==1 (two-statement) body qualifies only when
// its single binding's initializer is a primitive binary, which is the only
// single-local initializer owned by the sequential rail; a literal, aggregate,
// or reference single-local keeps the dedicated single-local shape.
bool isSequentialLocalReturnBlock(const hir::VerifiedHirModule& module,
                                  const hir::HirBlockStatement& block) {
  if (block.statements.size() < 2) return false;
  // Scan leading statements: each must be a local binding or an increment
  // write. At least one must be a local binding. The trailing statement is
  // the return.
  size_t localCount = 0;
  for (size_t i = 0; i + 1 < block.statements.size(); ++i) {
    if (localFor(module, block.statements[i]) != zc::none) {
      ++localCount;
      continue;
    }
    if (localWriteFor(module, block.statements[i]) != zc::none) continue;
    return false;
  }
  if (localCount == 0) return false;
  const size_t lastIndex = block.statements.size() - 1;
  if (returnFor(module, block.statements[lastIndex]) == zc::none) return false;
  // A trailing conditional return is the leading-local comparison shape, not a
  // sequential-local return. Both have K leading locals, but the leading-local
  // shape pools its comparison into the shared conditional term and reads its
  // operands as the comparison's leaves, so it is not a sequential body.
  {
    auto tailReturn = returnFor(module, block.statements[lastIndex]);
    ZC_IF_SOME(statement, tailReturn) {
      if (conditionalFor(module, statement.value) != zc::none) return false;
    }
  }
  if (localCount >= 2) return true;
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

/// \brief Extracts the result type from an operand.
static identity::SemanticTypeId operandResultType(const MirOperand& operand) {
  if (operand.kind() == MirOperandKind::Constant) { return operand.constantValue().type; }
  return operand.place().resultType();
}

/// \brief Extracts the result type from an rvalue.
static identity::SemanticTypeId rvalueResultType(const MirRvalue& rvalue) {
  switch (rvalue.kind()) {
    case MirRvalueKind::Use:
      return operandResultType(rvalue.useValue().operand);
    case MirRvalueKind::NominalAggregate:
      return rvalue.nominalAggregateValue().type;
    case MirRvalueKind::Comparison:
      return rvalue.comparisonValue().resultType;
    case MirRvalueKind::Arithmetic:
      return rvalue.arithmeticValue().resultType;
    case MirRvalueKind::Erase:
      return rvalue.eraseValue().targetType;
    case MirRvalueKind::ErrorUnion:
      return rvalue.errorUnionValue().unionType;
    case MirRvalueKind::ExtractField:
      return rvalue.extractFieldValue().resultType;
  }
  ZC_UNREACHABLE
}

bool validateStructuralWellFormedness(const MirFunction& function) {
  // Dense contiguous ordinals: ids are 1-indexed and match vector position.
  for (size_t i = 0; i < function.sourceScopes.size(); ++i) {
    if (function.sourceScopes[i].id != scopeId(static_cast<uint32_t>(i + 1))) return false;
  }
  for (size_t i = 0; i < function.locals.size(); ++i) {
    if (function.locals[i].id != localId(static_cast<uint32_t>(i + 1))) return false;
  }
  for (size_t i = 0; i < function.blocks.size(); ++i) {
    if (function.blocks[i].id != blockId(static_cast<uint32_t>(i + 1))) return false;
  }
  // Scope parent references: every parent must have a lower ordinal.
  for (const auto& scope : function.sourceScopes) {
    ZC_IF_SOME(parent, scope.parent) {
      bool found = false;
      for (const auto& candidate : function.sourceScopes) {
        if (candidate.id == parent) {
          found = true;
          break;
        }
      }
      if (!found) return false;
    }
  }
  // Local and block sourceScope references must resolve.
  for (const auto& local : function.locals) {
    bool found = false;
    for (const auto& scope : function.sourceScopes) {
      if (scope.id == local.sourceScope) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  for (const auto& block : function.blocks) {
    bool found = false;
    for (const auto& scope : function.sourceScopes) {
      if (scope.id == block.sourceScope) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  // Parameter locals must precede all non-parameter locals.
  bool seenNonParameter = false;
  for (const auto& local : function.locals) {
    if (local.kind == MirLocalKind::Parameter) {
      if (seenNonParameter) return false;
    } else {
      seenNonParameter = true;
    }
  }
  // Reachable CFG: every block must be reachable from block 0.
  if (function.blocks.empty()) return false;
  zc::Vector<bool> reached;
  reached.resize(function.blocks.size());
  for (size_t i = 0; i < function.blocks.size(); ++i) { reached[i] = false; }
  zc::Vector<uint32_t> worklist;
  worklist.add(0);
  reached[0] = true;
  while (!worklist.empty()) {
    const uint32_t index = worklist[worklist.size() - 1];
    worklist.removeLast();
    const auto& block = function.blocks[index];
    const auto addTarget = [&](MirBlockId target) {
      for (size_t i = 0; i < function.blocks.size(); ++i) {
        if (function.blocks[i].id == target && !reached[i]) {
          reached[i] = true;
          worklist.add(static_cast<uint32_t>(i));
        }
      }
    };
    const auto& terminator = block.terminator;
    if (terminator.kind() == MirTerminatorKind::Call) {
      addTarget(terminator.callValue().normalTarget);
      ZC_IF_SOME(unwind, terminator.callValue().unwindTarget) { addTarget(unwind); }
    } else if (terminator.kind() == MirTerminatorKind::Goto) {
      addTarget(terminator.gotoValue().target);
    } else if (terminator.kind() == MirTerminatorKind::SwitchInt) {
      for (const auto& arm : terminator.switchIntValue().arms) { addTarget(arm.target); }
      addTarget(terminator.switchIntValue().defaultTarget);
    }
  }
  for (bool r : reached) {
    if (!r) return false;
  }
  // StorageLive before first use: every non-parameter local must have a
  // StorageLive statement before any statement or terminator that references
  // it. This is a linear scan over block order; it is a conservative
  // approximation that catches missing StorageLive without a full dataflow
  // analysis (the ownership overlay in RFC 0007 owns precise liveness).
  zc::Vector<bool> storageLive;
  storageLive.resize(function.locals.size());
  for (size_t i = 0; i < function.locals.size(); ++i) {
    if (function.locals[i].kind == MirLocalKind::Parameter) {
      storageLive[i] = true;  // Parameters are live from entry.
    } else {
      storageLive[i] = false;
    }
  }
  const auto checkPlace = [&](const MirPlace& place) -> bool {
    for (size_t i = 0; i < function.locals.size(); ++i) {
      if (function.locals[i].id == place.local() && !storageLive[i]) return false;
    }
    return true;
  };
  const auto checkOperand = [&](const MirOperand& operand) -> bool {
    if (operand.kind() == MirOperandKind::Constant) return true;
    return checkPlace(operand.place());
  };
  const auto checkRvalue = [&](const MirRvalue& rvalue) -> bool {
    switch (rvalue.kind()) {
      case MirRvalueKind::Use:
        return checkOperand(rvalue.useValue().operand);
      case MirRvalueKind::NominalAggregate: {
        for (const auto& element : rvalue.nominalAggregateValue().elements) {
          if (!checkOperand(element.operand)) return false;
        }
        return true;
      }
      case MirRvalueKind::Comparison:
        return checkOperand(rvalue.comparisonValue().left) &&
               checkOperand(rvalue.comparisonValue().right);
      case MirRvalueKind::Arithmetic:
        return checkOperand(rvalue.arithmeticValue().left) &&
               checkOperand(rvalue.arithmeticValue().right);
      case MirRvalueKind::Erase:
        return checkOperand(rvalue.eraseValue().source);
      case MirRvalueKind::ErrorUnion:
        return checkOperand(rvalue.errorUnionValue().payload);
      case MirRvalueKind::ExtractField:
        return checkOperand(rvalue.extractFieldValue().source);
    }
    ZC_UNREACHABLE
  };
  for (const auto& block : function.blocks) {
    for (const auto& statement : block.statements) {
      if (statement.kind() == MirStatementKind::StorageLive) {
        const auto liveLocal = statement.storageLocal();
        for (size_t i = 0; i < function.locals.size(); ++i) {
          if (function.locals[i].id == liveLocal) {
            storageLive[i] = true;
            break;
          }
        }
        continue;
      }
      if (statement.kind() == MirStatementKind::Assign) {
        const auto& assign = statement.assignmentValue();
        if (!checkPlace(assign.destination)) return false;
        if (!checkRvalue(assign.value)) return false;
        // Type agreement: the rvalue result type must match the destination.
        if (rvalueResultType(assign.value) != assign.destination.resultType()) return false;
        // Internal operand type agreement for binary rvalues: comparison and
        // arithmetic require same-typed operands, and arithmetic preserves the
        // operand type as its result.
        if (assign.value.kind() == MirRvalueKind::Comparison) {
          const auto& comp = assign.value.comparisonValue();
          if (operandResultType(comp.left) != operandResultType(comp.right)) return false;
        } else if (assign.value.kind() == MirRvalueKind::Arithmetic) {
          const auto& arith = assign.value.arithmeticValue();
          if (operandResultType(arith.left) != operandResultType(arith.right)) return false;
          if (arith.resultType != operandResultType(arith.left)) return false;
        }
      } else if (statement.kind() == MirStatementKind::BorrowCreation) {
        const auto& borrow = statement.borrowCreationValue();
        if (!checkPlace(borrow.destination)) return false;
        if (!checkPlace(borrow.source)) return false;
      } else if (statement.kind() == MirStatementKind::SetDiscriminant) {
        if (!checkPlace(statement.setDiscriminantValue().destination)) return false;
      } else if (statement.kind() == MirStatementKind::Deinitialize) {
        if (!checkPlace(statement.deinitializeValue().destination)) return false;
      }
    }
    // Terminator operand liveness and return type agreement.
    const auto& terminator = block.terminator;
    if (terminator.kind() == MirTerminatorKind::Return) {
      ZC_IF_SOME(value, terminator.returnValue().value) {
        if (!checkOperand(value)) return false;
        if (operandResultType(value) != function.resultType) return false;
      }
    } else if (terminator.kind() == MirTerminatorKind::Call) {
      const auto& call = terminator.callValue();
      for (const auto& argument : call.arguments) {
        if (!checkOperand(argument)) return false;
      }
      if (!checkPlace(call.destination)) return false;
    } else if (terminator.kind() == MirTerminatorKind::SwitchInt) {
      if (!checkOperand(terminator.switchIntValue().discriminant)) return false;
    } else if (terminator.kind() == MirTerminatorKind::IndirectCall) {
      const auto& call = terminator.indirectCallValue();
      if (!checkOperand(call.receiver)) return false;
      for (const auto& argument : call.arguments) {
        if (!checkOperand(argument)) return false;
      }
      if (!checkPlace(call.destination)) return false;
    }
  }
  return true;
}

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
  // A raw-pointer write-read function (`unsafe { *ptr = <lit>; }; return unsafe
  // { *ptr };`) materializes two value nodes on the RHS: the write-value
  // literal expression and the parameter-reference expression. The shared
  // equation credits only the function on the LHS, so each such function
  // contributes one excess value node that this term restores.
  int64_t rawPointerWriteExcess = 0;
  for (const auto& writeFunction : hirModule.functions()) {
    if (writeFunction.writeUnsafeBlock != zc::none) ++rawPointerWriteExcess;
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
  // Destructors whose body is one or more scalar-let statements
  // (`deinit() { let x: T = <lit>; ... }`). Each binding carries one extra
  // expression (the literal initializer) beyond the void-function baseline,
  // so the checksum equation subtracts this count from the right side.
  int64_t voidLetBodyCount = 0;
  for (const auto& voidFunction : hirModule.functions()) {
    auto voidBlock = blockFor(hirModule, voidFunction.body);
    if (voidBlock == zc::none) continue;
    const auto& block = ZC_ASSERT_NONNULL(voidBlock);
    if (block.statements.size() == 0) {
      ++voidFunctionCount;
      continue;
    }
    const hir::HirNodeId voidTrailing = block.statements[block.statements.size() - 1];
    if (returnFor(hirModule, voidTrailing) == zc::none) {
      ++voidFunctionCount;
      for (size_t i = 0; i < block.statements.size(); ++i) {
        auto sourceLocal = localFor(hirModule, block.statements[i]);
        ZC_IF_SOME(local, sourceLocal) {
          if (local.initializer != zc::none &&
              expressionFor(hirModule, ZC_ASSERT_NONNULL(local.initializer)) != zc::none) {
            ++voidLetBodyCount;
          }
        }
      }
    }
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
      // Conditional arms that are local references do not materialize value
      // nodes on the RHS, so each one subtracts one credit from the
      // conditional*2 LHS term.
      const auto& cond = ZC_ASSERT_NONNULL(conditional);
      for (const auto armValue : {cond.thenReturnValue, cond.elseReturnValue}) {
        if (expressionFor(hirModule, armValue) == zc::none &&
            aggregateFor(hirModule, armValue) == zc::none &&
            parameterReferenceFor(hirModule, armValue) == zc::none) {
          --valueNodes;
        }
      }
      leadingLocalConditionalValueNodeExcess += valueNodes - binaryBindings - 1;
    }
  }
  // Binary-write local operands: each local reference that is an operand of a
  // binary write value (`x = x + 1`) materializes a localReference, not a value
  // node, so it joins the RHS checksum as a credit like parameterReturnCount.
  // A nested binary operand is recursively inspected for local references.
  int64_t binaryWriteLocalOperandCount = 0;
  for (const auto& write : hirModule.localWrites()) {
    auto binary = primitiveBinaryFor(hirModule, write.value);
    ZC_IF_SOME(binaryValue, binary) {
      zc::Vector<hir::HirNodeId> pending;
      pending.add(binaryValue.left);
      pending.add(binaryValue.right);
      for (size_t i = 0; i < pending.size(); ++i) {
        const auto operand = pending[i];
        if (localReferenceFor(hirModule, operand) != zc::none) {
          ++binaryWriteLocalOperandCount;
          continue;
        }
        auto nested = primitiveBinaryFor(hirModule, operand);
        ZC_IF_SOME(nestedValue, nested) {
          pending.add(nestedValue.left);
          pending.add(nestedValue.right);
        }
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
  // Comparison-condition loop-body functions (`mut x = <lit>; while (<ident>
  // <cmp> <lit>) { <writes> } return x;` or the do-while equivalent) carry a
  // checksum imbalance: the comparison primitive binary adds one lhs node,
  // but the return is a local reference (not a value node), so the rhs lacks
  // the matching node that a for-loop's literal return would contribute. Each
  // such function adds one rhs term.
  int64_t comparisonLoopBodyReturnCount = 0;
  for (const auto& doWhileFunction : hirModule.functions()) {
    auto doWhileBlock = blockFor(hirModule, doWhileFunction.body);
    ZC_IF_SOME(block, doWhileBlock) {
      if (block.statements.size() != 3) continue;
      auto sourceLocal = localFor(hirModule, block.statements[0]);
      auto loop = loopFor(hirModule, block.statements[1]);
      auto sourceReturn = returnFor(hirModule, block.statements[2]);
      if (sourceLocal == zc::none || loop == zc::none || sourceReturn == zc::none) continue;
      ZC_IF_SOME(loopValue, loop) {
        ZC_IF_SOME(returnStatement, sourceReturn) {
          if (primitiveBinaryFor(hirModule, loopValue.condition) != zc::none &&
              localReferenceFor(hirModule, returnStatement.value) != zc::none) {
            ++comparisonLoopBodyReturnCount;
          }
        }
      }
    }
  }
  const int64_t lhsChecksum =
      static_cast<int64_t>(hirModule.declarations().size() + hirModule.functions().size() +
                           hirModule.conditionals().size() * 2 +
                           hirModule.primitiveBinaryOperations().size() +
                           hirModule.loops().size()) +
      sequentialValueNodeExcess + directAggregateCallValueNodes +
      leadingLocalConditionalValueNodeExcess + forLoopAccumulatorCorrection + rawPointerWriteExcess;
  const int64_t rhsChecksum =
      static_cast<int64_t>(hirModule.expressions().size() + hirModule.calls().size() +
                           hirModule.aggregates().size() + uninitializedLocalReturnCount +
                           parameterReturnCount + parameterReborrowCount +
                           hirModule.parameterFieldProjections().size() +
                           receiverSelfCallValueNodes + voidFunctionCount - voidLetBodyCount -
                           localAliasReborrowCount - hirModule.localWrites().size() -
                           hirModule.parameterFieldWrites().size()) +
      binaryWriteLocalOperandCount + comparisonLoopBodyReturnCount;
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
                      const detail::LoopContext loopCtx{zc::none,     // label
                                                        blockId(2),   // header
                                                        blockId(3),   // body
                                                        blockId(4),   // exit
                                                        blockId(2)};  // continueTarget (header)
                      arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                                               loopCtx.body});
                      zc::Vector<MirStatement> exitStatements;
                      // A trailing break exits the body to the loop exit; a
                      // trailing continue or a write-only body jumps back to
                      // the header (the reducible back-edge).
                      const auto bodyTerminatorTarget =
                          loopValue.breakSpan != zc::none ? loopCtx.exit : loopCtx.continueTarget;
                      const auto& bodyTerminatorSpan =
                          loopValue.breakSpan != zc::none
                              ? ZC_ASSERT_NONNULL(loopValue.breakSpan)
                              : (loopValue.continueSpan != zc::none
                                     ? ZC_ASSERT_NONNULL(loopValue.continueSpan)
                                     : loopValue.sourceSpan);
                      zc::Vector<MirBasicBlock> blocks;
                      blocks.add(MirBasicBlock{
                          blockId(1), scopeId(1), zc::mv(entryStatements),
                          MirTerminator::gotoTarget(loopCtx.header, loopValue.sourceSpan.clone())});
                      blocks.add(MirBasicBlock{
                          loopCtx.header, scopeId(1), zc::Vector<MirStatement>{},
                          MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                   zc::mv(arms), loopCtx.exit,
                                                   loopValue.sourceSpan.clone())});
                      blocks.add(
                          MirBasicBlock{loopCtx.body, scopeId(1), zc::mv(bodyStatements),
                                        MirTerminator::gotoTarget(bodyTerminatorTarget,
                                                                  bodyTerminatorSpan.clone())});
                      blocks.add(MirBasicBlock{
                          loopCtx.exit, scopeId(1), zc::mv(exitStatements),
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
      // Do-while loop-body composite body: `mut x = <lit>; do { <writes to x>
      // } while (<ident> <cmp> <lit>); return x;`. Lowers to a reducible
      // four-block CFG with body-before-condition layout. Parameters occupy
      // localId(1..P); the user local x is localId(P+1); the comparison temp
      // is localId(P+2). The entry block declares and initializes x then jumps
      // to the body; the body carries the write assignments (Overwrite) then
      // jumps to the condition block; the condition block evaluates the
      // comparison into the temp and switches on it back to the body (true,
      // the reducible back-edge) or the exit (default); the exit returns a
      // place-use of x.
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
              auto conditionBinary = primitiveBinaryFor(hirModule, loopValue.condition);
              const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
              const auto userLocalId = localId(parameterCount + 1);
              const auto conditionTempId = localId(parameterCount + 2);
              ZC_IF_SOME(initialValue, initializer) {
                ZC_IF_SOME(localReference, reference) {
                  ZC_IF_SOME(condition, conditionBinary) {
                    const auto comparisonOperator = mirComparisonOperatorFor(condition.operation);
                    if (local.initializer != zc::none && local.local == localReference.local &&
                        local.type == declaration.resultType && initialValue.type == local.type &&
                        localReference.type == local.type &&
                        localReference.category == hir::HirValueCategory::Place &&
                        comparisonOperator != zc::none && condition.operandType == local.type &&
                        definition != zc::none) {
                      auto conditionLeft = localReferenceFor(hirModule, condition.left);
                      auto conditionRight = expressionFor(hirModule, condition.right);
                      ZC_IF_SOME(condLeft, conditionLeft) {
                        ZC_IF_SOME(condRight, conditionRight) {
                          if (condLeft.local != local.local || condLeft.type != local.type ||
                              condRight.type != local.type) {
                            // Not a do-while shape; fall through to the next
                            // arm.
                          } else {
                            // Build the loop-body write assignments. Each write
                            // is an Overwrite of x whose value is a literal
                            // constant, a copy place-use of a parameter, or an
                            // Arithmetic/Comparison rvalue, reusing the flat
                            // mut-local write lowering.
                            zc::Vector<MirStatement> bodyStatements;
                            bool built = true;
                            for (size_t writeIndex = 0; built && writeIndex < loopValue.body.size();
                                 ++writeIndex) {
                              auto write = localWriteFor(hirModule, loopValue.body[writeIndex]);
                              if (write == zc::none ||
                                  ZC_ASSERT_NONNULL(write).local != local.local ||
                                  ZC_ASSERT_NONNULL(write).field != zc::none ||
                                  ZC_ASSERT_NONNULL(write).type != local.type ||
                                  ZC_ASSERT_NONNULL(write).kind !=
                                      hir::HirLocalWriteKind::Overwrite) {
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
                                const auto writeComparisonOperator =
                                    mirComparisonOperatorFor(binaryValue.operation);
                                const auto writeArithmeticOperator =
                                    mirArithmeticOperatorFor(binaryValue.operation);
                                const bool isArithmeticBinary =
                                    writeComparisonOperator == zc::none &&
                                    writeArithmeticOperator != zc::none;
                                if (binaryValue.type != local.type ||
                                    binaryValue.category != hir::HirValueCategory::Value ||
                                    (writeComparisonOperator == zc::none &&
                                     writeArithmeticOperator == zc::none) ||
                                    (isArithmeticBinary && binaryValue.operandType != local.type)) {
                                  built = false;
                                } else {
                                  auto buildOperand =
                                      [&](hir::HirNodeId operandNode) -> zc::Maybe<MirOperand> {
                                    auto operandLiteral = expressionFor(hirModule, operandNode);
                                    ZC_IF_SOME(literalValue, operandLiteral) {
                                      if (literalValue.type != binaryValue.operandType)
                                        return zc::none;
                                      return MirOperand::constant(binaryValue.operandType,
                                                                  literalValue.value.clone());
                                    }
                                    auto operandParameter =
                                        parameterReferenceFor(hirModule, operandNode);
                                    ZC_IF_SOME(parameter, operandParameter) {
                                      if (parameter.type != binaryValue.operandType)
                                        return zc::none;
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
                                          MirPlace(
                                              localId(static_cast<uint32_t>(parameterIndex) + 1),
                                              binaryValue.operandType, zc::mv(projections),
                                              binaryValue.operandType));
                                    }
                                    // A local operand reads the written user
                                    // local (`x = x + 1`): a copy place-use
                                    // of that local.
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
                                                       ZC_ASSERT_NONNULL(writeArithmeticOperator),
                                                       zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                                       zc::mv(ZC_ASSERT_NONNULL(rightOperand)),
                                                       binaryValue.type)
                                                 : MirRvalue::comparison(
                                                       ZC_ASSERT_NONNULL(writeComparisonOperator),
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
                                  zc::mv(ZC_ASSERT_NONNULL(rvalue)),
                                  MirInitializationKind::Overwrite,
                                  ZC_ASSERT_NONNULL(write).sourceSpan.clone()));
                            }
                            if (!built) {
                              return rejectMir<BuiltMirCandidate>(
                                  ir::IrFailurePhase::MirConstruction,
                                  ir::IrFailureKind::InvalidFact, module, declaration.definition,
                                  identities, static_cast<uint32_t>(pending.size() + 1));
                            }
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
                            locals.add(MirLocalDeclaration{userLocalId, MirLocalKind::UserLocal,
                                                           local.type, scopeId(1),
                                                           local.sourceSpan.clone()});
                            locals.add(MirLocalDeclaration{conditionTempId, MirLocalKind::Temporary,
                                                           condition.type, scopeId(1),
                                                           condition.sourceSpan.clone()});
                            // Entry: StorageLive(x), StorageLive(temp),
                            // Assign(x = init, Initialize), Goto(bb2).
                            zc::Vector<MirStatement> entryStatements;
                            entryStatements.add(
                                MirStatement::storageLive(userLocalId, local.sourceSpan.clone()));
                            entryStatements.add(MirStatement::storageLive(
                                conditionTempId, condition.sourceSpan.clone()));
                            zc::Vector<MirProjection> initializeProjections;
                            entryStatements.add(MirStatement::assign(
                                MirPlace(userLocalId, local.type, zc::mv(initializeProjections),
                                         local.type),
                                MirRvalue::use(
                                    MirOperand::constant(local.type, initialValue.value.clone())),
                                MirInitializationKind::Initialize,
                                initialValue.sourceSpan.clone()));
                            // Condition: Assign(temp = Comparison(op, copy(x),
                            // <lit>), Overwrite), SwitchInt(copy(temp),
                            // [true->bb2], default=bb4).
                            //
                            // The condition block is the loop header: the body
                            // jumps back to it, so the fixpoint iteration in
                            // the ownership facts processes it more than once.
                            // Overwrite is required because the temp may already
                            // be initialized on the second and later passes.
                            zc::Vector<MirProjection> condLeftProjections;
                            auto condLeftOperand = placeUse(
                                proofs, copy,
                                MirPlace(userLocalId, condition.operandType,
                                         zc::mv(condLeftProjections), condition.operandType));
                            if (condLeftOperand == zc::none) {
                              return rejectMir<BuiltMirCandidate>(
                                  ir::IrFailurePhase::MirConstruction,
                                  ir::IrFailureKind::InvalidFact, module, declaration.definition,
                                  identities, static_cast<uint32_t>(pending.size() + 1));
                            }
                            zc::Vector<MirStatement> conditionStatements;
                            zc::Vector<MirProjection> tempProjections;
                            conditionStatements.add(MirStatement::assign(
                                MirPlace(conditionTempId, condition.type, zc::mv(tempProjections),
                                         condition.type),
                                MirRvalue::comparison(
                                    ZC_ASSERT_NONNULL(comparisonOperator),
                                    zc::mv(ZC_ASSERT_NONNULL(condLeftOperand)),
                                    MirOperand::constant(condRight.type, condRight.value.clone()),
                                    condition.type),
                                MirInitializationKind::Overwrite, condition.sourceSpan.clone()));
                            zc::Vector<MirProjection> discriminantProjections;
                            auto discriminant =
                                placeUse(proofs, copy,
                                         MirPlace(conditionTempId, condition.type,
                                                  zc::mv(discriminantProjections), condition.type));
                            if (discriminant == zc::none) {
                              return rejectMir<BuiltMirCandidate>(
                                  ir::IrFailurePhase::MirConstruction,
                                  ir::IrFailureKind::InvalidFact, module, declaration.definition,
                                  identities, static_cast<uint32_t>(pending.size() + 1));
                            }
                            // Resolve loop block layout: do-while has body at
                            // bb2 and condition at bb3; while swaps them.
                            const detail::LoopContext loopCtx{
                                zc::none, loopValue.isDoWhile ? blockId(3) : blockId(2),
                                loopValue.isDoWhile ? blockId(2) : blockId(3), blockId(4),
                                loopValue.isDoWhile ? blockId(3) : blockId(2)};
                            zc::Vector<MirSwitchIntArm> arms;
                            arms.add(MirSwitchIntArm{
                                checker::checked::CanonicalConstValue::boolean(true),
                                loopCtx.body});
                            // A trailing break exits to the loop exit; a
                            // trailing continue or a write-only body jumps to
                            // the continue target.
                            const auto bodyTerminatorTarget = loopValue.breakSpan != zc::none
                                                                  ? loopCtx.exit
                                                                  : loopCtx.continueTarget;
                            const auto& bodyTerminatorSpan =
                                loopValue.breakSpan != zc::none
                                    ? ZC_ASSERT_NONNULL(loopValue.breakSpan)
                                    : (loopValue.continueSpan != zc::none
                                           ? ZC_ASSERT_NONNULL(loopValue.continueSpan)
                                           : loopValue.sourceSpan);
                            zc::Vector<MirProjection> returnProjections;
                            auto returnOperand =
                                placeUse(proofs, copy,
                                         MirPlace(userLocalId, local.type,
                                                  zc::mv(returnProjections), local.type));
                            if (returnOperand == zc::none) {
                              return rejectMir<BuiltMirCandidate>(
                                  ir::IrFailurePhase::MirConstruction,
                                  ir::IrFailureKind::InvalidFact, module, declaration.definition,
                                  identities, static_cast<uint32_t>(pending.size() + 1));
                            }
                            zc::Vector<MirStatement> exitStatements;
                            zc::Vector<MirBasicBlock> blocks;
                            blocks.add(
                                MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                              MirTerminator::gotoTarget(
                                                  blockId(2), loopValue.sourceSpan.clone())});
                            if (loopValue.isDoWhile) {
                              // Do-while: bb2=body, bb3=condition.
                              blocks.add(MirBasicBlock{
                                  blockId(2), scopeId(1), zc::mv(bodyStatements),
                                  MirTerminator::gotoTarget(bodyTerminatorTarget,
                                                            bodyTerminatorSpan.clone())});
                              blocks.add(MirBasicBlock{
                                  blockId(3), scopeId(1), zc::mv(conditionStatements),
                                  MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                           zc::mv(arms), loopCtx.exit,
                                                           loopValue.sourceSpan.clone())});
                            } else {
                              // While: bb2=condition, bb3=body.
                              blocks.add(MirBasicBlock{
                                  blockId(2), scopeId(1), zc::mv(conditionStatements),
                                  MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                           zc::mv(arms), loopCtx.exit,
                                                           loopValue.sourceSpan.clone())});
                              blocks.add(MirBasicBlock{
                                  blockId(3), scopeId(1), zc::mv(bodyStatements),
                                  MirTerminator::gotoTarget(bodyTerminatorTarget,
                                                            bodyTerminatorSpan.clone())});
                            }
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
                                      const detail::LoopContext loopCtx{
                                          zc::none, blockId(2), blockId(3), blockId(4), blockId(2)};
                                      zc::Vector<MirSwitchIntArm> arms;
                                      arms.add(MirSwitchIntArm{
                                          checker::checked::CanonicalConstValue::boolean(true),
                                          loopCtx.body});
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
                                      // A trailing break exits to the loop exit;
                                      // a trailing continue or a write-only body
                                      // jumps back to the header (the reducible
                                      // back-edge).
                                      const auto bodyTerminatorTarget =
                                          loopValue.breakSpan != zc::none ? loopCtx.exit
                                                                          : loopCtx.continueTarget;
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
                                              loopCtx.exit, loopValue.sourceSpan.clone())});
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
                                                                      // Outer loop:
                                                                      // header=bb2,
                                                                      // body=bb3,
                                                                      // exit=bb7,
                                                                      // continue=bb6.
                                                                      const detail::LoopContext
                                                                          outerCtx{zc::none,
                                                                                   blockId(2),
                                                                                   blockId(3),
                                                                                   blockId(7),
                                                                                   blockId(6)};
                                                                      // Inner loop:
                                                                      // header=bb4,
                                                                      // body=bb5,
                                                                      // exit=bb6,
                                                                      // continue=bb4.
                                                                      const detail::LoopContext
                                                                          innerCtx{zc::none,
                                                                                   blockId(4),
                                                                                   blockId(5),
                                                                                   blockId(6),
                                                                                   blockId(4)};
                                                                      zc::Vector<MirSwitchIntArm>
                                                                          outerArms;
                                                                      outerArms.add(MirSwitchIntArm{
                                                                          checker::checked::
                                                                              CanonicalConstValue::
                                                                                  boolean(true),
                                                                          outerCtx.body});
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
                                                                          innerCtx.body});
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
                                                                      // A labeled break/continue
                                                                      // targeting the outer loop
                                                                      // skips the inner update
                                                                      // write and re-compare
                                                                      // entirely.
                                                                      const bool innerBreakTargetsOuter =
                                                                          innerLoopValue
                                                                              .breakTargetsEnclosing;
                                                                      const bool
                                                                          innerContinueTargetsOuter =
                                                                              innerLoopValue
                                                                                  .continueTargetsEnclosing;
                                                                      if (!innerBreakTargetsOuter &&
                                                                          !innerContinueTargetsOuter) {
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
                                                                              declaration
                                                                                  .definition,
                                                                              identities,
                                                                              static_cast<uint32_t>(
                                                                                  pending.size() +
                                                                                  1));
                                                                        }
                                                                        zc::Vector<MirProjection>
                                                                            innerUwOverwriteProjections;
                                                                        innerBodyStatements.add(MirStatement::assign(
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
                                                                                    innerInit
                                                                                        .type));
                                                                        if (innerBodyCondLeftOperand ==
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
                                                                        zc::Vector<MirProjection>
                                                                            innerBodyTempProjections;
                                                                        innerBodyStatements.add(MirStatement::assign(
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
                                                                      }
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
                                                                              outerCtx.exit,
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
                                                                              innerCtx.exit,
                                                                              innerLoopValue
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(5), scopeId(1),
                                                                          zc::mv(
                                                                              innerBodyStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              innerBreakTargetsOuter
                                                                                  ? outerCtx.exit
                                                                                  : (innerContinueTargetsOuter
                                                                                         ? outerCtx
                                                                                               .continueTarget
                                                                                         : innerCtx
                                                                                               .continueTarget),
                                                                              innerLoopValue
                                                                                  .sourceSpan
                                                                                  .clone())});
                                                                      blocks.add(MirBasicBlock{
                                                                          blockId(6), scopeId(1),
                                                                          zc::mv(
                                                                              outerContStatements),
                                                                          MirTerminator::gotoTarget(
                                                                              outerCtx.header,
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
                                            const detail::LoopContext loopCtx{
                                                zc::none, blockId(2), bodyBlockId, exitBlockId,
                                                blockId(2)};
                                            // A trailing break exits the
                                            // body to the loop exit; a
                                            // trailing continue or a
                                            // write-only body jumps back
                                            // to the header (the reducible
                                            // back-edge).
                                            const auto bodyTerminatorTarget =
                                                loopValue.breakSpan != zc::none
                                                    ? loopCtx.exit
                                                    : loopCtx.continueTarget;
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
      if (declaration.returnsErrorUnwrapCall) {
        // Forced-unwrap (`!!`) return: the call produces an error union; a
        // 4-block CFG extracts the tag, compares it with zero (success), and
        // either traps (Unreachable) or extracts the payload and returns it.
        const auto tagType = ZC_ASSERT_NONNULL(declaration.errorUnwrapTagType);
        const auto boolType = ZC_ASSERT_NONNULL(declaration.errorUnwrapBoolType);
        const uint32_t parameterCount = static_cast<uint32_t>(declaration.parameters.size());
        const auto temporaryLocal = localId(parameterCount + 1);
        const auto tagTempLocal = localId(parameterCount + 2);
        const auto boolTempLocal = localId(parameterCount + 3);
        const auto resultLocalId = localId(parameterCount + 4);
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
                                       directCall.resultType, scopeId(1),
                                       directCall.sourceSpan.clone()});
        locals.add(MirLocalDeclaration{tagTempLocal, MirLocalKind::Temporary, tagType, scopeId(1),
                                       directCall.sourceSpan.clone()});
        locals.add(MirLocalDeclaration{boolTempLocal, MirLocalKind::Temporary, boolType, scopeId(1),
                                       directCall.sourceSpan.clone()});
        locals.add(MirLocalDeclaration{resultLocalId, MirLocalKind::FunctionResult,
                                       declaration.resultType, scopeId(1), returnSpan.clone()});
        // Block 1 (entry): StorageLive(T) + Call(dest=T, continuation=block2).
        zc::Vector<MirStatement> entryStatements;
        entryStatements.add(
            MirStatement::storageLive(temporaryLocal, directCall.sourceSpan.clone()));
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
                                MirPlace(temporaryLocal, directCall.resultType,
                                         zc::mv(destinationProjections), directCall.resultType),
                                blockId(2), zc::mv(noUnwind), directCall.sourceSpan.clone());
        // Block 2 (test): ExtractField(tag from T, field 0) + Comparison(Eq,
        // tag, 0) + SwitchInt(bool, [true->block4, false->block3]).
        zc::Vector<MirStatement> testStatements;
        testStatements.add(MirStatement::storageLive(tagTempLocal, returnSpan.clone()));
        zc::Vector<MirProjection> tagSourceProjections;
        testStatements.add(MirStatement::assign(
            MirPlace(tagTempLocal, tagType, zc::Vector<MirProjection>{}, tagType),
            MirRvalue::extractField(
                MirOperand::move(MirPlace(temporaryLocal, directCall.resultType,
                                          zc::mv(tagSourceProjections), directCall.resultType)),
                0, tagType),
            MirInitializationKind::Initialize, returnSpan.clone()));
        testStatements.add(MirStatement::storageLive(boolTempLocal, returnSpan.clone()));
        zc::Vector<MirProjection> tagUseProjections;
        auto tagOperand = placeUse(
            proofs, copy, MirPlace(tagTempLocal, tagType, zc::mv(tagUseProjections), tagType));
        if (tagOperand == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        auto zeroConstant =
            checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
                checker::signature::IntegerSign::NonNegative, zc::Array<uint8_t>{}});
        testStatements.add(MirStatement::assign(
            MirPlace(boolTempLocal, boolType, zc::Vector<MirProjection>{}, boolType),
            MirRvalue::comparison(MirComparisonOperator::Eq, zc::mv(ZC_ASSERT_NONNULL(tagOperand)),
                                  MirOperand::constant(tagType, zc::mv(zeroConstant)), boolType),
            MirInitializationKind::Initialize, returnSpan.clone()));
        zc::Vector<MirProjection> discriminantProjections;
        auto discriminant =
            placeUse(proofs, copy,
                     MirPlace(boolTempLocal, boolType, zc::mv(discriminantProjections), boolType));
        if (discriminant == zc::none) {
          return rejectMir<BuiltMirCandidate>(
              ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::InvalidFact, module,
              declaration.definition, identities, static_cast<uint32_t>(pending.size() + 1));
        }
        zc::Vector<MirSwitchIntArm> arms;
        arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(4)});
        arms.add(
            MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
        // Block 4 (success): StorageLive(R) + Assign(R=ExtractField(T, field
        // 1)) + StorageDead(T) + Return(R).
        zc::Vector<MirStatement> successStatements;
        successStatements.add(MirStatement::storageLive(resultLocalId, returnSpan.clone()));
        zc::Vector<MirProjection> payloadSourceProjections;
        successStatements.add(MirStatement::assign(
            MirPlace(resultLocalId, declaration.resultType, zc::Vector<MirProjection>{},
                     declaration.resultType),
            MirRvalue::extractField(
                MirOperand::move(MirPlace(temporaryLocal, directCall.resultType,
                                          zc::mv(payloadSourceProjections), directCall.resultType)),
                1, declaration.resultType),
            MirInitializationKind::Initialize, returnSpan.clone()));
        successStatements.add(MirStatement::storageDead(temporaryLocal, returnSpan.clone()));
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
        blocks.add(
            MirBasicBlock{blockId(2), scopeId(1), zc::mv(testStatements),
                          MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                   zc::mv(arms), blockId(3), returnSpan.clone())});
        blocks.add(MirBasicBlock{blockId(3), scopeId(1), zc::Vector<MirStatement>{},
                                 MirTerminator::unreachable(returnSpan.clone())});
        blocks.add(
            MirBasicBlock{blockId(4), scopeId(1), zc::mv(successStatements),
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
    if (!validateStructuralWellFormedness(function)) {
      return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                         ir::IrFailureKind::InvalidFact, module, function.owner,
                                         identities, static_cast<uint32_t>(index + 1));
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
    // Canonical codec round-trip: every function must encode to the same
    // bytes the builder published, and owners must be in ascending order.
    // Structural well-formedness is already validated above; the recursive
    // MIR builder constructs each function by destination-driven construction,
    // so per-shape positional matching is no longer required.
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
