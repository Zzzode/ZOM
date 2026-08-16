// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checked-facts.h"

#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::checker::checked {
namespace {

bool authorizedDefinition(identity::DefId definition, identity::ModuleId module,
                          const CheckedFactsVerificationInput& input);
bool validPlace(const CheckedPlaceFact& fact, const CheckedFactsCandidate& candidate,
                const CheckedFactsVerificationInput& input);
bool validCanonicalValue(const CanonicalConstValue& value, identity::ModuleId module,
                         const CheckedFactsVerificationInput& input);
bool validPatternConstructor(const PatternConstructor& constructor, identity::ModuleId module,
                             const CheckedFactsVerificationInput& input);
bool validDisplayArgument(const CheckerDisplayArgument& argument, identity::ModuleId module,
                          const CheckedFactsVerificationInput& input);

void encodeAscii(identity::CanonicalEncoder& encoder, zc::StringPtr value) {
  for (const auto character : value) { encoder.encodeUint8(static_cast<uint8_t>(character)); }
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  return !lessBytes(left, right) && !lessBytes(right, left);
}

void encodeRaw(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  for (const auto byte : bytes) encoder.encodeUint8(byte);
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  auto leftBytes = leftEncoder.finish();
  auto rightBytes = rightEncoder.finish();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameSemanticOptions(const identity::SemanticCompilerOptionsKey& left,
                         const identity::SemanticCompilerOptionsKey& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  auto leftBytes = leftEncoder.finish();
  auto rightBytes = rightEncoder.finish();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameBindingTarget(const binder::BindingTarget& left,
                       const binder::BindingTarget& right) noexcept {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DefinitionBindingTarget>()) {
    return rightValue.is<binder::DefinitionBindingTarget>() &&
           leftValue.get<binder::DefinitionBindingTarget>().definition ==
               rightValue.get<binder::DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<binder::GenericParameterBindingTarget>()) {
    return rightValue.is<binder::GenericParameterBindingTarget>() &&
           leftValue.get<binder::GenericParameterBindingTarget>().parameter ==
               rightValue.get<binder::GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::CallableParameterBindingTarget>()) {
    return rightValue.is<binder::CallableParameterBindingTarget>() &&
           leftValue.get<binder::CallableParameterBindingTarget>().parameter ==
               rightValue.get<binder::CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::OwnerLocalBindingTarget>()) {
    return rightValue.is<binder::OwnerLocalBindingTarget>() &&
           leftValue.get<binder::OwnerLocalBindingTarget>().binding ==
               rightValue.get<binder::OwnerLocalBindingTarget>().binding;
  }
  return rightValue.is<binder::ModuleBindingTarget>() &&
         leftValue.get<binder::ModuleBindingTarget>().module ==
             rightValue.get<binder::ModuleBindingTarget>().module;
}

template <typename Map, typename Key>
zc::Maybe<const typename Map::Entry&> factEntry(const Map& map, const Key& key) {
  for (const auto& entry : map.entries()) {
    if (entry.key == key) return entry;
  }
  return zc::none;
}

class CheckedFactEncoder final {
public:
  CheckedFactEncoder(const CheckedFactsCandidate& candidate,
                     const CheckedFactsVerificationInput& input)
      : candidate(candidate), input(input) {}

  zc::Maybe<zc::Array<uint8_t>> substitution(uint32_t recordIndex) {
    if (recordIndex >= candidate.substitutionStore.records().size()) return zc::none;
    identity::CanonicalEncoder encoder;
    if (!encodeSubstitutionData(encoder,
                                candidate.substitutionStore.records()[recordIndex].value)) {
      return zc::none;
    }
    return encoder.finish();
  }

  zc::Maybe<zc::Array<uint8_t>> witness(uint32_t recordIndex) {
    if (recordIndex >= candidate.witnessStore.records().size()) return zc::none;
    zc::Vector<uint8_t> active(candidate.witnessStore.records().size());
    for (size_t index = 0; index < candidate.witnessStore.records().size(); ++index) active.add(0);
    identity::CanonicalEncoder encoder;
    if (!encodeWitnessByIndex(encoder, recordIndex, active)) return zc::none;
    return encoder.finish();
  }

  zc::Maybe<identity::Sha256Digest> constantRevision(const ConstantEvaluationFact& fact) const {
    identity::CanonicalEncoder encoder;
    encodeAscii(encoder, "zom.constant-evaluation"_zcc);
    encoder.encodeUint8(0);
    encoder.encodeDigest(input.contextFingerprint.digest());
    if (!encodeDefinition(encoder, fact.definition) || !encodeType(encoder, fact.type) ||
        !encodeConstValue(encoder, fact.value)) {
      return zc::none;
    }
    zc::Vector<zc::Array<uint8_t>> dependencies(fact.dependencies.size());
    for (const auto& dependency : fact.dependencies.entries()) {
      identity::CanonicalEncoder dependencyEncoder;
      if (!encodeDefinition(dependencyEncoder, dependency.key)) return zc::none;
      dependencyEncoder.encodeDigest(dependency.value);
      dependencies.add(dependencyEncoder.finish());
    }
    for (size_t index = 1; index < dependencies.size(); ++index) {
      auto current = zc::mv(dependencies[index]);
      size_t insertion = index;
      while (insertion > 0 && lessBytes(current.asPtr(), dependencies[insertion - 1].asPtr())) {
        dependencies[insertion] = zc::mv(dependencies[insertion - 1]);
        --insertion;
      }
      dependencies[insertion] = zc::mv(current);
    }
    for (size_t index = 1; index < dependencies.size(); ++index) {
      if (sameBytes(dependencies[index - 1].asPtr(), dependencies[index].asPtr())) {
        return zc::none;
      }
    }
    encoder.encodeSequenceSize(dependencies.size());
    for (const auto& dependency : dependencies) encodeRaw(encoder, dependency.asPtr());
    return identity::sha256(encoder.finish().asPtr());
  }

  zc::Maybe<zc::Array<uint8_t>> nodeFact(CheckedFactGroup group, ast::NodeId node) {
    auto key = checkedNode(node);
    if (key == zc::none) return zc::none;
    identity::CanonicalEncoder encoder;
    bool encoded = false;
    ZC_IF_SOME(nodeKey, key) {
      switch (group) {
        case CheckedFactGroup::NodeType:
          encoded = encodeNodeType(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Literal:
          encoded = encodeLiteral(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Aggregate:
          encoded = encodeAggregate(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Place:
          encoded = encodePlaceMapFact(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Coercion:
          encoded = encodeCoercionMapFact(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Cast:
          encoded = encodeCast(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Call:
          encoded = encodeCall(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::CompoundAssignment:
          encoded = encodeCompoundAssignment(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Member:
          encoded = encodeMember(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Index:
          encoded = encodeIndex(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Pattern:
          encoded = encodePatternMapFact(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::ObservedOperation:
          encoded = encodeObservedOperation(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::MarkerObligation:
          encoded = encodeMarkerObligation(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Exhaustiveness:
          encoded = encodeExhaustiveness(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::UnsafeOperation:
          encoded = encodeUnsafeOperation(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Projection:
          encoded = encodeProjection(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::Obligation:
          encoded = encodeObligation(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::ErrorUnionShape:
          encoded = encodeErrorUnionShape(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::ErrorOperator:
          encoded = encodeErrorOperator(encoder, nodeKey, node);
          break;
        case CheckedFactGroup::DefinitionType:
        case CheckedFactGroup::Constant:
        case CheckedFactGroup::Capture:
          break;
      }
    }
    if (!encoded) return zc::none;
    return encoder.finish();
  }

  zc::Maybe<zc::Array<uint8_t>> definitionFact(CheckedFactGroup group, identity::DefId definition) {
    identity::CanonicalEncoder encoder;
    bool encoded = false;
    if (group == CheckedFactGroup::DefinitionType) {
      ZC_IF_SOME(entry, factEntry(candidate.definitionTypes, definition)) {
        encoded = encodeDefinition(encoder, entry.key) && encodeType(encoder, entry.value);
      }
    } else if (group == CheckedFactGroup::Constant) {
      ZC_IF_SOME(entry, factEntry(candidate.constants, definition)) {
        encoded = encodeConstant(encoder, entry.value);
      }
    }
    if (!encoded) return zc::none;
    return encoder.finish();
  }

  zc::Maybe<zc::Array<uint8_t>> captureFact(const CaptureKey& key) {
    identity::CanonicalEncoder encoder;
    bool encoded = false;
    ZC_IF_SOME(entry, factEntry(candidate.captures, key)) {
      const auto& fact = entry.value;
      auto node = checkedNode(fact.place.node);
      ZC_IF_SOME(nodeKey, node) {
        encoded = fact.closure == key.closure && sameBindingTarget(fact.target, key.target) &&
                  encodeClosure(encoder, fact.closure) &&
                  encodeCaptureTarget(encoder, fact.closure, fact.target) &&
                  encodePlace(encoder, nodeKey, fact.place) &&
                  enumInRange(fact.mode, CaptureMode::SharedReference, CaptureMode::Copy) &&
                  enumInRange(fact.origin, CaptureOrigin::Explicit, CaptureOrigin::Inferred);
        if (encoded) {
          encoder.encodeUint8(static_cast<uint8_t>(fact.mode));
          encoder.encodeUint8(static_cast<uint8_t>(fact.origin));
          encoded = encodeType(encoder, fact.capturedType);
          if (encoded) fact.sourceSpan.encode(encoder);
        }
      }
    }
    if (!encoded) return zc::none;
    return encoder.finish();
  }

private:
  template <typename Enum>
  static bool enumInRange(Enum value, Enum first, Enum last) noexcept {
    return value >= first && value <= last;
  }

  template <typename Items, typename EncodeItem>
  bool encodeSortedUnique(identity::CanonicalEncoder& encoder, const Items& items,
                          EncodeItem&& encodeItem) const {
    zc::Vector<zc::Array<uint8_t>> records(items.size());
    for (const auto& item : items) {
      identity::CanonicalEncoder itemEncoder;
      if (!encodeItem(itemEncoder, item)) return false;
      records.add(itemEncoder.finish());
    }
    for (size_t index = 1; index < records.size(); ++index) {
      if (!lessBytes(records[index - 1].asPtr(), records[index].asPtr())) return false;
    }
    encoder.encodeSequenceSize(records.size());
    for (const auto& record : records) encodeRaw(encoder, record.asPtr());
    return true;
  }

  zc::Maybe<const CheckedNodeKey&> checkedNode(ast::NodeId node) const {
    zc::Maybe<const CheckedNodeKey&> result;
    for (const auto& requirement : input.nodeRequirements) {
      if (requirement.node != node) continue;
      if (result == zc::none) {
        result = requirement.key;
        continue;
      }
      bool matches = false;
      ZC_IF_SOME(existing, result) {
        matches = existing.syntaxKind == requirement.key.syntaxKind &&
                  existing.schemaPreorder == requirement.key.schemaPreorder &&
                  sameSpan(existing.sourceSpan, requirement.key.sourceSpan);
      }
      if (!matches) return zc::none;
    }
    return result;
  }

  void encodeNode(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key) const {
    encoder.encodeUint32(key.syntaxKind);
    encoder.encodeUint32(key.schemaPreorder);
    key.sourceSpan.encode(encoder);
  }

  bool encodeNode(identity::CanonicalEncoder& encoder, ast::NodeId node) const {
    auto key = checkedNode(node);
    ZC_IF_SOME(value, key) {
      encodeNode(encoder, value);
      return true;
    }
    return false;
  }

  bool encodeDefinition(identity::CanonicalEncoder& encoder, identity::DefId definition) const {
    ZC_IF_SOME(entry, input.identities.definition(definition)) {
      entry.key().encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeClosure(identity::CanonicalEncoder& encoder,
                     const binder::AnonymousOwnerLocalKey& closure) const {
    if (closure.owner().kind() == binder::StableBodyOwnerKind::Module) {
      ZC_IF_SOME(owner, closure.owner().moduleKey()) {
        if (input.identities.module(owner) == zc::none) return false;
      } else {
        return false;
      }
    } else {
      ZC_IF_SOME(owner, closure.owner().definitionKey()) {
        if (input.identities.definition(owner) == zc::none) return false;
      } else {
        return false;
      }
    }
    for (const auto& entry : input.anonymousEntities) {
      if (entry.key != closure) continue;
      closure.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeCaptureTarget(identity::CanonicalEncoder& encoder,
                           const binder::AnonymousOwnerLocalKey& closure,
                           const binder::BindingTarget& target) const {
    const auto& value = target.value();
    if (value.is<binder::CallableParameterBindingTarget>()) {
      const auto parameter = value.get<binder::CallableParameterBindingTarget>().parameter;
      encoder.encodeUint8(0x03);
      ZC_IF_SOME(authority, input.identities.callableParameter(parameter)) {
        ZC_IF_SOME(owner, closure.owner().definitionKey()) {
          if (authority.record().owner() != owner) return false;
          authority.key().encode(encoder);
          return true;
        }
      }
      return false;
    }
    if (value.is<binder::OwnerLocalBindingTarget>()) {
      const auto binding = value.get<binder::OwnerLocalBindingTarget>().binding;
      encoder.encodeUint8(0x04);
      for (const auto& entry : input.ownerLocalBindings) {
        if (entry.binding != binding) continue;
        if (entry.key.owner() != closure.owner()) return false;
        entry.key.encode(encoder);
        return true;
      }
    }
    return false;
  }

  bool encodeImpl(identity::CanonicalEncoder& encoder, identity::ImplId implementation) const {
    ZC_IF_SOME(entry, input.identities.implementation(implementation)) {
      entry.key().encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeType(identity::CanonicalEncoder& encoder,
                  identity::SemanticTypeId semanticType) const {
    auto lookup = input.semanticTypes.get(semanticType);
    if (!lookup.is<type::SemanticTypeLookup>()) return false;
    const auto bytes = lookup.get<type::SemanticTypeLookup>().key().bytes();
    constexpr zc::StringPtr domain = "zom.semantic-type-key\0"_zcc;
    if (bytes.size() <= domain.size()) return false;
    for (size_t index = 0; index < domain.size(); ++index) {
      if (bytes[index] != static_cast<uint8_t>(domain[index])) return false;
    }
    encodeRaw(encoder, bytes.slice(domain.size(), bytes.size()));
    return true;
  }

  bool encodeOptionalType(identity::CanonicalEncoder& encoder,
                          const zc::Maybe<identity::SemanticTypeId>& value) const {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(type, value) { return encodeType(encoder, type); }
    return false;
  }

  bool encodeInterface(identity::CanonicalEncoder& encoder,
                       const InterfaceInstantiation& interface) const {
    if (!encodeDefinition(encoder, interface.interface)) return false;
    encoder.encodeSequenceSize(interface.arguments.size());
    for (const auto argument : interface.arguments) {
      if (!encodeType(encoder, argument)) return false;
    }
    return true;
  }

  bool encodeConstValue(identity::CanonicalEncoder& encoder,
                        const CanonicalConstValue& value) const {
    auto bytes = signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
        value, candidate.module, input.identities, input.semanticTypes);
    ZC_IF_SOME(record, bytes) {
      encodeRaw(encoder, record.asPtr());
      return true;
    }
    return false;
  }

  bool encodeSubstitutionData(identity::CanonicalEncoder& encoder,
                              const SubstitutionData& value) const {
    if (value.parameters.size() != value.arguments.size()) return false;
    for (size_t index = 0; index < value.parameters.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (value.parameters[previous] == value.parameters[index]) return false;
      }
    }
    encoder.encodeSequenceSize(value.parameters.size());
    for (const auto parameter : value.parameters) {
      if (!encodeDefinition(encoder, parameter)) return false;
    }
    encoder.encodeSequenceSize(value.arguments.size());
    for (const auto argument : value.arguments) {
      if (!encodeType(encoder, argument)) return false;
    }
    return true;
  }

  zc::Maybe<uint32_t> substitutionIndex(CanonicalSubstitutionId id) const {
    for (uint32_t index = 0; index < candidate.substitutionStore.records().size(); ++index) {
      ZC_IF_SOME(candidateId, candidate.substitutionStore.idAt(index)) {
        if (candidateId == id) return index;
      }
    }
    return zc::none;
  }

  zc::Maybe<uint32_t> witnessIndex(WitnessArgumentsId id) const {
    for (uint32_t index = 0; index < candidate.witnessStore.records().size(); ++index) {
      ZC_IF_SOME(candidateId, candidate.witnessStore.idAt(index)) {
        if (candidateId == id) return index;
      }
    }
    return zc::none;
  }

  bool encodeSubstitutionRecord(identity::CanonicalEncoder& encoder,
                                CanonicalSubstitutionId id) const {
    auto index = substitutionIndex(id);
    ZC_IF_SOME(value, index) {
      return encodeSubstitutionData(encoder, candidate.substitutionStore.records()[value].value);
    }
    return false;
  }

  bool encodeWitnessByIndex(identity::CanonicalEncoder& encoder, uint32_t recordIndex,
                            zc::Vector<uint8_t>& active) const {
    const auto records = candidate.witnessStore.records();
    if (recordIndex >= records.size() || active[recordIndex] != 0) return false;
    active[recordIndex] = 1;
    const auto& value = records[recordIndex].value;
    encoder.encodeSequenceSize(value.entries.size());
    for (const auto& entry : value.entries) {
      if (!encodeType(encoder, entry.subject) || !encodeInterface(encoder, entry.interface) ||
          !encodeImpl(encoder, entry.impl)) {
        active[recordIndex] = 0;
        return false;
      }
      for (size_t index = 0; index < entry.associatedBindings.size(); ++index) {
        for (size_t previous = 0; previous < index; ++previous) {
          if (entry.associatedBindings[previous].associated ==
              entry.associatedBindings[index].associated) {
            active[recordIndex] = 0;
            return false;
          }
        }
      }
      if (!encodeSortedUnique(
              encoder, entry.associatedBindings,
              [&](identity::CanonicalEncoder& item, const AssociatedTypeBindingData& binding) {
                return encodeDefinition(item, binding.associated) && encodeType(item, binding.type);
              })) {
        active[recordIndex] = 0;
        return false;
      }
      encoder.encodeSequenceSize(entry.nested.size());
      for (const auto nested : entry.nested) {
        auto index = witnessIndex(nested);
        bool encoded = false;
        ZC_IF_SOME(valueIndex, index) {
          encoded = valueIndex < recordIndex && encodeWitnessByIndex(encoder, valueIndex, active);
        }
        if (!encoded) {
          active[recordIndex] = 0;
          return false;
        }
      }
    }
    active[recordIndex] = 0;
    return true;
  }

  bool encodeWitnessRecord(identity::CanonicalEncoder& encoder, WitnessArgumentsId id) const {
    auto index = witnessIndex(id);
    ZC_IF_SOME(value, index) {
      zc::Vector<uint8_t> active(candidate.witnessStore.records().size());
      for (size_t item = 0; item < candidate.witnessStore.records().size(); ++item) active.add(0);
      return encodeWitnessByIndex(encoder, value, active);
    }
    return false;
  }

  bool encodeOptionalSubstitution(identity::CanonicalEncoder& encoder,
                                  const zc::Maybe<CanonicalSubstitutionId>& value) const {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(id, value) { return encodeSubstitutionRecord(encoder, id); }
    return false;
  }

  bool encodeOptionalWitnesses(identity::CanonicalEncoder& encoder,
                               const zc::Maybe<WitnessArgumentsId>& value) const {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(id, value) { return encodeWitnessRecord(encoder, id); }
    return false;
  }

  bool encodeCoercion(identity::CanonicalEncoder& encoder,
                      const CoercionAdjustment& adjustment) const {
    if (!enumInRange(adjustment.site, CoercionSite::AnnotatedInitializer,
                     CoercionSite::ExplicitDynAnnotation)) {
      return false;
    }
    encoder.encodeUint8(static_cast<uint8_t>(adjustment.site));
    if (!encodeType(encoder, adjustment.source) || !encodeType(encoder, adjustment.destination)) {
      return false;
    }
    encoder.encodeSequenceSize(adjustment.steps.size());
    for (const auto& step : adjustment.steps) {
      const auto& value = step.variant();
      if (value.is<NeverToStep>()) {
        encoder.encodeUint8(0x01);
      } else if (value.is<ToAnyStep>()) {
        encoder.encodeUint8(0x02);
      } else if (value.is<ReborrowSharedStep>()) {
        encoder.encodeUint8(0x03);
      } else if (value.is<ReferenceToRawConstStep>()) {
        encoder.encodeUint8(0x04);
      } else if (value.is<ReferenceToRawMutableStep>()) {
        encoder.encodeUint8(0x05);
      } else if (value.is<RawMutToConstStep>()) {
        encoder.encodeUint8(0x06);
      } else if (value.is<UnionInjectStep>()) {
        const auto& inject = value.get<UnionInjectStep>();
        encoder.encodeUint8(0x07);
        encoder.encodeUint32(inject.alternativeIndex);
        if (!encodeType(encoder, inject.alternative)) return false;
      } else if (value.is<DynEraseStep>()) {
        const auto& erase = value.get<DynEraseStep>();
        encoder.encodeUint8(0x08);
        if (!encodeInterface(encoder, erase.interface) || !encodeImpl(encoder, erase.impl) ||
            !encodeWitnessRecord(encoder, erase.witnesses)) {
          return false;
        }
      } else if (value.is<DynUpcastStep>()) {
        const auto& upcast = value.get<DynUpcastStep>();
        encoder.encodeUint8(0x09);
        encoder.encodeSequenceSize(upcast.path.size());
        for (const auto definition : upcast.path) {
          if (!encodeDefinition(encoder, definition)) return false;
        }
      } else {
        return false;
      }
    }
    adjustment.sourceSpan.encode(encoder);
    return true;
  }

  bool encodeOptionalCoercion(identity::CanonicalEncoder& encoder,
                              const zc::Maybe<CoercionAdjustment>& value) const {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(adjustment, value) { return encodeCoercion(encoder, adjustment); }
    return false;
  }

  bool encodeReceiverAdjustment(identity::CanonicalEncoder& encoder,
                                const ReceiverAdjustment& adjustment) const {
    if (!encodeType(encoder, adjustment.source) || !encodeType(encoder, adjustment.destination) ||
        adjustment.steps.size() == 0) {
      return false;
    }
    encoder.encodeSequenceSize(adjustment.steps.size());
    for (const auto step : adjustment.steps) {
      if (!enumInRange(step, ReceiverAdjustmentStep::DereferenceShared,
                       ReceiverAdjustmentStep::CopyValue)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(step));
    }
    adjustment.sourceSpan.encode(encoder);
    return true;
  }

  bool encodeArgument(identity::CanonicalEncoder& encoder,
                      const CheckedArgumentFact& argument) const {
    return encodeNode(encoder, argument.sourceNode) && encodeType(encoder, argument.sourceType) &&
           encodeType(encoder, argument.parameterType) &&
           encodeOptionalCoercion(encoder, argument.adjustment);
  }

  bool encodeSelected(identity::CanonicalEncoder& encoder, const SelectedCallable& selected) const {
    const auto& value = selected.variant();
    if (value.is<DirectCallable>()) {
      encoder.encodeUint8(0x01);
      return encodeDefinition(encoder, value.get<DirectCallable>().callee);
    }
    if (value.is<ConcreteMethodCallable>()) {
      encoder.encodeUint8(0x02);
      return encodeDefinition(encoder, value.get<ConcreteMethodCallable>().method);
    }
    if (value.is<ImplMethodCallable>()) {
      const auto& method = value.get<ImplMethodCallable>();
      encoder.encodeUint8(0x03);
      return encodeImpl(encoder, method.impl) && encodeDefinition(encoder, method.method);
    }
    if (value.is<WitnessMethodCallable>()) {
      const auto& method = value.get<WitnessMethodCallable>();
      encoder.encodeUint8(0x04);
      return encodeDefinition(encoder, method.witnessParameter) &&
             encodeDefinition(encoder, method.interface) &&
             encodeDefinition(encoder, method.method);
    }
    if (value.is<DynMethodCallable>()) {
      const auto& method = value.get<DynMethodCallable>();
      encoder.encodeUint8(0x05);
      return encodeDefinition(encoder, method.interface) &&
             encodeDefinition(encoder, method.method);
    }
    if (!value.is<PrimitiveCallable>()) return false;
    const auto operation = value.get<PrimitiveCallable>().operation;
    if (!enumInRange(operation, PrimitiveOperation::UnaryPlus, PrimitiveOperation::NullCoalesce)) {
      return false;
    }
    encoder.encodeUint8(0x06);
    encoder.encodeUint8(static_cast<uint8_t>(operation));
    return true;
  }

  bool encodeCallEnvelope(identity::CanonicalEncoder& encoder,
                          const CheckedCallEnvelope& invocation) const {
    if (!encodeSelected(encoder, invocation.selected) ||
        !encodeType(encoder, invocation.calleeType)) {
      return false;
    }
    if (invocation.receiver == zc::none) {
      encoder.encodeNone();
    } else {
      encoder.encodeSome();
      bool encoded = false;
      ZC_IF_SOME(receiver, invocation.receiver) { encoded = encodeArgument(encoder, receiver); }
      if (!encoded) return false;
    }
    if (invocation.receiverMode == zc::none) {
      encoder.encodeNone();
    } else {
      encoder.encodeSome();
      bool encoded = false;
      ZC_IF_SOME(mode, invocation.receiverMode) {
        encoded = enumInRange(mode, ReceiverMode::Static, ReceiverMode::Move);
        if (encoded) encoder.encodeUint8(static_cast<uint8_t>(mode));
      }
      if (!encoded) return false;
    }
    if (invocation.receiverAdjustment == zc::none) {
      encoder.encodeNone();
    } else {
      encoder.encodeSome();
      bool encoded = false;
      ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
        encoded = encodeReceiverAdjustment(encoder, adjustment);
      }
      if (!encoded) return false;
    }
    encoder.encodeSequenceSize(invocation.arguments.size());
    for (const auto& argument : invocation.arguments) {
      if (!encodeArgument(encoder, argument)) return false;
    }
    return encodeType(encoder, invocation.successType) &&
           encodeType(encoder, invocation.resultType) &&
           encodeOptionalSubstitution(encoder, invocation.substitutions) &&
           encodeOptionalWitnesses(encoder, invocation.witnesses) &&
           encodeOptionalType(encoder, invocation.raises);
  }

  bool encodePatternConstructor(identity::CanonicalEncoder& encoder,
                                const PatternConstructor& constructor) const {
    const auto& value = constructor.variant();
    if (value.is<WildcardPattern>()) {
      encoder.encodeUint8(0x01);
      return true;
    }
    if (value.is<LiteralPattern>()) {
      encoder.encodeUint8(0x02);
      return encodeConstValue(encoder, value.get<LiteralPattern>().value);
    }
    if (value.is<TuplePattern>()) {
      encoder.encodeUint8(0x03);
      encoder.encodeUint32(value.get<TuplePattern>().arity);
      return true;
    }
    if (value.is<ObjectPattern>()) {
      encoder.encodeUint8(0x04);
      const auto& fields = value.get<ObjectPattern>().fields;
      return encodeSortedUnique(
          encoder, fields,
          [](identity::CanonicalEncoder& item, const identity::SemanticIdentifier& field) {
            field.encode(item);
            return true;
          });
    }
    if (value.is<UnionAlternativePattern>()) {
      const auto& alternative = value.get<UnionAlternativePattern>();
      encoder.encodeUint8(0x05);
      encoder.encodeUint32(alternative.index);
      return encodeType(encoder, alternative.type);
    }
    if (value.is<EnumVariantPattern>()) {
      encoder.encodeUint8(0x06);
      return encodeDefinition(encoder, value.get<EnumVariantPattern>().variant);
    }
    if (!value.is<NominalPattern>()) return false;
    encoder.encodeUint8(0x07);
    return encodeDefinition(encoder, value.get<NominalPattern>().definition);
  }

  bool encodePlace(identity::CanonicalEncoder& encoder, const CheckedNodeKey& node,
                   const CheckedPlaceFact& fact) const {
    encodeNode(encoder, node);
    const auto& root = fact.root.variant();
    bool encodedRoot = false;
    if (root.is<DefinitionPlaceRoot>()) {
      encoder.encodeUint8(0x01);
      encodedRoot = encodeDefinition(encoder, root.get<DefinitionPlaceRoot>().definition);
    } else if (root.is<OwnerLocalPlaceRoot>()) {
      encoder.encodeUint8(0x04);
      const auto binding = root.get<OwnerLocalPlaceRoot>().binding;
      if (!binding.isValid() || !binding.belongsTo(input.module)) return false;
      for (const auto& entry : input.ownerLocalBindings) {
        if (entry.binding != binding) continue;
        entry.key.encode(encoder);
        encodedRoot = true;
        break;
      }
    } else if (root.is<CallableParameterPlaceRoot>()) {
      encoder.encodeUint8(0x05);
      auto parameter =
          input.identities.callableParameter(root.get<CallableParameterPlaceRoot>().parameter);
      auto module = input.identities.module(input.module);
      ZC_IF_SOME(parameterValue, parameter) {
        ZC_IF_SOME(moduleValue, module) {
          auto owner = input.identities.definition(parameterValue.record().owner());
          ZC_IF_SOME(ownerValue, owner) {
            const auto ownerModule = ownerValue.record().module().encode();
            const auto currentModule = moduleValue.key().encode();
            if (sameBytes(ownerModule.asPtr(), currentModule.asPtr())) {
              parameterValue.key().encode(encoder);
              encodedRoot = true;
            }
          }
        }
      }
    } else if (root.is<DereferencePlaceRoot>()) {
      encoder.encodeUint8(0x02);
      encodedRoot = encodeNode(encoder, root.get<DereferencePlaceRoot>().node);
    } else if (root.is<TemporaryPlaceRoot>()) {
      encoder.encodeUint8(0x03);
      encodedRoot = encodeNode(encoder, root.get<TemporaryPlaceRoot>().node);
    }
    if (!encodedRoot) return false;
    encoder.encodeSequenceSize(fact.projections.size());
    for (const auto& projection : fact.projections) {
      const auto& value = projection.variant();
      if (value.is<FieldProjection>()) {
        encoder.encodeUint8(0x01);
        if (!encodeDefinition(encoder, value.get<FieldProjection>().field)) return false;
      } else if (value.is<TupleIndexProjection>()) {
        encoder.encodeUint8(0x02);
        encoder.encodeUint32(value.get<TupleIndexProjection>().index);
      } else if (value.is<IndexProjection>()) {
        encoder.encodeUint8(0x03);
        if (!encodeNode(encoder, value.get<IndexProjection>().index)) return false;
      } else {
        return false;
      }
    }
    if (!encodeType(encoder, fact.type)) return false;
    encoder.encodeBool(fact.mutablePlace);
    encoder.encodeBool(fact.movable);
    return true;
  }

  bool encodeProjectionKey(identity::CanonicalEncoder& encoder, const ProjectionKey& key) const {
    return encodeType(encoder, key.subject) && encodeInterface(encoder, key.interface) &&
           encodeDefinition(encoder, key.associated);
  }

  bool encodeMarkerEvidence(identity::CanonicalEncoder& encoder,
                            const MarkerEvidence& evidence) const {
    const auto& value = evidence.variant();
    if (value.is<signature::ExplicitMarkerEvidence>()) {
      encoder.encodeUint8(0x01);
      return encodeImpl(encoder, value.get<signature::ExplicitMarkerEvidence>().impl);
    }
    if (value.is<signature::StructuralMarkerEvidence>()) {
      encoder.encodeUint8(0x02);
      const auto& components = value.get<signature::StructuralMarkerEvidence>().components;
      return encodeSortedUnique(
          encoder, components,
          [&](identity::CanonicalEncoder& item,
              const signature::MarkerComponentEvidence& component) {
            item.encodeSequenceSize(component.path.size());
            for (const auto& step : component.path) {
              const auto& stepValue = step.variant();
              if (stepValue.is<signature::TupleElementStep>()) {
                item.encodeUint8(0x01);
                item.encodeUint32(stepValue.get<signature::TupleElementStep>().index);
              } else if (stepValue.is<signature::ObjectFieldStep>()) {
                item.encodeUint8(0x02);
                stepValue.get<signature::ObjectFieldStep>().name.encode(item);
              } else if (stepValue.is<signature::ArrayElementStep>()) {
                item.encodeUint8(0x03);
              } else if (stepValue.is<signature::NominalFieldStep>()) {
                item.encodeUint8(0x04);
                if (!encodeDefinition(item, stepValue.get<signature::NominalFieldStep>().field)) {
                  return false;
                }
              } else if (stepValue.is<signature::ReferenceReferentStep>()) {
                item.encodeUint8(0x05);
              } else if (stepValue.is<signature::EnumVariantPayloadStep>()) {
                const auto& payload = stepValue.get<signature::EnumVariantPayloadStep>();
                item.encodeUint8(0x06);
                if (!encodeDefinition(item, payload.variant)) return false;
                item.encodeUint32(payload.index);
              } else {
                return false;
              }
            }
            return encodeType(item, component.componentType) &&
                   encodeDefinition(item, component.supportingFact.marker) &&
                   encodeType(item, component.supportingFact.subject);
          });
    }
    if (!value.is<signature::BuiltinMarkerEvidence>()) return false;
    const auto primitive = value.get<signature::BuiltinMarkerEvidence>().primitive;
    if (!enumInRange(primitive, type::semantic::PrimitiveKind::I8,
                     type::semantic::PrimitiveKind::Null)) {
      return false;
    }
    encoder.encodeUint8(0x03);
    encoder.encodeUint8(static_cast<uint8_t>(primitive));
    return true;
  }

  bool encodeNodeType(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                      ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.nodeTypes, node)) {
      encodeNode(encoder, key);
      return encodeType(encoder, entry.value);
    }
    return false;
  }

  bool encodeLiteral(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                     ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.literals, node)) {
      encodeNode(encoder, key);
      return encodeConstValue(encoder, entry.value.literal) &&
             encodeType(encoder, entry.value.type) &&
             (entry.value.sourceSpan.encode(encoder), true);
    }
    return false;
  }

  bool encodeConstant(identity::CanonicalEncoder& encoder,
                      const ConstantEvaluationFact& fact) const {
    auto revision = constantRevision(fact);
    bool revisionMatches = false;
    ZC_IF_SOME(value, revision) { revisionMatches = value == fact.evaluationRevision; }
    if (!revisionMatches) return false;
    if (!encodeDefinition(encoder, fact.definition) || !encodeNode(encoder, fact.expression) ||
        !encodeConstValue(encoder, fact.value) || !encodeType(encoder, fact.type)) {
      return false;
    }
    const auto dependencies = fact.dependencies.entries();
    for (size_t index = 0; index < dependencies.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (dependencies[previous].key == dependencies[index].key) return false;
      }
    }
    if (!encodeSortedUnique(encoder, dependencies,
                            [&](identity::CanonicalEncoder& item,
                                const ImmutableFactMap<identity::DefId,
                                                       identity::Sha256Digest>::Entry& dependency) {
                              if (!encodeDefinition(item, dependency.key)) return false;
                              item.encodeDigest(dependency.value);
                              return true;
                            })) {
      return false;
    }
    encoder.encodeDigest(fact.evaluationRevision);
    return true;
  }

  bool encodeAggregate(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                       ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.aggregates, node)) {
      const auto& fact = entry.value;
      encodeNode(encoder, key);
      const auto& kind = fact.kind.variant();
      if (kind.is<TupleAggregate>()) {
        encoder.encodeUint8(0x01);
      } else if (kind.is<ArrayAggregate>()) {
        encoder.encodeUint8(0x02);
      } else if (kind.is<ObjectAggregate>()) {
        encoder.encodeUint8(0x03);
      } else if (kind.is<NominalAggregate>()) {
        encoder.encodeUint8(0x04);
        if (!encodeDefinition(encoder, kind.get<NominalAggregate>().definition)) return false;
      } else {
        return false;
      }
      if (!encodeType(encoder, fact.resultType)) return false;
      encoder.encodeSequenceSize(fact.elements.size());
      for (const auto& element : fact.elements) {
        if (!encodeNode(encoder, element.sourceNode)) return false;
        if (element.field == zc::none) {
          encoder.encodeNone();
        } else {
          encoder.encodeSome();
          bool fieldEncoded = false;
          ZC_IF_SOME(field, element.field) { fieldEncoded = encodeDefinition(encoder, field); }
          if (!fieldEncoded) return false;
        }
        encoder.encodeUint32(element.index);
        if (!encodeType(encoder, element.sourceType) ||
            !encodeType(encoder, element.destinationType) ||
            !encodeOptionalCoercion(encoder, element.adjustment)) {
          return false;
        }
      }
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodePlaceMapFact(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                          ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.places, node)) {
      return encodePlace(encoder, key, entry.value);
    }
    return false;
  }

  bool encodeCoercionMapFact(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                             ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.coercions, node)) {
      encodeNode(encoder, key);
      return encodeCoercion(encoder, entry.value);
    }
    return false;
  }

  bool encodeCast(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                  ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.casts, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.mode, CastMode::Guaranteed, CastMode::ForcedChecked) ||
          !enumInRange(fact.kind, CastKind::IntegerWiden, CastKind::RawPointerReinterpret) ||
          !enumInRange(fact.unsafeRequirement, UnsafeRequirement::None,
                       UnsafeRequirement::RawPointerBoundary)) {
        return false;
      }
      encodeNode(encoder, key);
      encoder.encodeUint8(static_cast<uint8_t>(fact.mode));
      encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
      if (!encodeType(encoder, fact.source) || !encodeType(encoder, fact.target) ||
          !encodeType(encoder, fact.result)) {
        return false;
      }
      if (fact.impl == zc::none) {
        encoder.encodeNone();
      } else {
        encoder.encodeSome();
        bool encoded = false;
        ZC_IF_SOME(value, fact.impl) { encoded = encodeImpl(encoder, value); }
        if (!encoded) return false;
      }
      if (!encodeOptionalWitnesses(encoder, fact.witnesses)) return false;
      encoder.encodeSequenceSize(fact.dynPath.size());
      for (const auto definition : fact.dynPath) {
        if (!encodeDefinition(encoder, definition)) return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(fact.unsafeRequirement));
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeCall(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                  ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.calls, node)) {
      encodeNode(encoder, key);
      if (!encodeCallEnvelope(encoder, entry.value.invocation)) return false;
      entry.value.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeCompoundAssignment(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                                ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.compoundAssignments, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.operation, CompoundAssignmentOperation::AddAssign,
                       CompoundAssignmentOperation::NullCoalesceAssign)) {
        return false;
      }
      encodeNode(encoder, key);
      if (!encodeNode(encoder, fact.placeNode)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(fact.operation));
      if (!encodeCallEnvelope(encoder, fact.invocation) ||
          !encodeOptionalCoercion(encoder, fact.writebackAdjustment)) {
        return false;
      }
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeMember(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                    ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.members, node)) {
      const auto& fact = entry.value;
      encodeNode(encoder, key);
      return encodeType(encoder, fact.receiverType) && encodeDefinition(encoder, fact.member) &&
             encodeType(encoder, fact.memberType) &&
             encodeOptionalCoercion(encoder, fact.adjustment);
    }
    return false;
  }

  bool encodeIndex(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                   ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.indexes, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.accessMode, IndexAccessMode::Read, IndexAccessMode::MutablePlace)) {
        return false;
      }
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.collectionType) || !encodeType(encoder, fact.indexType) ||
          !encodeType(encoder, fact.elementType)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(fact.accessMode));
      return encodeType(encoder, fact.accessResultType);
    }
    return false;
  }

  bool encodePatternMapFact(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                            ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.patterns, node)) {
      const auto& fact = entry.value;
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.scrutineeType) ||
          !encodePatternConstructor(encoder, fact.constructor)) {
        return false;
      }
      for (size_t index = 0; index < fact.bindings.size(); ++index) {
        for (size_t previous = 0; previous < index; ++previous) {
          if (fact.bindings[previous].binding == fact.bindings[index].binding) return false;
        }
      }
      if (!encodeSortedUnique(
              encoder, fact.bindings,
              [&](identity::CanonicalEncoder& item, const PatternBindingFact& binding) {
                return encodeDefinition(item, binding.binding) && encodeType(item, binding.type);
              })) {
        return false;
      }
      for (size_t index = 0; index < fact.refinements.size(); ++index) {
        for (size_t previous = 0; previous < index; ++previous) {
          if (fact.refinements[previous].node == fact.refinements[index].node) return false;
        }
      }
      if (!encodeSortedUnique(
              encoder, fact.refinements,
              [&](identity::CanonicalEncoder& item, const PatternRefinementFact& refinement) {
                return encodeNode(item, refinement.node) && encodeType(item, refinement.type);
              })) {
        return false;
      }
      encoder.encodeBool(fact.reachable);
      return encodeOptionalType(encoder, fact.guardMayRaise);
    }
    return false;
  }

  bool encodeObservedOperation(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                               ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.observedOperations, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.operation, ObservedOperation::Raise, ObservedOperation::Suspend)) {
        return false;
      }
      encodeNode(encoder, key);
      encoder.encodeUint8(static_cast<uint8_t>(fact.operation));
      if (!encodeOptionalType(encoder, fact.raisedType)) return false;
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeMarkerObligation(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                              ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.markerObligations, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.polarity, Polarity::Positive, Polarity::Negative)) return false;
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.subject) || !encodeDefinition(encoder, fact.marker)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(fact.polarity));
      return encodeMarkerEvidence(encoder, fact.evidence);
    }
    return false;
  }

  bool encodeExhaustiveness(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                            ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.exhaustiveness, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.domain, ExhaustivenessDomain::Closed,
                       ExhaustivenessDomain::OpenRequiresCatchAll)) {
        return false;
      }
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.scrutineeType)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(fact.domain));
      if (!encodeSortedUnique(
              encoder, fact.coveredConstructors,
              [&](identity::CanonicalEncoder& item, const PatternConstructor& constructor) {
                return encodePatternConstructor(item, constructor);
              }) ||
          !encodeSortedUnique(
              encoder, fact.missingConstructors,
              [&](identity::CanonicalEncoder& item, const PatternConstructor& constructor) {
                return encodePatternConstructor(item, constructor);
              }) ||
          !encodeSortedUnique(encoder, fact.unreachableArms,
                              [&](identity::CanonicalEncoder& item, ast::NodeId arm) {
                                return encodeNode(item, arm);
                              })) {
        return false;
      }
      return true;
    }
    return false;
  }

  bool encodeUnsafeOperation(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                             ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.unsafeOperations, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.operation, UnsafeOperation::RawDereference,
                       UnsafeOperation::PackedFieldAccess)) {
        return false;
      }
      encodeNode(encoder, key);
      encoder.encodeUint8(static_cast<uint8_t>(fact.operation));
      if (fact.enclosingUnsafeNode == zc::none) {
        encoder.encodeNone();
      } else {
        encoder.encodeSome();
        bool encoded = false;
        ZC_IF_SOME(value, fact.enclosingUnsafeNode) { encoded = encodeNode(encoder, value); }
        if (!encoded) return false;
      }
      encoder.encodeBool(fact.acknowledged);
      return true;
    }
    return false;
  }

  bool encodeProjection(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                        ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.projections, node)) {
      const auto& fact = entry.value;
      encodeNode(encoder, key);
      return encodeProjectionKey(encoder, fact.key) && encodeType(encoder, fact.result) &&
             encodeImpl(encoder, fact.impl) && encodeWitnessRecord(encoder, fact.witnesses);
    }
    return false;
  }

  bool encodeObligation(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                        ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.obligations, node)) {
      const auto& fact = entry.value;
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.subject) || !encodeInterface(encoder, fact.interface)) {
        return false;
      }
      const auto& resolution = fact.resolution.variant();
      if (resolution.is<NoImplResolution>()) {
        encoder.encodeUint8(0x01);
        return true;
      }
      if (resolution.is<UniqueImplResolution>()) {
        const auto& unique = resolution.get<UniqueImplResolution>();
        encoder.encodeUint8(0x02);
        return encodeImpl(encoder, unique.impl) &&
               encodeSubstitutionRecord(encoder, unique.substitutions) &&
               encodeWitnessRecord(encoder, unique.witnesses);
      }
      if (!resolution.is<AmbiguousImplResolution>()) return false;
      const auto& ambiguous = resolution.get<AmbiguousImplResolution>();
      encoder.encodeUint8(0x03);
      return encodeSortedUnique(
          encoder, ambiguous.candidates,
          [&](identity::CanonicalEncoder& item, identity::ImplId implementation) {
            return encodeImpl(item, implementation);
          });
    }
    return false;
  }

  bool encodeErrorUnionShape(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                             ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.errorUnionShapes, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.origin, ErrorUnionShapeOrigin::RaisingCall,
                       ErrorUnionShapeOrigin::Coercion)) {
        return false;
      }
      encodeNode(encoder, key);
      if (!encodeType(encoder, fact.valueType) || !encodeType(encoder, fact.successType) ||
          !encodeType(encoder, fact.residualType)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(fact.origin));
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeErrorOperator(identity::CanonicalEncoder& encoder, const CheckedNodeKey& key,
                           ast::NodeId node) const {
    ZC_IF_SOME(entry, factEntry(candidate.errorOperators, node)) {
      const auto& fact = entry.value;
      if (!enumInRange(fact.kind, ErrorOperatorKind::Propagate, ErrorOperatorKind::ForcedUnwrap)) {
        return false;
      }
      encodeNode(encoder, key);
      encoder.encodeUint8(static_cast<uint8_t>(fact.kind));
      if (!encodeType(encoder, fact.operandType) || !encodeType(encoder, fact.successType) ||
          !encodeType(encoder, fact.residualType) ||
          !encodeOptionalType(encoder, fact.enclosingRaises)) {
        return false;
      }
      fact.sourceSpan.encode(encoder);
      return true;
    }
    return false;
  }

  const CheckedFactsCandidate& candidate;
  const CheckedFactsVerificationInput& input;
};

