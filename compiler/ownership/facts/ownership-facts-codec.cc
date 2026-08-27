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

#include "compiler/ownership/facts/ownership-facts-codec.h"

#include "compiler/checker/facts/signature-facts.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::ownership::facts {
namespace {

namespace signature = checker::signature;

// ---- Shared primitive encoders ----
//
// These encode the same public MIR and overlay types as the event overlay codec.
// The byte layouts match the overlay's canonical encoding so that one type has
// one canonical representation across the ownership library.

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

bool encodeSemanticType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId type,
                        const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  encoder.encodeByteString(lookup.get<type::SemanticTypeLookup>().key().bytes());
  return true;
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

bool encodeLogicalDropAction(identity::CanonicalEncoder& encoder,
                             const zc::Maybe<LogicalDropAction>& action,
                             const checker::CheckerIdentityAuthority& identities,
                             const type::SemanticTypeStore& semanticTypes) {
  if (action == zc::none) {
    encoder.encodeUint8(0x00);
    return true;
  }
  ZC_IF_SOME(value, action) {
    if (value.is<LogicalDropDeclaredAction>()) {
      auto definition = identities.definition(value.get<LogicalDropDeclaredAction>().deinitializer);
      if (definition == zc::none) return false;
      encoder.encodeUint8(0x01);
      ZC_IF_SOME(def, definition) {
        auto bytes = def.key().encode();
        encoder.encodeByteString(bytes.asPtr());
      }
      return true;
    }
    if (value.is<LogicalDropBuiltinAction>()) {
      encoder.encodeUint8(0x02);
      return encodeSemanticType(encoder, value.get<LogicalDropBuiltinAction>().ownerType,
                                semanticTypes);
    }
    encoder.encodeUint8(0x03);
    return encodeSemanticType(encoder, value.get<LogicalDropDynamicAction>().existentialType,
                              semanticTypes);
  }
  return false;
}

// ---- Facts-level primitive encoders ----

zc::Maybe<zc::Array<uint8_t>> encodeMovePathKey(const MovePathKey& key,
                                                const checker::CheckerIdentityAuthority& identities,
                                                const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto owner = identities.definition(key.owner);
  if (owner == zc::none) return zc::none;
  ZC_IF_SOME(def, owner) {
    auto bytes = def.key().encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  auto place = encodePlace(key.place, identities, semanticTypes);
  if (place == zc::none) return zc::none;
  ZC_IF_SOME(bytes, place) { encoder.encodeByteString(bytes.asPtr()); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLinearObligationKey(
    const LinearObligationKey& key, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto introduction = encodeEventKey(key.introduction, identities);
  if (introduction == zc::none) return zc::none;
  ZC_IF_SOME(bytes, introduction) { encoder.encodeByteString(bytes.asPtr()); }
  auto place = encodeMovePathKey(key.place, identities, semanticTypes);
  if (place == zc::none) return zc::none;
  ZC_IF_SOME(bytes, place) { encoder.encodeByteString(bytes.asPtr()); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLinearCarrierKey(
    const LinearCarrierKey& key, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto obligation = encodeLinearObligationKey(key.obligation, identities, semanticTypes);
  if (obligation == zc::none) return zc::none;
  ZC_IF_SOME(bytes, obligation) { encoder.encodeByteString(bytes.asPtr()); }
  auto creation = encodeEventKey(key.creation, identities);
  if (creation == zc::none) return zc::none;
  ZC_IF_SOME(bytes, creation) { encoder.encodeByteString(bytes.asPtr()); }
  auto place = encodeMovePathKey(key.place, identities, semanticTypes);
  if (place == zc::none) return zc::none;
  ZC_IF_SOME(bytes, place) { encoder.encodeByteString(bytes.asPtr()); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLinearTransfer(
    const LinearTransfer& transfer, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto from = encodeMovePathKey(transfer.from, identities, semanticTypes);
  if (from == zc::none) return zc::none;
  ZC_IF_SOME(bytes, from) { encoder.encodeByteString(bytes.asPtr()); }
  auto to = encodeMovePathKey(transfer.to, identities, semanticTypes);
  if (to == zc::none) return zc::none;
  ZC_IF_SOME(bytes, to) { encoder.encodeByteString(bytes.asPtr()); }
  auto event = encodeEventKey(transfer.event, identities);
  if (event == zc::none) return zc::none;
  ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLinearConsumption(
    const LinearConsumption& consumption, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto place = encodeMovePathKey(consumption.place, identities, semanticTypes);
  if (place == zc::none) return zc::none;
  ZC_IF_SOME(bytes, place) { encoder.encodeByteString(bytes.asPtr()); }
  auto event = encodeEventKey(consumption.event, identities);
  if (event == zc::none) return zc::none;
  ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
  encoder.encodeUint8(static_cast<uint8_t>(consumption.kind));
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeOwnershipPoint(
    const OwnershipPoint& point, const checker::CheckerIdentityAuthority& identities) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(point.kind()));
  switch (point.kind()) {
    case OwnershipPointKind::Cfg:
      if (!encodeMirPoint(encoder, point.cfgValue().point)) return zc::none;
      break;
    case OwnershipPointKind::BeforeEvent: {
      auto event = encodeEventKey(point.beforeEventValue().event, identities);
      if (event == zc::none) return zc::none;
      ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
      break;
    }
    case OwnershipPointKind::AfterEvent: {
      auto event = encodeEventKey(point.afterEventValue().event, identities);
      if (event == zc::none) return zc::none;
      ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
      break;
    }
  }
  return encoder.finish();
}

void encodeOrigin(identity::CanonicalEncoder& encoder,
                  const zc::OneOf<ParameterReferenceOrigin, LocalReferenceOrigin>& origin) {
  if (origin.is<ParameterReferenceOrigin>()) {
    encoder.encodeUint8(0x01);
    encoder.encodeUint32(origin.get<ParameterReferenceOrigin>().rootParameter);
  } else {
    encoder.encodeUint8(0x02);
  }
}

// ---- Raw-provenance encoders ----

zc::Maybe<zc::Array<uint8_t>> encodeRawProvenanceCarrierKey(
    const RawProvenanceCarrierKey& key, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto introduction = encodeEventKey(key.introduction, identities);
  if (introduction == zc::none) return zc::none;
  ZC_IF_SOME(bytes, introduction) { encoder.encodeByteString(bytes.asPtr()); }
  auto destination = encodeMovePathKey(key.destination, identities, semanticTypes);
  if (destination == zc::none) return zc::none;
  ZC_IF_SOME(bytes, destination) { encoder.encodeByteString(bytes.asPtr()); }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeRawProvenanceOrigin(
    const RawProvenanceOrigin& origin, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  if (origin.is<RawReferenceOrigin>()) {
    encoder.encodeUint8(0x01);  // Reference
    const auto& ref = origin.get<RawReferenceOrigin>().origin;
    auto entry = encodeEventKey(ref.entry, identities);
    if (entry == zc::none) return zc::none;
    ZC_IF_SOME(bytes, entry) { encoder.encodeByteString(bytes.asPtr()); }
    auto activation = encodeOwnershipPoint(ref.activation, identities);
    if (activation == zc::none) return zc::none;
    ZC_IF_SOME(bytes, activation) { encoder.encodeByteString(bytes.asPtr()); }
    encodeOrigin(encoder, ref.detail);
    auto referent = encodeMovePathKey(ref.referent, identities, semanticTypes);
    if (referent == zc::none) return zc::none;
    ZC_IF_SOME(bytes, referent) { encoder.encodeByteString(bytes.asPtr()); }
  } else if (origin.is<RawInputOrigin>()) {
    encoder.encodeUint8(0x02);  // RawInput
    const auto& input = origin.get<RawInputOrigin>();
    encoder.encodeUint8(input.isReceiver ? 0x01 : 0x00);
    encoder.encodeUint32(input.parameterIndex);
  } else if (origin.is<RawStaticAddressOrigin>()) {
    encoder.encodeUint8(0x03);  // StaticAddress
    auto creation = encodeEventKey(origin.get<RawStaticAddressOrigin>().creation, identities);
    if (creation == zc::none) return zc::none;
    ZC_IF_SOME(bytes, creation) { encoder.encodeByteString(bytes.asPtr()); }
  } else {
    encoder.encodeUint8(0x04);  // UnsafeAddress
    const auto& boundary = origin.get<RawUnsafeAddressOrigin>().boundary;
    auto event = encodeEventKey(boundary.event, identities);
    if (event == zc::none) return zc::none;
    ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
    encoder.encodeUint32(boundary.unsafeOrdinal);
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeRawProvenanceFact(
    const RawProvenanceFact& fact, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  auto key = encodeRawProvenanceCarrierKey(fact.key, identities, semanticTypes);
  if (key == zc::none) return zc::none;
  ZC_IF_SOME(bytes, key) { encoder.encodeByteString(bytes.asPtr()); }
  encoder.encodeSequenceSize(fact.predecessors.size());
  for (const auto& predecessor : fact.predecessors) {
    auto pred = encodeRawProvenanceCarrierKey(predecessor, identities, semanticTypes);
    if (pred == zc::none) return zc::none;
    ZC_IF_SOME(bytes, pred) { encoder.encodeByteString(bytes.asPtr()); }
  }
  encoder.encodeSequenceSize(fact.origins.size());
  for (const auto& origin : fact.origins) {
    auto originBytes = encodeRawProvenanceOrigin(origin, identities, semanticTypes);
    if (originBytes == zc::none) return zc::none;
    ZC_IF_SOME(bytes, originBytes) { encoder.encodeByteString(bytes.asPtr()); }
  }
  return encoder.finish();
}

void encodeOwner(identity::CanonicalEncoder& encoder, identity::DefId owner,
                 const checker::CheckerIdentityAuthority& identities) {
  auto definition = identities.definition(owner);
  if (definition == zc::none) {
    encoder.encodeUint8(0x00);
    return;
  }
  encoder.encodeUint8(0x01);
  ZC_IF_SOME(def, definition) {
    auto bytes = def.key().encode();
    encoder.encodeByteString(bytes.asPtr());
  }
}

// ---- Facts inventory group encoders (groups 1-8) ----

zc::Maybe<zc::Array<uint8_t>> encodeMovePathsGroup(
    const VerifiedOwnershipInputs& inputs, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = inputs.movePaths().functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.facts.size());
    for (const auto& fact : function.facts) {
      auto key = encodeMovePathKey(fact.key, identities, semanticTypes);
      if (key == zc::none) return zc::none;
      ZC_IF_SOME(bytes, key) { encoder.encodeByteString(bytes.asPtr()); }
      if (fact.parent == zc::none) {
        encoder.encodeUint8(0x00);
      } else {
        encoder.encodeUint8(0x01);
        ZC_IF_SOME(parent, fact.parent) {
          auto parentKey = encodeMovePathKey(parent, identities, semanticTypes);
          if (parentKey == zc::none) return zc::none;
          ZC_IF_SOME(bytes, parentKey) { encoder.encodeByteString(bytes.asPtr()); }
        }
      }
    }
    encoder.encodeSequenceSize(function.conflicts.size());
    for (const auto& conflict : function.conflicts) {
      auto first = encodeMovePathKey(conflict.first, identities, semanticTypes);
      auto second = encodeMovePathKey(conflict.second, identities, semanticTypes);
      if (first == zc::none || second == zc::none) return zc::none;
      ZC_IF_SOME(bytes, first) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, second) { encoder.encodeByteString(bytes.asPtr()); }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeFlowGroup(const VerifiedOwnershipInputs& inputs,
                                              const checker::CheckerIdentityAuthority& identities) {
  identity::CanonicalEncoder encoder;
  const auto functions = inputs.flow().functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.points.size());
    for (const auto& point : function.points) {
      auto bytes = encodeOwnershipPoint(point, identities);
      if (bytes == zc::none) return zc::none;
      ZC_IF_SOME(value, bytes) { encoder.encodeByteString(value.asPtr()); }
    }
    encoder.encodeSequenceSize(function.edges.size());
    for (const auto& edge : function.edges) {
      auto from = encodeOwnershipPoint(edge.from, identities);
      auto to = encodeOwnershipPoint(edge.to, identities);
      if (from == zc::none || to == zc::none) return zc::none;
      ZC_IF_SOME(value, from) { encoder.encodeByteString(value.asPtr()); }
      ZC_IF_SOME(value, to) { encoder.encodeByteString(value.asPtr()); }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeInitGroup(const VerifiedOwnershipInputs& inputs,
                                              const checker::CheckerIdentityAuthority& identities,
                                              const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = inputs.initialization().functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.facts.size());
    for (const auto& fact : function.facts) {
      identity::CanonicalEncoder pointEncoder;
      if (!encodeMirPoint(pointEncoder, fact.point)) return zc::none;
      auto pointBytes = pointEncoder.finish();
      encoder.encodeByteString(pointBytes.asPtr());
      auto key = encodeMovePathKey(fact.key, identities, semanticTypes);
      if (key == zc::none) return zc::none;
      ZC_IF_SOME(bytes, key) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeBool(fact.state.storageLive);
      encoder.encodeBool(fact.state.mayBeInitialized);
      encoder.encodeBool(fact.state.mustBeInitialized);
      encoder.encodeSequenceSize(fact.lossCauses.size());
      for (const auto& cause : fact.lossCauses) {
        encoder.encodeUint8(static_cast<uint8_t>(cause.kind));
        auto event = encodeEventKey(cause.event, identities);
        if (event == zc::none) return zc::none;
        ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
        auto path = encodeMovePathKey(cause.path, identities, semanticTypes);
        if (path == zc::none) return zc::none;
        ZC_IF_SOME(bytes, path) { encoder.encodeByteString(bytes.asPtr()); }
      }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeLoansGroup(const VerifiedOwnershipInputs& inputs,
                                               const checker::CheckerIdentityAuthority& identities,
                                               const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto loans = inputs.loans().loans();
  encoder.encodeSequenceSize(loans.size());
  for (const auto& loan : loans) {
    encodeOwner(encoder, loan.owner, identities);
    auto issue = encodeEventKey(loan.issue, identities);
    auto commit = encodeEventKey(loan.commit, identities);
    if (issue == zc::none || commit == zc::none) return zc::none;
    ZC_IF_SOME(bytes, issue) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, commit) { encoder.encodeByteString(bytes.asPtr()); }
    encoder.encodeUint8(static_cast<uint8_t>(loan.kind));
    auto activeFrom = encodeOwnershipPoint(loan.activeFrom, identities);
    if (activeFrom == zc::none) return zc::none;
    ZC_IF_SOME(bytes, activeFrom) { encoder.encodeByteString(bytes.asPtr()); }
    auto source = encodeMovePathKey(loan.source, identities, semanticTypes);
    auto destination = encodeMovePathKey(loan.destination, identities, semanticTypes);
    if (source == zc::none || destination == zc::none) return zc::none;
    ZC_IF_SOME(bytes, source) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, destination) { encoder.encodeByteString(bytes.asPtr()); }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeReferenceDefinitionsGroup(
    const VerifiedOwnershipInputs& inputs, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto definitions = inputs.references().definitions();
  encoder.encodeSequenceSize(definitions.size());
  for (const auto& definition : definitions) {
    encodeOwner(encoder, definition.owner, identities);
    auto introduction = encodeEventKey(definition.introduction, identities);
    auto loan = encodeEventKey(definition.loan, identities);
    auto returned = encodeEventKey(definition.returned, identities);
    if (introduction == zc::none || loan == zc::none || returned == zc::none) return zc::none;
    ZC_IF_SOME(bytes, introduction) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, loan) { encoder.encodeByteString(bytes.asPtr()); }
    auto entry = encodeEventKey(definition.origin.entry, identities);
    if (entry == zc::none) return zc::none;
    ZC_IF_SOME(bytes, entry) { encoder.encodeByteString(bytes.asPtr()); }
    auto activation = encodeOwnershipPoint(definition.origin.activation, identities);
    if (activation == zc::none) return zc::none;
    ZC_IF_SOME(bytes, activation) { encoder.encodeByteString(bytes.asPtr()); }
    encodeOrigin(encoder, definition.origin.detail);
    auto referent = encodeMovePathKey(definition.origin.referent, identities, semanticTypes);
    if (referent == zc::none) return zc::none;
    ZC_IF_SOME(bytes, referent) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, returned) { encoder.encodeByteString(bytes.asPtr()); }
    auto destination = encodeMovePathKey(definition.destination, identities, semanticTypes);
    if (destination == zc::none) return zc::none;
    ZC_IF_SOME(bytes, destination) { encoder.encodeByteString(bytes.asPtr()); }
    const auto& live = definition.livePoints;
    auto afterCommit = encodeOwnershipPoint(live.afterCommit, identities);
    auto afterCommitCfg = encodeOwnershipPoint(live.afterCommitCfg, identities);
    auto beforeReturnCfg = encodeOwnershipPoint(live.beforeReturnCfg, identities);
    auto beforeReturn = encodeOwnershipPoint(live.beforeReturn, identities);
    auto afterReturn = encodeOwnershipPoint(live.afterReturn, identities);
    if (afterCommit == zc::none || afterCommitCfg == zc::none || beforeReturnCfg == zc::none ||
        beforeReturn == zc::none || afterReturn == zc::none) {
      return zc::none;
    }
    ZC_IF_SOME(bytes, afterCommit) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, afterCommitCfg) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, beforeReturnCfg) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, beforeReturn) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, afterReturn) { encoder.encodeByteString(bytes.asPtr()); }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeRegionsGroup(
    const VerifiedOwnershipInputs& inputs, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto regions = inputs.regions().regions();
  encoder.encodeSequenceSize(regions.size());
  for (const auto& region : regions) {
    encodeOwner(encoder, region.owner, identities);
    auto entry = encodeEventKey(region.entry, identities);
    auto loan = encodeEventKey(region.loan, identities);
    if (entry == zc::none || loan == zc::none) return zc::none;
    ZC_IF_SOME(bytes, entry) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, loan) { encoder.encodeByteString(bytes.asPtr()); }
    encodeOrigin(encoder, region.origin);
    encoder.encodeSequenceSize(region.members.size());
    for (const auto& member : region.members) {
      auto point = encodeOwnershipPoint(member, identities);
      if (point == zc::none) return zc::none;
      ZC_IF_SOME(bytes, point) { encoder.encodeByteString(bytes.asPtr()); }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeStatesGroup(const VerifiedOwnershipInputs& inputs,
                                                const checker::CheckerIdentityAuthority& identities,
                                                const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto states = inputs.states().states();
  encoder.encodeSequenceSize(states.size());
  for (const auto& state : states) {
    encodeOwner(encoder, state.owner, identities);
    auto point = encodeOwnershipPoint(state.point, identities);
    auto loan = encodeEventKey(state.loan, identities);
    if (point == zc::none || loan == zc::none) return zc::none;
    ZC_IF_SOME(bytes, point) { encoder.encodeByteString(bytes.asPtr()); }
    ZC_IF_SOME(bytes, loan) { encoder.encodeByteString(bytes.asPtr()); }
    encodeOrigin(encoder, state.origin);
    auto destination = encodeMovePathKey(state.destination, identities, semanticTypes);
    if (destination == zc::none) return zc::none;
    ZC_IF_SOME(bytes, destination) { encoder.encodeByteString(bytes.asPtr()); }
  }
  return encoder.finish();
}

bool encodeDropResourceSubject(identity::CanonicalEncoder& encoder,
                               const DropResourceSubject& subject,
                               const checker::CheckerIdentityAuthority& identities,
                               const type::SemanticTypeStore& semanticTypes) {
  auto introduction = encodeEventKey(subject.introduction, identities);
  auto origin = encodeMovePathKey(subject.origin, identities, semanticTypes);
  if (introduction == zc::none || origin == zc::none) return false;
  ZC_IF_SOME(bytes, introduction) { encoder.encodeByteString(bytes.asPtr()); }
  ZC_IF_SOME(bytes, origin) { encoder.encodeByteString(bytes.asPtr()); }
  return encodeSemanticType(encoder, subject.originType, semanticTypes);
}

zc::Maybe<zc::Array<uint8_t>> encodeResourcesGroup(
    const VerifiedOwnershipInputs& inputs, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = inputs.resources().functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.facts.size());
    for (const auto& fact : function.facts) {
      if (!encodeDropResourceSubject(encoder, fact.subject, identities, semanticTypes)) {
        return zc::none;
      }
      encoder.encodeUint8(static_cast<uint8_t>(fact.requirement));
      if (!encodeLogicalDropAction(encoder, fact.dropAction, identities, semanticTypes)) {
        return zc::none;
      }
      encoder.encodeUint32(fact.declarationOrdinal);
    }
    encoder.encodeSequenceSize(function.transfers.size());
    for (const auto& transfer : function.transfers) {
      auto from = encodeMovePathKey(transfer.from, identities, semanticTypes);
      auto to = encodeMovePathKey(transfer.to, identities, semanticTypes);
      auto event = encodeEventKey(transfer.event, identities);
      if (from == zc::none || to == zc::none || event == zc::none) return zc::none;
      ZC_IF_SOME(bytes, from) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, to) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
    }
    encoder.encodeSequenceSize(function.castRoutes.size());
    for (const auto& route : function.castRoutes) {
      if (!encodeDropResourceSubject(encoder, route.subject, identities, semanticTypes)) {
        return zc::none;
      }
      auto from = encodeMovePathKey(route.from, identities, semanticTypes);
      auto to = encodeMovePathKey(route.to, identities, semanticTypes);
      auto event = encodeEventKey(route.event, identities);
      if (from == zc::none || to == zc::none || event == zc::none) return zc::none;
      ZC_IF_SOME(bytes, from) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, to) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
    }
    encoder.encodeSequenceSize(function.dropPlans.size());
    for (const auto& plan : function.dropPlans) {
      if (!encodeDropResourceSubject(encoder, plan.subject, identities, semanticTypes)) {
        return zc::none;
      }
      encoder.encodeUint8(static_cast<uint8_t>(plan.mode));
      encoder.encodeSequenceSize(plan.components.size());
      for (const auto& component : plan.components) {
        encoder.encodeUint32(component.factOrdinal);
        if (!encodeLogicalDropAction(encoder, component.action, identities, semanticTypes)) {
          return zc::none;
        }
      }
    }
    encoder.encodeSequenceSize(function.linearObligations.size());
    for (const auto& obligation : function.linearObligations) {
      auto obligationKey = encodeLinearObligationKey(obligation.key, identities, semanticTypes);
      if (obligationKey == zc::none) return zc::none;
      ZC_IF_SOME(bytes, obligationKey) { encoder.encodeByteString(bytes.asPtr()); }
      if (!encodeSemanticType(encoder, obligation.subject, semanticTypes)) return zc::none;
      encoder.encodeSequenceSize(obligation.transfers.size());
      for (const auto& transfer : obligation.transfers) {
        auto transferBytes = encodeLinearTransfer(transfer, identities, semanticTypes);
        if (transferBytes == zc::none) return zc::none;
        ZC_IF_SOME(bytes, transferBytes) { encoder.encodeByteString(bytes.asPtr()); }
      }
      encoder.encodeSequenceSize(obligation.consumptions.size());
      for (const auto& consumption : obligation.consumptions) {
        auto consumptionBytes = encodeLinearConsumption(consumption, identities, semanticTypes);
        if (consumptionBytes == zc::none) return zc::none;
        ZC_IF_SOME(bytes, consumptionBytes) { encoder.encodeByteString(bytes.asPtr()); }
      }
    }
    encoder.encodeSequenceSize(function.linearCarriers.size());
    for (const auto& carrier : function.linearCarriers) {
      auto carrierKey = encodeLinearCarrierKey(carrier.key, identities, semanticTypes);
      if (carrierKey == zc::none) return zc::none;
      ZC_IF_SOME(bytes, carrierKey) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeSequenceSize(carrier.incoming.size());
      for (const auto& transition : carrier.incoming) {
        auto pred = encodeLinearCarrierKey(transition.predecessor, identities, semanticTypes);
        if (pred == zc::none) return zc::none;
        ZC_IF_SOME(bytes, pred) { encoder.encodeByteString(bytes.asPtr()); }
        auto transferBytes = encodeLinearTransfer(transition.transfer, identities, semanticTypes);
        if (transferBytes == zc::none) return zc::none;
        ZC_IF_SOME(bytes, transferBytes) { encoder.encodeByteString(bytes.asPtr()); }
      }
    }
    encoder.encodeSequenceSize(function.linearSccs.size());
    for (const auto& scc : function.linearSccs) {
      encoder.encodeSequenceSize(scc.carriers.size());
      for (const auto& carrier : scc.carriers) {
        auto carrierBytes = encodeLinearCarrierKey(carrier, identities, semanticTypes);
        if (carrierBytes == zc::none) return zc::none;
        ZC_IF_SOME(bytes, carrierBytes) { encoder.encodeByteString(bytes.asPtr()); }
      }
    }
    encoder.encodeSequenceSize(function.rawOriginUniverse.size());
    for (const auto& origin : function.rawOriginUniverse) {
      auto originBytes = encodeRawProvenanceOrigin(origin, identities, semanticTypes);
      if (originBytes == zc::none) return zc::none;
      ZC_IF_SOME(bytes, originBytes) { encoder.encodeByteString(bytes.asPtr()); }
    }
    encoder.encodeSequenceSize(function.rawProvenance.size());
    for (const auto& fact : function.rawProvenance) {
      auto factBytes = encodeRawProvenanceFact(fact, identities, semanticTypes);
      if (factBytes == zc::none) return zc::none;
      ZC_IF_SOME(bytes, factBytes) { encoder.encodeByteString(bytes.asPtr()); }
    }
  }
  return encoder.finish();
}

// ---- Overlay-derived group encoders (groups 9-12) ----

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
    auto record =
        signature::SignatureFactsCanonicalCodec::encodeMarkerFact(proof, identities, semanticTypes);
    if (record == zc::none) return zc::none;
    encoder.encodeUint8(0x01);
    ZC_IF_SOME(value, record) { encoder.encodeByteString(value.asPtr()); }
  } else if (use.decision.is<OwnershipMarkerDecisionExplicitNegative>()) {
    const auto& proof = use.decision.get<OwnershipMarkerDecisionExplicitNegative>().explicitFact;
    auto record =
        signature::SignatureFactsCanonicalCodec::encodeMarkerFact(proof, identities, semanticTypes);
    if (record == zc::none) return zc::none;
    encoder.encodeUint8(0x02);
    ZC_IF_SOME(value, record) { encoder.encodeByteString(value.asPtr()); }
  } else {
    encoder.encodeUint8(0x03);
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeRouteProof(const CastResourceRouteProof& proof,
                                               const checker::CheckerIdentityAuthority& identities,
                                               const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  if (proof.is<CastResourceRouteIdentity>()) {
    encoder.encodeUint8(0x01);
  } else if (proof.is<CastResourceRouteUnionInject>()) {
    encoder.encodeUint8(0x02);
    if (!encodeSemanticType(encoder, proof.get<CastResourceRouteUnionInject>().alternative,
                            semanticTypes)) {
      return zc::none;
    }
  } else if (proof.is<CastResourceRouteDynErase>()) {
    encoder.encodeUint8(0x03);
    const auto& erase = proof.get<CastResourceRouteDynErase>();
    auto interfaceDef = identities.definition(erase.interface.interface);
    if (interfaceDef == zc::none) return zc::none;
    ZC_IF_SOME(value, interfaceDef) {
      auto bytes = value.key().encode();
      encoder.encodeByteString(bytes.asPtr());
    }
    encoder.encodeSequenceSize(erase.interface.arguments.size());
    for (const auto arg : erase.interface.arguments) {
      if (!encodeSemanticType(encoder, arg, semanticTypes)) return zc::none;
    }
    auto impl = identities.implementation(erase.impl);
    if (impl == zc::none) return zc::none;
    ZC_IF_SOME(value, impl) {
      auto bytes = value.key().encode();
      encoder.encodeByteString(bytes.asPtr());
    }
    encoder.encodeUint8(erase.witnesses.isValid() ? 0x01 : 0x00);
  } else if (proof.is<CastResourceRouteDynUpcast>()) {
    encoder.encodeUint8(0x04);
    const auto& upcast = proof.get<CastResourceRouteDynUpcast>();
    encoder.encodeSequenceSize(upcast.path.size());
    for (const auto def : upcast.path) {
      auto entry = identities.definition(def);
      if (entry == zc::none) return zc::none;
      ZC_IF_SOME(value, entry) {
        auto bytes = value.key().encode();
        encoder.encodeByteString(bytes.asPtr());
      }
    }
  } else if (proof.is<CastResourceRouteCheckedPayload>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeUint8(static_cast<uint8_t>(proof.get<CastResourceRouteCheckedPayload>().kind));
  } else {
    return zc::none;
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeDropPlansGroup(
    const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = overlay.functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.logicalDropPlans.size());
    for (const auto& plan : function.logicalDropPlans) {
      auto initialization = encodeEventKey(plan.initialization, identities);
      auto root = encodePlace(plan.root, identities, semanticTypes);
      if (initialization == zc::none || root == zc::none) return zc::none;
      ZC_IF_SOME(bytes, initialization) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, root) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeSequenceSize(plan.components.size());
      for (const auto& component : plan.components) {
        auto place = encodePlace(component.place, identities, semanticTypes);
        auto copy = encodeMarkerUseKey(component.copyDecision, identities, semanticTypes);
        auto linear = encodeMarkerUseKey(component.linearDecision, identities, semanticTypes);
        if (place == zc::none || copy == zc::none || linear == zc::none) return zc::none;
        ZC_IF_SOME(bytes, place) { encoder.encodeByteString(bytes.asPtr()); }
        if (!encodeSemanticType(encoder, component.valueType, semanticTypes)) return zc::none;
        if (!encodeLogicalDropAction(encoder, component.dropAction, identities, semanticTypes)) {
          return zc::none;
        }
        ZC_IF_SOME(bytes, copy) { encoder.encodeByteString(bytes.asPtr()); }
        ZC_IF_SOME(bytes, linear) { encoder.encodeByteString(bytes.asPtr()); }
        encoder.encodeUint32(component.declarationOrdinal);
      }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeUnsafeOccurrencesGroup(
    const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities) {
  identity::CanonicalEncoder encoder;
  const auto functions = overlay.functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.unsafeOccurrences.size());
    for (const auto& occurrence : function.unsafeOccurrences) {
      auto event = encodeEventKey(occurrence.key.event, identities);
      if (event == zc::none) return zc::none;
      ZC_IF_SOME(bytes, event) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeUint32(occurrence.key.unsafeOrdinal);
      encoder.encodeUint8(static_cast<uint8_t>(occurrence.operation));
      encoder.encodeUint8(static_cast<uint8_t>(occurrence.requirement));
      if (occurrence.acknowledgement == zc::none) {
        encoder.encodeUint8(0x00);
      } else {
        encoder.encodeUint8(0x01);
        ZC_IF_SOME(ackEvent, occurrence.acknowledgement) {
          auto ack = encodeEventKey(ackEvent, identities);
          if (ack == zc::none) return zc::none;
          ZC_IF_SOME(bytes, ack) { encoder.encodeByteString(bytes.asPtr()); }
        }
      }
      occurrence.sourceSpan.encode(encoder);
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeCastResourcePlansGroup(
    const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = overlay.functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.castResourcePlans.size());
    for (const auto& plan : function.castResourcePlans) {
      auto check = encodeEventKey(plan.key.check, identities);
      if (check == zc::none) return zc::none;
      ZC_IF_SOME(bytes, check) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeUint8(static_cast<uint8_t>(plan.mode));
      encoder.encodeUint8(static_cast<uint8_t>(plan.kind));
      if (!encodeSemanticType(encoder, plan.carrierType, semanticTypes) ||
          !encodeSemanticType(encoder, plan.targetType, semanticTypes) ||
          !encodeSemanticType(encoder, plan.resultType, semanticTypes)) {
        return zc::none;
      }
      auto carrierPlan = encodeEventKey(plan.carrierPlan, identities);
      auto successPlan = encodeEventKey(plan.successPlan, identities);
      if (carrierPlan == zc::none || successPlan == zc::none) return zc::none;
      ZC_IF_SOME(bytes, carrierPlan) { encoder.encodeByteString(bytes.asPtr()); }
      ZC_IF_SOME(bytes, successPlan) { encoder.encodeByteString(bytes.asPtr()); }
      encoder.encodeSequenceSize(plan.routes.size());
      for (const auto& route : plan.routes) {
        auto carrier = encodePlace(route.carrier, identities, semanticTypes);
        auto result = encodePlace(route.result, identities, semanticTypes);
        auto proof = encodeRouteProof(route.proof, identities, semanticTypes);
        if (carrier == zc::none || result == zc::none || proof == zc::none) return zc::none;
        ZC_IF_SOME(bytes, carrier) { encoder.encodeByteString(bytes.asPtr()); }
        ZC_IF_SOME(bytes, result) { encoder.encodeByteString(bytes.asPtr()); }
        ZC_IF_SOME(bytes, proof) { encoder.encodeByteString(bytes.asPtr()); }
      }
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> encodeMarkerDecisionsGroup(
    const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  identity::CanonicalEncoder encoder;
  const auto functions = overlay.functions();
  encoder.encodeSequenceSize(functions.size());
  for (const auto& function : functions) {
    encodeOwner(encoder, function.owner, identities);
    encoder.encodeSequenceSize(function.markerUses.size());
    for (const auto& use : function.markerUses) {
      auto record = encodeMarkerUse(use, identities, semanticTypes);
      if (record == zc::none) return zc::none;
      ZC_IF_SOME(bytes, record) { encoder.encodeByteString(bytes.asPtr()); }
    }
  }
  return encoder.finish();
}

// ---- Metadata group encoder (group 13) ----

zc::Maybe<zc::Array<uint8_t>> encodeMetadataGroup(const VerifiedOwnershipInputs& inputs) {
  identity::CanonicalEncoder encoder;
  encoder.encodeBool(inputs.semanticContext().isValid());
  encoder.encodeDigest(inputs.builtRevision().digest());
  encoder.encodeDigest(inputs.overlayRevision().digest());
  encoder.encodeDigest(inputs.borrowEvidenceRevision().digest());
  return encoder.finish();
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> OwnershipFactsCodec::encodeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    zc::ArrayPtr<const zc::Array<uint8_t>> canonicalGroups) {
  if (expandedModuleKey.size() == 0 || canonicalGroups.size() != 13) return zc::none;
  identity::CanonicalEncoder encoder;
  constexpr char domain[] = "zom.ownership-facts";
  for (size_t index = 0; index + 1 < sizeof(domain); ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0x00);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeSequenceSize(canonicalGroups.size());
  for (const auto& group : canonicalGroups) {
    if (group.size() == 0) return zc::none;
    encoder.encodeByteString(group.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<zc::Array<uint8_t>> OwnershipFactsCodec::encode(
    const VerifiedOwnershipInputs& inputs, const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  auto moduleEntry = identities.module(inputs.module());
  if (moduleEntry == zc::none) return zc::none;
  zc::Array<uint8_t> expandedModuleKey;
  ZC_IF_SOME(entry, moduleEntry) { expandedModuleKey = entry.key().encode(); }

  zc::Vector<zc::Array<uint8_t>> groups;
  auto addGroup = [&groups](zc::Maybe<zc::Array<uint8_t>>&& record) -> bool {
    if (record == zc::none) return false;
    ZC_IF_SOME(bytes, record) { groups.add(zc::mv(bytes)); }
    return true;
  };

  if (!addGroup(encodeMovePathsGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeFlowGroup(inputs, identities))) return zc::none;
  if (!addGroup(encodeInitGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeLoansGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeReferenceDefinitionsGroup(inputs, identities, semanticTypes)))
    return zc::none;
  if (!addGroup(encodeRegionsGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeStatesGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeResourcesGroup(inputs, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeDropPlansGroup(overlay, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeUnsafeOccurrencesGroup(overlay, identities))) return zc::none;
  if (!addGroup(encodeCastResourcePlansGroup(overlay, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeMarkerDecisionsGroup(overlay, identities, semanticTypes))) return zc::none;
  if (!addGroup(encodeMetadataGroup(inputs))) return zc::none;

  return encodeFramed(inputs.contextFingerprint().digest(), expandedModuleKey.asPtr(),
                      groups.asPtr());
}

zc::Maybe<OwnershipFactsRevision> OwnershipFactsCodec::compute(
    const VerifiedOwnershipInputs& inputs, const VerifiedOwnershipEventOverlay& overlay,
    const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes) {
  auto bytes = encode(inputs, overlay, identities, semanticTypes);
  if (bytes == zc::none) return zc::none;
  ZC_IF_SOME(value, bytes) {
    auto digest = identity::sha256(value.asPtr());
    ZC_IF_SOME(hash, digest) { return OwnershipFactsRevision::fromDigest(hash); }
  }
  return zc::none;
}

}  // namespace zomlang::compiler::ownership::facts
