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

#include "zomlang/compiler/checker/body/marker-proof.h"
#include "zomlang/compiler/driver/core/marker-authority.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ownership/surface-admission.h"

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

bool MirPoint::operator==(const MirPoint& other) const noexcept {
  if (kind() != other.kind()) return false;
  switch (kind()) {
    case MirPointKind::Entry:
      return true;
    case MirPointKind::BeforeStatement:
      return beforeStatementValue().block == other.beforeStatementValue().block &&
             beforeStatementValue().ordinal == other.beforeStatementValue().ordinal;
    case MirPointKind::AfterStatement:
      return afterStatementValue().block == other.afterStatementValue().block &&
             afterStatementValue().ordinal == other.afterStatementValue().ordinal;
    case MirPointKind::BeforeTerminator:
      return beforeTerminatorValue().block == other.beforeTerminatorValue().block;
    case MirPointKind::Edge:
      return edgeValue().from == other.edgeValue().from &&
             edgeValue().edgeOrdinal == other.edgeValue().edgeOrdinal &&
             edgeValue().to == other.edgeValue().to;
    case MirPointKind::Exit:
      return exitValue().block == other.exitValue().block &&
             exitValue().kind == other.exitValue().kind;
  }
  return false;
}

bool MirPoint::operator<(const MirPoint& other) const noexcept {
  if (kind() != other.kind()) {
    return static_cast<uint8_t>(kind()) < static_cast<uint8_t>(other.kind());
  }
  switch (kind()) {
    case MirPointKind::Entry:
      return false;
    case MirPointKind::BeforeStatement: {
      const auto& left = beforeStatementValue();
      const auto& right = other.beforeStatementValue();
      if (left.block != right.block) return left.block.ordinal() < right.block.ordinal();
      return left.ordinal < right.ordinal;
    }
    case MirPointKind::AfterStatement: {
      const auto& left = afterStatementValue();
      const auto& right = other.afterStatementValue();
      if (left.block != right.block) return left.block.ordinal() < right.block.ordinal();
      return left.ordinal < right.ordinal;
    }
    case MirPointKind::BeforeTerminator:
      return beforeTerminatorValue().block.ordinal() <
             other.beforeTerminatorValue().block.ordinal();
    case MirPointKind::Edge: {
      const auto& left = edgeValue();
      const auto& right = other.edgeValue();
      if (left.from != right.from) return left.from.ordinal() < right.from.ordinal();
      if (left.edgeOrdinal != right.edgeOrdinal) return left.edgeOrdinal < right.edgeOrdinal;
      return left.to.ordinal() < right.to.ordinal();
    }
    case MirPointKind::Exit: {
      const auto& left = exitValue();
      const auto& right = other.exitValue();
      if (left.block != right.block) return left.block.ordinal() < right.block.ordinal();
      return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    }
  }
  return false;
}

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

zc::Maybe<zc::Array<uint8_t>> encodeMarkerUseKey(
    const OwnershipMarkerUseKey& key, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  auto event = encodeEventKey(key.event, identities);
  auto marker = identities.definition(key.marker);
  auto subject = semanticTypes.get(key.subject);
  if (event == zc::none || marker == zc::none || !subject.is<type::SemanticTypeLookup>()) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, event) { encoder.encodeByteString(value.asPtr()); }
  ZC_IF_SOME(value, marker) {
    auto bytes = value.key().encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  encoder.encodeByteString(subject.get<type::SemanticTypeLookup>().key().bytes());
  encoder.encodeDigest(key.markerPolicyRevision.digest());
  encoder.encodeDigest(key.coherenceRevision.digest());
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeMarkerUse(const OwnershipMarkerUse& use,
                                              const checker::CheckerIdentityAuthority& identities,
                                              const type::SemanticTypeStore& semanticTypes) {
  auto key = encodeMarkerUseKey(use.key, identities, semanticTypes);
  if (key == zc::none) return zc::none;
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, key) { encoder.encodeByteString(value.asPtr()); }
  if (use.decision.is<OwnershipMarkerDecisionPositive>()) {
    const auto& proof = use.decision.get<OwnershipMarkerDecisionPositive>().proof;
    if (proof.key.marker != use.key.marker || proof.key.subject != use.key.subject ||
        proof.polarity != checker::signature::Polarity::Positive) {
      return zc::none;
    }
    auto record = checker::signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        proof, identities, semanticTypes);
    if (record == zc::none) return zc::none;
    encoder.encodeUint8(0x01);
    ZC_IF_SOME(value, record) { encoder.encodeByteString(value.asPtr()); }
    return encoder.finish();
  }
  if (use.decision.is<OwnershipMarkerDecisionExplicitNegative>()) {
    const auto& fact = use.decision.get<OwnershipMarkerDecisionExplicitNegative>().explicitFact;
    if (fact.key.marker != use.key.marker || fact.key.subject != use.key.subject ||
        fact.polarity != checker::signature::Polarity::Negative) {
      return zc::none;
    }
    auto record = checker::signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        fact, identities, semanticTypes);
    if (record == zc::none) return zc::none;
    encoder.encodeUint8(0x02);
    ZC_IF_SOME(value, record) { encoder.encodeByteString(value.asPtr()); }
    return encoder.finish();
  }
  encoder.encodeUint8(0x03);
  return encoder.finish();
}

bool encodeSemanticType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId type,
                        const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  encoder.encodeByteString(lookup.get<type::SemanticTypeLookup>().key().bytes());
  return true;
}