bool encodeSortedRecords(identity::CanonicalEncoder& encoder,
                         zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  zc::Vector<zc::ArrayPtr<const uint8_t>> sorted(records.size());
  for (const auto record : records) {
    if (record.size() == 0) return false;
    sorted.add(record);
  }
  for (size_t index = 1; index < sorted.size(); ++index) {
    const auto current = sorted[index];
    size_t insertion = index;
    while (insertion > 0 && lessBytes(current, sorted[insertion - 1])) {
      sorted[insertion] = sorted[insertion - 1];
      --insertion;
    }
    sorted[insertion] = current;
  }
  for (size_t index = 1; index < sorted.size(); ++index) {
    if (sameBytes(sorted[index - 1], sorted[index])) return false;
  }
  encoder.encodeSequenceSize(sorted.size());
  for (const auto record : sorted) encoder.encodeByteString(record);
  return true;
}

signature::CheckerVerificationFailure checkerInvariant(signature::CheckerInvariantKind kind,
                                                       identity::ModuleId module,
                                                       uint32_t ordinal) {
  zc::Maybe<identity::DefId> noOwner;
  zc::Maybe<ast::NodeId> noNode;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  return signature::CheckerVerificationFailure(signature::CheckerInvariantFact{
      kind, signature::CheckerInvariantStage::Verification, module, zc::mv(noOwner), zc::mv(noNode),
      zc::mv(noSpan), zc::mv(noPath), zc::mv(noExpected), zc::mv(noActual), ordinal});
}

