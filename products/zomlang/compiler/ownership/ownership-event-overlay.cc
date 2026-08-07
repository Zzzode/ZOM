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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::ownership {

MirPoint::MirPoint(MirEntryPoint point) noexcept : value(point) {}
MirPoint::MirPoint(MirBeforeStatementPoint point) noexcept : value(point) {}
MirPoint::MirPoint(MirAfterStatementPoint point) noexcept : value(point) {}
MirPoint::MirPoint(MirBeforeTerminatorPoint point) noexcept : value(point) {}
MirPoint::MirPoint(MirEdgePoint point) noexcept : value(point) {}
MirPoint::MirPoint(MirExitPoint point) noexcept : value(point) {}

MirPoint MirPoint::entry() noexcept { return MirPoint(MirEntryPoint{}); }
MirPoint MirPoint::beforeStatement(mir::MirBlockId block, uint32_t ordinal) noexcept {
  return MirPoint(MirBeforeStatementPoint{block, ordinal});
}
MirPoint MirPoint::afterStatement(mir::MirBlockId block, uint32_t ordinal) noexcept {
  return MirPoint(MirAfterStatementPoint{block, ordinal});
}
MirPoint MirPoint::beforeTerminator(mir::MirBlockId block) noexcept {
  return MirPoint(MirBeforeTerminatorPoint{block});
}
MirPoint MirPoint::edge(mir::MirBlockId from, uint32_t edgeOrdinal, mir::MirBlockId to) noexcept {
  return MirPoint(MirEdgePoint{from, edgeOrdinal, to});
}
MirPoint MirPoint::exit(mir::MirBlockId block, MirExitKind kind) noexcept {
  return MirPoint(MirExitPoint{block, kind});
}

MirPointKind MirPoint::kind() const noexcept {
  if (value.is<MirEntryPoint>()) return MirPointKind::Entry;
  if (value.is<MirBeforeStatementPoint>()) return MirPointKind::BeforeStatement;
  if (value.is<MirAfterStatementPoint>()) return MirPointKind::AfterStatement;
  if (value.is<MirBeforeTerminatorPoint>()) return MirPointKind::BeforeTerminator;
  if (value.is<MirEdgePoint>()) return MirPointKind::Edge;
  return MirPointKind::Exit;
}
const MirBeforeStatementPoint& MirPoint::beforeStatementValue() const {
  return value.get<MirBeforeStatementPoint>();
}
const MirAfterStatementPoint& MirPoint::afterStatementValue() const {
  return value.get<MirAfterStatementPoint>();
}
const MirBeforeTerminatorPoint& MirPoint::beforeTerminatorValue() const {
  return value.get<MirBeforeTerminatorPoint>();
}
const MirEdgePoint& MirPoint::edgeValue() const { return value.get<MirEdgePoint>(); }
const MirExitPoint& MirPoint::exitValue() const { return value.get<MirExitPoint>(); }

namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept;

bool encodeMirPoint(identity::CanonicalEncoder& encoder, const MirPoint& point) {
  encoder.encodeUint8(static_cast<uint8_t>(point.kind()));
  switch (point.kind()) {
    case MirPointKind::Entry:
      return true;
    case MirPointKind::BeforeStatement: {
      const auto& value = point.beforeStatementValue();
      if (!value.block.isValid()) return false;
      encoder.encodeUint32(value.block.ordinal());
      encoder.encodeUint32(value.ordinal);
      return true;
    }
    case MirPointKind::AfterStatement: {
      const auto& value = point.afterStatementValue();
      if (!value.block.isValid()) return false;
      encoder.encodeUint32(value.block.ordinal());
      encoder.encodeUint32(value.ordinal);
      return true;
    }
    case MirPointKind::BeforeTerminator: {
      const auto& value = point.beforeTerminatorValue();
      if (!value.block.isValid()) return false;
      encoder.encodeUint32(value.block.ordinal());
      return true;
    }
    case MirPointKind::Edge: {
      const auto& value = point.edgeValue();
      if (!value.from.isValid() || !value.to.isValid()) return false;
      encoder.encodeUint32(value.from.ordinal());
      encoder.encodeUint32(value.edgeOrdinal);
      encoder.encodeUint32(value.to.ordinal());
      return true;
    }
    case MirPointKind::Exit: {
      const auto& value = point.exitValue();
      const uint8_t exitKind = static_cast<uint8_t>(value.kind);
      if (!value.block.isValid() || exitKind < static_cast<uint8_t>(MirExitKind::Return) ||
          exitKind > static_cast<uint8_t>(MirExitKind::Unreachable)) {
        return false;
      }
      encoder.encodeUint32(value.block.ordinal());
      encoder.encodeUint8(exitKind);
      return true;
    }
  }
  return false;
}

zc::Maybe<zc::Array<uint8_t>> encodeEventKey(const MirEventKey& event,
                                             const checker::CheckerIdentityAuthority& identities) {
  identity::CanonicalEncoder encoder;
  auto owner = identities.definition(event.location.owner);
  if (owner == zc::none) return zc::none;
  ZC_IF_SOME(key, owner) {
    auto bytes = key.key().encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  if (!encodeMirPoint(encoder, event.location.point)) return zc::none;
  encoder.encodeUint32(event.operandOrdinal);
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeFunctionOverlay(
    const OwnershipFunctionEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities) {
  identity::CanonicalEncoder encoder;
  auto owner = identities.definition(overlay.owner);
  if (owner == zc::none) return zc::none;
  ZC_IF_SOME(key, owner) {
    auto bytes = key.key().encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  encoder.encodeSequenceSize(overlay.slots.size());
  zc::Array<uint8_t> previousKey;
  bool hasPreviousKey = false;
  for (const auto& slot : overlay.slots) {
    if (slot.key.location.owner != overlay.owner) return zc::none;
    auto encodedKey = encodeEventKey(slot.key, identities);
    if (encodedKey == zc::none) return zc::none;
    zc::Array<uint8_t> keyBytes;
    ZC_IF_SOME(bytes, encodedKey) { keyBytes = zc::mv(bytes); }
    if (hasPreviousKey && !lessBytes(previousKey.asPtr(), keyBytes.asPtr())) return zc::none;
    encoder.encodeByteString(keyBytes.asPtr());

    const uint8_t stage = static_cast<uint8_t>(slot.stage);
    if (stage < static_cast<uint8_t>(OwnershipEventStage::Source) ||
        stage > static_cast<uint8_t>(OwnershipEventStage::Commit) || slot.roles.size() == 0) {
      return zc::none;
    }
    identity::CanonicalEncoder slotEncoder;
    for (uint8_t byte : keyBytes) { slotEncoder.encodeUint8(byte); }
    slotEncoder.encodeUint8(stage);
    slotEncoder.encodeSequenceSize(slot.roles.size());
    uint8_t previousRole = 0;
    for (auto role : slot.roles) {
      const uint8_t roleTag = static_cast<uint8_t>(role);
      if (roleTag <= previousRole ||
          roleTag > static_cast<uint8_t>(OwnershipEventRole::CastCarrierDrop)) {
        return zc::none;
      }
      const uint8_t encodedRole[] = {roleTag};
      slotEncoder.encodeByteString(zc::arrayPtr(encodedRole));
      previousRole = roleTag;
    }
    auto slotBytes = slotEncoder.finish();
    encoder.encodeByteString(slotBytes.asPtr());
    previousKey = zc::mv(keyBytes);
    hasPreviousKey = true;
  }
  for (uint8_t emptyMap = 0; emptyMap < 5; ++emptyMap) { encoder.encodeSequenceSize(0); }
  return encoder.finish();
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
ir::IrOperationResult<VerifiedValue> rejectOwnership(
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
  ZC_IREQUIRE(fallback != zc::none, "Ownership event overlay failure fallback must be legal");
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

zc::Maybe<identity::DefId> firstFunctionDefinition(const mir::VerifiedBuiltMir& builtMir) {
  if (builtMir.functions().size() != 0) return builtMir.functions()[0].owner;
  return zc::none;
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool lessMirPoint(const MirPoint& left, const MirPoint& right) noexcept {
  if (left.kind() != right.kind())
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  switch (left.kind()) {
    case MirPointKind::Entry:
      return false;
    case MirPointKind::BeforeStatement: {
      const auto& leftValue = left.beforeStatementValue();
      const auto& rightValue = right.beforeStatementValue();
      if (leftValue.block != rightValue.block)
        return leftValue.block.ordinal() < rightValue.block.ordinal();
      return leftValue.ordinal < rightValue.ordinal;
    }
    case MirPointKind::AfterStatement: {
      const auto& leftValue = left.afterStatementValue();
      const auto& rightValue = right.afterStatementValue();
      if (leftValue.block != rightValue.block)
        return leftValue.block.ordinal() < rightValue.block.ordinal();
      return leftValue.ordinal < rightValue.ordinal;
    }
    case MirPointKind::BeforeTerminator:
      return left.beforeTerminatorValue().block.ordinal() <
             right.beforeTerminatorValue().block.ordinal();
    case MirPointKind::Edge: {
      const auto& leftValue = left.edgeValue();
      const auto& rightValue = right.edgeValue();
      if (leftValue.from != rightValue.from)
        return leftValue.from.ordinal() < rightValue.from.ordinal();
      if (leftValue.edgeOrdinal != rightValue.edgeOrdinal)
        return leftValue.edgeOrdinal < rightValue.edgeOrdinal;
      return leftValue.to.ordinal() < rightValue.to.ordinal();
    }
    case MirPointKind::Exit: {
      const auto& leftValue = left.exitValue();
      const auto& rightValue = right.exitValue();
      if (leftValue.block != rightValue.block)
        return leftValue.block.ordinal() < rightValue.block.ordinal();
      return static_cast<uint8_t>(leftValue.kind) < static_cast<uint8_t>(rightValue.kind);
    }
  }
  return false;
}

bool lessEventKey(const MirEventKey& left, const MirEventKey& right) noexcept {
  if (lessMirPoint(left.location.point, right.location.point)) return true;
  if (lessMirPoint(right.location.point, left.location.point)) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

void sortRoles(zc::Vector<OwnershipEventRole>& roles) {
  for (size_t index = 1; index < roles.size(); ++index) {
    auto current = roles[index];
    size_t insertion = index;
    while (insertion > 0 &&
           static_cast<uint8_t>(current) < static_cast<uint8_t>(roles[insertion - 1])) {
      roles[insertion] = roles[insertion - 1];
      --insertion;
    }
    roles[insertion] = current;
  }
}

void sortSlots(zc::Vector<MirEventSlot>& slots) {
  for (auto& slot : slots) { sortRoles(slot.roles); }
  for (size_t index = 1; index < slots.size(); ++index) {
    auto current = zc::mv(slots[index]);
    size_t insertion = index;
    while (insertion > 0 && lessEventKey(current.key, slots[insertion - 1].key)) {
      slots[insertion] = zc::mv(slots[insertion - 1]);
      --insertion;
    }
    slots[insertion] = zc::mv(current);
  }
}

bool sortFunctions(zc::Vector<OwnershipFunctionEventOverlay>& functions,
                   const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<zc::Array<uint8_t>> keys;
  for (const auto& function : functions) {
    auto key = identities.definition(function.owner);
    if (key == zc::none) return false;
    ZC_IF_SOME(value, key) { keys.add(value.key().encode()); }
  }
  for (size_t index = 1; index < functions.size(); ++index) {
    auto currentFunction = zc::mv(functions[index]);
    auto currentKey = zc::mv(keys[index]);
    size_t insertion = index;
    while (insertion > 0 && lessBytes(currentKey.asPtr(), keys[insertion - 1].asPtr())) {
      functions[insertion] = zc::mv(functions[insertion - 1]);
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    functions[insertion] = zc::mv(currentFunction);
    keys[insertion] = zc::mv(currentKey);
  }
  return true;
}

zc::Maybe<zc::Vector<OwnershipFunctionEventOverlay>> projectCandidateFunctions(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  for (const auto& function : builtMir.functions()) {
    zc::Vector<MirEventSlot> slots;
    for (const auto& block : function.blocks) {
      uint32_t statementOrdinal = 0;
      for (const auto& statement : block.statements) {
        auto emit = [&](uint32_t eventOrdinal, OwnershipEventStage stage,
                        zc::Vector<OwnershipEventRole>&& roles) {
          slots.add(MirEventSlot{
              MirEventKey{MirLocation{function.owner,
                                      MirPoint::beforeStatement(block.id, statementOrdinal)},
                          eventOrdinal},
              stage, zc::mv(roles)});
        };
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign: {
            const auto& operand = statement.assignmentValue().value.useValue().operand;
            zc::Vector<OwnershipEventRole> operandRoles;
            switch (operand.kind()) {
              case mir::MirOperandKind::Copy:
                operandRoles.add(OwnershipEventRole::OperandRead);
                operandRoles.add(OwnershipEventRole::OperandCopy);
                break;
              case mir::MirOperandKind::Move:
                operandRoles.add(OwnershipEventRole::OperandRead);
                operandRoles.add(OwnershipEventRole::OperandMove);
                break;
              case mir::MirOperandKind::Constant:
                operandRoles.add(OwnershipEventRole::ConstantOperand);
                break;
            }
            emit(0, OwnershipEventStage::Source, zc::mv(operandRoles));
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            emit(1, OwnershipEventStage::Effect, zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            emit(2, OwnershipEventStage::Commit, zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::StorageLive: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::StorageLive);
            emit(0, OwnershipEventStage::Effect, zc::mv(effectRoles));
            break;
          }
          case mir::MirStatementKind::StorageDead: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::StorageDead);
            emit(0, OwnershipEventStage::Effect, zc::mv(effectRoles));
            break;
          }
          case mir::MirStatementKind::BorrowCreation: {
            zc::Vector<OwnershipEventRole> sourceRoles;
            sourceRoles.add(OwnershipEventRole::OperandRead);
            emit(0, OwnershipEventStage::Source, zc::mv(sourceRoles));
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::BorrowIssue);
            emit(1, OwnershipEventStage::Effect, zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            emit(2, OwnershipEventStage::Commit, zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::SetDiscriminant: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            emit(0, OwnershipEventStage::Effect, zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            commitRoles.add(OwnershipEventRole::SetDiscriminant);
            emit(1, OwnershipEventStage::Commit, zc::mv(commitRoles));
            break;
          }
          case mir::MirStatementKind::Deinitialize: {
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            effectRoles.add(OwnershipEventRole::Deinitialize);
            emit(0, OwnershipEventStage::Effect, zc::mv(effectRoles));
            break;
          }
        }
        ++statementOrdinal;
      }
      uint32_t terminatorOrdinal = 0;
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(value, block.terminator.returnValue().value) {
          zc::Vector<OwnershipEventRole> operandRoles;
          switch (value.kind()) {
            case mir::MirOperandKind::Copy:
              operandRoles.add(OwnershipEventRole::OperandRead);
              operandRoles.add(OwnershipEventRole::OperandCopy);
              break;
            case mir::MirOperandKind::Move:
              operandRoles.add(OwnershipEventRole::OperandRead);
              operandRoles.add(OwnershipEventRole::OperandMove);
              break;
            case mir::MirOperandKind::Constant:
              operandRoles.add(OwnershipEventRole::ConstantOperand);
              break;
          }
          slots.add(MirEventSlot{
              MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                          terminatorOrdinal++},
              OwnershipEventStage::Source, zc::mv(operandRoles)});
        }
      }
      zc::Vector<OwnershipEventRole> effectRoles;
      effectRoles.add(OwnershipEventRole::Operation);
      slots.add(MirEventSlot{
          MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                      terminatorOrdinal},
          OwnershipEventStage::Effect, zc::mv(effectRoles)});
    }
    sortSlots(slots);
    functions.add(OwnershipFunctionEventOverlay{function.owner, zc::mv(slots)});
  }
  if (!sortFunctions(functions, identities)) return zc::none;
  return functions;
}

zc::Maybe<zc::Vector<OwnershipFunctionEventOverlay>> reconstructExpectedFunctions(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  for (const auto& function : builtMir.functions()) {
    zc::Vector<MirEventSlot> slots;
    for (const auto& block : function.blocks) {
      uint32_t statementOrdinal = 0;
      for (const auto& statement : block.statements) {
        auto record = [&](uint32_t eventOrdinal, OwnershipEventStage stage,
                          zc::Vector<OwnershipEventRole>&& roles) {
          slots.add(MirEventSlot{
              MirEventKey{MirLocation{function.owner,
                                      MirPoint::beforeStatement(block.id, statementOrdinal)},
                          eventOrdinal},
              stage, zc::mv(roles)});
        };
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign: {
            zc::Vector<OwnershipEventRole> source;
            switch (statement.assignmentValue().value.useValue().operand.kind()) {
              case mir::MirOperandKind::Copy:
                source.add(OwnershipEventRole::OperandRead);
                source.add(OwnershipEventRole::OperandCopy);
                break;
              case mir::MirOperandKind::Move:
                source.add(OwnershipEventRole::OperandRead);
                source.add(OwnershipEventRole::OperandMove);
                break;
              case mir::MirOperandKind::Constant:
                source.add(OwnershipEventRole::ConstantOperand);
                break;
            }
            record(0, OwnershipEventStage::Source, zc::mv(source));
            zc::Vector<OwnershipEventRole> effect;
            effect.add(OwnershipEventRole::Operation);
            record(1, OwnershipEventStage::Effect, zc::mv(effect));
            zc::Vector<OwnershipEventRole> commit;
            commit.add(OwnershipEventRole::DestinationWrite);
            record(2, OwnershipEventStage::Commit, zc::mv(commit));
            break;
          }
          case mir::MirStatementKind::StorageLive: {
            zc::Vector<OwnershipEventRole> roles;
            roles.add(OwnershipEventRole::Operation);
            roles.add(OwnershipEventRole::StorageLive);
            record(0, OwnershipEventStage::Effect, zc::mv(roles));
            break;
          }
          case mir::MirStatementKind::StorageDead: {
            zc::Vector<OwnershipEventRole> roles;
            roles.add(OwnershipEventRole::Operation);
            roles.add(OwnershipEventRole::StorageDead);
            record(0, OwnershipEventStage::Effect, zc::mv(roles));
            break;
          }
          case mir::MirStatementKind::BorrowCreation: {
            zc::Vector<OwnershipEventRole> source;
            source.add(OwnershipEventRole::OperandRead);
            record(0, OwnershipEventStage::Source, zc::mv(source));
            zc::Vector<OwnershipEventRole> effect;
            effect.add(OwnershipEventRole::Operation);
            effect.add(OwnershipEventRole::BorrowIssue);
            record(1, OwnershipEventStage::Effect, zc::mv(effect));
            zc::Vector<OwnershipEventRole> commit;
            commit.add(OwnershipEventRole::DestinationWrite);
            record(2, OwnershipEventStage::Commit, zc::mv(commit));
            break;
          }
          case mir::MirStatementKind::SetDiscriminant: {
            zc::Vector<OwnershipEventRole> effect;
            effect.add(OwnershipEventRole::Operation);
            record(0, OwnershipEventStage::Effect, zc::mv(effect));
            zc::Vector<OwnershipEventRole> commit;
            commit.add(OwnershipEventRole::DestinationWrite);
            commit.add(OwnershipEventRole::SetDiscriminant);
            record(1, OwnershipEventStage::Commit, zc::mv(commit));
            break;
          }
          case mir::MirStatementKind::Deinitialize: {
            zc::Vector<OwnershipEventRole> roles;
            roles.add(OwnershipEventRole::Operation);
            roles.add(OwnershipEventRole::Deinitialize);
            record(0, OwnershipEventStage::Effect, zc::mv(roles));
            break;
          }
        }
        ++statementOrdinal;
      }
      uint32_t terminatorOrdinal = 0;
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(operand, block.terminator.returnValue().value) {
          zc::Vector<OwnershipEventRole> source;
          switch (operand.kind()) {
            case mir::MirOperandKind::Copy:
              source.add(OwnershipEventRole::OperandRead);
              source.add(OwnershipEventRole::OperandCopy);
              break;
            case mir::MirOperandKind::Move:
              source.add(OwnershipEventRole::OperandRead);
              source.add(OwnershipEventRole::OperandMove);
              break;
            case mir::MirOperandKind::Constant:
              source.add(OwnershipEventRole::ConstantOperand);
              break;
          }
          slots.add(MirEventSlot{
              MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                          terminatorOrdinal++},
              OwnershipEventStage::Source, zc::mv(source)});
        }
      }
      zc::Vector<OwnershipEventRole> effect;
      effect.add(OwnershipEventRole::Operation);
      slots.add(MirEventSlot{
          MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                      terminatorOrdinal},
          OwnershipEventStage::Effect, zc::mv(effect)});
    }
    sortSlots(slots);
    functions.add(OwnershipFunctionEventOverlay{function.owner, zc::mv(slots)});
  }
  if (!sortFunctions(functions, identities)) return zc::none;
  return functions;
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> OwnershipEventOverlayCodec::encodeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& checkedFactsRevision,
    const identity::Sha256Digest& builtRevisionDigest,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  if (expandedModuleKey.size() == 0) return zc::none;
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.ownership-event-overlay";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(checkedFactsRevision);
  encoder.encodeDigest(builtRevisionDigest);
  encoder.encodeSequenceSize(canonicalFunctions.size());
  for (const auto& function : canonicalFunctions) {
    if (function.size() == 0) return zc::none;
    encoder.encodeByteString(function.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> OwnershipEventOverlayCodec::encode(
    const identity::SemanticContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const mir::MirRevisionId& builtRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  return encodeFramed(contextFingerprint.digest(), expandedModuleKey, checkedFactsRevision.digest(),
                      builtRevision.digest(), canonicalFunctions);
}

zc::Maybe<OwnershipEventOverlayRevision> OwnershipEventOverlayCodec::compute(
    const identity::SemanticContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const mir::MirRevisionId& builtRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  auto bytes = encode(contextFingerprint, expandedModuleKey, checkedFactsRevision, builtRevision,
                      canonicalFunctions);
  if (bytes == zc::none) return zc::none;
  ZC_IF_SOME(value, bytes) {
    auto digest = identity::sha256(value.asPtr());
    ZC_IF_SOME(hash, digest) { return OwnershipEventOverlayRevision::fromDigest(hash); }
  }
  return zc::none;
}

ir::IrOperationResult<OwnershipEventOverlayCandidate> OwnershipEventOverlayBuilder::build(
    const mir::VerifiedBuiltMir& builtMir) {
  const auto identities = builtMir.retainIdentityAuthority();
  const auto module = builtMir.module();
  auto functions = projectCandidateFunctions(builtMir, identities);
  if (functions == zc::none) {
    return rejectOwnership<OwnershipEventOverlayCandidate>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidOwnershipProof,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<OwnershipEventOverlayCandidate>::verified(
        OwnershipEventOverlayCandidate(
            builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
            builtMir.checkedFactsRevision(), builtMir.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedOwnershipEventOverlay> OwnershipEventOverlayVerifier::verify(
    OwnershipEventOverlayCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir) {
  const auto identities = builtMir.retainIdentityAuthority();
  const auto module = builtMir.module();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.checkedFactsRevision.digest() != builtMir.checkedFactsRevision().digest() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.functions.size() != builtMir.functions().size()) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InputRevisionMismatch,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  auto expectedFunctions = reconstructExpectedFunctions(builtMir, identities);
  if (expectedFunctions == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidOwnershipProof,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  zc::Vector<zc::Array<uint8_t>> recomputedRecords;
  ZC_IF_SOME(expected, expectedFunctions) {
    if (expected.size() != candidate.functions.size()) {
      return rejectOwnership<VerifiedOwnershipEventOverlay>(
          ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::AdditionalFact, module,
          firstFunctionDefinition(builtMir), identities, 0);
    }
    for (size_t index = 0; index < expected.size(); ++index) {
      const auto& expectedFunction = expected[index];
      const auto& candidateFunction = candidate.functions[index];
      if (expectedFunction.owner != candidateFunction.owner) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidFact, module,
            expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
      }
      for (const auto& slot : candidateFunction.slots) {
        if (slot.key.location.owner != candidateFunction.owner) {
          return rejectOwnership<VerifiedOwnershipEventOverlay>(
              ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidFact, module,
              expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
        }
      }
      auto expectedEncoded = encodeFunctionOverlay(expectedFunction, identities);
      auto candidateEncoded = encodeFunctionOverlay(candidateFunction, identities);
      if (expectedEncoded == zc::none || candidateEncoded == zc::none) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
            module, expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
      }
      zc::Array<uint8_t> expectedBytes;
      zc::Array<uint8_t> candidateBytes;
      ZC_IF_SOME(value, expectedEncoded) { expectedBytes = zc::mv(value); }
      ZC_IF_SOME(value, candidateEncoded) { candidateBytes = zc::mv(value); }
      if (expectedBytes.asPtr() != candidateBytes.asPtr()) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
            module, expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
      }
      recomputedRecords.add(zc::mv(expectedBytes));
    }
  }
  auto moduleKey = identities.module(builtMir.module());
  if (moduleKey == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::MissingRequiredFact,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  zc::Array<uint8_t> expandedModuleKey;
  ZC_IF_SOME(key, moduleKey) { expandedModuleKey = key.key().encode(); }
  zc::Maybe<OwnershipEventOverlayRevision> revision = OwnershipEventOverlayCodec::compute(
      candidate.contextFingerprint, expandedModuleKey.asPtr(), candidate.checkedFactsRevision,
      candidate.builtRevision, recomputedRecords.asPtr());
  if (revision == zc::none) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::CanonicalCodecMismatch,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  ZC_IF_SOME(value, revision) {
    auto impl = zc::heap<VerifiedOwnershipEventOverlay::Impl>(
        candidate.semanticContext, candidate.contextFingerprint.clone(), candidate.module,
        candidate.checkedFactsRevision, candidate.builtRevision, zc::mv(candidate.functions), value,
        builtMir.retainBoundModule());
    return ir::IrOperationResult<VerifiedOwnershipEventOverlay>::verified(
        VerifiedOwnershipEventOverlay(zc::mv(impl)));
  }
  ZC_UNREACHABLE
}

struct VerifiedOwnershipEventOverlay::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId module,
       checker::checked::CheckedFactsRevision checkedFactsRevision,
       mir::MirRevisionId builtRevision, zc::Vector<OwnershipFunctionEventOverlay>&& functions,
       OwnershipEventOverlayRevision revision,
       driver::module_graph_query::CheckerBoundModuleView&& boundModule) noexcept
      : boundModule(zc::mv(boundModule)),
        semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        builtRevision(builtRevision),
        functions(zc::mv(functions)),
        revision(revision) {}

  driver::module_graph_query::CheckerBoundModuleView boundModule;
  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  mir::MirRevisionId builtRevision;
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  OwnershipEventOverlayRevision revision;
};

VerifiedOwnershipEventOverlay::VerifiedOwnershipEventOverlay(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedOwnershipEventOverlay::~VerifiedOwnershipEventOverlay() noexcept(false) = default;
VerifiedOwnershipEventOverlay::VerifiedOwnershipEventOverlay(
    VerifiedOwnershipEventOverlay&&) noexcept = default;
VerifiedOwnershipEventOverlay& VerifiedOwnershipEventOverlay::operator=(
    VerifiedOwnershipEventOverlay&&) noexcept = default;

identity::SemanticContextBrand VerifiedOwnershipEventOverlay::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedOwnershipEventOverlay::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedOwnershipEventOverlay::module() const noexcept { return impl->module; }
const checker::checked::CheckedFactsRevision& VerifiedOwnershipEventOverlay::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsRevision;
}
const mir::MirRevisionId& VerifiedOwnershipEventOverlay::builtRevision() const noexcept {
  return impl->builtRevision;
}
zc::ArrayPtr<const OwnershipFunctionEventOverlay> VerifiedOwnershipEventOverlay::functions()
    const noexcept {
  return impl->functions.asPtr();
}
const OwnershipEventOverlayRevision& VerifiedOwnershipEventOverlay::revision() const noexcept {
  return impl->revision;
}

}  // namespace zomlang::compiler::ownership