zc::Maybe<zc::Array<uint8_t>> encodeDeferredActivation(
    const DeferredActivationFact& fact, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  if (fact.loan.issue.location.owner != fact.receiverSource.location.owner ||
      fact.loan.issue.location.owner != fact.activation.location.owner ||
      fact.receiverMode != checker::checked::ReceiverMode::Mutable ||
      fact.adjustmentSteps.size() != 1 ||
      fact.adjustmentSteps[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
    return zc::none;
  }
  auto loan = encodeEventKey(fact.loan.issue, identities);
  auto receiver = encodeEventKey(fact.receiverSource, identities);
  auto activation = encodeEventKey(fact.activation, identities);
  if (loan == zc::none || receiver == zc::none || activation == zc::none) return zc::none;
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, loan) { encoder.encodeByteString(value.asPtr()); }
  ZC_IF_SOME(value, receiver) { encoder.encodeByteString(value.asPtr()); }
  ZC_IF_SOME(value, activation) { encoder.encodeByteString(value.asPtr()); }
  encoder.encodeUint8(static_cast<uint8_t>(fact.receiverMode));
  if (!encodeSemanticType(encoder, fact.adjustmentSource, semanticTypes) ||
      !encodeSemanticType(encoder, fact.adjustmentDestination, semanticTypes)) {
    return zc::none;
  }
  encoder.encodeSequenceSize(fact.adjustmentSteps.size());
  for (const auto step : fact.adjustmentSteps) { encoder.encodeUint8(static_cast<uint8_t>(step)); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodePlace(const mir::MirPlace& place,
                                          const checker::CheckerIdentityAuthority& identities,
                                          const type::SemanticTypeStore& semanticTypes) {
  if (!place.local().isValid() || !place.hasConsistentTypeChain()) return zc::none;
  identity::CanonicalEncoder encoder;
  encoder.encodeUint32(place.local().ordinal());
  if (!encodeSemanticType(encoder, place.rootType(), semanticTypes)) return zc::none;
  encoder.encodeSequenceSize(place.projections().size());
  for (const auto& projection : place.projections()) {
    identity::CanonicalEncoder item;
    item.encodeUint8(static_cast<uint8_t>(projection.kind()));
    if (!encodeSemanticType(item, projection.inputType(), semanticTypes) ||
        !encodeSemanticType(item, projection.resultType(), semanticTypes)) {
      return zc::none;
    }
    switch (projection.kind()) {
      case mir::MirProjectionKind::Field: {
        auto field = identities.definition(projection.fieldValue().field);
        if (field == zc::none) return zc::none;
        ZC_IF_SOME(value, field) {
          auto bytes = value.key().encode();
          item.encodeByteString(bytes.asPtr());
        }
        break;
      }
      case mir::MirProjectionKind::Index:
        if (!projection.indexValue().index.isValid()) return zc::none;
        item.encodeUint32(projection.indexValue().index.ordinal());
        break;
      case mir::MirProjectionKind::Dereference:
        break;
      case mir::MirProjectionKind::Downcast: {
        auto variant = identities.definition(projection.downcastValue().variant);
        if (variant == zc::none) return zc::none;
        ZC_IF_SOME(value, variant) {
          auto bytes = value.key().encode();
          item.encodeByteString(bytes.asPtr());
        }
        break;
      }
      case mir::MirProjectionKind::Subslice:
        item.encodeUint32(projection.subsliceValue().first);
        item.encodeUint32(projection.subsliceValue().pastLast);
        break;
    }
    auto bytes = item.finish();
    encoder.encodeByteString(bytes.asPtr());
  }
  if (!encodeSemanticType(encoder, place.resultType(), semanticTypes)) return zc::none;
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLogicalDropPlan(
    const LogicalDropPlan& plan, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  auto initialization = encodeEventKey(plan.initialization, identities);
  auto root = encodePlace(plan.root, identities, semanticTypes);
  if (initialization == zc::none || root == zc::none) return zc::none;
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, initialization) { encoder.encodeByteString(value.asPtr()); }
  ZC_IF_SOME(value, root) { encoder.encodeByteString(value.asPtr()); }
  encoder.encodeSequenceSize(plan.components.size());
  zc::Array<uint8_t> previousPlace;
  bool hasPreviousPlace = false;
  for (const auto& component : plan.components) {
    auto place = encodePlace(component.place, identities, semanticTypes);
    auto copy = encodeMarkerUseKey(component.copyDecision, identities, semanticTypes);
    auto linear = encodeMarkerUseKey(component.linearDecision, identities, semanticTypes);
    if (place == zc::none || copy == zc::none || linear == zc::none ||
        !encodeSemanticType(encoder, component.valueType, semanticTypes)) {
      return zc::none;
    }
    ZC_IF_SOME(placeBytes, place) {
      if (hasPreviousPlace && !lessBytes(previousPlace.asPtr(), placeBytes.asPtr()))
        return zc::none;
      encoder.encodeByteString(placeBytes.asPtr());
      previousPlace = zc::heapArray(placeBytes.asPtr());
      hasPreviousPlace = true;
    }
    if (component.dropAction == zc::none) {
      encoder.encodeUint8(0x00);
    } else {
      ZC_IF_SOME(action, component.dropAction) {
        if (action.is<LogicalDropDeclaredAction>()) {
          auto definition =
              identities.definition(action.get<LogicalDropDeclaredAction>().deinitializer);
          if (definition == zc::none) return zc::none;
          encoder.encodeUint8(0x01);
          ZC_IF_SOME(value, definition) {
            auto bytes = value.key().encode();
            encoder.encodeByteString(bytes.asPtr());
          }
        } else if (action.is<LogicalDropBuiltinAction>()) {
          encoder.encodeUint8(0x02);
          if (!encodeSemanticType(encoder, action.get<LogicalDropBuiltinAction>().ownerType,
                                  semanticTypes)) {
            return zc::none;
          }
        } else {
          encoder.encodeUint8(0x03);
          if (!encodeSemanticType(encoder, action.get<LogicalDropDynamicAction>().existentialType,
                                  semanticTypes)) {
            return zc::none;
          }
        }
      }
    }
    ZC_IF_SOME(value, copy) { encoder.encodeByteString(value.asPtr()); }
    ZC_IF_SOME(value, linear) { encoder.encodeByteString(value.asPtr()); }
    encoder.encodeUint32(component.declarationOrdinal);
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeFunctionOverlay(
    const OwnershipFunctionEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
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
  encoder.encodeSequenceSize(overlay.deferredActivations.size());
  zc::Array<uint8_t> previousDeferredLoan;
  bool hasPreviousDeferredLoan = false;
  for (const auto& activation : overlay.deferredActivations) {
    if (activation.loan.issue.location.owner != overlay.owner) return zc::none;
    auto key = encodeEventKey(activation.loan.issue, identities);
    auto record = encodeDeferredActivation(activation, identities, semanticTypes);
    if (key == zc::none || record == zc::none) return zc::none;
    ZC_IF_SOME(keyBytes, key) {
      if (hasPreviousDeferredLoan && !lessBytes(previousDeferredLoan.asPtr(), keyBytes.asPtr())) {
        return zc::none;
      }
      encoder.encodeByteString(keyBytes.asPtr());
      previousDeferredLoan = zc::heapArray(keyBytes.asPtr());
      hasPreviousDeferredLoan = true;
    }
    ZC_IF_SOME(recordBytes, record) { encoder.encodeByteString(recordBytes.asPtr()); }
  }
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(overlay.markerUses.size());
  zc::Array<uint8_t> previousMarkerKey;
  bool hasPreviousMarkerKey = false;
  for (const auto& use : overlay.markerUses) {
    if (use.key.event.location.owner != overlay.owner) return zc::none;
    auto key = encodeMarkerUseKey(use.key, identities, semanticTypes);
    auto record = encodeMarkerUse(use, identities, semanticTypes);
    if (key == zc::none || record == zc::none) return zc::none;
    ZC_IF_SOME(keyBytes, key) {
      if (hasPreviousMarkerKey && !lessBytes(previousMarkerKey.asPtr(), keyBytes.asPtr())) {
        return zc::none;
      }
      encoder.encodeByteString(keyBytes.asPtr());
      previousMarkerKey = zc::heapArray(keyBytes.asPtr());
      hasPreviousMarkerKey = true;
    }
    ZC_IF_SOME(recordBytes, record) { encoder.encodeByteString(recordBytes.asPtr()); }
  }
  encoder.encodeSequenceSize(overlay.logicalDropPlans.size());
  zc::Array<uint8_t> previousInitialization;
  bool hasPreviousInitialization = false;
  for (const auto& plan : overlay.logicalDropPlans) {
    if (plan.initialization.location.owner != overlay.owner) return zc::none;
    auto key = encodeEventKey(plan.initialization, identities);
    auto record = encodeLogicalDropPlan(plan, identities, semanticTypes);
    if (key == zc::none || record == zc::none) return zc::none;
    ZC_IF_SOME(keyBytes, key) {
      if (hasPreviousInitialization &&
          !lessBytes(previousInitialization.asPtr(), keyBytes.asPtr())) {
        return zc::none;
      }
      encoder.encodeByteString(keyBytes.asPtr());
      previousInitialization = zc::heapArray(keyBytes.asPtr());
      hasPreviousInitialization = true;
    }
    ZC_IF_SOME(recordBytes, record) { encoder.encodeByteString(recordBytes.asPtr()); }
  }
  encoder.encodeSequenceSize(0);
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

bool hasExactOverlayInput(const OwnershipEventOverlayInput& input) {
  const auto& admitted = input.admitted;
  const auto& checked = input.checked;
  const auto& hir = input.hir;
  const auto& built = input.built;
  const auto& body = input.body;
  const auto& bound = body.boundModule;
  const auto& admittedBound = admitted.boundModule();
  const auto& requirements = body.requirements;

  return admitted.semanticContext() == bound.semanticContext() &&
         admitted.semanticContext() == admittedBound.semanticContext() &&
         admitted.compilationUnit() == admittedBound.compilationUnit() &&
         admitted.crate() == admittedBound.crate() && admitted.module() == admittedBound.module() &&
         admitted.sourceFile() == admittedBound.sourceFile() &&
         admitted.semanticFingerprint().digest() == admittedBound.semanticFingerprint().digest() &&
         admitted.parsedModule().contentDigest() == admittedBound.parsedModule().contentDigest() &&
         admitted.parsedModule().receipt().digest() ==
             admittedBound.parsedModule().receipt().digest() &&
         admitted.module() == bound.module() && admitted.crate() == bound.crate() &&
         admitted.compilationUnit() == bound.compilationUnit() &&
         admitted.sourceFile() == bound.sourceFile() &&
         admitted.semanticFingerprint().digest() == bound.semanticFingerprint().digest() &&
         admitted.parsedModule().contentDigest() == bound.parsedModule().contentDigest() &&
         admitted.parsedModule().receipt().digest() == bound.parsedModule().receipt().digest() &&
         checked.semanticContext() == hir.semanticContext() &&
         checked.semanticContext() == built.semanticContext() &&
         checked.semanticContext() == bound.semanticContext() &&
         checked.contextFingerprint().digest() == hir.contextFingerprint().digest() &&
         checked.contextFingerprint().digest() == built.contextFingerprint().digest() &&
         checked.contextFingerprint().digest() == bound.semanticFingerprint().digest() &&
         checked.compilationUnit() == hir.compilationUnit() &&
         checked.compilationUnit() == built.compilationUnit() && checked.crate() == hir.crate() &&
         checked.crate() == built.crate() && checked.module() == hir.module() &&
         checked.module() == built.module() && checked.module() == bound.module() &&
         checked.sourceContentDigest() == hir.sourceContentDigest() &&
         checked.sourceContentDigest() == bound.parsedModule().contentDigest() &&
         checked.parsedModuleReceipt().digest() == hir.parsedModuleReceiptDigest() &&
         checked.parsedModuleReceipt().digest() == bound.parsedModule().receipt().digest() &&
         checked.checkedFactsRevision().digest() == hir.checkedFactsRevision().digest() &&
         checked.checkedFactsRevision().digest() == built.checkedFactsRevision().digest() &&
         checked.dispatchFactsRevision().digest() == hir.dispatchFactsRevision().digest() &&
         checked.dispatchFactsRevision().digest() == built.dispatchFactsRevision().digest() &&
         checked.borrowEvidenceRevision().digest() == hir.borrowEvidenceRevision().digest() &&
         checked.borrowEvidenceRevision().digest() == built.borrowEvidenceRevision().digest() &&
         body.identities.semanticContext() == checked.semanticContext() &&
         body.identities.fingerprint().digest() == checked.contextFingerprint().digest() &&
         body.signatureFacts.semanticContext() == checked.semanticContext() &&
         body.signatureFacts.contextFingerprint().digest() ==
             checked.contextFingerprint().digest() &&
         body.signatureFacts.module() == checked.module() &&
         body.signatureFacts.sourceContentDigest() == checked.sourceContentDigest() &&
         body.signatureFacts.parsedModuleReceipt().digest() ==
             checked.parsedModuleReceipt().digest() &&
         body.importedSignatures.semanticContext() == checked.semanticContext() &&
         body.importedSignatures.contextFingerprint().digest() ==
             checked.contextFingerprint().digest() &&
         body.importedSignatures.requester() == checked.module() &&
         body.coherence.semanticContext() == checked.semanticContext() &&
         body.coherence.contextFingerprint().digest() == checked.contextFingerprint().digest() &&
         body.signatureFacts.markerPolicyRegistryRevision().digest() ==
             body.coherence.markerPolicyRegistryRevision().digest() &&
         body.standardMarkers.context() == checked.semanticContext() &&
         body.standardMarkers.fingerprint().digest() == checked.contextFingerprint().digest() &&
         body.semanticTypes.context() == checked.semanticContext() &&
         requirements.semanticContext() == checked.semanticContext() &&
         requirements.module() == checked.module() &&
         requirements.sourceContentDigest() == checked.sourceContentDigest() &&
         requirements.parsedModuleReceipt().digest() == checked.parsedModuleReceipt().digest();
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool lessEventKey(const MirEventKey& left, const MirEventKey& right) noexcept {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.belongsTo(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
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

void sortDeferredActivations(zc::Vector<DeferredActivationFact>& activations) {
  for (size_t index = 1; index < activations.size(); ++index) {
    auto current = zc::mv(activations[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessEventKey(current.loan.issue, activations[insertion - 1].loan.issue)) {
      activations[insertion] = zc::mv(activations[insertion - 1]);
      --insertion;
    }
    activations[insertion] = zc::mv(current);
  }
}

void sortSources(zc::Vector<MirEventSource>& sources) {
  for (size_t index = 1; index < sources.size(); ++index) {
    auto current = zc::mv(sources[index]);
    size_t insertion = index;
    while (insertion > 0 && lessEventKey(current.key, sources[insertion - 1].key)) {
      sources[insertion] = zc::mv(sources[insertion - 1]);
      --insertion;
    }
    sources[insertion] = zc::mv(current);
  }
}

zc::Maybe<OwnershipMarkerDecision> markerDecision(checker::marker::MarkerProofEngine& proofs,
                                                  identity::DefId marker,
                                                  identity::SemanticTypeId subject) {
  auto result = proofs.prove(marker, subject);
  if (result.is<checker::marker::MarkerProofPositive>()) {
    return OwnershipMarkerDecision(OwnershipMarkerDecisionPositive{
        result.get<checker::marker::MarkerProofPositive>().proof.clone()});
  }
  if (result.is<checker::marker::MarkerProofNegative>()) {
    return OwnershipMarkerDecision(OwnershipMarkerDecisionExplicitNegative{
        result.get<checker::marker::MarkerProofNegative>().explicitFact.clone()});
  }
  if (result.is<checker::marker::MarkerProofUnsatisfied>()) {
    return OwnershipMarkerDecision(OwnershipMarkerDecisionUnsatisfied{});
  }
  return zc::none;
}

bool appendMarkerUse(zc::Vector<OwnershipMarkerUse>& uses,
                     checker::marker::MarkerProofEngine& proofs,
                     const OwnershipEventOverlayInput& input, const MirEventKey& event,
                     identity::DefId marker, identity::SemanticTypeId subject) {
  auto decision = markerDecision(proofs, marker, subject);
  if (decision == zc::none) return false;
  ZC_IF_SOME(value, decision) {
    uses.add(OwnershipMarkerUse{
        OwnershipMarkerUseKey{event, marker, subject, input.body.markerPolicies.revision(),
                              input.body.coherence.revision()},
        zc::mv(value)});
  }
  return true;
}

bool sortMarkerUses(zc::Vector<OwnershipMarkerUse>& uses,
                    const checker::CheckerIdentityAuthority& identities,
                    const type::SemanticTypeStore& semanticTypes) {
  zc::Vector<zc::Array<uint8_t>> keys;
  for (const auto& use : uses) {
    auto key = encodeMarkerUseKey(use.key, identities, semanticTypes);
    if (key == zc::none) return false;
    ZC_IF_SOME(value, key) { keys.add(zc::mv(value)); }
  }
  for (size_t index = 1; index < uses.size(); ++index) {
    auto currentUse = zc::mv(uses[index]);
    auto currentKey = zc::mv(keys[index]);
    size_t insertion = index;
    while (insertion > 0 && lessBytes(currentKey.asPtr(), keys[insertion - 1].asPtr())) {
      uses[insertion] = zc::mv(uses[insertion - 1]);
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    uses[insertion] = zc::mv(currentUse);
    keys[insertion] = zc::mv(currentKey);
  }
  for (size_t index = 1; index < keys.size(); ++index) {
    if (!lessBytes(keys[index - 1].asPtr(), keys[index].asPtr())) return false;
  }
  return true;
}

bool appendLogicalDropPlan(zc::Vector<LogicalDropPlan>& plans,
                           const OwnershipEventOverlayInput& input,
                           const MirEventKey& initialization, const mir::MirPlace& root,
                           const OwnershipMarkerUse& copyUse, const OwnershipMarkerUse& linearUse) {
  zc::Vector<LogicalDropPlanComponent> components;
  const bool copyPositive = copyUse.decision.is<OwnershipMarkerDecisionPositive>();
  const bool linearPositive = linearUse.decision.is<OwnershipMarkerDecisionPositive>();
  if (!copyPositive || linearPositive) {
    zc::Maybe<LogicalDropAction> noAction;
    components.add(LogicalDropPlanComponent{root.clone(), root.resultType(), zc::mv(noAction),
                                            copyUse.key, linearUse.key, 0});
  }
  plans.add(LogicalDropPlan{initialization, root.clone(), zc::mv(components)});
  return true;
}

bool sortLogicalDropPlans(zc::Vector<LogicalDropPlan>& plans,
                          const checker::CheckerIdentityAuthority& identities) {
  zc::Vector<zc::Array<uint8_t>> keys;
  for (const auto& plan : plans) {
    auto key = encodeEventKey(plan.initialization, identities);
    if (key == zc::none) return false;
    ZC_IF_SOME(value, key) { keys.add(zc::mv(value)); }
  }
  for (size_t index = 1; index < plans.size(); ++index) {
    auto currentPlan = zc::mv(plans[index]);
    auto currentKey = zc::mv(keys[index]);
    size_t insertion = index;
    while (insertion > 0 && lessBytes(currentKey.asPtr(), keys[insertion - 1].asPtr())) {
      plans[insertion] = zc::mv(plans[insertion - 1]);
      keys[insertion] = zc::mv(keys[insertion - 1]);
      --insertion;
    }
    plans[insertion] = zc::mv(currentPlan);
    keys[insertion] = zc::mv(currentKey);
  }
  for (size_t index = 1; index < keys.size(); ++index) {
    if (!lessBytes(keys[index - 1].asPtr(), keys[index].asPtr())) return false;
  }
  return true;
}

zc::Maybe<DeferredActivationFact> projectDeferredActivation(const OwnershipEventOverlayInput& input,
                                                            const mir::MirFunction& function,
                                                            const mir::MirBasicBlock& block,
                                                            const mir::MirCallTerminator& call) {
  if (call.effect.kind() != mir::MirCallEffectKind::ActivateMutableReceiver) { return zc::none; }
  const auto activated = call.effect.activatedMutableReceiver();
  if (activated == zc::none) return zc::none;
  zc::Maybe<const hir::HirReceiverCallExpression&> receiverCall;
  for (const auto& candidate : input.hir.receiverCalls()) {
    if (candidate.callee != call.callee ||
        !sameSpan(candidate.sourceSpan, block.terminator.sourceSpan())) {
      continue;
    }
    if (receiverCall != zc::none) return zc::none;
    receiverCall = candidate;
  }
  if (receiverCall == zc::none) return zc::none;

  zc::Maybe<uint32_t> issueOrdinal;
  for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
    const auto& statement = block.statements[ordinal];
    if (statement.kind() != mir::MirStatementKind::BorrowCreation) continue;
    const auto& borrow = statement.borrowCreationValue();
    if (borrow.kind != mir::MirBorrowKind::Mutable ||
        borrow.destination.local() != ZC_ASSERT_NONNULL(activated)) {
      continue;
    }
    if (issueOrdinal != zc::none) return zc::none;
    issueOrdinal = ordinal;
  }
  if (issueOrdinal == zc::none) return zc::none;

  ZC_IF_SOME(hirCall, receiverCall) {
    zc::Vector<checker::checked::ReceiverAdjustmentStep> steps;
    for (const auto step : hirCall.receiverAdjustments) { steps.add(step); }
    const MirEventKey issue{
        MirLocation{function.owner,
                    MirPoint::beforeStatement(block.id, ZC_ASSERT_NONNULL(issueOrdinal))},
        1};
    const MirEventKey receiverSource{
        MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
    const MirEventKey activation{
        MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 1};
    return DeferredActivationFact{LoanKey{issue},
                                  receiverSource,
                                  activation,
                                  hirCall.receiverMode,
                                  hirCall.receiverSourceType,
                                  hirCall.receiverType,
                                  zc::mv(steps)};
  }
  ZC_UNREACHABLE
}

zc::Maybe<identity::SourceSpan> sourceForEvent(const mir::MirFunction& function,
                                               const MirEventKey& event) {
  if (event.location.owner != function.owner) return zc::none;
  const auto& point = event.location.point;
  if (point.kind() == MirPointKind::Entry) {
    if (event.operandOrdinal >= function.locals.size()) return zc::none;
    return function.locals[event.operandOrdinal].sourceSpan.clone();
  }

  mir::MirBlockId block;
  switch (point.kind()) {
    case MirPointKind::BeforeStatement:
      block = point.beforeStatementValue().block;
      break;
    case MirPointKind::AfterStatement:
      block = point.afterStatementValue().block;
      break;
    case MirPointKind::BeforeTerminator:
      block = point.beforeTerminatorValue().block;
      break;
    case MirPointKind::Edge:
      block = point.edgeValue().from;
      break;
    case MirPointKind::Exit:
      block = point.exitValue().block;
      break;
    case MirPointKind::Entry:
      ZC_UNREACHABLE
  }
  for (const auto& candidate : function.blocks) {
    if (candidate.id != block) continue;
    if (point.kind() == MirPointKind::BeforeStatement) {
      const auto ordinal = point.beforeStatementValue().ordinal;
      if (ordinal >= candidate.statements.size()) return zc::none;
      return candidate.statements[ordinal].sourceSpan().clone();
    }
    if (point.kind() == MirPointKind::AfterStatement) {
      const auto ordinal = point.afterStatementValue().ordinal;
      if (ordinal >= candidate.statements.size()) return zc::none;
      return candidate.statements[ordinal].sourceSpan().clone();
    }
    return candidate.terminator.sourceSpan().clone();
  }
  return zc::none;
}

zc::Maybe<zc::Vector<MirEventSource>> projectSourceMap(const mir::MirFunction& function,
                                                       const zc::Vector<MirEventSlot>& slots) {
  zc::Vector<MirEventSource> sources;
  for (const auto& slot : slots) {
    auto span = sourceForEvent(function, slot.key);
    if (span == zc::none) return zc::none;
    ZC_IF_SOME(value, span) { sources.add(MirEventSource{slot.key, zc::mv(value)}); }
  }
  sortSources(sources);
  for (size_t index = 1; index < sources.size(); ++index) {
    if (sources[index - 1].key == sources[index].key) return zc::none;
  }
  return sources;
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
    const OwnershipEventOverlayInput& input, const checker::CheckerIdentityAuthority& identities) {
  const auto& builtMir = input.built;
  auto proofInput = checker::marker::MarkerProofInput::from(input.body);
  if (proofInput == zc::none) return zc::none;
  checker::marker::MarkerProofEngine proofs(zc::mv(ZC_ASSERT_NONNULL(proofInput)));
  const auto copy = input.body.standardMarkers.copy();
  const auto linear = input.body.standardMarkers.linear();
  if (!copy.isValid() || !linear.isValid() || copy == linear) return zc::none;
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  for (const auto& function : builtMir.functions()) {
    zc::Vector<MirEventSlot> slots;
    zc::Vector<DeferredActivationFact> deferredActivations;
    zc::Vector<OwnershipMarkerUse> markerUses;
    zc::Vector<LogicalDropPlan> logicalDropPlans;
    for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
      zc::Vector<OwnershipEventRole> roles;
      const MirEventKey event{MirLocation{function.owner, MirPoint::entry()}, ordinal};
      roles.add(OwnershipEventRole::EntryRoot);
      slots.add(MirEventSlot{event, OwnershipEventStage::Commit, zc::mv(roles)});
    }
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
            zc::Vector<OwnershipEventRole> operandRoles;
            const auto& rvalue = statement.assignmentValue().value;
            if (rvalue.kind() == mir::MirRvalueKind::Use) {
              const auto& operand = rvalue.useValue().operand;
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
            } else {
              operandRoles.add(OwnershipEventRole::ConstantOperand);
            }
            emit(0, OwnershipEventStage::Source, zc::mv(operandRoles));
            if (rvalue.kind() == mir::MirRvalueKind::Use &&
                rvalue.useValue().operand.kind() == mir::MirOperandKind::Copy) {
              const auto& operand = rvalue.useValue().operand;
              const MirEventKey event{MirLocation{function.owner, MirPoint::beforeStatement(
                                                                      block.id, statementOrdinal)},
                                      0};
              if (!appendMarkerUse(markerUses, proofs, input, event, copy,
                                   operand.place().resultType())) {
                return zc::none;
              }
            }
            zc::Vector<OwnershipEventRole> effectRoles;
            effectRoles.add(OwnershipEventRole::Operation);
            emit(1, OwnershipEventStage::Effect, zc::mv(effectRoles));
            zc::Vector<OwnershipEventRole> commitRoles;
            commitRoles.add(OwnershipEventRole::DestinationWrite);
            emit(2, OwnershipEventStage::Commit, zc::mv(commitRoles));
            const MirEventKey initialization{
                MirLocation{function.owner, MirPoint::beforeStatement(block.id, statementOrdinal)},
                2};
            if (rvalue.kind() == mir::MirRvalueKind::Use ||
                rvalue.kind() == mir::MirRvalueKind::NominalAggregate) {
              if (!appendMarkerUse(markerUses, proofs, input, initialization, copy,
                                   statement.assignmentValue().destination.resultType()) ||
                  !appendMarkerUse(markerUses, proofs, input, initialization, linear,
                                   statement.assignmentValue().destination.resultType())) {
                return zc::none;
              }
              if (markerUses.size() < 2 ||
                  !appendLogicalDropPlan(logicalDropPlans, input, initialization,
                                         statement.assignmentValue().destination,
                                         markerUses[markerUses.size() - 2],
                                         markerUses[markerUses.size() - 1])) {
                return zc::none;
              }
            }
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
          if (value.kind() == mir::MirOperandKind::Copy) {
            const MirEventKey event{
                MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
            if (!appendMarkerUse(markerUses, proofs, input, event, copy,
                                 value.place().resultType())) {
              return zc::none;
            }
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        const auto& call = block.terminator.callValue();
        if (call.unwindTarget != zc::none) return zc::none;
        for (const auto& argument : call.arguments) {
          zc::Vector<OwnershipEventRole> argumentRoles;
          switch (argument.kind()) {
            case mir::MirOperandKind::Copy:
              argumentRoles.add(OwnershipEventRole::OperandRead);
              argumentRoles.add(OwnershipEventRole::OperandCopy);
              break;
            case mir::MirOperandKind::Move:
              argumentRoles.add(OwnershipEventRole::OperandRead);
              argumentRoles.add(OwnershipEventRole::OperandMove);
              break;
            case mir::MirOperandKind::Constant:
              argumentRoles.add(OwnershipEventRole::ConstantOperand);
              break;
          }
          const MirEventKey event{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                                  terminatorOrdinal++};
          slots.add(MirEventSlot{event, OwnershipEventStage::Source, zc::mv(argumentRoles)});
          if (argument.kind() == mir::MirOperandKind::Copy &&
              !appendMarkerUse(markerUses, proofs, input, event, copy,
                               argument.place().resultType())) {
            return zc::none;
          }
        }
        zc::Vector<OwnershipEventRole> commitRoles;
        commitRoles.add(OwnershipEventRole::DestinationWrite);
        slots.add(MirEventSlot{
            MirEventKey{MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)},
                        0},
            OwnershipEventStage::Commit, zc::mv(commitRoles)});
        if (call.effect.commitsOnNormalEdge()) {
          if (call.effect.activatedMutableReceiver() == zc::none) return zc::none;
          zc::Vector<OwnershipEventRole> activationRoles;
          activationRoles.add(OwnershipEventRole::BorrowActivation);
          slots.add(MirEventSlot{
              MirEventKey{
                  MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 1},
              OwnershipEventStage::Commit, zc::mv(activationRoles)});
          auto deferred = projectDeferredActivation(input, function, block, call);
          if (deferred == zc::none) return zc::none;
          ZC_IF_SOME(value, deferred) { deferredActivations.add(zc::mv(value)); }
        }
        const MirEventKey initialization{
            MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 0};
        if (!appendMarkerUse(markerUses, proofs, input, initialization, copy,
                             call.destination.resultType()) ||
            !appendMarkerUse(markerUses, proofs, input, initialization, linear,
                             call.destination.resultType())) {
          return zc::none;
        }
        if (markerUses.size() < 2 ||
            !appendLogicalDropPlan(logicalDropPlans, input, initialization, call.destination,
                                   markerUses[markerUses.size() - 2],
                                   markerUses[markerUses.size() - 1])) {
          return zc::none;
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
    sortDeferredActivations(deferredActivations);
    if (!sortMarkerUses(markerUses, identities, input.body.semanticTypes)) return zc::none;
    if (!sortLogicalDropPlans(logicalDropPlans, identities)) return zc::none;
    auto sourceMap = projectSourceMap(function, slots);
    if (sourceMap == zc::none) return zc::none;
    ZC_IF_SOME(value, sourceMap) {
      functions.add(OwnershipFunctionEventOverlay{function.owner, zc::mv(slots), zc::mv(value),
                                                  zc::mv(deferredActivations), zc::mv(markerUses),
                                                  zc::mv(logicalDropPlans)});
    }
  }
  if (!sortFunctions(functions, identities)) return zc::none;
  return functions;
}

zc::Maybe<zc::Vector<OwnershipFunctionEventOverlay>> reconstructExpectedFunctions(
    const OwnershipEventOverlayInput& input, const checker::CheckerIdentityAuthority& identities) {
  const auto& builtMir = input.built;
  auto proofInput = checker::marker::MarkerProofInput::from(input.body);
  if (proofInput == zc::none) return zc::none;
  checker::marker::MarkerProofEngine proofs(zc::mv(ZC_ASSERT_NONNULL(proofInput)));
  const auto copy = input.body.standardMarkers.copy();
  const auto linear = input.body.standardMarkers.linear();
  if (!copy.isValid() || !linear.isValid() || copy == linear) return zc::none;
  zc::Vector<OwnershipFunctionEventOverlay> functions;
  for (const auto& function : builtMir.functions()) {
    zc::Vector<MirEventSlot> slots;
    zc::Vector<DeferredActivationFact> deferredActivations;
    zc::Vector<OwnershipMarkerUse> markerUses;
    zc::Vector<LogicalDropPlan> logicalDropPlans;
    for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
      zc::Vector<OwnershipEventRole> roles;
      const MirEventKey event{MirLocation{function.owner, MirPoint::entry()}, ordinal};
      roles.add(OwnershipEventRole::EntryRoot);
      slots.add(MirEventSlot{event, OwnershipEventStage::Commit, zc::mv(roles)});
    }
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
            const auto& rvalue = statement.assignmentValue().value;
            if (rvalue.kind() == mir::MirRvalueKind::Use) {
              switch (rvalue.useValue().operand.kind()) {
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
            } else {
              source.add(OwnershipEventRole::ConstantOperand);
            }
            record(0, OwnershipEventStage::Source, zc::mv(source));
            if (rvalue.kind() == mir::MirRvalueKind::Use &&
                rvalue.useValue().operand.kind() == mir::MirOperandKind::Copy) {
              const auto& operand = rvalue.useValue().operand;
              const MirEventKey event{MirLocation{function.owner, MirPoint::beforeStatement(
                                                                      block.id, statementOrdinal)},
                                      0};
              if (!appendMarkerUse(markerUses, proofs, input, event, copy,
                                   operand.place().resultType())) {
                return zc::none;
              }
            }
            zc::Vector<OwnershipEventRole> effect;
            effect.add(OwnershipEventRole::Operation);
            record(1, OwnershipEventStage::Effect, zc::mv(effect));
            zc::Vector<OwnershipEventRole> commit;
            commit.add(OwnershipEventRole::DestinationWrite);
            record(2, OwnershipEventStage::Commit, zc::mv(commit));
            const MirEventKey initialization{
                MirLocation{function.owner, MirPoint::beforeStatement(block.id, statementOrdinal)},
                2};
            if (rvalue.kind() == mir::MirRvalueKind::Use ||
                rvalue.kind() == mir::MirRvalueKind::NominalAggregate) {
              if (!appendMarkerUse(markerUses, proofs, input, initialization, copy,
                                   statement.assignmentValue().destination.resultType()) ||
                  !appendMarkerUse(markerUses, proofs, input, initialization, linear,
                                   statement.assignmentValue().destination.resultType())) {
                return zc::none;
              }
              if (markerUses.size() < 2 ||
                  !appendLogicalDropPlan(logicalDropPlans, input, initialization,
                                         statement.assignmentValue().destination,
                                         markerUses[markerUses.size() - 2],
                                         markerUses[markerUses.size() - 1])) {
                return zc::none;
              }
            }
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
          if (operand.kind() == mir::MirOperandKind::Copy) {
            const MirEventKey event{
                MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
            if (!appendMarkerUse(markerUses, proofs, input, event, copy,
                                 operand.place().resultType())) {
              return zc::none;
            }
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        const auto& call = block.terminator.callValue();
        if (call.unwindTarget != zc::none) return zc::none;
        for (const auto& argument : call.arguments) {
          zc::Vector<OwnershipEventRole> argumentRoles;
          switch (argument.kind()) {
            case mir::MirOperandKind::Copy:
              argumentRoles.add(OwnershipEventRole::OperandRead);
              argumentRoles.add(OwnershipEventRole::OperandCopy);
              break;
            case mir::MirOperandKind::Move:
              argumentRoles.add(OwnershipEventRole::OperandRead);
              argumentRoles.add(OwnershipEventRole::OperandMove);
              break;
            case mir::MirOperandKind::Constant:
              argumentRoles.add(OwnershipEventRole::ConstantOperand);
              break;
          }
          const MirEventKey event{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                                  terminatorOrdinal++};
          slots.add(MirEventSlot{event, OwnershipEventStage::Source, zc::mv(argumentRoles)});
          if (argument.kind() == mir::MirOperandKind::Copy &&
              !appendMarkerUse(markerUses, proofs, input, event, copy,
                               argument.place().resultType())) {
            return zc::none;
          }
        }
        zc::Vector<OwnershipEventRole> commit;
        commit.add(OwnershipEventRole::DestinationWrite);
        slots.add(MirEventSlot{
            MirEventKey{MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)},
                        0},
            OwnershipEventStage::Commit, zc::mv(commit)});
        if (call.effect.commitsOnNormalEdge()) {
          if (call.effect.activatedMutableReceiver() == zc::none) return zc::none;
          zc::Vector<OwnershipEventRole> activationRoles;
          activationRoles.add(OwnershipEventRole::BorrowActivation);
          slots.add(MirEventSlot{
              MirEventKey{
                  MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 1},
              OwnershipEventStage::Commit, zc::mv(activationRoles)});
          auto deferred = projectDeferredActivation(input, function, block, call);
          if (deferred == zc::none) return zc::none;
          ZC_IF_SOME(value, deferred) { deferredActivations.add(zc::mv(value)); }
        }
        const MirEventKey initialization{
            MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 0};
        if (!appendMarkerUse(markerUses, proofs, input, initialization, copy,
                             call.destination.resultType()) ||
            !appendMarkerUse(markerUses, proofs, input, initialization, linear,
                             call.destination.resultType())) {
          return zc::none;
        }
        if (markerUses.size() < 2 ||
            !appendLogicalDropPlan(logicalDropPlans, input, initialization, call.destination,
                                   markerUses[markerUses.size() - 2],
                                   markerUses[markerUses.size() - 1])) {
          return zc::none;
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
    sortDeferredActivations(deferredActivations);
    if (!sortMarkerUses(markerUses, identities, input.body.semanticTypes)) return zc::none;
    if (!sortLogicalDropPlans(logicalDropPlans, identities)) return zc::none;
    auto sourceMap = projectSourceMap(function, slots);
    if (sourceMap == zc::none) return zc::none;
    ZC_IF_SOME(value, sourceMap) {
      functions.add(OwnershipFunctionEventOverlay{function.owner, zc::mv(slots), zc::mv(value),
                                                  zc::mv(deferredActivations), zc::mv(markerUses),
                                                  zc::mv(logicalDropPlans)});
    }
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
    const identity::ContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checker::checked::CheckedFactsRevision& checkedFactsRevision,
    const mir::MirRevisionId& builtRevision,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions) {
  return encodeFramed(contextFingerprint.digest(), expandedModuleKey, checkedFactsRevision.digest(),
                      builtRevision.digest(), canonicalFunctions);
}

zc::Maybe<OwnershipEventOverlayRevision> OwnershipEventOverlayCodec::compute(
    const identity::ContextFingerprint& contextFingerprint,
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
    const OwnershipEventOverlayInput& input) {
  const auto& builtMir = input.built;
  const auto identities = builtMir.retainIdentityAuthority();
  const auto module = builtMir.module();
  if (!hasExactOverlayInput(input)) {
    return rejectOwnership<OwnershipEventOverlayCandidate>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InputRevisionMismatch,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  auto functions = projectCandidateFunctions(input, identities);
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
    OwnershipEventOverlayCandidate&& candidate, const OwnershipEventOverlayInput& input) {
  const auto& builtMir = input.built;
  const auto identities = builtMir.retainIdentityAuthority();
  const auto module = builtMir.module();
  if (!hasExactOverlayInput(input) || candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.checkedFactsRevision.digest() != builtMir.checkedFactsRevision().digest() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.functions.size() != builtMir.functions().size()) {
    return rejectOwnership<VerifiedOwnershipEventOverlay>(
        ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InputRevisionMismatch,
        module, firstFunctionDefinition(builtMir), identities, 0);
  }
  auto expectedFunctions = reconstructExpectedFunctions(input, identities);
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
      if (expectedFunction.sourceMap.size() != candidateFunction.sourceMap.size()) {
        return rejectOwnership<VerifiedOwnershipEventOverlay>(
            ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::AdditionalFact, module,
            expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
      }
      for (size_t sourceIndex = 0; sourceIndex < expectedFunction.sourceMap.size(); ++sourceIndex) {
        const auto& expectedSource = expectedFunction.sourceMap[sourceIndex];
        const auto& candidateSource = candidateFunction.sourceMap[sourceIndex];
        if (expectedSource.key != candidateSource.key ||
            !sameSpan(expectedSource.span, candidateSource.span)) {
          return rejectOwnership<VerifiedOwnershipEventOverlay>(
              ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidFact, module,
              expectedFunction.owner, identities, static_cast<uint32_t>(sourceIndex + 1));
        }
      }
      for (const auto& slot : candidateFunction.slots) {
        if (slot.key.location.owner != candidateFunction.owner) {
          return rejectOwnership<VerifiedOwnershipEventOverlay>(
              ir::IrFailurePhase::OwnershipProofValidation, ir::IrFailureKind::InvalidFact, module,
              expectedFunction.owner, identities, static_cast<uint32_t>(index + 1));
        }
      }
      auto expectedEncoded =
          encodeFunctionOverlay(expectedFunction, identities, input.body.semanticTypes);
      auto candidateEncoded =
          encodeFunctionOverlay(candidateFunction, identities, input.body.semanticTypes);
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
        builtMir.retainAdmittedBoundModule());
    return ir::IrOperationResult<VerifiedOwnershipEventOverlay>::verified(
        VerifiedOwnershipEventOverlay(zc::mv(impl)));
  }
  ZC_UNREACHABLE
}

struct VerifiedOwnershipEventOverlay::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
       checker::checked::CheckedFactsRevision checkedFactsRevision,
       mir::MirRevisionId builtRevision, zc::Vector<OwnershipFunctionEventOverlay>&& functions,
       OwnershipEventOverlayRevision revision, OwnershipAdmittedBoundModule&& boundModule) noexcept
      : boundModule(zc::mv(boundModule)),
        semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        builtRevision(builtRevision),
        functions(zc::mv(functions)),
        revision(revision) {}

  OwnershipAdmittedBoundModule boundModule;
  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
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
const identity::ContextFingerprint& VerifiedOwnershipEventOverlay::contextFingerprint()
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