CheckedFactsVerificationResult reject(signature::CheckerVerificationFailure&& failure) {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(zc::mv(failure));
  return CheckedFactsInvariantRejected{zc::mv(failures)};
}

template <typename Key, typename Value>
bool recordsAreCanonical(const ImmutableFactMap<Key, Value>& map) {
  const auto entries = map.entries();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].canonicalRecord.size() == 0) return false;
    for (size_t previous = 0; previous < index; ++previous) {
      if (entries[previous].key == entries[index].key) return false;
    }
    if (index > 0 && !lessBytes(entries[index - 1].canonicalRecord.asPtr(),
                                entries[index].canonicalRecord.asPtr())) {
      return false;
    }
  }
  return true;
}

template <typename Value>
bool storeRecordsAreCanonical(zc::ArrayPtr<const FrozenStoreRecord<Value>> records) {
  for (size_t index = 0; index < records.size(); ++index) {
    if (records[index].canonicalRecord.size() == 0) return false;
    if (index > 0 && !lessBytes(records[index - 1].canonicalRecord.asPtr(),
                                records[index].canonicalRecord.asPtr())) {
      return false;
    }
  }
  return true;
}

template <typename Key, typename Value>
zc::Vector<zc::ArrayPtr<const uint8_t>> recordViews(const ImmutableFactMap<Key, Value>& map) {
  zc::Vector<zc::ArrayPtr<const uint8_t>> result(map.size());
  for (const auto& entry : map.entries()) result.add(entry.canonicalRecord.asPtr());
  return result;
}

template <typename Value>
zc::Vector<zc::ArrayPtr<const uint8_t>> storeRecordViews(
    zc::ArrayPtr<const FrozenStoreRecord<Value>> records) {
  zc::Vector<zc::ArrayPtr<const uint8_t>> result(records.size());
  for (const auto& record : records) result.add(record.canonicalRecord.asPtr());
  return result;
}

template <typename Value>
bool containsNode(const ImmutableFactMap<ast::NodeId, Value>& map, ast::NodeId node) {
  for (const auto& entry : map.entries()) {
    if (entry.key == node) return true;
  }
  return false;
}

template <typename Value>
bool containsDefinition(const ImmutableFactMap<identity::DefId, Value>& map,
                        identity::DefId definition) {
  for (const auto& entry : map.entries()) {
    if (entry.key == definition) return true;
  }
  return false;
}

bool containsCapture(const CaptureFactMap& map, const CaptureKey& key) {
  for (const auto& entry : map.entries()) {
    if (entry.key == key) return true;
  }
  return false;
}

bool hasNodeFact(const CheckedFactsCandidate& candidate, CheckedFactGroup group, ast::NodeId node) {
  switch (group) {
    case CheckedFactGroup::NodeType:
      return containsNode(candidate.nodeTypes, node);
    case CheckedFactGroup::Literal:
      return containsNode(candidate.literals, node);
    case CheckedFactGroup::Aggregate:
      return containsNode(candidate.aggregates, node);
    case CheckedFactGroup::Place:
      return containsNode(candidate.places, node);
    case CheckedFactGroup::Coercion:
      return containsNode(candidate.coercions, node);
    case CheckedFactGroup::Cast:
      return containsNode(candidate.casts, node);
    case CheckedFactGroup::Call:
      return containsNode(candidate.calls, node);
    case CheckedFactGroup::CompoundAssignment:
      return containsNode(candidate.compoundAssignments, node);
    case CheckedFactGroup::Member:
      return containsNode(candidate.members, node);
    case CheckedFactGroup::Index:
      return containsNode(candidate.indexes, node);
    case CheckedFactGroup::Pattern:
      return containsNode(candidate.patterns, node);
    case CheckedFactGroup::ObservedOperation:
      return containsNode(candidate.observedOperations, node);
    case CheckedFactGroup::MarkerObligation:
      return containsNode(candidate.markerObligations, node);
    case CheckedFactGroup::Exhaustiveness:
      return containsNode(candidate.exhaustiveness, node);
    case CheckedFactGroup::UnsafeOperation:
      return containsNode(candidate.unsafeOperations, node);
    case CheckedFactGroup::Projection:
      return containsNode(candidate.projections, node);
    case CheckedFactGroup::Obligation:
      return containsNode(candidate.obligations, node);
    case CheckedFactGroup::ErrorUnionShape:
      return containsNode(candidate.errorUnionShapes, node);
    case CheckedFactGroup::ErrorOperator:
      return containsNode(candidate.errorOperators, node);
    case CheckedFactGroup::DefinitionType:
    case CheckedFactGroup::Constant:
    case CheckedFactGroup::Capture:
      return false;
  }
  return false;
}

bool hasDefinitionFact(const CheckedFactsCandidate& candidate, CheckedFactGroup group,
                       identity::DefId definition) {
  switch (group) {
    case CheckedFactGroup::DefinitionType:
      return containsDefinition(candidate.definitionTypes, definition);
    case CheckedFactGroup::Constant:
      return containsDefinition(candidate.constants, definition);
    default:
      return false;
  }
}

bool hasNodeRequirement(zc::ArrayPtr<const NodeFactRequirement> requirements,
                        CheckedFactGroup group, ast::NodeId node) {
  for (const auto& requirement : requirements) {
    if (requirement.group == group && requirement.node == node) return true;
  }
  return false;
}

bool hasDefinitionRequirement(zc::ArrayPtr<const DefinitionFactRequirement> requirements,
                              CheckedFactGroup group, identity::DefId definition) {
  for (const auto& requirement : requirements) {
    if (requirement.group == group && requirement.definition == definition) return true;
  }
  return false;
}

bool encodeDisplayType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId semanticType,
                       const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(semanticType);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto bytes = lookup.get<type::SemanticTypeLookup>().key().bytes();
  constexpr zc::StringPtr domain = "zom.semantic-type-key\0"_zcc;
  if (bytes.size() <= domain.size()) return false;
  for (size_t index = 0; index < domain.size(); ++index) {
    if (bytes[index] != static_cast<uint8_t>(domain[index])) return false;
  }
  encodeRaw(encoder, bytes.slice(domain.size(), bytes.size()));
  return true;
}

bool encodeDisplayDefinition(identity::CanonicalEncoder& encoder, identity::DefId definition,
                             const CheckerIdentityAuthority& identities) {
  ZC_IF_SOME(entry, identities.definition(definition)) {
    entry.key().encode(encoder);
    return true;
  }
  return false;
}

bool encodeDisplayConstValue(identity::CanonicalEncoder& encoder, const CanonicalConstValue& value,
                             identity::ModuleId module, const CheckerIdentityAuthority& identities,
                             const type::SemanticTypeStore& semanticTypes) {
  auto record = signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
      value, module, identities, semanticTypes);
  ZC_IF_SOME(bytes, record) {
    encodeRaw(encoder, bytes.asPtr());
    return true;
  }
  return false;
}

