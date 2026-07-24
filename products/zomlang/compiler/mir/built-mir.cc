// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/mir/built-mir.h"

#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/sha256.h"
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

MirProjection MirProjection::field(identity::DefId field) noexcept {
  return MirProjection(MirFieldProjection{field});
}

MirProjection MirProjection::index(MirLocalId index) noexcept {
  return MirProjection(MirIndexProjection{index});
}

MirProjection MirProjection::dereference() noexcept {
  return MirProjection(MirDereferenceProjection{});
}

MirProjection MirProjection::downcast(identity::DefId variant) noexcept {
  return MirProjection(MirDowncastProjection{variant});
}

zc::Maybe<MirProjection> MirProjection::subslice(uint32_t first, uint32_t pastLast) noexcept {
  if (first > pastLast) return zc::none;
  return MirProjection(MirSubsliceProjection{first, pastLast});
}

MirProjection MirProjection::clone() const noexcept {
  switch (kind()) {
    case MirProjectionKind::Field:
      return field(fieldValue().field);
    case MirProjectionKind::Index:
      return index(indexValue().index);
    case MirProjectionKind::Dereference:
      return dereference();
    case MirProjectionKind::Downcast:
      return downcast(downcastValue().variant);
    case MirProjectionKind::Subslice:
      return MirProjection(MirSubsliceProjection{subsliceValue().first, subsliceValue().pastLast});
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
  Impl(MirLocalId local, zc::Vector<MirProjection>&& projections) noexcept
      : local(local), projections(zc::mv(projections)) {}

  MirLocalId local;
  zc::Vector<MirProjection> projections;
};

MirPlace::MirPlace(MirLocalId local, zc::Vector<MirProjection>&& projections) noexcept
    : impl(zc::heap<Impl>(local, zc::mv(projections))) {}
MirPlace::~MirPlace() noexcept(false) = default;
MirPlace::MirPlace(MirPlace&&) noexcept = default;
MirPlace& MirPlace::operator=(MirPlace&&) noexcept = default;

MirPlace MirPlace::clone() const {
  zc::Vector<MirProjection> projections;
  for (const auto& projection : impl->projections) projections.add(projection.clone());
  return MirPlace(impl->local, zc::mv(projections));
}

MirLocalId MirPlace::local() const noexcept { return impl->local; }

zc::ArrayPtr<const MirProjection> MirPlace::projections() const noexcept {
  return impl->projections.asPtr();
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

MirRvalue MirRvalue::use(MirOperand&& operand) noexcept {
  return MirRvalue(MirUseRvalue{zc::mv(operand)});
}

MirRvalue MirRvalue::clone() const { return use(value.operand.clone()); }

MirRvalueKind MirRvalue::kind() const noexcept { return MirRvalueKind::Use; }

const MirUseRvalue& MirRvalue::useValue() const { return value; }

MirStatement::MirStatement(MirAssignmentStatement&& value) noexcept : value(zc::mv(value)) {}
MirStatement::MirStatement(MirStorageLiveStatement value) noexcept : value(value) {}
MirStatement::MirStatement(MirStorageDeadStatement value) noexcept : value(value) {}
MirStatement::MirStatement(MirBorrowCreationStatement&& value) noexcept : value(zc::mv(value)) {}
MirStatement::MirStatement(MirSetDiscriminantStatement&& value) noexcept : value(zc::mv(value)) {}
MirStatement::MirStatement(MirDeinitializeStatement&& value) noexcept : value(zc::mv(value)) {}

MirStatement MirStatement::assign(MirPlace&& destination, MirRvalue&& value,
                                  MirInitializationKind initialization) noexcept {
  return MirStatement(MirAssignmentStatement{zc::mv(destination), zc::mv(value), initialization});
}

MirStatement MirStatement::storageLive(MirLocalId local) noexcept {
  return MirStatement(MirStorageLiveStatement{local});
}

MirStatement MirStatement::storageDead(MirLocalId local) noexcept {
  return MirStatement(MirStorageDeadStatement{local});
}

MirStatement MirStatement::borrowCreation(MirPlace&& destination, MirBorrowKind kind,
                                          MirPlace&& source) noexcept {
  return MirStatement(MirBorrowCreationStatement{zc::mv(destination), kind, zc::mv(source)});
}

MirStatement MirStatement::setDiscriminant(MirPlace&& destination,
                                           identity::DefId variant) noexcept {
  return MirStatement(MirSetDiscriminantStatement{zc::mv(destination), variant});
}

MirStatement MirStatement::deinitialize(MirPlace&& destination) noexcept {
  return MirStatement(MirDeinitializeStatement{zc::mv(destination)});
}

MirStatement MirStatement::clone() const {
  switch (kind()) {
    case MirStatementKind::Assign: {
      const auto& assignment = assignmentValue();
      return assign(assignment.destination.clone(), assignment.value.clone(),
                    assignment.initialization);
    }
    case MirStatementKind::StorageLive:
      return storageLive(storageLocal());
    case MirStatementKind::StorageDead:
      return storageDead(storageLocal());
    case MirStatementKind::BorrowCreation: {
      const auto& borrow = borrowCreationValue();
      return borrowCreation(borrow.destination.clone(), borrow.kind, borrow.source.clone());
    }
    case MirStatementKind::SetDiscriminant: {
      const auto& discriminant = setDiscriminantValue();
      return setDiscriminant(discriminant.destination.clone(), discriminant.variant);
    }
    case MirStatementKind::Deinitialize:
      return deinitialize(deinitializeValue().destination.clone());
  }
  ZC_UNREACHABLE
}

MirStatementKind MirStatement::kind() const noexcept {
  if (value.is<MirAssignmentStatement>()) return MirStatementKind::Assign;
  if (value.is<MirStorageLiveStatement>()) return MirStatementKind::StorageLive;
  if (value.is<MirStorageDeadStatement>()) return MirStatementKind::StorageDead;
  if (value.is<MirBorrowCreationStatement>()) return MirStatementKind::BorrowCreation;
  if (value.is<MirSetDiscriminantStatement>()) return MirStatementKind::SetDiscriminant;
  return MirStatementKind::Deinitialize;
}

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

MirTerminator::MirTerminator(MirReturnTerminator&& value) noexcept : value(zc::mv(value)) {}
MirTerminator::MirTerminator(MirUnreachableTerminator value) noexcept : value(value) {}

MirTerminator MirTerminator::returnValue(MirOperand&& value) noexcept {
  zc::Maybe<MirOperand> result = zc::mv(value);
  return MirTerminator(MirReturnTerminator{zc::mv(result)});
}

MirTerminator MirTerminator::returnVoid() noexcept {
  zc::Maybe<MirOperand> value;
  return MirTerminator(MirReturnTerminator{zc::mv(value)});
}

MirTerminator MirTerminator::unreachable() noexcept {
  return MirTerminator(MirUnreachableTerminator{});
}

MirTerminator MirTerminator::clone() const {
  if (kind() == MirTerminatorKind::Unreachable) return unreachable();
  ZC_IF_SOME(operand, returnValue().value) { return MirTerminator::returnValue(operand.clone()); }
  return returnVoid();
}

MirTerminatorKind MirTerminator::kind() const noexcept {
  return value.is<MirReturnTerminator>() ? MirTerminatorKind::Return
                                         : MirTerminatorKind::Unreachable;
}

const MirReturnTerminator& MirTerminator::returnValue() const {
  return value.get<MirReturnTerminator>();
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

class RegistryIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit RegistryIdentityResolver(
      const identity::SemanticIdentityRegistrySet& registries) noexcept
      : registries(registries) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = registries.modules().lookup(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = registries.definitions().lookup(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.encode());
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
  const identity::SemanticIdentityRegistrySet& registries;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectMir(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    zc::Maybe<identity::DefId> definition, const identity::SemanticIdentityRegistrySet& registries,
    uint32_t ordinal, zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  RegistryIdentityResolver identities(registries);
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
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, identities);
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
                      const identity::SemanticIdentityRegistrySet& registries) {
  auto key = registries.definitions().lookup(definition);
  if (key == zc::none) return false;
  ZC_IF_SOME(value, key) {
    auto bytes = value.encode();
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
                    const identity::SemanticIdentityRegistrySet& registries,
                    const type::SemanticTypeStore& semanticTypes) {
  auto bytes = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      value, module, registries, semanticTypes);
  if (bytes == zc::none) return false;
  ZC_IF_SOME(record, bytes) {
    encoder.encodeByteString(record.asPtr());
    return true;
  }
  return false;
}

bool encodeProjection(identity::CanonicalEncoder& encoder, const MirProjection& projection,
                      const identity::SemanticIdentityRegistrySet& registries) {
  encoder.encodeUint8(static_cast<uint8_t>(projection.kind()));
  switch (projection.kind()) {
    case MirProjectionKind::Field:
      return encodeDefinition(encoder, projection.fieldValue().field, registries);
    case MirProjectionKind::Index:
      encoder.encodeUint32(projection.indexValue().index.ordinal());
      return projection.indexValue().index.isValid();
    case MirProjectionKind::Dereference:
      return true;
    case MirProjectionKind::Downcast:
      return encodeDefinition(encoder, projection.downcastValue().variant, registries);
    case MirProjectionKind::Subslice:
      encoder.encodeUint32(projection.subsliceValue().first);
      encoder.encodeUint32(projection.subsliceValue().pastLast);
      return projection.subsliceValue().first <= projection.subsliceValue().pastLast;
  }
  return false;
}

bool encodePlace(identity::CanonicalEncoder& encoder, const MirPlace& place,
                 const identity::SemanticIdentityRegistrySet& registries) {
  if (!place.local().isValid()) return false;
  encoder.encodeUint32(place.local().ordinal());
  encoder.encodeSequenceSize(place.projections().size());
  for (const auto& projection : place.projections()) {
    if (!projection.isStructurallyValid() || !encodeProjection(encoder, projection, registries)) {
      return false;
    }
  }
  return true;
}

bool encodeOperand(identity::CanonicalEncoder& encoder, const MirOperand& operand,
                   identity::ModuleId module,
                   const identity::SemanticIdentityRegistrySet& registries,
                   const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(operand.kind()));
  if (operand.kind() == MirOperandKind::Copy || operand.kind() == MirOperandKind::Move) {
    return encodePlace(encoder, operand.place(), registries);
  }
  const auto& constant = operand.constantValue();
  return encodeType(encoder, constant.type, semanticTypes) &&
         encodeConstant(encoder, constant.value, module, registries, semanticTypes);
}

bool encodeRvalue(identity::CanonicalEncoder& encoder, const MirRvalue& value,
                  identity::ModuleId module,
                  const identity::SemanticIdentityRegistrySet& registries,
                  const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(value.kind()));
  return encodeOperand(encoder, value.useValue().operand, module, registries, semanticTypes);
}

bool encodeStatement(identity::CanonicalEncoder& encoder, const MirStatement& statement,
                     identity::ModuleId module,
                     const identity::SemanticIdentityRegistrySet& registries,
                     const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(statement.kind()));
  switch (statement.kind()) {
    case MirStatementKind::Assign: {
      const auto& assignment = statement.assignmentValue();
      if (!encodePlace(encoder, assignment.destination, registries) ||
          !encodeRvalue(encoder, assignment.value, module, registries, semanticTypes)) {
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
      if (!encodePlace(encoder, borrow.destination, registries)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(borrow.kind));
      return encodePlace(encoder, borrow.source, registries);
    }
    case MirStatementKind::SetDiscriminant: {
      const auto& discriminant = statement.setDiscriminantValue();
      return encodePlace(encoder, discriminant.destination, registries) &&
             encodeDefinition(encoder, discriminant.variant, registries);
    }
    case MirStatementKind::Deinitialize:
      return encodePlace(encoder, statement.deinitializeValue().destination, registries);
  }
  return false;
}

bool encodeTerminator(identity::CanonicalEncoder& encoder, const MirTerminator& terminator,
                      identity::ModuleId module,
                      const identity::SemanticIdentityRegistrySet& registries,
                      const type::SemanticTypeStore& semanticTypes) {
  encoder.encodeUint8(static_cast<uint8_t>(terminator.kind()));
  if (terminator.kind() == MirTerminatorKind::Unreachable) return true;
  ZC_IF_SOME(value, terminator.returnValue().value) {
    encoder.encodeSome();
    return encodeOperand(encoder, value, module, registries, semanticTypes);
  }
  encoder.encodeNone();
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeFunction(
    const MirFunction& function, identity::ModuleId module,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  if (!encodeDefinition(encoder, function.owner, registries)) return zc::none;
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
      if (!encodeStatement(encoder, statement, module, registries, semanticTypes)) {
        return zc::none;
      }
    }
    if (!encodeTerminator(encoder, block.terminator, module, registries, semanticTypes)) {
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

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const identity::SemanticIdentityRegistrySet& registries,
                  const type::SemanticTypeStore& semanticTypes) {
  auto leftRecord = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      left, module, registries, semanticTypes);
  auto rightRecord = checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      right, module, registries, semanticTypes);
  if (leftRecord == zc::none || rightRecord == zc::none) return false;
  bool same = false;
  ZC_IF_SOME(leftBytes, leftRecord) {
    ZC_IF_SOME(rightBytes, rightRecord) { same = leftBytes.asPtr() == rightBytes.asPtr(); }
  }
  return same;
}