bool encodeDisplayPattern(identity::CanonicalEncoder& encoder, const PatternConstructor& pattern,
                          identity::ModuleId module, const CheckerIdentityAuthority& identities,
                          const type::SemanticTypeStore& semanticTypes) {
  const auto& value = pattern.variant();
  if (value.is<WildcardPattern>()) {
    encoder.encodeUint8(0x01);
    return true;
  }
  if (value.is<LiteralPattern>()) {
    encoder.encodeUint8(0x02);
    return encodeDisplayConstValue(encoder, value.get<LiteralPattern>().value, module, identities,
                                   semanticTypes);
  }
  if (value.is<TuplePattern>()) {
    encoder.encodeUint8(0x03);
    encoder.encodeUint32(value.get<TuplePattern>().arity);
    return true;
  }
  if (value.is<ObjectPattern>()) {
    encoder.encodeUint8(0x04);
    const auto& fields = value.get<ObjectPattern>().fields;
    encoder.encodeSequenceSize(fields.size());
    for (const auto& field : fields) field.encode(encoder);
    return true;
  }
  if (value.is<UnionAlternativePattern>()) {
    const auto& alternative = value.get<UnionAlternativePattern>();
    encoder.encodeUint8(0x05);
    encoder.encodeUint32(alternative.index);
    return encodeDisplayType(encoder, alternative.type, semanticTypes);
  }
  if (value.is<EnumVariantPattern>()) {
    encoder.encodeUint8(0x06);
    return encodeDisplayDefinition(encoder, value.get<EnumVariantPattern>().variant, identities);
  }
  if (!value.is<NominalPattern>()) return false;
  encoder.encodeUint8(0x07);
  return encodeDisplayDefinition(encoder, value.get<NominalPattern>().definition, identities);
}

zc::Maybe<zc::Array<uint8_t>> encodeDisplayArgumentRecord(
    const CheckerDisplayArgument& argument, identity::ModuleId module,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes) {
  if (identities.module(module) == zc::none ||
      semanticTypes.context() != identities.semanticContext()) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.checker-display-argument"_zcc);
  encoder.encodeUint8(0);
  const auto& value = argument.variant();
  if (value.is<TypeDisplayArg>()) {
    encoder.encodeUint8(0x01);
    const auto& type = value.get<TypeDisplayArg>();
    if (!encodeDisplayType(encoder, type.type, semanticTypes)) return zc::none;
    if (type.sourceAlias == zc::none) {
      encoder.encodeNone();
    } else {
      encoder.encodeSome();
      ZC_IF_SOME(alias, type.sourceAlias) { alias.encode(encoder); }
    }
    return encoder.finish();
  }
  if (value.is<PrimitiveTypeDisplayArg>()) {
    const auto kind = value.get<PrimitiveTypeDisplayArg>().kind;
    if (kind < type::semantic::PrimitiveKind::I8 || kind > type::semantic::PrimitiveKind::Null) {
      return zc::none;
    }
    encoder.encodeUint8(0x02);
    encoder.encodeUint8(static_cast<uint8_t>(kind));
    return encoder.finish();
  }
  if (value.is<DefinitionDisplayArg>()) {
    encoder.encodeUint8(0x03);
    if (!encodeDisplayDefinition(encoder, value.get<DefinitionDisplayArg>().definition,
                                 identities)) {
      return zc::none;
    }
    return encoder.finish();
  }
  if (value.is<IdentifierDisplayArg>()) {
    encoder.encodeUint8(0x04);
    value.get<IdentifierDisplayArg>().identifier.encode(encoder);
    return encoder.finish();
  }
  if (value.is<CountDisplayArg>()) {
    encoder.encodeUint8(0x05);
    encoder.encodeUint64(value.get<CountDisplayArg>().count);
    return encoder.finish();
  }
  if (value.is<ConstraintContextDisplayArg>()) {
    const auto reason = value.get<ConstraintContextDisplayArg>().reason;
    if (reason < ConstraintReasonKind::Annotation || reason > ConstraintReasonKind::Cast) {
      return zc::none;
    }
    encoder.encodeUint8(0x06);
    encoder.encodeUint8(static_cast<uint8_t>(reason));
    return encoder.finish();
  }
  if (value.is<OperatorDisplayArg>()) {
    auto operation = checker::encodeOperatorKind(value.get<OperatorDisplayArg>().operation);
    if (operation == zc::none) return zc::none;
    encoder.encodeUint8(0x07);
    ZC_IF_SOME(bytes, operation) { encodeRaw(encoder, bytes.asPtr()); }
    return encoder.finish();
  }
  if (value.is<LiteralDisplayArg>()) {
    encoder.encodeUint8(0x08);
    if (!encodeDisplayConstValue(encoder, value.get<LiteralDisplayArg>().literal, module,
                                 identities, semanticTypes)) {
      return zc::none;
    }
    return encoder.finish();
  }
  if (!value.is<PatternsDisplayArg>()) return zc::none;
  const auto& patterns = value.get<PatternsDisplayArg>().patterns;
  if (patterns.size() == 0) return zc::none;
  encoder.encodeUint8(0x09);
  encoder.encodeSequenceSize(patterns.size());
  for (const auto& pattern : patterns) {
    if (!encodeDisplayPattern(encoder, pattern, module, identities, semanticTypes)) {
      return zc::none;
    }
  }
  return encoder.finish();
}

bool sameDisplayArgument(const CheckerDisplayArgument& left, const CheckerDisplayArgument& right,
                         identity::ModuleId module, const CheckerIdentityAuthority& identities,
                         const type::SemanticTypeStore& semanticTypes) {
  auto leftRecord = encodeDisplayArgumentRecord(left, module, identities, semanticTypes);
  auto rightRecord = encodeDisplayArgumentRecord(right, module, identities, semanticTypes);
  if (leftRecord == zc::none || rightRecord == zc::none) return false;
  zc::ArrayPtr<const uint8_t> leftBytes;
  zc::ArrayPtr<const uint8_t> rightBytes;
  ZC_IF_SOME(value, leftRecord) { leftBytes = value.asPtr(); }
  ZC_IF_SOME(value, rightRecord) { rightBytes = value.asPtr(); }
  return sameBytes(leftBytes, rightBytes);
}

bool sameRecoveryPolicy(const CheckerRecoveryPolicy& left, const CheckerRecoveryPolicy& right) {
  const auto& leftValue = left.variant();
  const auto& rightValue = right.variant();
  if (leftValue.is<NoRecoveryPolicy>()) return rightValue.is<NoRecoveryPolicy>();
  if (leftValue.is<AdvisoryAfterSuccessRecoveryPolicy>()) {
    return rightValue.is<AdvisoryAfterSuccessRecoveryPolicy>();
  }
  return rightValue.is<CreateRootRecoveryPolicy>() &&
         leftValue.get<CreateRootRecoveryPolicy>().recoveryClass ==
             rightValue.get<CreateRootRecoveryPolicy>().recoveryClass &&
         leftValue.get<CreateRootRecoveryPolicy>().suppressIfChildRecovery ==
             rightValue.get<CreateRootRecoveryPolicy>().suppressIfChildRecovery;
}

bool sameFailure(const CheckerFailureRef& left, const CheckerFailureRef& right,
                 identity::ModuleId module, const CheckerIdentityAuthority& identities,
                 const type::SemanticTypeStore& semanticTypes) {
  if (left.diagnostic != right.diagnostic || left.stage != right.stage ||
      left.primaryNode != right.primaryNode || !sameSpan(left.primarySpan, right.primarySpan) ||
      left.producer != right.producer ||
      left.emitterOrdinal.stageTag != right.emitterOrdinal.stageTag ||
      left.emitterOrdinal.ownerSchemaPreorder != right.emitterOrdinal.ownerSchemaPreorder ||
      left.emitterOrdinal.siteSchemaPreorder != right.emitterOrdinal.siteSchemaPreorder ||
      left.emitterOrdinal.itemOrdinal != right.emitterOrdinal.itemOrdinal ||
      left.arguments.size() != right.arguments.size() || left.notes.size() != right.notes.size() ||
      !sameRecoveryPolicy(left.recoveryPolicy, right.recoveryPolicy)) {
    return false;
  }
  for (size_t index = 0; index < left.arguments.size(); ++index) {
    if (!sameDisplayArgument(left.arguments[index], right.arguments[index], module, identities,
                             semanticTypes)) {
      return false;
    }
  }
  for (size_t index = 0; index < left.notes.size(); ++index) {
    const auto& leftNote = left.notes[index];
    const auto& rightNote = right.notes[index];
    if (!(leftNote.diagnostic == rightNote.diagnostic) ||
        !sameSpan(leftNote.span, rightNote.span) ||
        (leftNote.causeDefinition == zc::none) != (rightNote.causeDefinition == zc::none) ||
        leftNote.arguments.size() != rightNote.arguments.size()) {
      return false;
    }
    if (leftNote.causeDefinition != zc::none) {
      identity::DefId leftDefinition;
      identity::DefId rightDefinition;
      ZC_IF_SOME(value, leftNote.causeDefinition) { leftDefinition = value; }
      ZC_IF_SOME(value, rightNote.causeDefinition) { rightDefinition = value; }
      if (leftDefinition != rightDefinition) return false;
    }
    for (size_t argument = 0; argument < leftNote.arguments.size(); ++argument) {
      if (!sameDisplayArgument(leftNote.arguments[argument], rightNote.arguments[argument], module,
                               identities, semanticTypes)) {
        return false;
      }
    }
  }
  if ((left.recovery == zc::none) != (right.recovery == zc::none)) return false;
  if (left.recovery != zc::none) {
    TypeErrorId leftId;
    TypeErrorId rightId;
    ZC_IF_SOME(value, left.recovery) { leftId = value; }
    ZC_IF_SOME(value, right.recovery) { rightId = value; }
    if (leftId != rightId) return false;
  }
  return true;
}

bool registeredFailure(const CheckerFailureRef& failure,
                       zc::ArrayPtr<const CheckerFailureRef> registered, identity::ModuleId module,
                       const CheckerIdentityAuthority& identities,
                       const type::SemanticTypeStore& semanticTypes) {
  for (const auto& candidate : registered) {
    if (sameFailure(failure, candidate, module, identities, semanticTypes)) return true;
  }
  return false;
}

bool recoveryIsOwned(const CheckerFailureRef& failure,
                     zc::ArrayPtr<const FrozenRecoveryLedger> ledgers) {
  if (failure.recovery == zc::none) return true;
  TypeErrorId id;
  ZC_IF_SOME(value, failure.recovery) { id = value; }
  for (const auto& ledger : ledgers) {
    if (ledger.contains(id)) return true;
  }
  return false;
}

bool validType(const type::SemanticTypeStore& semanticTypes, identity::SemanticTypeId type) {
  return semanticTypes.get(type).is<type::SemanticTypeLookup>();
}

bool validDefinition(const CheckerIdentityAuthority& identities, identity::DefId definition) {
  return identities.definition(definition) != zc::none;
}

bool validImpl(const CheckerIdentityAuthority& identities, identity::ImplId impl) {
  return identities.implementation(impl) != zc::none;
}

bool validateStoreContents(const CheckedFactsCandidate& candidate,
                           const CheckedFactsVerificationInput& input) {
  for (const auto& record : candidate.substitutionStore.records()) {
    if (record.value.parameters.size() != record.value.arguments.size()) return false;
    for (const auto parameter : record.value.parameters) {
      if (!validDefinition(input.identities, parameter)) return false;
    }
    for (const auto argument : record.value.arguments) {
      if (!validType(input.semanticTypes, argument)) return false;
    }
  }
  for (const auto& record : candidate.witnessStore.records()) {
    for (const auto& entry : record.value.entries) {
      if (!validType(input.semanticTypes, entry.subject) ||
          !validDefinition(input.identities, entry.interface.interface) ||
          !validImpl(input.identities, entry.impl)) {
        return false;
      }
      for (const auto argument : entry.interface.arguments) {
        if (!validType(input.semanticTypes, argument)) return false;
      }
      for (const auto& binding : entry.associatedBindings) {
        if (!validDefinition(input.identities, binding.associated) ||
            !validType(input.semanticTypes, binding.type)) {
          return false;
        }
      }
      for (const auto nested : entry.nested) {
        if (!candidate.witnessStore.contains(nested)) return false;
      }
    }
  }
  return true;
}

template <typename Map, typename NodeOf>
bool embeddedNodesMatch(const Map& map, NodeOf&& nodeOf) {
  for (const auto& entry : map.entries()) {
    if (!entry.key || entry.key != nodeOf(entry.value)) return false;
  }
  return true;
}

bool validateEmbeddedKeys(const CheckedFactsCandidate& candidate) {
  return embeddedNodesMatch(candidate.literals, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.aggregates, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.casts, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.calls, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.compoundAssignments,
                            [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.places, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.members, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.indexes, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.patterns, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.observedOperations,
                            [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.markerObligations,
                            [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.exhaustiveness, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.unsafeOperations,
                            [](const auto& fact) { return fact.operationNode; }) &&
         embeddedNodesMatch(candidate.projections, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.obligations, [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.errorUnionShapes,
                            [](const auto& fact) { return fact.node; }) &&
         embeddedNodesMatch(candidate.errorOperators, [](const auto& fact) { return fact.node; });
}

bool validateCrossFactRules(const CheckedFactsCandidate& candidate) {
  for (const auto& entry : candidate.calls.entries()) {
    const auto& invocation = entry.value.invocation;
    if ((invocation.raises == zc::none) && invocation.resultType != invocation.successType)
      return false;
    if (invocation.raises != zc::none) {
      bool matched = false;
      identity::SemanticTypeId raises;
      ZC_IF_SOME(value, invocation.raises) { raises = value; }
      for (const auto& shape : candidate.errorUnionShapes.entries()) {
        if (shape.key != entry.key) continue;
        matched = shape.value.origin == ErrorUnionShapeOrigin::RaisingCall &&
                  shape.value.valueType == invocation.resultType &&
                  shape.value.successType == invocation.successType &&
                  shape.value.residualType == raises;
      }
      if (!matched) return false;
    }
  }
  for (const auto& entry : candidate.compoundAssignments.entries()) {
    const auto& fact = entry.value;
    if (containsNode(candidate.calls, entry.key) || fact.invocation.receiver == zc::none ||
        fact.invocation.receiverMode == zc::none ||
        fact.invocation.receiverAdjustment == zc::none || fact.invocation.arguments.size() != 1) {
      return false;
    }
  }
  for (const auto& entry : candidate.errorOperators.entries()) {
    bool matched = false;
    for (const auto& shape : candidate.errorUnionShapes.entries()) {
      if (shape.key == entry.key && shape.value.valueType == entry.value.operandType &&
          shape.value.successType == entry.value.successType &&
          shape.value.residualType == entry.value.residualType) {
        matched = true;
      }
    }
    if (!matched) return false;
  }
  return true;
}

}  // namespace

CaptureKey CaptureKey::clone() const { return CaptureKey{closure.clone(), target.clone()}; }

bool CaptureKey::operator==(const CaptureKey& other) const noexcept {
  return closure == other.closure && sameBindingTarget(target, other.target);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeDisplayArgument(
    const CheckerDisplayArgument& argument, identity::ModuleId module,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes) {
  return encodeDisplayArgumentRecord(argument, module, identities, semanticTypes);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeSubstitution(
    uint32_t recordIndex, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).substitution(recordIndex);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeWitness(
    uint32_t recordIndex, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).witness(recordIndex);
}

zc::Maybe<identity::Sha256Digest> CheckedFactsCanonicalCodec::computeConstantEvaluationRevision(
    const ConstantEvaluationFact& fact, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).constantRevision(fact);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeNodeFact(
    CheckedFactGroup group, ast::NodeId node, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).nodeFact(group, node);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeDefinitionFact(
    CheckedFactGroup group, identity::DefId definition, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).definitionFact(group, definition);
}

zc::Maybe<zc::Array<uint8_t>> CheckedFactsCanonicalCodec::encodeCaptureFact(
    const CaptureKey& key, const CheckedFactsCandidate& candidate,
    const CheckedFactsVerificationInput& input) {
  return CheckedFactEncoder(candidate, input).captureFact(key);
}

bool CheckedFactsCanonicalCodec::recordsMatch(const CheckedFactsCandidate& candidate,
                                              const CheckedFactsVerificationInput& input) {
  for (uint32_t index = 0; index < candidate.substitutionStore.records().size(); ++index) {
    auto expected = encodeSubstitution(index, candidate, input);
    if (expected == zc::none) return false;
    bool matches = false;
    ZC_IF_SOME(record, expected) {
      matches = sameBytes(record.asPtr(),
                          candidate.substitutionStore.records()[index].canonicalRecord.asPtr());
    }
    if (!matches) return false;
  }
  for (uint32_t index = 0; index < candidate.witnessStore.records().size(); ++index) {
    auto expected = encodeWitness(index, candidate, input);
    if (expected == zc::none) return false;
    bool matches = false;
    ZC_IF_SOME(record, expected) {
      matches = sameBytes(record.asPtr(),
                          candidate.witnessStore.records()[index].canonicalRecord.asPtr());
    }
    if (!matches) return false;
  }

#define ZOM_MATCH_NODE_RECORDS(name, group)                                               \
  for (const auto& entry : candidate.name.entries()) {                                    \
    auto expected = encodeNodeFact(CheckedFactGroup::group, entry.key, candidate, input); \
    if (expected == zc::none) return false;                                               \
    bool matches = false;                                                                 \
    ZC_IF_SOME(record, expected) {                                                        \
      matches = sameBytes(record.asPtr(), entry.canonicalRecord.asPtr());                 \
    }                                                                                     \
    if (!matches) return false;                                                           \
  }
  ZOM_MATCH_NODE_RECORDS(nodeTypes, NodeType)
  ZOM_MATCH_NODE_RECORDS(literals, Literal)
  ZOM_MATCH_NODE_RECORDS(aggregates, Aggregate)
  ZOM_MATCH_NODE_RECORDS(places, Place)
  ZOM_MATCH_NODE_RECORDS(coercions, Coercion)
  ZOM_MATCH_NODE_RECORDS(casts, Cast)
  ZOM_MATCH_NODE_RECORDS(calls, Call)
  ZOM_MATCH_NODE_RECORDS(compoundAssignments, CompoundAssignment)
  ZOM_MATCH_NODE_RECORDS(members, Member)
  ZOM_MATCH_NODE_RECORDS(indexes, Index)
  ZOM_MATCH_NODE_RECORDS(patterns, Pattern)
  ZOM_MATCH_NODE_RECORDS(observedOperations, ObservedOperation)
  ZOM_MATCH_NODE_RECORDS(markerObligations, MarkerObligation)
  ZOM_MATCH_NODE_RECORDS(exhaustiveness, Exhaustiveness)
  ZOM_MATCH_NODE_RECORDS(unsafeOperations, UnsafeOperation)
  ZOM_MATCH_NODE_RECORDS(projections, Projection)
  ZOM_MATCH_NODE_RECORDS(obligations, Obligation)
  ZOM_MATCH_NODE_RECORDS(errorUnionShapes, ErrorUnionShape)
  ZOM_MATCH_NODE_RECORDS(errorOperators, ErrorOperator)
#undef ZOM_MATCH_NODE_RECORDS

#define ZOM_MATCH_DEFINITION_RECORDS(name, group)                                               \
  for (const auto& entry : candidate.name.entries()) {                                          \
    auto expected = encodeDefinitionFact(CheckedFactGroup::group, entry.key, candidate, input); \
    if (expected == zc::none) return false;                                                     \
    bool matches = false;                                                                       \
    ZC_IF_SOME(record, expected) {                                                              \
      matches = sameBytes(record.asPtr(), entry.canonicalRecord.asPtr());                       \
    }                                                                                           \
    if (!matches) return false;                                                                 \
  }
  ZOM_MATCH_DEFINITION_RECORDS(definitionTypes, DefinitionType)
  ZOM_MATCH_DEFINITION_RECORDS(constants, Constant)
#undef ZOM_MATCH_DEFINITION_RECORDS

  for (const auto& entry : candidate.captures.entries()) {
    auto expected = encodeCaptureFact(entry.key, candidate, input);
    if (expected == zc::none) return false;
    bool matches = false;
    ZC_IF_SOME(record, expected) {
      matches = sameBytes(record.asPtr(), entry.canonicalRecord.asPtr());
    }
    if (!matches) return false;
  }
  return true;
}

struct FrozenSubstitutionStore::Impl final {
  Impl(identity::SemanticContextBrand context, identity::RegistryBrand issuer,
       zc::Vector<Record>&& records)
      : context(context), issuer(issuer), records(zc::mv(records)) {}
  identity::SemanticContextBrand context;
  identity::RegistryBrand issuer;
  zc::Vector<Record> records;
};

zc::Maybe<FrozenSubstitutionStore> FrozenSubstitutionStore::from(
    identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
    zc::Vector<Record>&& records) {
  if (!semanticContext.isValid() || !issuer.belongsTo(semanticContext) ||
      records.size() > UINT32_MAX) {
    return zc::none;
  }
  return FrozenSubstitutionStore(
      zc::heap<FrozenSubstitutionStore::Impl>(semanticContext, issuer, zc::mv(records)));
}
FrozenSubstitutionStore::FrozenSubstitutionStore(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
FrozenSubstitutionStore::~FrozenSubstitutionStore() noexcept(false) = default;
FrozenSubstitutionStore::FrozenSubstitutionStore(FrozenSubstitutionStore&&) noexcept = default;
FrozenSubstitutionStore& FrozenSubstitutionStore::operator=(FrozenSubstitutionStore&&) noexcept =
    default;
identity::SemanticContextBrand FrozenSubstitutionStore::semanticContext() const noexcept {
  return impl->context;
}
identity::RegistryBrand FrozenSubstitutionStore::issuer() const noexcept { return impl->issuer; }
zc::ArrayPtr<const FrozenSubstitutionStore::Record> FrozenSubstitutionStore::records()
    const noexcept {
  return impl->records.asPtr();
}
zc::Maybe<CanonicalSubstitutionId> FrozenSubstitutionStore::idAt(uint32_t index) const noexcept {
  if (index >= impl->records.size()) return zc::none;
  return CanonicalSubstitutionTag::issue(impl->context, impl->issuer, index);
}
bool FrozenSubstitutionStore::contains(CanonicalSubstitutionId id) const noexcept {
  return id.belongsTo(impl->issuer) && CanonicalSubstitutionTag::slot(id) < impl->records.size();
}

struct FrozenWitnessStore::Impl final {
  Impl(identity::SemanticContextBrand context, identity::RegistryBrand issuer,
       zc::Vector<Record>&& records)
      : context(context), issuer(issuer), records(zc::mv(records)) {}
  identity::SemanticContextBrand context;
  identity::RegistryBrand issuer;
  zc::Vector<Record> records;
};

zc::Maybe<FrozenWitnessStore> FrozenWitnessStore::from(
    identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
    zc::Vector<Record>&& records) {
  if (!semanticContext.isValid() || !issuer.belongsTo(semanticContext) ||
      records.size() > UINT32_MAX) {
    return zc::none;
  }
  return FrozenWitnessStore(
      zc::heap<FrozenWitnessStore::Impl>(semanticContext, issuer, zc::mv(records)));
}
FrozenWitnessStore::FrozenWitnessStore(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
FrozenWitnessStore::~FrozenWitnessStore() noexcept(false) = default;
FrozenWitnessStore::FrozenWitnessStore(FrozenWitnessStore&&) noexcept = default;
FrozenWitnessStore& FrozenWitnessStore::operator=(FrozenWitnessStore&&) noexcept = default;
identity::SemanticContextBrand FrozenWitnessStore::semanticContext() const noexcept {
  return impl->context;
}
identity::RegistryBrand FrozenWitnessStore::issuer() const noexcept { return impl->issuer; }
zc::ArrayPtr<const FrozenWitnessStore::Record> FrozenWitnessStore::records() const noexcept {
  return impl->records.asPtr();
}
zc::Maybe<WitnessArgumentsId> FrozenWitnessStore::idAt(uint32_t index) const noexcept {
  if (index >= impl->records.size()) return zc::none;
  return WitnessArgumentsTag::issue(impl->context, impl->issuer, index);
}
bool FrozenWitnessStore::contains(WitnessArgumentsId id) const noexcept {
  return id.belongsTo(impl->issuer) && WitnessArgumentsTag::slot(id) < impl->records.size();
}

bool CheckedFactsCanonicalCodec::writeCanonicalRecords(CheckedFactsCandidate& candidate,
                                                       const CheckedFactsVerificationInput& input) {
  for (uint32_t index = 0; index < candidate.substitutionStore.impl->records.size(); ++index) {
    auto encoded = encodeSubstitution(index, candidate, input);
    if (encoded == zc::none) return false;
    ZC_IF_SOME(record, encoded) {
      candidate.substitutionStore.impl->records[index].canonicalRecord = zc::mv(record);
    }
  }
  for (uint32_t index = 0; index < candidate.witnessStore.impl->records.size(); ++index) {
    auto encoded = encodeWitness(index, candidate, input);
    if (encoded == zc::none) return false;
    ZC_IF_SOME(record, encoded) {
      candidate.witnessStore.impl->records[index].canonicalRecord = zc::mv(record);
    }
  }

#define ZOM_WRITE_NODE_RECORDS(name, group)                                              \
  for (auto& entry : candidate.name.values) {                                            \
    auto encoded = encodeNodeFact(CheckedFactGroup::group, entry.key, candidate, input); \
    if (encoded == zc::none) return false;                                               \
    ZC_IF_SOME(record, encoded) { entry.canonicalRecord = zc::mv(record); }              \
  }
  ZOM_WRITE_NODE_RECORDS(nodeTypes, NodeType)
  ZOM_WRITE_NODE_RECORDS(literals, Literal)
  ZOM_WRITE_NODE_RECORDS(aggregates, Aggregate)
  ZOM_WRITE_NODE_RECORDS(places, Place)
  ZOM_WRITE_NODE_RECORDS(coercions, Coercion)
  ZOM_WRITE_NODE_RECORDS(casts, Cast)
  ZOM_WRITE_NODE_RECORDS(calls, Call)
  ZOM_WRITE_NODE_RECORDS(compoundAssignments, CompoundAssignment)
  ZOM_WRITE_NODE_RECORDS(members, Member)
  ZOM_WRITE_NODE_RECORDS(indexes, Index)
  ZOM_WRITE_NODE_RECORDS(patterns, Pattern)
  ZOM_WRITE_NODE_RECORDS(observedOperations, ObservedOperation)
  ZOM_WRITE_NODE_RECORDS(markerObligations, MarkerObligation)
  ZOM_WRITE_NODE_RECORDS(exhaustiveness, Exhaustiveness)
  ZOM_WRITE_NODE_RECORDS(unsafeOperations, UnsafeOperation)
  ZOM_WRITE_NODE_RECORDS(projections, Projection)
  ZOM_WRITE_NODE_RECORDS(obligations, Obligation)
  ZOM_WRITE_NODE_RECORDS(errorUnionShapes, ErrorUnionShape)
  ZOM_WRITE_NODE_RECORDS(errorOperators, ErrorOperator)
#undef ZOM_WRITE_NODE_RECORDS

  for (auto& entry : candidate.constants.values) {
    auto revision = computeConstantEvaluationRevision(entry.value, candidate, input);
    if (revision == zc::none) return false;
    ZC_IF_SOME(value, revision) { entry.value.evaluationRevision = value; }
  }

#define ZOM_WRITE_DEFINITION_RECORDS(name, group)                                              \
  for (auto& entry : candidate.name.values) {                                                  \
    auto encoded = encodeDefinitionFact(CheckedFactGroup::group, entry.key, candidate, input); \
    if (encoded == zc::none) return false;                                                     \
    ZC_IF_SOME(record, encoded) { entry.canonicalRecord = zc::mv(record); }                    \
  }
  ZOM_WRITE_DEFINITION_RECORDS(definitionTypes, DefinitionType)
  ZOM_WRITE_DEFINITION_RECORDS(constants, Constant)
#undef ZOM_WRITE_DEFINITION_RECORDS

  for (auto& entry : candidate.captures.values) {
    auto encoded = encodeCaptureFact(entry.key, candidate, input);
    if (encoded == zc::none) return false;
    ZC_IF_SOME(record, encoded) { entry.canonicalRecord = zc::mv(record); }
  }

#define ZOM_SORT_RECORDS(name)                                                         \
  for (size_t index = 1; index < candidate.name.values.size(); ++index) {              \
    auto current = zc::mv(candidate.name.values[index]);                               \
    size_t insertion = index;                                                          \
    while (insertion > 0 &&                                                            \
           lessBytes(current.canonicalRecord.asPtr(),                                  \
                     candidate.name.values[insertion - 1].canonicalRecord.asPtr())) {  \
      candidate.name.values[insertion] = zc::mv(candidate.name.values[insertion - 1]); \
      --insertion;                                                                     \
    }                                                                                  \
    candidate.name.values[insertion] = zc::mv(current);                                \
  }
  ZOM_SORT_RECORDS(nodeTypes)
  ZOM_SORT_RECORDS(definitionTypes)
  ZOM_SORT_RECORDS(literals)
  ZOM_SORT_RECORDS(constants)
  ZOM_SORT_RECORDS(aggregates)
  ZOM_SORT_RECORDS(places)
  ZOM_SORT_RECORDS(coercions)
  ZOM_SORT_RECORDS(casts)
  ZOM_SORT_RECORDS(calls)
  ZOM_SORT_RECORDS(compoundAssignments)
  ZOM_SORT_RECORDS(members)
  ZOM_SORT_RECORDS(indexes)
  ZOM_SORT_RECORDS(patterns)
  ZOM_SORT_RECORDS(observedOperations)
  ZOM_SORT_RECORDS(captures)
  ZOM_SORT_RECORDS(markerObligations)
  ZOM_SORT_RECORDS(exhaustiveness)
  ZOM_SORT_RECORDS(unsafeOperations)
  ZOM_SORT_RECORDS(projections)
  ZOM_SORT_RECORDS(obligations)
  ZOM_SORT_RECORDS(errorUnionShapes)
  ZOM_SORT_RECORDS(errorOperators)
#undef ZOM_SORT_RECORDS
  return true;
}

zc::Maybe<FrozenRecoveryLedger> FrozenRecoveryLedger::from(
    identity::SemanticContextBrand semanticContext, identity::RegistryBrand issuer,
    uint32_t errorCount, zc::Array<uint8_t>&& canonicalRecord) {
  if (!semanticContext.isValid() || !issuer.belongsTo(semanticContext) || errorCount == 0 ||
      canonicalRecord.size() == 0) {
    return zc::none;
  }
  return FrozenRecoveryLedger(semanticContext, issuer, errorCount, zc::mv(canonicalRecord));
}
zc::Maybe<TypeErrorId> FrozenRecoveryLedger::idAt(uint32_t index) const noexcept {
  if (index >= errorCountValue) return zc::none;
  return TypeErrorTag::issue(context, registry, index);
}
bool FrozenRecoveryLedger::contains(TypeErrorId id) const noexcept {
  return id.belongsTo(registry) && TypeErrorTag::slot(id) < errorCountValue;
}

CheckedFactsRevision::CheckedFactsRevision(const identity::Sha256Digest& value) noexcept
    : value(value) {}
const identity::Sha256Digest& CheckedFactsRevision::digest() const noexcept { return value; }

zc::Maybe<CheckedFactsRevision> CheckedFactsRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedOwningModule,
    const identity::Sha256Digest& sourceContentDigest,
    const identity::Sha256Digest& parsedModuleReceipt,
    const identity::Sha256Digest& signatureFactsRevision,
    const identity::Sha256Digest& importedSignatureViewRevision,
    const identity::Sha256Digest& coherenceViewRevision,
    const identity::SemanticCompilerOptionsKey& semanticOptions,
    const CheckedFactsCanonicalGroups& groups) {
  if (expandedOwningModule.size() == 0) return zc::none;
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.checked-facts-revision"_zcc);
  encoder.encodeUint8(0);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedOwningModule);
  encoder.encodeDigest(sourceContentDigest);
  encoder.encodeDigest(parsedModuleReceipt);
  encoder.encodeDigest(signatureFactsRevision);
  encoder.encodeDigest(importedSignatureViewRevision);
  encoder.encodeDigest(coherenceViewRevision);
  semanticOptions.encode(encoder);
  if (!encodeSortedRecords(encoder, groups.substitutions) ||
      !encodeSortedRecords(encoder, groups.witnesses) ||
      !encodeSortedRecords(encoder, groups.nodeTypes) ||
      !encodeSortedRecords(encoder, groups.definitionTypes) ||
      !encodeSortedRecords(encoder, groups.literals) ||
      !encodeSortedRecords(encoder, groups.constants) ||
      !encodeSortedRecords(encoder, groups.aggregates) ||
      !encodeSortedRecords(encoder, groups.places) ||
      !encodeSortedRecords(encoder, groups.coercions) ||
      !encodeSortedRecords(encoder, groups.casts) || !encodeSortedRecords(encoder, groups.calls) ||
      !encodeSortedRecords(encoder, groups.compoundAssignments) ||
      !encodeSortedRecords(encoder, groups.members) ||
      !encodeSortedRecords(encoder, groups.indexes) ||
      !encodeSortedRecords(encoder, groups.patterns) ||
      !encodeSortedRecords(encoder, groups.observedOperations) ||
      !encodeSortedRecords(encoder, groups.captures) ||
      !encodeSortedRecords(encoder, groups.markerObligations) ||
      !encodeSortedRecords(encoder, groups.exhaustiveness) ||
      !encodeSortedRecords(encoder, groups.unsafeOperations) ||
      !encodeSortedRecords(encoder, groups.projections) ||
      !encodeSortedRecords(encoder, groups.obligations) ||
      !encodeSortedRecords(encoder, groups.errorUnionShapes) ||
      !encodeSortedRecords(encoder, groups.errorOperators)) {
    return zc::none;
  }
  ZC_IF_SOME(digest, identity::sha256(encoder.finish().asPtr())) {
    return CheckedFactsRevision(digest);
  }
  return zc::none;
}

struct VerifiedCheckedFacts::Impl final {
  Impl(CheckedFactsCandidate&& candidate, CheckedFactsRevision&& revision)
      : revision(zc::mv(revision)),
        semanticContext(candidate.semanticContext),
        contextFingerprint(zc::mv(candidate.contextFingerprint)),
        module(candidate.module),
        sourceContentDigest(candidate.sourceContentDigest),
        parsedModuleReceipt(candidate.parsedModuleReceipt),
        signatureFactsRevision(candidate.signatureFactsRevision),
        importedSignatureViewRevision(candidate.importedSignatureViewRevision),
        coherenceViewRevision(candidate.coherenceViewRevision),
        semanticOptions(candidate.semanticOptions),
        substitutionStore(zc::mv(candidate.substitutionStore)),
        witnessStore(zc::mv(candidate.witnessStore)),
        nodeTypes(zc::mv(candidate.nodeTypes)),
        definitionTypes(zc::mv(candidate.definitionTypes)),
        literals(zc::mv(candidate.literals)),
        constants(zc::mv(candidate.constants)),
        aggregates(zc::mv(candidate.aggregates)),
        places(zc::mv(candidate.places)),
        coercions(zc::mv(candidate.coercions)),
        casts(zc::mv(candidate.casts)),
        calls(zc::mv(candidate.calls)),
        compoundAssignments(zc::mv(candidate.compoundAssignments)),
        members(zc::mv(candidate.members)),
        indexes(zc::mv(candidate.indexes)),
        patterns(zc::mv(candidate.patterns)),
        observedOperations(zc::mv(candidate.observedOperations)),
        captures(zc::mv(candidate.captures)),
        markerObligations(zc::mv(candidate.markerObligations)),
        exhaustiveness(zc::mv(candidate.exhaustiveness)),
        unsafeOperations(zc::mv(candidate.unsafeOperations)),
        projections(zc::mv(candidate.projections)),
        obligations(zc::mv(candidate.obligations)),
        errorUnionShapes(zc::mv(candidate.errorUnionShapes)),
        errorOperators(zc::mv(candidate.errorOperators)),
        advisories(zc::mv(candidate.advisories)) {}

  CheckedFactsRevision revision;
  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  binder::ParsedModuleReceipt parsedModuleReceipt;
  signature::SignatureFactsRevision signatureFactsRevision;
  cross_module::ImportedSignatureViewRevision importedSignatureViewRevision;
  cross_module::CoherenceViewRevision coherenceViewRevision;
  identity::SemanticCompilerOptionsKey semanticOptions;
  FrozenSubstitutionStore substitutionStore;
  FrozenWitnessStore witnessStore;
  NodeTypeMap nodeTypes;
  DefinitionTypeMap definitionTypes;
  LiteralFactMap literals;
  ConstantFactMap constants;
  AggregateFactMap aggregates;
  PlaceFactMap places;
  CoercionFactMap coercions;
  CastFactMap casts;
  CallFactMap calls;
  CompoundAssignmentFactMap compoundAssignments;
  MemberFactMap members;
  IndexFactMap indexes;
  PatternFactMap patterns;
  ObservedOperationFactMap observedOperations;
  CaptureFactMap captures;
  MarkerObligationFactMap markerObligations;
  ExhaustivenessFactMap exhaustiveness;
  UnsafeOperationFactMap unsafeOperations;
  ProjectionFactMap projections;
  ObligationFactMap obligations;
  ErrorUnionShapeFactMap errorUnionShapes;
  ErrorOperatorFactMap errorOperators;
  zc::Vector<CheckerAdvisoryRef> advisories;
};

VerifiedCheckedFacts::VerifiedCheckedFacts(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
VerifiedCheckedFacts::~VerifiedCheckedFacts() noexcept(false) = default;
VerifiedCheckedFacts::VerifiedCheckedFacts(VerifiedCheckedFacts&&) noexcept = default;
VerifiedCheckedFacts& VerifiedCheckedFacts::operator=(VerifiedCheckedFacts&&) noexcept = default;
const CheckedFactsRevision& VerifiedCheckedFacts::revision() const noexcept {
  return impl->revision;
}
identity::SemanticContextBrand VerifiedCheckedFacts::semanticContext() const noexcept {
  return impl->semanticContext;
}
identity::ModuleId VerifiedCheckedFacts::module() const noexcept { return impl->module; }
const signature::SignatureFactsRevision& VerifiedCheckedFacts::signatureFactsRevision()
    const noexcept {
  return impl->signatureFactsRevision;
}
const cross_module::ImportedSignatureViewRevision&
VerifiedCheckedFacts::importedSignatureViewRevision() const noexcept {
  return impl->importedSignatureViewRevision;
}
const cross_module::CoherenceViewRevision& VerifiedCheckedFacts::coherenceViewRevision()
    const noexcept {
  return impl->coherenceViewRevision;
}
const FrozenSubstitutionStore& VerifiedCheckedFacts::substitutionStore() const noexcept {
  return impl->substitutionStore;
}
const FrozenWitnessStore& VerifiedCheckedFacts::witnessStore() const noexcept {
  return impl->witnessStore;
}

#define ZOM_CHECKED_FACT_ACCESSOR_IMPL(name, Type) \
  const Type& VerifiedCheckedFacts::name() const noexcept { return impl->name; }
ZOM_CHECKED_FACT_ACCESSOR_IMPL(nodeTypes, NodeTypeMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(definitionTypes, DefinitionTypeMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(literals, LiteralFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(constants, ConstantFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(aggregates, AggregateFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(places, PlaceFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(coercions, CoercionFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(casts, CastFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(calls, CallFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(compoundAssignments, CompoundAssignmentFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(members, MemberFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(indexes, IndexFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(patterns, PatternFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(observedOperations, ObservedOperationFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(captures, CaptureFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(markerObligations, MarkerObligationFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(exhaustiveness, ExhaustivenessFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(unsafeOperations, UnsafeOperationFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(projections, ProjectionFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(obligations, ObligationFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(errorUnionShapes, ErrorUnionShapeFactMap)
ZOM_CHECKED_FACT_ACCESSOR_IMPL(errorOperators, ErrorOperatorFactMap)
#undef ZOM_CHECKED_FACT_ACCESSOR_IMPL

zc::ArrayPtr<const CheckerAdvisoryRef> VerifiedCheckedFacts::advisories() const noexcept {
  return impl->advisories.asPtr();
}

namespace {

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  auto leftBytes = left.encode();
  auto rightBytes = right.encode();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool authorizedDefinition(identity::DefId definition, const CheckedFactsCandidate& candidate,
                          const CheckedFactsVerificationInput& input) {
  return authorizedDefinition(definition, candidate.module, input);
}

bool authorizedDefinition(identity::DefId definition, identity::ModuleId module,
                          const CheckedFactsVerificationInput& input) {
  auto definitionEntry = input.identities.definition(definition);
  auto moduleEntry = input.identities.module(module);
  if (definitionEntry == zc::none || moduleEntry == zc::none) return false;
  bool local = false;
  ZC_IF_SOME(definitionValue, definitionEntry) {
    ZC_IF_SOME(moduleValue, moduleEntry) {
      local = sameModule(definitionValue.record().module(), moduleValue.key());
    }
  }
  if (local) return true;
  for (const auto imported : input.importedDefinitions) {
    if (imported == definition) return true;
  }
  return false;
}

bool validPlace(const CheckedPlaceFact& fact, const CheckedFactsCandidate& candidate,
                const CheckedFactsVerificationInput& input) {
  if (!fact.node || !validType(input.semanticTypes, fact.type)) return false;
  const auto& root = fact.root.variant();
  if (root.is<DefinitionPlaceRoot>()) {
    if (!authorizedDefinition(root.get<DefinitionPlaceRoot>().definition, candidate, input)) {
      return false;
    }
  } else if (root.is<OwnerLocalPlaceRoot>()) {
    const auto binding = root.get<OwnerLocalPlaceRoot>().binding;
    if (!binding.isValid() || !binding.belongsTo(candidate.module)) return false;
    bool found = false;
    for (const auto& entry : input.ownerLocalBindings) {
      if (entry.binding == binding) found = true;
    }
    if (!found) return false;
  } else if (root.is<CallableParameterPlaceRoot>()) {
    auto parameter =
        input.identities.callableParameter(root.get<CallableParameterPlaceRoot>().parameter);
    auto module = input.identities.module(candidate.module);
    if (parameter == zc::none || module == zc::none) return false;
    bool local = false;
    ZC_IF_SOME(parameterValue, parameter) {
      auto owner = input.identities.definition(parameterValue.record().owner());
      ZC_IF_SOME(ownerValue, owner) {
        ZC_IF_SOME(moduleValue, module) {
          local = sameModule(ownerValue.record().module(), moduleValue.key());
        }
      }
    }
    if (!local) return false;
  } else if (!root.is<DereferencePlaceRoot>() && !root.is<TemporaryPlaceRoot>()) {
    return false;
  }

  for (const auto& projection : fact.projections) {
    const auto& value = projection.variant();
    if (value.is<FieldProjection>()) {
      if (!authorizedDefinition(value.get<FieldProjection>().field, candidate, input)) return false;
    } else if (!value.is<TupleIndexProjection>() && !value.is<IndexProjection>()) {
      return false;
    }
  }
  return true;
}

bool authorizedImpl(identity::ImplId impl, const CheckedFactsVerificationInput& input) {
  if (!validImpl(input.identities, impl)) return false;
  for (const auto coherent : input.coherentImpls) {
    if (coherent == impl) return true;
  }
  return false;
}

bool validModuleSpan(const identity::ModuleKey& module, const identity::SourceFileKey& source,
                     const identity::SourceSpan& span) {
  return source.belongsTo(module.crate()) && span.belongsTo(source);
}

bool validInterface(const InterfaceInstantiation& interface, const CheckedFactsCandidate& candidate,
                    const CheckedFactsVerificationInput& input) {
  if (!authorizedDefinition(interface.interface, candidate, input)) return false;
  for (const auto argument : interface.arguments) {
    if (!validType(input.semanticTypes, argument)) return false;
  }
  return true;
}

bool validCoercion(const CoercionAdjustment& adjustment, const CheckedFactsCandidate& candidate,
                   const CheckedFactsVerificationInput& input,
                   const identity::ModuleKey& moduleKey) {
  if (!validType(input.semanticTypes, adjustment.source) ||
      !validType(input.semanticTypes, adjustment.destination) || adjustment.steps.size() == 0 ||
      !validModuleSpan(moduleKey, input.source, adjustment.sourceSpan)) {
    return false;
  }
  for (const auto& step : adjustment.steps) {
    const auto& variant = step.variant();
    if (variant.is<UnionInjectStep>()) {
      if (!validType(input.semanticTypes, variant.get<UnionInjectStep>().alternative)) return false;
    } else if (variant.is<DynEraseStep>()) {
      const auto& dyn = variant.get<DynEraseStep>();
      if (!validInterface(dyn.interface, candidate, input) || !authorizedImpl(dyn.impl, input) ||
          !candidate.witnessStore.contains(dyn.witnesses)) {
        return false;
      }
    } else if (variant.is<DynUpcastStep>()) {
      const auto& path = variant.get<DynUpcastStep>().path;
      if (path.size() == 0) return false;
      for (const auto interface : path) {
        if (!authorizedDefinition(interface, candidate, input)) return false;
      }
    }
  }
  return true;
}

bool validArgument(const CheckedArgumentFact& argument, const CheckedFactsCandidate& candidate,
                   const CheckedFactsVerificationInput& input,
                   const identity::ModuleKey& moduleKey) {
  if (!argument.sourceNode || !validType(input.semanticTypes, argument.sourceType) ||
      !validType(input.semanticTypes, argument.parameterType)) {
    return false;
  }
  ZC_IF_SOME(adjustment, argument.adjustment) {
    if (!validCoercion(adjustment, candidate, input, moduleKey) ||
        adjustment.source != argument.sourceType ||
        adjustment.destination != argument.parameterType) {
      return false;
    }
  }
  return true;
}

bool validSelectedCallable(const SelectedCallable& selected, const CheckedFactsCandidate& candidate,
                           const CheckedFactsVerificationInput& input) {
  const auto& variant = selected.variant();
  if (variant.is<DirectCallable>()) {
    return authorizedDefinition(variant.get<DirectCallable>().callee, candidate, input);
  }
  if (variant.is<ConcreteMethodCallable>()) {
    return authorizedDefinition(variant.get<ConcreteMethodCallable>().method, candidate, input);
  }
  if (variant.is<ImplMethodCallable>()) {
    const auto& method = variant.get<ImplMethodCallable>();
    return authorizedImpl(method.impl, input) &&
           authorizedDefinition(method.method, candidate, input);
  }
  if (variant.is<WitnessMethodCallable>()) {
    const auto& method = variant.get<WitnessMethodCallable>();
    return authorizedDefinition(method.witnessParameter, candidate, input) &&
           authorizedDefinition(method.interface, candidate, input) &&
           authorizedDefinition(method.method, candidate, input);
  }
  if (variant.is<DynMethodCallable>()) {
    const auto& method = variant.get<DynMethodCallable>();
    return authorizedDefinition(method.interface, candidate, input) &&
           authorizedDefinition(method.method, candidate, input);
  }
  return variant.is<PrimitiveCallable>();
}

bool validCallEnvelope(const CheckedCallEnvelope& invocation,
                       const CheckedFactsCandidate& candidate,
                       const CheckedFactsVerificationInput& input,
                       const identity::ModuleKey& moduleKey) {
  if (!validSelectedCallable(invocation.selected, candidate, input) ||
      !validType(input.semanticTypes, invocation.calleeType) ||
      !validType(input.semanticTypes, invocation.successType) ||
      !validType(input.semanticTypes, invocation.resultType)) {
    return false;
  }
  const bool hasReceiver = invocation.receiver != zc::none;
  if (hasReceiver != (invocation.receiverMode != zc::none) ||
      hasReceiver != (invocation.receiverAdjustment != zc::none)) {
    return false;
  }
  if (invocation.selected.variant().is<ConcreteMethodCallable>()) {
    if (!hasReceiver) return false;
    ZC_IF_SOME(receiver, invocation.receiver) {
      ZC_IF_SOME(mode, invocation.receiverMode) {
        ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
          if (mode != ReceiverMode::Mutable || receiver.sourceType != adjustment.source ||
              receiver.parameterType != adjustment.destination || adjustment.steps.size() != 1 ||
              adjustment.steps[0] != ReceiverAdjustmentStep::BorrowMutable) {
            return false;
          }
        }
      }
    }
  }
  ZC_IF_SOME(receiver, invocation.receiver) {
    if (!validArgument(receiver, candidate, input, moduleKey)) return false;
  }
  ZC_IF_SOME(adjustment, invocation.receiverAdjustment) {
    if (!validType(input.semanticTypes, adjustment.source) ||
        !validType(input.semanticTypes, adjustment.destination) || adjustment.steps.size() == 0 ||
        !validModuleSpan(moduleKey, input.source, adjustment.sourceSpan)) {
      return false;
    }
  }
  for (const auto& argument : invocation.arguments) {
    if (!validArgument(argument, candidate, input, moduleKey)) return false;
  }
  ZC_IF_SOME(substitutions, invocation.substitutions) {
    if (!candidate.substitutionStore.contains(substitutions)) return false;
  }
  ZC_IF_SOME(witnesses, invocation.witnesses) {
    if (!candidate.witnessStore.contains(witnesses)) return false;
  }
  ZC_IF_SOME(raises, invocation.raises) {
    if (!validType(input.semanticTypes, raises)) return false;
  }
  return true;
}

bool validCanonicalValue(const CanonicalConstValue& value, const CheckedFactsCandidate& candidate,
                         const CheckedFactsVerificationInput& input) {
  return validCanonicalValue(value, candidate.module, input);
}

bool validCanonicalValue(const CanonicalConstValue& value, identity::ModuleId module,
                         const CheckedFactsVerificationInput& input) {
  return signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
             value, module, input.identities, input.semanticTypes) != zc::none;
}

bool validPatternConstructor(const PatternConstructor& constructor,
                             const CheckedFactsCandidate& candidate,
                             const CheckedFactsVerificationInput& input) {
  return validPatternConstructor(constructor, candidate.module, input);
}

bool validPatternConstructor(const PatternConstructor& constructor, identity::ModuleId module,
                             const CheckedFactsVerificationInput& input) {
  const auto& variant = constructor.variant();
  if (variant.is<LiteralPattern>()) {
    const auto& literal = variant.get<LiteralPattern>().value;
    return literal.tag() <= signature::CanonicalConstValueTag::Unit &&
           validCanonicalValue(literal, module, input);
  }
  if (variant.is<UnionAlternativePattern>()) {
    return validType(input.semanticTypes, variant.get<UnionAlternativePattern>().type);
  }
  if (variant.is<EnumVariantPattern>()) {
    return authorizedDefinition(variant.get<EnumVariantPattern>().variant, module, input);
  }
  if (variant.is<NominalPattern>()) {
    return authorizedDefinition(variant.get<NominalPattern>().definition, module, input);
  }
  return true;
}

enum class DisplayArgumentKind : uint8_t {
  Type,
  PrimitiveType,
  Definition,
  Identifier,
  Count,
  ConstraintContext,
  Operator,
  Literal,
  Patterns
};

DisplayArgumentKind displayArgumentKind(const CheckerDisplayArgument& argument) {
  const auto& value = argument.variant();
  if (value.is<TypeDisplayArg>()) return DisplayArgumentKind::Type;
  if (value.is<PrimitiveTypeDisplayArg>()) return DisplayArgumentKind::PrimitiveType;
  if (value.is<DefinitionDisplayArg>()) return DisplayArgumentKind::Definition;
  if (value.is<IdentifierDisplayArg>()) return DisplayArgumentKind::Identifier;
  if (value.is<CountDisplayArg>()) return DisplayArgumentKind::Count;
  if (value.is<ConstraintContextDisplayArg>()) return DisplayArgumentKind::ConstraintContext;
  if (value.is<OperatorDisplayArg>()) return DisplayArgumentKind::Operator;
  if (value.is<LiteralDisplayArg>()) return DisplayArgumentKind::Literal;
  return DisplayArgumentKind::Patterns;
}

bool argumentKinds(zc::ArrayPtr<const CheckerDisplayArgument> arguments) {
  return arguments.size() == 0;
}

bool argumentKinds(zc::ArrayPtr<const CheckerDisplayArgument> arguments,
                   DisplayArgumentKind first) {
  return arguments.size() == 1 && displayArgumentKind(arguments[0]) == first;
}

bool argumentKinds(zc::ArrayPtr<const CheckerDisplayArgument> arguments, DisplayArgumentKind first,
                   DisplayArgumentKind second) {
  return arguments.size() == 2 && displayArgumentKind(arguments[0]) == first &&
         displayArgumentKind(arguments[1]) == second;
}

bool argumentKinds(zc::ArrayPtr<const CheckerDisplayArgument> arguments, DisplayArgumentKind first,
                   DisplayArgumentKind second, DisplayArgumentKind third) {
  return arguments.size() == 3 && displayArgumentKind(arguments[0]) == first &&
         displayArgumentKind(arguments[1]) == second && displayArgumentKind(arguments[2]) == third;
}

bool validArgumentSchema(const CheckerFailureRef& failure) {
  using diagnostics::DiagID;
  using Kind = DisplayArgumentKind;
  const auto arguments = failure.arguments.asPtr();
  switch (failure.diagnostic.diagnosticId()) {
    case DiagID::DynGenericMethod:
    case DiagID::DynSelfReturn:
    case DiagID::DynMoveSelf:
    case DiagID::DynUnassociatedType:
    case DiagID::DynGatNotAllowed:
    case DiagID::DynSuperNotObjectSafe:
    case DiagID::DynDuplicateAssociatedTypeBinding:
      return argumentKinds(arguments, Kind::Definition, Kind::Definition);
    case DiagID::DynStaticMethod:
    case DiagID::CannotInferTypeParameter:
    case DiagID::CannotMutateImmutableVariable:
    case DiagID::MissingStructField:
    case DiagID::ConstantValueOutOfRange:
    case DiagID::ConstantDependencyCycle:
      return argumentKinds(arguments, Kind::Definition);
    case DiagID::DynUnsizedParameter:
      return argumentKinds(arguments, Kind::Definition, Kind::Definition, Kind::Type);
    case DiagID::TypeCheckerTypeMismatch:
    case DiagID::CheckerInvalidCast:
    case DiagID::ErrorPropagateOutsideRaises:
    case DiagID::ArrayElementTypeMismatch:
    case DiagID::InvalidDynUpcast:
      return argumentKinds(arguments, Kind::Type, Kind::Type);
    case DiagID::CannotUnifyTypes:
      return argumentKinds(arguments, Kind::Type, Kind::Type, Kind::ConstraintContext);
    case DiagID::InfiniteType:
    case DiagID::CannotCallNonFunction:
    case DiagID::ErrorUnwrapNonUnion:
    case DiagID::CannotDereferenceType:
    case DiagID::PostfixUpdateRequiresNumeric:
    case DiagID::ErrorPropagateNonUnion:
    case DiagID::IndexRequiresInteger:
    case DiagID::CannotIndexType:
    case DiagID::ConditionMustBeBool:
    case DiagID::MissingReturnValue:
    case DiagID::AggregateLiteralTargetRequired:
    case DiagID::MatchGuardMustBeBool:
      return argumentKinds(arguments, Kind::Type);
    case DiagID::ExplicitTypeArgumentCountMismatch:
    case DiagID::CallArgumentCountMismatch:
      return argumentKinds(arguments, Kind::Count, Kind::Count);
    case DiagID::ConflictingImpl:
    case DiagID::OrphanImpl:
      return argumentKinds(arguments, Kind::Definition, Kind::Type);
    case DiagID::CheckerTraitNotImplemented:
      return argumentKinds(arguments, Kind::Type, Kind::Definition);
    case DiagID::OperatorTraitSignatureMismatch:
      return argumentKinds(arguments, Kind::Operator, Kind::Type);
    case DiagID::NoAssociatedTypeProjection:
      return argumentKinds(arguments, Kind::Definition, Kind::Type);
    case DiagID::AmbiguousAssociatedTypeProjection:
    case DiagID::MemberNotFound:
      return argumentKinds(arguments, Kind::Identifier, Kind::Type);
    case DiagID::CheckerNonExhaustiveMatch:
      return argumentKinds(arguments, Kind::Patterns);
    case DiagID::InvalidBinaryOperands:
    case DiagID::InvalidComparisonOperands:
      return argumentKinds(arguments, Kind::Operator, Kind::Type, Kind::Type);
    case DiagID::UnknownStructField:
      return argumentKinds(arguments, Kind::Identifier);
    case DiagID::BodyLiteralOutOfRange:
      return argumentKinds(arguments, Kind::Literal, Kind::PrimitiveType);
    case DiagID::ConstantArithmeticFailure:
      return argumentKinds(arguments, Kind::Operator);
    case DiagID::CannotInferNullInitializer:
    case DiagID::ErrorUnionEmpty:
    case DiagID::ExplicitTypeArgumentsRequireGenericCallee:
    case DiagID::TupleIndexRequiresIntegerLiteral:
    case DiagID::TupleIndexOutOfBounds:
    case DiagID::RecursiveTypeAliasCycle:
    case DiagID::ConstantExpressionNotAllowed:
      return argumentKinds(arguments);
    default:
      return false;
  }
}

bool validDisplayArgument(const CheckerDisplayArgument& argument, identity::ModuleId module,
                          const CheckedFactsVerificationInput& input) {
  const auto& value = argument.variant();
  if (value.is<TypeDisplayArg>()) {
    return validType(input.semanticTypes, value.get<TypeDisplayArg>().type);
  }
  if (value.is<PrimitiveTypeDisplayArg>()) {
    const auto kind = value.get<PrimitiveTypeDisplayArg>().kind;
    return kind >= type::semantic::PrimitiveKind::I8 && kind <= type::semantic::PrimitiveKind::Null;
  }
  if (value.is<DefinitionDisplayArg>()) {
    return authorizedDefinition(value.get<DefinitionDisplayArg>().definition, module, input);
  }
  if (value.is<IdentifierDisplayArg>() || value.is<CountDisplayArg>()) return true;
  if (value.is<ConstraintContextDisplayArg>()) {
    const auto reason = static_cast<uint8_t>(value.get<ConstraintContextDisplayArg>().reason);
    return reason >= 0x01 && reason <= 0x0c;
  }
  if (value.is<OperatorDisplayArg>()) {
    return checker::encodeOperatorKind(value.get<OperatorDisplayArg>().operation) != zc::none;
  }
  if (value.is<LiteralDisplayArg>()) {
    const auto& literal = value.get<LiteralDisplayArg>().literal;
    return literal.tag() <= signature::CanonicalConstValueTag::Unit &&
           validCanonicalValue(literal, module, input);
  }
  const auto& patterns = value.get<PatternsDisplayArg>().patterns;
  if (patterns.size() == 0) return false;
  for (const auto& pattern : patterns) {
    if (!validPatternConstructor(pattern, module, input)) return false;
  }
  return true;
}

bool hasRootPolicy(const CheckerFailureRef& failure, CheckerRecoveryClass recoveryClass,
                   bool suppressIfChildRecovery) {
  const auto& policy = failure.recoveryPolicy.variant();
  return policy.is<CreateRootRecoveryPolicy>() &&
         policy.get<CreateRootRecoveryPolicy>().recoveryClass == recoveryClass &&
         policy.get<CreateRootRecoveryPolicy>().suppressIfChildRecovery == suppressIfChildRecovery;
}

bool hasNoRecoveryPolicy(const CheckerFailureRef& failure) {
  return failure.recoveryPolicy.variant().is<NoRecoveryPolicy>();
}

bool validDiagnosticProduction(const CheckerFailureRef& failure) {
  using diagnostics::DiagID;
  using Class = CheckerRecoveryClass;
  using Producer = CheckerDiagnosticProducer;
  using Stage = CheckerDiagnosticStage;
  const auto diagnostic = failure.diagnostic.diagnosticId();
  const auto matches = [&](Stage stage, Producer producer, Class recoveryClass,
                           bool suppressIfChildRecovery) {
    return failure.stage == stage && failure.producer == producer &&
           hasRootPolicy(failure, recoveryClass, suppressIfChildRecovery);
  };
  switch (diagnostic) {
    case DiagID::DynGenericMethod:
    case DiagID::DynSelfReturn:
    case DiagID::DynMoveSelf:
    case DiagID::DynUnassociatedType:
    case DiagID::DynStaticMethod:
    case DiagID::DynGatNotAllowed:
    case DiagID::DynUnsizedParameter:
    case DiagID::DynSuperNotObjectSafe:
      return matches(Stage::Body, Producer::DynUse, Class::FailedObligation, true);
    case DiagID::TypeCheckerTypeMismatch:
    case DiagID::CannotUnifyTypes:
      return matches(Stage::Body, Producer::Inference, Class::TypeMismatch, true);
    case DiagID::InfiniteType:
    case DiagID::CannotInferTypeParameter:
    case DiagID::CannotInferNullInitializer:
      return matches(Stage::Body, Producer::Inference, Class::FailedInference, false);
    case DiagID::CannotCallNonFunction:
    case DiagID::ExplicitTypeArgumentsRequireGenericCallee:
      return matches(Stage::Body, Producer::Call, Class::InvalidOperation, true);
    case DiagID::CheckerInvalidCast:
    case DiagID::InvalidDynUpcast:
      return matches(Stage::Body, Producer::Cast, Class::InvalidOperation, true);
    case DiagID::ExplicitTypeArgumentCountMismatch:
    case DiagID::CallArgumentCountMismatch:
      return matches(Stage::Body, Producer::Call, Class::TypeMismatch, true);
    case DiagID::ConflictingImpl:
      return failure.stage == Stage::Coherence && failure.producer == Producer::Coherence &&
             hasNoRecoveryPolicy(failure);
    case DiagID::CheckerTraitNotImplemented:
      return matches(Stage::Body, Producer::Obligation, Class::FailedObligation, true);
    case DiagID::OperatorTraitSignatureMismatch:
      return matches(Stage::Body, Producer::Operator, Class::FailedObligation, true);
    case DiagID::NoAssociatedTypeProjection:
    case DiagID::AmbiguousAssociatedTypeProjection:
      return matches(Stage::Body, Producer::Projection, Class::FailedProjection, true);
    case DiagID::CheckerNonExhaustiveMatch:
      return failure.stage == Stage::Exhaustiveness &&
             failure.producer == Producer::Exhaustiveness && hasNoRecoveryPolicy(failure);
    case DiagID::CannotMutateImmutableVariable:
      return matches(Stage::Body, Producer::Mutation, Class::InvalidOperation, true);
    case DiagID::ErrorPropagateOutsideRaises:
    case DiagID::ErrorUnwrapNonUnion:
    case DiagID::ErrorPropagateNonUnion:
      return matches(Stage::Body, Producer::ErrorOperator, Class::InvalidOperation, true);
    case DiagID::ErrorUnionEmpty:
      return matches(Stage::Body, Producer::ErrorOperator, Class::InvalidOperation, false);
    case DiagID::InvalidBinaryOperands:
    case DiagID::InvalidComparisonOperands:
    case DiagID::PostfixUpdateRequiresNumeric:
      return matches(Stage::Body, Producer::Operator, Class::InvalidOperation, true);
    case DiagID::CannotDereferenceType:
      return matches(Stage::Body, Producer::Dereference, Class::InvalidOperation, true);
    case DiagID::MemberNotFound:
      return matches(Stage::Body, Producer::Call, Class::InvalidOperation, true);
    case DiagID::IndexRequiresInteger:
    case DiagID::TupleIndexRequiresIntegerLiteral:
    case DiagID::TupleIndexOutOfBounds:
    case DiagID::CannotIndexType:
      return matches(Stage::Body, Producer::Index, Class::InvalidOperation, true);
    case DiagID::ConditionMustBeBool:
      return matches(Stage::Body, Producer::Condition, Class::TypeMismatch, true);
    case DiagID::MissingReturnValue:
      return matches(Stage::Body, Producer::Return, Class::TypeMismatch, true);
    case DiagID::AggregateLiteralTargetRequired:
    case DiagID::UnknownStructField:
    case DiagID::MissingStructField:
      return matches(Stage::Body, Producer::Aggregate, Class::InvalidOperation, true);
    case DiagID::ArrayElementTypeMismatch:
      return matches(Stage::Body, Producer::Aggregate, Class::TypeMismatch, true);
    case DiagID::MatchGuardMustBeBool:
      return matches(Stage::Exhaustiveness, Producer::Exhaustiveness, Class::TypeMismatch, true);
    case DiagID::RecursiveTypeAliasCycle:
      return matches(Stage::Signature, Producer::Alias, Class::InvalidTypeExpression, false);
    case DiagID::OrphanImpl:
      return failure.stage == Stage::Coherence && failure.producer == Producer::Orphan &&
             hasNoRecoveryPolicy(failure);
    case DiagID::DynDuplicateAssociatedTypeBinding:
      return matches(Stage::Body, Producer::DynUse, Class::InvalidTypeExpression, true);
    case DiagID::BodyLiteralOutOfRange:
      return matches(Stage::Body, Producer::Constant, Class::FailedInference, true);
    case DiagID::ConstantValueOutOfRange:
    case DiagID::ConstantDependencyCycle:
      return matches(Stage::ConstantEvaluation, Producer::Constant, Class::InvalidTypeExpression,
                     false);
    case DiagID::ConstantExpressionNotAllowed:
    case DiagID::ConstantArithmeticFailure:
      return matches(Stage::ConstantEvaluation, Producer::Constant, Class::InvalidOperation, false);
    default:
      return false;
  }
}

bool validSourceFailure(const CheckerFailureRef& failure, identity::ModuleId module,
                        const CheckedFactsVerificationInput& input,
                        const identity::ModuleKey& moduleKey) {
  if (!failure.primaryNode || failure.stage == CheckerDiagnosticStage::Advisory ||
      failure.emitterOrdinal.stageTag != static_cast<uint8_t>(failure.stage) ||
      !validModuleSpan(moduleKey, input.source, failure.primarySpan) ||
      !validArgumentSchema(failure) || !validDiagnosticProduction(failure)) {
    return false;
  }
  for (const auto& argument : failure.arguments) {
    if (!validDisplayArgument(argument, module, input)) return false;
  }
  for (const auto& note : failure.notes) {
    if (!validModuleSpan(moduleKey, input.source, note.span) ||
        diagnostics::getDiagnosticInfo(note.diagnostic.diagnosticId()).argCount !=
            note.arguments.size()) {
      return false;
    }
    ZC_IF_SOME(definition, note.causeDefinition) {
      if (!authorizedDefinition(definition, module, input)) return false;
    }
    for (const auto& argument : note.arguments) {
      if (!validDisplayArgument(argument, module, input)) return false;
    }
  }
  const auto& policy = failure.recoveryPolicy.variant();
  if (policy.is<AdvisoryAfterSuccessRecoveryPolicy>()) return false;
  if (policy.is<NoRecoveryPolicy>()) return failure.recovery == zc::none;
  const auto& root = policy.get<CreateRootRecoveryPolicy>();
  const auto recoveryClass = static_cast<uint8_t>(root.recoveryClass);
  return recoveryClass >= 0x01 && recoveryClass <= 0x06 && failure.recovery != zc::none;
}

bool validateTypedFacts(const CheckedFactsCandidate& candidate,
                        const CheckedFactsVerificationInput& input,
                        const identity::ModuleKey& moduleKey) {
  for (const auto& entry : candidate.literals.entries()) {
    if (!validCanonicalValue(entry.value.literal, candidate, input)) return false;
  }
  for (const auto& entry : candidate.constants.entries()) {
    if (!validCanonicalValue(entry.value.value, candidate, input)) return false;
  }
  for (const auto& entry : candidate.coercions.entries()) {
    if (!validCoercion(entry.value, candidate, input, moduleKey)) return false;
  }
  for (const auto& entry : candidate.places.entries()) {
    if (!validPlace(entry.value, candidate, input)) return false;
  }
  for (const auto& entry : candidate.calls.entries()) {
    if (!validModuleSpan(moduleKey, input.source, entry.value.sourceSpan) ||
        !validCallEnvelope(entry.value.invocation, candidate, input, moduleKey)) {
      return false;
    }
  }
  for (const auto& entry : candidate.compoundAssignments.entries()) {
    if (!entry.value.placeNode ||
        !validModuleSpan(moduleKey, input.source, entry.value.sourceSpan) ||
        !validCallEnvelope(entry.value.invocation, candidate, input, moduleKey)) {
      return false;
    }
    ZC_IF_SOME(adjustment, entry.value.writebackAdjustment) {
      if (!validCoercion(adjustment, candidate, input, moduleKey)) return false;
    }
  }
  for (const auto& entry : candidate.casts.entries()) {
    const auto& fact = entry.value;
    if (!validType(input.semanticTypes, fact.source) ||
        !validType(input.semanticTypes, fact.target) ||
        !validType(input.semanticTypes, fact.result) ||
        !validModuleSpan(moduleKey, input.source, fact.sourceSpan)) {
      return false;
    }
    ZC_IF_SOME(impl, fact.impl) {
      if (!authorizedImpl(impl, input)) return false;
    }
    ZC_IF_SOME(witnesses, fact.witnesses) {
      if (!candidate.witnessStore.contains(witnesses)) return false;
    }
    for (const auto interface : fact.dynPath) {
      if (!authorizedDefinition(interface, candidate, input)) return false;
    }
  }
  for (const auto& entry : candidate.members.entries()) {
    if (!validType(input.semanticTypes, entry.value.receiverType) ||
        !validType(input.semanticTypes, entry.value.memberType) ||
        !authorizedDefinition(entry.value.member, candidate, input)) {
      return false;
    }
  }
  for (const auto& entry : candidate.indexes.entries()) {
    const auto& fact = entry.value;
    if (!validType(input.semanticTypes, fact.collectionType) ||
        !validType(input.semanticTypes, fact.indexType) ||
        !validType(input.semanticTypes, fact.elementType) ||
        !validType(input.semanticTypes, fact.accessResultType) ||
        !containsNode(candidate.calls, entry.key)) {
      return false;
    }
  }
  for (const auto& entry : candidate.projections.entries()) {
    if (!validType(input.semanticTypes, entry.value.key.subject) ||
        !validInterface(entry.value.key.interface, candidate, input) ||
        !authorizedDefinition(entry.value.key.associated, candidate, input) ||
        !validType(input.semanticTypes, entry.value.result) ||
        !authorizedImpl(entry.value.impl, input) ||
        !candidate.witnessStore.contains(entry.value.witnesses)) {
      return false;
    }
  }
  for (const auto& entry : candidate.patterns.entries()) {
    if (!validType(input.semanticTypes, entry.value.scrutineeType) ||
        !validPatternConstructor(entry.value.constructor, candidate, input)) {
      return false;
    }
  }
  for (const auto& entry : candidate.exhaustiveness.entries()) {
    if (!validType(input.semanticTypes, entry.value.scrutineeType)) return false;
    for (const auto& constructor : entry.value.coveredConstructors) {
      if (!validPatternConstructor(constructor, candidate, input)) return false;
    }
    for (const auto& constructor : entry.value.missingConstructors) {
      if (!validPatternConstructor(constructor, candidate, input)) return false;
    }
  }
  for (const auto& entry : candidate.obligations.entries()) {
    if (!validType(input.semanticTypes, entry.value.subject) ||
        !validInterface(entry.value.interface, candidate, input)) {
      return false;
    }
    const auto& resolution = entry.value.resolution.variant();
    if (resolution.is<UniqueImplResolution>()) {
      const auto& unique = resolution.get<UniqueImplResolution>();
      if (!authorizedImpl(unique.impl, input) ||
          !candidate.substitutionStore.contains(unique.substitutions) ||
          !candidate.witnessStore.contains(unique.witnesses)) {
        return false;
      }
    }
    if (resolution.is<AmbiguousImplResolution>()) return false;
  }
  return true;
}

}  // namespace

CheckedFactsSourceRejectionVerificationResult CheckedFactsSourceRejectionVerifier::verify(
    CheckedFactsSourceRejected&& rejection, const CheckedFactsVerificationInput& input) {
  const auto rejectSourceInvariant = [&](signature::CheckerInvariantKind kind, uint32_t ordinal) {
    zc::Vector<signature::CheckerVerificationFailure> failures;
    failures.add(checkerInvariant(kind, input.module, ordinal));
    return CheckedFactsSourceRejectionVerificationResult(
        CheckedFactsInvariantRejected{zc::mv(failures)});
  };

  if (input.identities.module(input.module) == zc::none ||
      input.semanticContext != input.identities.semanticContext() ||
      input.semanticContext != input.semanticTypes.context() || rejection.failures.size() == 0) {
    return rejectSourceInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, 0);
  }
  auto moduleEntry = input.identities.module(input.module);
  if (moduleEntry == zc::none || input.identities.sourceFile(input.source) == zc::none) {
    return rejectSourceInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, 0);
  }
  ZC_IF_SOME(entry, moduleEntry) {
    if (!input.source.belongsTo(entry.key().crate())) {
      return rejectSourceInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, 0);
    }
  }

  for (size_t ledgerIndex = 0; ledgerIndex < rejection.recoveryLedgers.size(); ++ledgerIndex) {
    const auto& ledger = rejection.recoveryLedgers[ledgerIndex];
    if (ledger.semanticContext() != input.semanticContext || ledger.canonicalRecord().size() == 0 ||
        ledger.errorCount() == 0) {
      return rejectSourceInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                                   static_cast<uint32_t>(ledgerIndex));
    }
    for (size_t previous = 0; previous < ledgerIndex; ++previous) {
      if (rejection.recoveryLedgers[previous].issuer() == ledger.issuer()) {
        return rejectSourceInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                                     static_cast<uint32_t>(ledgerIndex));
      }
    }
  }

  for (size_t index = 0; index < rejection.failures.size(); ++index) {
    const auto& failure = rejection.failures[index];
    bool valid = false;
    ZC_IF_SOME(entry, moduleEntry) {
      valid = validSourceFailure(failure, input.module, input, entry.key());
    }
    if (!valid ||
        diagnostics::getDiagnosticInfo(failure.diagnostic.diagnosticId()).argCount !=
            failure.arguments.size() ||
        !registeredFailure(failure, input.registeredPrimaryFailures, input.module, input.identities,
                           input.semanticTypes) ||
        !recoveryIsOwned(failure, rejection.recoveryLedgers.asPtr())) {
      return rejectSourceInvariant(signature::CheckerInvariantKind::InvalidEmitterOrdinal,
                                   static_cast<uint32_t>(index));
    }
    if (index != 0) {
      const auto& previous = rejection.failures[index - 1].emitterOrdinal;
      const auto& current = failure.emitterOrdinal;
      const bool strictlyOrdered = previous.stageTag < current.stageTag ||
                                   (previous.stageTag == current.stageTag &&
                                    (previous.ownerSchemaPreorder < current.ownerSchemaPreorder ||
                                     (previous.ownerSchemaPreorder == current.ownerSchemaPreorder &&
                                      (previous.siteSchemaPreorder < current.siteSchemaPreorder ||
                                       (previous.siteSchemaPreorder == current.siteSchemaPreorder &&
                                        previous.itemOrdinal < current.itemOrdinal)))));
      if (!strictlyOrdered) {
        return rejectSourceInvariant(signature::CheckerInvariantKind::InvalidEmitterOrdinal,
                                     static_cast<uint32_t>(index));
      }
    }
  }

  for (size_t ledgerIndex = 0; ledgerIndex < rejection.recoveryLedgers.size(); ++ledgerIndex) {
    const auto& ledger = rejection.recoveryLedgers[ledgerIndex];
    for (uint32_t slot = 0; slot < ledger.errorCount(); ++slot) {
      auto id = ledger.idAt(slot);
      if (id == zc::none) {
        return rejectSourceInvariant(signature::CheckerInvariantKind::InferenceLifecycle, slot);
      }
      uint32_t owners = 0;
      ZC_IF_SOME(value, id) {
        for (const auto& failure : rejection.failures) {
          ZC_IF_SOME(recovery, failure.recovery) {
            if (recovery == value) ++owners;
          }
        }
      }
      if (owners != 1) {
        return rejectSourceInvariant(signature::CheckerInvariantKind::InferenceLifecycle, slot);
      }
    }
  }
  return zc::mv(rejection);
}

CheckedFactsVerificationResult CheckedFactsVerifier::verify(
    CheckedFactsCandidate&& candidate, const CheckedFactsVerificationInput& input) {
  if (input.identities.module(candidate.module) == zc::none) {
    return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                   candidate.module, 0));
  }
  if (candidate.semanticContext != input.semanticContext ||
      candidate.semanticContext != input.identities.semanticContext() ||
      candidate.semanticContext != input.semanticTypes.context() ||
      candidate.module != input.module ||
      candidate.contextFingerprint.digest() != input.contextFingerprint.digest() ||
      candidate.sourceContentDigest != input.sourceContentDigest ||
      candidate.parsedModuleReceipt.digest() != input.parsedModuleReceipt.digest() ||
      candidate.signatureFactsRevision.digest() != input.signatureFactsRevision.digest() ||
      candidate.importedSignatureViewRevision.digest() !=
          input.importedSignatureViewRevision.digest() ||
      candidate.coherenceViewRevision.digest() != input.coherenceViewRevision.digest() ||
      !sameSemanticOptions(candidate.semanticOptions, input.semanticOptions) ||
      candidate.substitutionStore.semanticContext() != candidate.semanticContext ||
      candidate.witnessStore.semanticContext() != candidate.semanticContext) {
    return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                   candidate.module, 0));
  }
  auto verifiedModule = input.identities.module(candidate.module);
  if (verifiedModule == zc::none || input.identities.sourceFile(input.source) == zc::none) {
    return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                   candidate.module, 0));
  }
  ZC_IF_SOME(entry, verifiedModule) {
    if (!input.source.belongsTo(entry.key().crate())) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                     candidate.module, 0));
    }
  }
#define ZOM_VALIDATE_RECORDS(name)                                                          \
  if (!recordsAreCanonical(candidate.name)) {                                               \
    return reject(checkerInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch, \
                                   candidate.module, 0));                                   \
  }
  ZOM_VALIDATE_RECORDS(nodeTypes)
  ZOM_VALIDATE_RECORDS(definitionTypes)
  ZOM_VALIDATE_RECORDS(literals)
  ZOM_VALIDATE_RECORDS(constants)
  ZOM_VALIDATE_RECORDS(aggregates)
  ZOM_VALIDATE_RECORDS(places)
  ZOM_VALIDATE_RECORDS(coercions)
  ZOM_VALIDATE_RECORDS(casts)
  ZOM_VALIDATE_RECORDS(calls)
  ZOM_VALIDATE_RECORDS(compoundAssignments)
  ZOM_VALIDATE_RECORDS(members)
  ZOM_VALIDATE_RECORDS(indexes)
  ZOM_VALIDATE_RECORDS(patterns)
  ZOM_VALIDATE_RECORDS(observedOperations)
  ZOM_VALIDATE_RECORDS(captures)
  ZOM_VALIDATE_RECORDS(markerObligations)
  ZOM_VALIDATE_RECORDS(exhaustiveness)
  ZOM_VALIDATE_RECORDS(unsafeOperations)
  ZOM_VALIDATE_RECORDS(projections)
  ZOM_VALIDATE_RECORDS(obligations)
  ZOM_VALIDATE_RECORDS(errorUnionShapes)
  ZOM_VALIDATE_RECORDS(errorOperators)
#undef ZOM_VALIDATE_RECORDS
  if (!storeRecordsAreCanonical(candidate.substitutionStore.records()) ||
      !storeRecordsAreCanonical(candidate.witnessStore.records())) {
    return reject(checkerInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                   candidate.module, 0));
  }

  for (size_t index = 0; index < input.nodeRequirements.size(); ++index) {
    const auto& requirement = input.nodeRequirements[index];
    if (!requirement.node || requirement.key.schemaPreorder == UINT32_MAX ||
        !hasNodeFact(candidate, requirement.group, requirement.node)) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::MissingRequiredFact,
                                     candidate.module, static_cast<uint32_t>(index)));
    }
    ZC_IF_SOME(moduleEntry, verifiedModule) {
      if (!validModuleSpan(moduleEntry.key(), input.source, requirement.key.sourceSpan)) {
        return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                       candidate.module, static_cast<uint32_t>(index)));
      }
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (input.nodeRequirements[previous].group == requirement.group &&
          input.nodeRequirements[previous].node == requirement.node) {
        return reject(checkerInvariant(signature::CheckerInvariantKind::AdditionalFact,
                                       candidate.module, static_cast<uint32_t>(index)));
      }
    }
  }
  for (size_t index = 0; index < input.definitionRequirements.size(); ++index) {
    const auto& requirement = input.definitionRequirements[index];
    if (!hasDefinitionFact(candidate, requirement.group, requirement.definition)) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::MissingRequiredFact,
                                     candidate.module, static_cast<uint32_t>(index)));
    }
  }
  for (size_t index = 0; index < input.captureRequirements.size(); ++index) {
    if (!containsCapture(candidate.captures, input.captureRequirements[index].key)) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::MissingRequiredFact,
                                     candidate.module, static_cast<uint32_t>(index)));
    }
  }