bool validScalarFunction(const MirFunction& function, const hir::HirValueDeclaration& declaration,
                         const hir::HirScalarLiteralExpression& expression,
                         identity::ModuleId module,
                         const identity::SemanticIdentityRegistrySet& registries,
                         const type::SemanticTypeStore& semanticTypes) {
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
      block.statements[1].kind() != MirStatementKind::Assign) {
    return false;
  }
  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.initialization != MirInitializationKind::Initialize ||
      assignment.value.kind() != MirRvalueKind::Use ||
      assignment.value.useValue().operand.kind() != MirOperandKind::Constant) {
    return false;
  }
  const auto& constant = assignment.value.useValue().operand.constantValue();
  if (constant.type != expression.type ||
      !sameConstant(constant.value, expression.value, module, registries, semanticTypes) ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn = value.kind() == MirOperandKind::Move && value.place().local() == local.id &&
                  value.place().projections().size() == 0;
  }
  return validReturn;
}

bool validScalarReturnFunction(const MirFunction& function,
                               const hir::HirFunctionDeclaration& declaration,
                               const hir::HirBlockStatement& sourceBlock,
                               const hir::HirReturnStatement& sourceReturn,
                               const hir::HirScalarLiteralExpression& expression,
                               identity::ModuleId module,
                               const identity::SemanticIdentityRegistrySet& registries,
                               const type::SemanticTypeStore& semanticTypes) {
  if (function.owner != declaration.definition || function.kind != MirFunctionKind::Function ||
      function.sourceDefinitionKind != identity::DefinitionKind::Function ||
      function.resultType != declaration.resultType ||
      !sameSpan(function.sourceSpan, declaration.sourceSpan) || function.sourceScopes.size() != 1 ||
      function.locals.size() != 0 || function.blocks.size() != 1 ||
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
      block.sourceScope != scope.id || block.statements.size() != 0 ||
      block.terminator.kind() != MirTerminatorKind::Return ||
      block.terminator.returnValue().value == zc::none) {
    return false;
  }
  bool validReturn = false;
  ZC_IF_SOME(value, block.terminator.returnValue().value) {
    validReturn = value.kind() == MirOperandKind::Constant &&
                  value.constantValue().type == expression.type &&
                  sameConstant(value.constantValue().value, expression.value, module, registries,
                               semanticTypes);
  }
  return validReturn;
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
    const identity::SemanticContextFingerprint& contextFingerprint,
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
    const identity::SemanticContextFingerprint& contextFingerprint,
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
       identity::SemanticContextFingerprint&& contextFingerprint, identity::PackageId package,
       identity::CrateId crate, identity::ModuleId module,
       const checker::checked::CheckedFactsRevision& checkedFactsRevision,
       const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
       const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
       driver::borrow_evidence::VerifiedBorrowEvidenceLease&& borrowEvidenceLease,
       const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository,
       zc::Vector<MirFunction>&& functions, zc::Vector<zc::Array<uint8_t>>&& canonicalFunctions,
       MirRevisionId revision) noexcept
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        package(package),
        crate(crate),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        dispatchFactsRevision(dispatchFactsRevision),
        borrowEvidenceRevision(borrowEvidenceRevision),
        borrowEvidenceLease(zc::mv(borrowEvidenceLease)),
        borrowEvidenceRepository(borrowEvidenceRepository),
        functions(zc::mv(functions)),
        canonicalFunctions(zc::mv(canonicalFunctions)),
        revision(revision) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::PackageId package;
  identity::CrateId crate;
  identity::ModuleId module;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  checker::dispatch::DispatchFactsRevision dispatchFactsRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  driver::borrow_evidence::VerifiedBorrowEvidenceLease borrowEvidenceLease;
  const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository;
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  MirRevisionId revision;
};