#define ZOM_REJECT_ADDITIONAL_NODE(name, group)                                                    \
  for (const auto& entry : candidate.name.entries()) {                                             \
    if (!hasNodeRequirement(input.nodeRequirements, CheckedFactGroup::group, entry.key)) {         \
      return reject(                                                                               \
          checkerInvariant(signature::CheckerInvariantKind::AdditionalFact, candidate.module, 0)); \
    }                                                                                              \
  }
  ZOM_REJECT_ADDITIONAL_NODE(nodeTypes, NodeType)
  ZOM_REJECT_ADDITIONAL_NODE(literals, Literal)
  ZOM_REJECT_ADDITIONAL_NODE(aggregates, Aggregate)
  ZOM_REJECT_ADDITIONAL_NODE(places, Place)
  ZOM_REJECT_ADDITIONAL_NODE(coercions, Coercion)
  ZOM_REJECT_ADDITIONAL_NODE(casts, Cast)
  ZOM_REJECT_ADDITIONAL_NODE(calls, Call)
  ZOM_REJECT_ADDITIONAL_NODE(compoundAssignments, CompoundAssignment)
  ZOM_REJECT_ADDITIONAL_NODE(members, Member)
  ZOM_REJECT_ADDITIONAL_NODE(indexes, Index)
  ZOM_REJECT_ADDITIONAL_NODE(patterns, Pattern)
  ZOM_REJECT_ADDITIONAL_NODE(observedOperations, ObservedOperation)
  ZOM_REJECT_ADDITIONAL_NODE(markerObligations, MarkerObligation)
  ZOM_REJECT_ADDITIONAL_NODE(exhaustiveness, Exhaustiveness)
  ZOM_REJECT_ADDITIONAL_NODE(unsafeOperations, UnsafeOperation)
  ZOM_REJECT_ADDITIONAL_NODE(projections, Projection)
  ZOM_REJECT_ADDITIONAL_NODE(obligations, Obligation)
  ZOM_REJECT_ADDITIONAL_NODE(errorUnionShapes, ErrorUnionShape)
  ZOM_REJECT_ADDITIONAL_NODE(errorOperators, ErrorOperator)