VerifiedBuiltMir::VerifiedBuiltMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedBuiltMir::~VerifiedBuiltMir() noexcept(false) = default;
VerifiedBuiltMir::VerifiedBuiltMir(VerifiedBuiltMir&&) noexcept = default;
VerifiedBuiltMir& VerifiedBuiltMir::operator=(VerifiedBuiltMir&&) noexcept = default;

identity::SemanticContextBrand VerifiedBuiltMir::semanticContext() const noexcept {
  return impl->semanticContext;
}

const identity::SemanticContextFingerprint& VerifiedBuiltMir::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}

identity::PackageId VerifiedBuiltMir::package() const noexcept { return impl->package; }
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

const MirRevisionId& VerifiedBuiltMir::revision() const noexcept { return impl->revision; }

zc::ArrayPtr<const MirFunction> VerifiedBuiltMir::functions() const noexcept {
  return impl->functions.asPtr();
}

zc::ArrayPtr<const zc::Array<uint8_t>> VerifiedBuiltMir::canonicalFunctionRecords() const noexcept {
  return impl->canonicalFunctions.asPtr();
}

ir::IrOperationResult<BuiltMirCandidate> BuiltMirBuilder::build(
    const hir::VerifiedHirModule& hirModule) {
  const auto module = hirModule.module();
  const auto& registries = hirModule.registries();
  const auto& semanticTypes = hirModule.semanticTypes();
  const auto evidence =
      hirModule.borrowEvidenceRepository().lookup(hirModule.borrowEvidenceLease());
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != hirModule.borrowEvidenceRevision().digest() ||
      hirModule.borrowEvidenceLease().key().revision.digest() !=
          hirModule.borrowEvidenceRevision().digest() ||
      hirModule.declarations().size() + hirModule.functions().size() !=
          hirModule.expressions().size() ||
      hirModule.functions().size() != hirModule.blocks().size() ||
      hirModule.functions().size() != hirModule.returns().size()) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InputRevisionMismatch, module,
                                        firstDefinition(hirModule), registries, 0);
  }

  zc::Vector<PendingMirFunction> pending;
  for (const auto& declaration : hirModule.declarations()) {
    auto expression = expressionFor(hirModule, declaration.initializer);
    auto definition = registries.definitions().lookup(declaration.definition);
    auto semanticType = semanticTypes.get(declaration.inferredType);
    if (expression == zc::none || definition == zc::none ||
        !semanticType.is<type::SemanticTypeLookup>() ||
        !declaration.definition.belongsTo(hirModule.semanticContext()) ||
        !declaration.inferredType.belongsTo(hirModule.semanticContext())) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, registries, static_cast<uint32_t>(pending.size() + 1));
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
      statements.add(MirStatement::storageLive(localId(1)));
      zc::Vector<MirProjection> destinationProjections;
      auto constant = MirOperand::constant(declaration.inferredType, literal.value.clone());
      statements.add(MirStatement::assign(MirPlace(localId(1), zc::mv(destinationProjections)),
                                          MirRvalue::use(zc::mv(constant)),
                                          MirInitializationKind::Initialize));
      zc::Vector<MirProjection> returnProjections;
      auto returnOperand = MirOperand::move(MirPlace(localId(1), zc::mv(returnProjections)));
      zc::Vector<MirBasicBlock> blocks;
      blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                               MirTerminator::returnValue(zc::mv(returnOperand))});
      MirFunction function{declaration.definition,
                           MirFunctionKind::ModuleInitializer,
                           declaration.definitionKind,
                           declaration.inferredType,
                           declaration.sourceSpan.clone(),
                           zc::mv(scopes),
                           zc::mv(locals),
                           zc::mv(blocks)};
      zc::Array<uint8_t> ownerKey;
      ZC_IF_SOME(key, definition) { ownerKey = key.encode(); }
      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
      continue;
    }
    ZC_UNREACHABLE
  }
  for (const auto& declaration : hirModule.functions()) {
    auto sourceBlock = blockFor(hirModule, declaration.body);
    if (sourceBlock == zc::none) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, registries, static_cast<uint32_t>(pending.size() + 1));
    }
    hir::HirNodeId returnNode;
    ZC_IF_SOME(value, sourceBlock) {
      if (value.statements.size() == 1) returnNode = value.statements[0];
    }
    auto sourceReturn = returnFor(hirModule, returnNode);
    hir::HirNodeId expressionNode;
    ZC_IF_SOME(value, sourceReturn) { expressionNode = value.value; }
    auto expression = expressionFor(hirModule, expressionNode);
    auto definition = registries.definitions().lookup(declaration.definition);
    auto semanticType = semanticTypes.get(declaration.resultType);
    if (sourceReturn == zc::none || expression == zc::none || definition == zc::none ||
        !semanticType.is<type::SemanticTypeLookup>() ||
        !declaration.definition.belongsTo(hirModule.semanticContext()) ||
        !declaration.resultType.belongsTo(hirModule.semanticContext())) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::MissingRequiredFact, module,
          declaration.definition, registries, static_cast<uint32_t>(pending.size() + 1));
    }
    ZC_IF_SOME(literal, expression) {
      zc::Vector<MirSourceScope> scopes;
      zc::Maybe<MirSourceScopeId> noParent;
      scopes.add(MirSourceScope{scopeId(1), zc::mv(noParent), declaration.sourceSpan.clone()});
      zc::Vector<MirLocalDeclaration> locals;
      zc::Vector<MirStatement> statements;
      auto returnOperand = MirOperand::constant(declaration.resultType, literal.value.clone());
      zc::Vector<MirBasicBlock> blocks;
      blocks.add(MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                               MirTerminator::returnValue(zc::mv(returnOperand))});
      MirFunction function{declaration.definition,
                           MirFunctionKind::Function,
                           identity::DefinitionKind::Function,
                           declaration.resultType,
                           declaration.sourceSpan.clone(),
                           zc::mv(scopes),
                           zc::mv(locals),
                           zc::mv(blocks)};
      zc::Array<uint8_t> ownerKey;
      ZC_IF_SOME(key, definition) { ownerKey = key.encode(); }
      pending.add(PendingMirFunction{zc::mv(function), zc::mv(ownerKey)});
      continue;
    }
    ZC_UNREACHABLE
  }

  sortFunctions(pending);
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  for (auto& item : pending) {
    auto encoded = encodeFunction(item.function, module, registries, semanticTypes);
    if (encoded == zc::none) {
      return rejectMir<BuiltMirCandidate>(
          ir::IrFailurePhase::MirConstruction, ir::IrFailureKind::CanonicalCodecMismatch, module,
          item.function.owner, registries, static_cast<uint32_t>(functions.size() + 1));
    }
    ZC_IF_SOME(record, encoded) { canonicalFunctions.add(zc::mv(record)); }
    functions.add(zc::mv(item.function));
  }
  auto moduleKey = registries.modules().lookup(module);
  if (moduleKey == zc::none) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::InvalidFact, module,
                                        firstDefinition(hirModule), registries, 0);
  }
  zc::Maybe<MirRevisionId> revision;
  ZC_IF_SOME(key, moduleKey) {
    auto expanded = key.encode();
    revision = MirRevisionCodec::computeBuilt(
        hirModule.contextFingerprint(), expanded.asPtr(), hirModule.checkedFactsRevision(),
        hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(),
        canonicalFunctions.asPtr());
  }
  if (revision == zc::none) {
    return rejectMir<BuiltMirCandidate>(ir::IrFailurePhase::MirConstruction,
                                        ir::IrFailureKind::CanonicalCodecMismatch, module,
                                        firstDefinition(hirModule), registries, 0);
  }
  ZC_IF_SOME(value, revision) {
    return ir::IrOperationResult<BuiltMirCandidate>::verified(
        BuiltMirCandidate(hirModule, zc::mv(functions), zc::mv(canonicalFunctions), value));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedBuiltMir> BuiltMirVerifier::verify(BuiltMirCandidate&& candidate) {
  const auto& hirModule = candidate.sourceHir;
  const auto module = hirModule.module();
  const auto& registries = hirModule.registries();
  const auto& semanticTypes = hirModule.semanticTypes();
  const auto evidence =
      hirModule.borrowEvidenceRepository().lookup(hirModule.borrowEvidenceLease());
  if (!evidence.isResolved() ||
      evidence.evidence().revision().digest() != hirModule.borrowEvidenceRevision().digest() ||
      candidate.functions.size() !=
          hirModule.declarations().size() + hirModule.functions().size() ||
      candidate.canonicalFunctions.size() != candidate.functions.size()) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), registries, 0);
  }

  zc::Vector<zc::Array<uint8_t>> recomputedFunctions;
  zc::Array<uint8_t> previousOwner;
  for (size_t index = 0; index < candidate.functions.size(); ++index) {
    const auto& function = candidate.functions[index];
    zc::Maybe<const hir::HirValueDeclaration&> declaration;
    for (const auto& value : hirModule.declarations()) {
      if (value.definition != function.owner) continue;
      if (declaration != zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::AdditionalFact, module,
            function.owner, registries, static_cast<uint32_t>(index + 1));
      }
      declaration = value;
    }
    zc::Maybe<const hir::HirFunctionDeclaration&> sourceFunction;
    for (const auto& value : hirModule.functions()) {
      if (value.definition != function.owner) continue;
      if (sourceFunction != zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::AdditionalFact, module,
            function.owner, registries, static_cast<uint32_t>(index + 1));
      }
      sourceFunction = value;
    }
    if ((declaration == zc::none) == (sourceFunction == zc::none)) {
      return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                         ir::IrFailureKind::AdditionalFact, module, function.owner,
                                         registries, static_cast<uint32_t>(index + 1));
    }
    ZC_IF_SOME(sourceDeclaration, declaration) {
      auto expression = expressionFor(hirModule, sourceDeclaration.initializer);
      if (expression == zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::MissingRequiredFact,
            module, function.owner, registries, static_cast<uint32_t>(index + 1));
      }
      ZC_IF_SOME(sourceExpression, expression) {
        if (!validScalarFunction(function, sourceDeclaration, sourceExpression, module, registries,
                                 semanticTypes)) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             registries, static_cast<uint32_t>(index + 1));
        }
        auto owner = registries.definitions().lookup(function.owner);
        auto record = encodeFunction(function, module, registries, semanticTypes);
        if (owner == zc::none || record == zc::none) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, registries, static_cast<uint32_t>(index + 1));
        }
        zc::Array<uint8_t> ownerBytes;
        ZC_IF_SOME(value, owner) { ownerBytes = value.encode(); }
        if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
          return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                             ir::IrFailureKind::InvalidFact, module, function.owner,
                                             registries, static_cast<uint32_t>(index + 1));
        }
        previousOwner = zc::mv(ownerBytes);
        ZC_IF_SOME(value, record) {
          if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
            return rejectMir<VerifiedBuiltMir>(
                ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
                module, function.owner, registries, static_cast<uint32_t>(index + 1));
          }
          recomputedFunctions.add(zc::mv(value));
        }
        continue;
      }
    }
    ZC_IF_SOME(sourceDeclaration, sourceFunction) {
      auto sourceBlock = blockFor(hirModule, sourceDeclaration.body);
      hir::HirNodeId returnNode;
      ZC_IF_SOME(value, sourceBlock) {
        if (value.statements.size() == 1) returnNode = value.statements[0];
      }
      auto sourceReturn = returnFor(hirModule, returnNode);
      hir::HirNodeId expressionNode;
      ZC_IF_SOME(value, sourceReturn) { expressionNode = value.value; }
      auto expression = expressionFor(hirModule, expressionNode);
      if (sourceBlock == zc::none || sourceReturn == zc::none || expression == zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::MissingRequiredFact,
            module, function.owner, registries, static_cast<uint32_t>(index + 1));
      }
      bool valid = false;
      ZC_IF_SOME(block, sourceBlock) {
        ZC_IF_SOME(returnStatement, sourceReturn) {
          ZC_IF_SOME(sourceExpression, expression) {
            valid = validScalarReturnFunction(function, sourceDeclaration, block, returnStatement,
                                              sourceExpression, module, registries, semanticTypes);
          }
        }
      }
      if (!valid) {
        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                           ir::IrFailureKind::InvalidFact, module, function.owner,
                                           registries, static_cast<uint32_t>(index + 1));
      }
      auto owner = registries.definitions().lookup(function.owner);
      auto record = encodeFunction(function, module, registries, semanticTypes);
      if (owner == zc::none || record == zc::none) {
        return rejectMir<VerifiedBuiltMir>(
            ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
            module, function.owner, registries, static_cast<uint32_t>(index + 1));
      }
      zc::Array<uint8_t> ownerBytes;
      ZC_IF_SOME(value, owner) { ownerBytes = value.encode(); }
      if (index != 0 && !lessBytes(previousOwner.asPtr(), ownerBytes.asPtr())) {
        return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                           ir::IrFailureKind::InvalidFact, module, function.owner,
                                           registries, static_cast<uint32_t>(index + 1));
      }
      previousOwner = zc::mv(ownerBytes);
      ZC_IF_SOME(value, record) {
        if (value.asPtr() != candidate.canonicalFunctions[index].asPtr()) {
          return rejectMir<VerifiedBuiltMir>(
              ir::IrFailurePhase::BuiltMirVerification, ir::IrFailureKind::CanonicalCodecMismatch,
              module, function.owner, registries, static_cast<uint32_t>(index + 1));
        }
        recomputedFunctions.add(zc::mv(value));
      }
      continue;
    }
    ZC_UNREACHABLE
  }

  auto moduleKey = registries.modules().lookup(module);
  zc::Maybe<MirRevisionId> recomputedRevision;
  if (moduleKey != zc::none) {
    ZC_IF_SOME(key, moduleKey) {
      auto expanded = key.encode();
      recomputedRevision = MirRevisionCodec::computeBuilt(
          hirModule.contextFingerprint(), expanded.asPtr(), hirModule.checkedFactsRevision(),
          hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(),
          recomputedFunctions.asPtr());
    }
  }
  if (recomputedRevision == zc::none) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::CanonicalCodecMismatch, module,
                                       firstDefinition(hirModule), registries, 0);
  }
  bool revisionMatches = false;
  ZC_IF_SOME(value, recomputedRevision) {
    revisionMatches = value.digest() == candidate.revision.digest();
  }
  if (!revisionMatches) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), registries, 0);
  }
  auto lease =
      hirModule.borrowEvidenceRepository().lease(module, hirModule.borrowEvidenceRevision());
  if (lease == zc::none) {
    return rejectMir<VerifiedBuiltMir>(ir::IrFailurePhase::BuiltMirVerification,
                                       ir::IrFailureKind::InputRevisionMismatch, module,
                                       firstDefinition(hirModule), registries, 0);
  }
  ZC_IF_SOME(borrowLease, lease) {
    auto impl = zc::heap<VerifiedBuiltMir::Impl>(
        hirModule.semanticContext(), hirModule.contextFingerprint().clone(), hirModule.package(),
        hirModule.crate(), module, hirModule.checkedFactsRevision(),
        hirModule.dispatchFactsRevision(), hirModule.borrowEvidenceRevision(), zc::mv(borrowLease),
        hirModule.borrowEvidenceRepository(), zc::mv(candidate.functions),
        zc::mv(recomputedFunctions), candidate.revision);
    return ir::IrOperationResult<VerifiedBuiltMir>::verified(VerifiedBuiltMir(zc::mv(impl)));
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::mir