#undef ZOM_REJECT_ADDITIONAL_NODE

  for (const auto& entry : candidate.definitionTypes.entries()) {
    if (!hasDefinitionRequirement(input.definitionRequirements, CheckedFactGroup::DefinitionType,
                                  entry.key)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::AdditionalFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.constants.entries()) {
    if (!hasDefinitionRequirement(input.definitionRequirements, CheckedFactGroup::Constant,
                                  entry.key)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::AdditionalFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.captures.entries()) {
    bool required = false;
    for (const auto& requirement : input.captureRequirements) {
      if (requirement.key == entry.key) required = true;
    }
    if (!required) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::AdditionalFact, candidate.module, 0));
    }
  }

  if (!CheckedFactsCanonicalCodec::recordsMatch(candidate, input)) {
    return reject(checkerInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                   candidate.module, 0));
  }

  if (!validateEmbeddedKeys(candidate) || !validateCrossFactRules(candidate) ||
      !validateStoreContents(candidate, input)) {
    return reject(
        checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
  }
  ZC_IF_SOME(moduleEntry, verifiedModule) {
    if (!validateTypedFacts(candidate, input, moduleEntry.key())) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.nodeTypes.entries()) {
    if (!entry.key || !validType(input.semanticTypes, entry.value)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.definitionTypes.entries()) {
    if (!validDefinition(input.identities, entry.key) ||
        !validType(input.semanticTypes, entry.value)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.literals.entries()) {
    if (entry.value.literal.tag() > signature::CanonicalConstValueTag::Unit ||
        !validType(input.semanticTypes, entry.value.type)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }
  }
  for (const auto& entry : candidate.constants.entries()) {
    if (entry.key != entry.value.definition || !validDefinition(input.identities, entry.key) ||
        !validType(input.semanticTypes, entry.value.type) ||
        !recordsAreCanonical(entry.value.dependencies)) {
      return reject(
          checkerInvariant(signature::CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }
  }

  for (const auto& ledger : candidate.recoveryLedgers) {
    if (ledger.semanticContext() != candidate.semanticContext ||
        ledger.canonicalRecord().size() == 0) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                                     candidate.module, 0));
    }
  }
  for (const auto& failure : candidate.sourceFailures) {
    bool sourceFailureIsValid = false;
    ZC_IF_SOME(moduleEntry, verifiedModule) {
      sourceFailureIsValid =
          validSourceFailure(failure, candidate.module, input, moduleEntry.key());
    }
    if (diagnostics::getDiagnosticInfo(failure.diagnostic.diagnosticId()).argCount !=
            failure.arguments.size() ||
        !registeredFailure(failure, input.registeredPrimaryFailures, candidate.module,
                           input.identities, input.semanticTypes) ||
        !recoveryIsOwned(failure, candidate.recoveryLedgers.asPtr()) || !sourceFailureIsValid) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::InvalidEmitterOrdinal,
                                     candidate.module, 0));
    }
    ZC_IF_SOME(moduleEntry, verifiedModule) {
      if (!validModuleSpan(moduleEntry.key(), input.source, failure.primarySpan)) {
        return reject(checkerInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                       candidate.module, 0));
      }
    }
  }
  if (candidate.sourceFailures.size() != 0) {
    return CheckedFactsSourceRejected{zc::mv(candidate.sourceFailures),
                                      zc::mv(candidate.advisories),
                                      zc::mv(candidate.recoveryLedgers)};
  }
  if (candidate.recoveryLedgers.size() != 0) {
    return reject(
        checkerInvariant(signature::CheckerInvariantKind::InferenceLifecycle, candidate.module, 0));
  }

  zc::Vector<zc::ArrayPtr<const uint8_t>> substitutions =
      storeRecordViews(candidate.substitutionStore.records());
  zc::Vector<zc::ArrayPtr<const uint8_t>> witnesses =
      storeRecordViews(candidate.witnessStore.records());
#define ZOM_RECORD_VIEWS(name) auto name##Records = recordViews(candidate.name)
  ZOM_RECORD_VIEWS(nodeTypes);
  ZOM_RECORD_VIEWS(definitionTypes);
  ZOM_RECORD_VIEWS(literals);
  ZOM_RECORD_VIEWS(constants);
  ZOM_RECORD_VIEWS(aggregates);
  ZOM_RECORD_VIEWS(places);
  ZOM_RECORD_VIEWS(coercions);
  ZOM_RECORD_VIEWS(casts);
  ZOM_RECORD_VIEWS(calls);
  ZOM_RECORD_VIEWS(compoundAssignments);
  ZOM_RECORD_VIEWS(members);
  ZOM_RECORD_VIEWS(indexes);
  ZOM_RECORD_VIEWS(patterns);
  ZOM_RECORD_VIEWS(observedOperations);
  ZOM_RECORD_VIEWS(captures);
  ZOM_RECORD_VIEWS(markerObligations);
  ZOM_RECORD_VIEWS(exhaustiveness);
  ZOM_RECORD_VIEWS(unsafeOperations);
  ZOM_RECORD_VIEWS(projections);
  ZOM_RECORD_VIEWS(obligations);
  ZOM_RECORD_VIEWS(errorUnionShapes);
  ZOM_RECORD_VIEWS(errorOperators);
#undef ZOM_RECORD_VIEWS

  const CheckedFactsCanonicalGroups groups{substitutions.asPtr(),
                                           witnesses.asPtr(),
                                           nodeTypesRecords.asPtr(),
                                           definitionTypesRecords.asPtr(),
                                           literalsRecords.asPtr(),
                                           constantsRecords.asPtr(),
                                           aggregatesRecords.asPtr(),
                                           placesRecords.asPtr(),
                                           coercionsRecords.asPtr(),
                                           castsRecords.asPtr(),
                                           callsRecords.asPtr(),
                                           compoundAssignmentsRecords.asPtr(),
                                           membersRecords.asPtr(),
                                           indexesRecords.asPtr(),
                                           patternsRecords.asPtr(),
                                           observedOperationsRecords.asPtr(),
                                           capturesRecords.asPtr(),
                                           markerObligationsRecords.asPtr(),
                                           exhaustivenessRecords.asPtr(),
                                           unsafeOperationsRecords.asPtr(),
                                           projectionsRecords.asPtr(),
                                           obligationsRecords.asPtr(),
                                           errorUnionShapesRecords.asPtr(),
                                           errorOperatorsRecords.asPtr()};
  ZC_IF_SOME(entry, verifiedModule) {
    auto moduleBytes = entry.key().encode();
    auto revision = CheckedFactsRevision::computeFramed(
        candidate.contextFingerprint.digest(), moduleBytes.asPtr(), candidate.sourceContentDigest,
        candidate.parsedModuleReceipt.digest(), candidate.signatureFactsRevision.digest(),
        candidate.importedSignatureViewRevision.digest(), candidate.coherenceViewRevision.digest(),
        candidate.semanticOptions, groups);
    if (revision == zc::none) {
      return reject(checkerInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                     candidate.module, 0));
    }
    ZC_IF_SOME(value, revision) {
      return VerifiedCheckedFacts(
          zc::heap<VerifiedCheckedFacts::Impl>(zc::mv(candidate), zc::mv(value)));
    }
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::checked
