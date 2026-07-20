// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/dispatch-facts.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::checker::dispatch {
namespace {

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    output.add(static_cast<uint8_t>((value >> (56U - index * 8U)) & 0xffU));
  }
}

void appendByteString(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  appendUint64(output, bytes.size());
  append(output, bytes);
}

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  return left.size() == right.size() && !lessBytes(left, right) && !lessBytes(right, left);
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

bool sameNode(const checked::CheckedNodeKey& left, const checked::CheckedNodeKey& right) {
  return left.syntaxKind == right.syntaxKind && left.schemaPreorder == right.schemaPreorder &&
         sameSpan(left.sourceSpan, right.sourceSpan);
}

checked::CheckedNodeKey cloneNode(const checked::CheckedNodeKey& value) {
  return checked::CheckedNodeKey{value.syntaxKind, value.schemaPreorder, value.sourceSpan.clone()};
}

zc::Maybe<PrimitiveOperation> cloneOperation(const zc::Maybe<PrimitiveOperation>& operation) {
  ZC_IF_SOME(value, operation) { return value; }
  return zc::none;
}

zc::Maybe<CompoundAssignmentOperation> cloneCompoundOperation(
    const zc::Maybe<CompoundAssignmentOperation>& operation) {
  ZC_IF_SOME(value, operation) { return value; }
  return zc::none;
}

checked::CoercionStep cloneCoercionStep(const checked::CoercionStep& step) {
  const auto& value = step.variant();
  if (value.is<checked::NeverToStep>()) return checked::CoercionStep(checked::NeverToStep{});
  if (value.is<checked::ToAnyStep>()) return checked::CoercionStep(checked::ToAnyStep{});
  if (value.is<checked::ReborrowSharedStep>()) {
    return checked::CoercionStep(checked::ReborrowSharedStep{});
  }
  if (value.is<checked::ReferenceToRawConstStep>()) {
    return checked::CoercionStep(checked::ReferenceToRawConstStep{});
  }
  if (value.is<checked::ReferenceToRawMutableStep>()) {
    return checked::CoercionStep(checked::ReferenceToRawMutableStep{});
  }
  if (value.is<checked::RawMutToConstStep>()) {
    return checked::CoercionStep(checked::RawMutToConstStep{});
  }
  if (value.is<checked::UnionInjectStep>()) {
    return checked::CoercionStep(value.get<checked::UnionInjectStep>());
  }
  if (value.is<checked::DynEraseStep>()) {
    const auto& erase = value.get<checked::DynEraseStep>();
    zc::Vector<identity::SemanticTypeId> arguments(erase.interface.arguments.size());
    for (const auto argument : erase.interface.arguments) arguments.add(argument);
    return checked::CoercionStep(checked::DynEraseStep{
        checked::InterfaceInstantiation{erase.interface.interface, zc::mv(arguments)}, erase.impl,
        erase.witnesses});
  }
  const auto& upcast = value.get<checked::DynUpcastStep>();
  zc::Vector<identity::DefId> path(upcast.path.size());
  for (const auto definition : upcast.path) path.add(definition);
  return checked::CoercionStep(checked::DynUpcastStep{zc::mv(path)});
}

checked::CoercionAdjustment cloneCoercion(const checked::CoercionAdjustment& adjustment) {
  zc::Vector<checked::CoercionStep> steps(adjustment.steps.size());
  for (const auto& step : adjustment.steps) steps.add(cloneCoercionStep(step));
  return checked::CoercionAdjustment{adjustment.site, adjustment.source, adjustment.destination,
                                     zc::mv(steps), adjustment.sourceSpan.clone()};
}

zc::Maybe<checked::CoercionAdjustment> cloneCoercion(
    const zc::Maybe<checked::CoercionAdjustment>& adjustment) {
  ZC_IF_SOME(value, adjustment) { return cloneCoercion(value); }
  return zc::none;
}

zc::Maybe<identity::DefId> cloneOwner(const zc::Maybe<identity::DefId>& owner) {
  ZC_IF_SOME(value, owner) { return value; }
  return zc::none;
}

zc::Maybe<checked::CheckedNodeKey> maybeNode(const checked::CheckedNodeKey& node) {
  return cloneNode(node);
}

zc::Maybe<identity::SourceSpan> maybeSpan(const identity::SourceSpan& span) { return span.clone(); }

identity::IdentityInvariantKind identityKind(identity::FrozenRegistryFailure failure) {
  switch (failure) {
    case identity::FrozenRegistryFailure::InvalidContext:
    case identity::FrozenRegistryFailure::InvalidHandle:
      return identity::IdentityInvariantKind::InvalidHandle;
    case identity::FrozenRegistryFailure::ForeignContext:
      return identity::IdentityInvariantKind::ForeignContext;
    case identity::FrozenRegistryFailure::SlotOutOfRange:
      return identity::IdentityInvariantKind::SlotOutOfRange;
    case identity::FrozenRegistryFailure::DuplicateCanonicalKey:
      return identity::IdentityInvariantKind::DuplicateCanonicalKey;
    case identity::FrozenRegistryFailure::DigestCollision:
      return identity::IdentityInvariantKind::DigestCollision;
    case identity::FrozenRegistryFailure::InvalidAuthority:
      return identity::IdentityInvariantKind::InvalidClosedValue;
    case identity::FrozenRegistryFailure::UnknownOwner:
    case identity::FrozenRegistryFailure::OwnerModuleMismatch:
    case identity::FrozenRegistryFailure::OwnerPrefixMismatch:
    case identity::FrozenRegistryFailure::RepeatedOwner:
    case identity::FrozenRegistryFailure::SelfOwner:
      return identity::IdentityInvariantKind::AncestorMismatch;
    case identity::FrozenRegistryFailure::PostFreezeMutation:
      return identity::IdentityInvariantKind::PostFreezeMutation;
    case identity::FrozenRegistryFailure::AncestorMismatch:
    case identity::FrozenRegistryFailure::RegistryNotFrozen:
      return identity::IdentityInvariantKind::AncestorMismatch;
    case identity::FrozenRegistryFailure::None:
      break;
  }
  ZC_UNREACHABLE
}

identity::IdentityInvariant registryInvariant(identity::FrozenRegistryFailure failure,
                                              identity::IdentityAllocationPhase phase,
                                              uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto result = identity::IdentityInvariant::from(identityKind(failure), phase,
                                                  zc::mv(noStructural), zc::mv(noRange),
                                                  identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

DispatchFactsInvariantRejected rejected(identity::IdentityInvariant&& invariant) {
  zc::Vector<DispatchVerificationFailure> failures;
  failures.add(DispatchVerificationFailure(zc::mv(invariant)));
  return DispatchFactsInvariantRejected{zc::mv(failures)};
}

DispatchFactsInvariantRejected rejected(DispatchInvariantKind kind, DispatchInvariantStage stage,
                                        identity::ModuleId module, uint32_t ordinal,
                                        zc::Maybe<identity::DefId>&& owner = zc::none,
                                        zc::Maybe<checked::CheckedNodeKey>&& node = zc::none,
                                        zc::Maybe<identity::SourceSpan>&& span = zc::none,
                                        zc::Vector<uint32_t>&& path = zc::Vector<uint32_t>(),
                                        zc::Maybe<identity::Sha256Digest>&& expected = zc::none,
                                        zc::Maybe<identity::Sha256Digest>&& actual = zc::none) {
  zc::Vector<DispatchVerificationFailure> failures;
  failures.add(DispatchVerificationFailure(
      DispatchInvariantFact{kind, stage, module, zc::mv(owner), zc::mv(node), zc::mv(span),
                            zc::mv(path), zc::mv(expected), zc::mv(actual), ordinal}));
  return DispatchFactsInvariantRejected{zc::mv(failures)};
}

DispatchVerificationResult reject(identity::IdentityInvariant&& invariant) {
  return rejected(zc::mv(invariant));
}

DispatchVerificationResult reject(DispatchInvariantKind kind, DispatchInvariantStage stage,
                                  identity::ModuleId module, uint32_t ordinal,
                                  zc::Maybe<identity::DefId>&& owner = zc::none,
                                  zc::Maybe<checked::CheckedNodeKey>&& node = zc::none,
                                  zc::Maybe<identity::SourceSpan>&& span = zc::none,
                                  zc::Vector<uint32_t>&& path = zc::Vector<uint32_t>(),
                                  zc::Maybe<identity::Sha256Digest>&& expected = zc::none,
                                  zc::Maybe<identity::Sha256Digest>&& actual = zc::none) {
  return rejected(kind, stage, module, ordinal, zc::mv(owner), zc::mv(node), zc::mv(span),
                  zc::mv(path), zc::mv(expected), zc::mv(actual));
}

void encodeRaw(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  for (const auto byte : bytes) encoder.encodeUint8(byte);
}

void encodeNode(identity::CanonicalEncoder& encoder, const checked::CheckedNodeKey& node) {
  encoder.encodeUint32(node.syntaxKind);
  encoder.encodeUint32(node.schemaPreorder);
  node.sourceSpan.encode(encoder);
}

class FactEncoder final {
public:
  FactEncoder(const identity::SemanticIdentityRegistrySet& registries,
              const type::SemanticTypeStore& semanticTypes,
              const checked::VerifiedCheckedFacts& checkedFacts,
              const identity::SourceFileKey& source)
      : registries(registries),
        semanticTypes(semanticTypes),
        checkedFacts(checkedFacts),
        source(source) {}

  zc::Maybe<zc::Array<uint8_t>> encode(const checked::CheckedNodeKey& checkedNode,
                                       const DispatchFact& fact) {
    if (!checkedNode.sourceSpan.belongsTo(source) || !fact.sourceSpan.belongsTo(source) ||
        !sameSpan(checkedNode.sourceSpan, fact.sourceSpan)) {
      return zc::none;
    }
    identity::CanonicalEncoder encoder;
    encodeNode(encoder, checkedNode);
    if (!encodeTarget(encoder, fact.target) ||
        !encodeResultTransform(encoder, fact.resultTransform) ||
        !encodeReceiver(encoder, fact.receiver)) {
      return zc::none;
    }
    encoder.encodeSequenceSize(fact.arguments.size());
    for (const auto& argument : fact.arguments) {
      if (!encodeArgument(encoder, argument)) return zc::none;
    }
    if (!encodeType(encoder, fact.successType) || !encodeType(encoder, fact.resultType) ||
        !encodeSubstitution(encoder, fact.substitutions) ||
        !encodeWitnesses(encoder, fact.witnesses) || !encodeOptionalType(encoder, fact.raises)) {
      return zc::none;
    }
    fact.sourceSpan.encode(encoder);
    return encoder.finish();
  }

  bool encodeCoercionRecord(identity::CanonicalEncoder& encoder,
                            const checked::CoercionAdjustment& adjustment) {
    if (adjustment.site < checked::CoercionSite::AnnotatedInitializer ||
        adjustment.site > checked::CoercionSite::ExplicitDynAnnotation ||
        !adjustment.sourceSpan.belongsTo(source)) {
      return false;
    }
    encoder.encodeUint8(static_cast<uint8_t>(adjustment.site));
    if (!encodeType(encoder, adjustment.source) || !encodeType(encoder, adjustment.destination)) {
      return false;
    }
    encoder.encodeSequenceSize(adjustment.steps.size());
    for (const auto& step : adjustment.steps) {
      const auto& value = step.variant();
      if (value.is<checked::NeverToStep>()) {
        encoder.encodeUint8(0x01);
      } else if (value.is<checked::ToAnyStep>()) {
        encoder.encodeUint8(0x02);
      } else if (value.is<checked::ReborrowSharedStep>()) {
        encoder.encodeUint8(0x03);
      } else if (value.is<checked::ReferenceToRawConstStep>()) {
        encoder.encodeUint8(0x04);
      } else if (value.is<checked::ReferenceToRawMutableStep>()) {
        encoder.encodeUint8(0x05);
      } else if (value.is<checked::RawMutToConstStep>()) {
        encoder.encodeUint8(0x06);
      } else if (value.is<checked::UnionInjectStep>()) {
        const auto& inject = value.get<checked::UnionInjectStep>();
        encoder.encodeUint8(0x07);
        encoder.encodeUint32(inject.alternativeIndex);
        if (!encodeType(encoder, inject.alternative)) return false;
      } else if (value.is<checked::DynEraseStep>()) {
        const auto& erase = value.get<checked::DynEraseStep>();
        encoder.encodeUint8(0x08);
        if (!encodeInterface(encoder, erase.interface) || !encodeImpl(encoder, erase.impl) ||
            !encodeWitnessRecord(encoder, erase.witnesses)) {
          return false;
        }
      } else {
        const auto& upcast = value.get<checked::DynUpcastStep>();
        encoder.encodeUint8(0x09);
        encoder.encodeSequenceSize(upcast.path.size());
        for (const auto definition : upcast.path) {
          if (!encodeDefinition(encoder, definition)) return false;
        }
      }
    }
    adjustment.sourceSpan.encode(encoder);
    return true;
  }

  bool encodeReceiverAdjustmentRecord(identity::CanonicalEncoder& encoder,
                                      const checked::ReceiverAdjustment& adjustment) {
    if (!adjustment.sourceSpan.belongsTo(source) || !encodeType(encoder, adjustment.source) ||
        !encodeType(encoder, adjustment.destination)) {
      return false;
    }
    encoder.encodeSequenceSize(adjustment.steps.size());
    for (const auto step : adjustment.steps) {
      if (step < checked::ReceiverAdjustmentStep::DereferenceShared ||
          step > checked::ReceiverAdjustmentStep::CopyValue) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(step));
    }
    adjustment.sourceSpan.encode(encoder);
    return true;
  }

private:
  bool encodeDefinition(identity::CanonicalEncoder& encoder, identity::DefId definition) {
    if (registries.definitions().validate(definition) != identity::FrozenRegistryFailure::None) {
      return false;
    }
    ZC_IF_SOME(key, registries.definitions().lookup(definition)) {
      key.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeImpl(identity::CanonicalEncoder& encoder, identity::ImplId implementation) {
    if (registries.impls().validate(implementation) != identity::FrozenRegistryFailure::None) {
      return false;
    }
    ZC_IF_SOME(key, registries.impls().lookup(implementation)) {
      key.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId semanticType) {
    auto lookup = semanticTypes.get(semanticType);
    if (!lookup.is<type::SemanticTypeLookup>()) return false;
    const auto bytes = lookup.get<type::SemanticTypeLookup>().key().bytes();
    constexpr zc::StringPtr domain = "zom.semantic-type-key.v1\0"_zcc;
    if (bytes.size() <= domain.size()) return false;
    for (size_t index = 0; index < domain.size(); ++index) {
      if (bytes[index] != static_cast<uint8_t>(domain[index])) return false;
    }
    encodeRaw(encoder, bytes.slice(domain.size(), bytes.size()));
    return true;
  }

  bool encodeInterface(identity::CanonicalEncoder& encoder,
                       const checked::InterfaceInstantiation& interface) {
    if (!encodeDefinition(encoder, interface.interface)) return false;
    encoder.encodeSequenceSize(interface.arguments.size());
    for (const auto argument : interface.arguments) {
      if (!encodeType(encoder, argument)) return false;
    }
    return true;
  }

  bool encodeStoreRecord(identity::CanonicalEncoder& encoder, checked::CanonicalSubstitutionId id) {
    const auto records = checkedFacts.substitutionStore().records();
    for (uint32_t index = 0; index < records.size(); ++index) {
      ZC_IF_SOME(candidate, checkedFacts.substitutionStore().idAt(index)) {
        if (candidate == id) {
          if (records[index].canonicalRecord.size() == 0) return false;
          encodeRaw(encoder, records[index].canonicalRecord.asPtr());
          return true;
        }
      }
    }
    return false;
  }

  bool encodeWitnessRecord(identity::CanonicalEncoder& encoder, checked::WitnessArgumentsId id) {
    const auto records = checkedFacts.witnessStore().records();
    for (uint32_t index = 0; index < records.size(); ++index) {
      ZC_IF_SOME(candidate, checkedFacts.witnessStore().idAt(index)) {
        if (candidate == id) {
          if (records[index].canonicalRecord.size() == 0) return false;
          encodeRaw(encoder, records[index].canonicalRecord.asPtr());
          return true;
        }
      }
    }
    return false;
  }

  bool encodeTarget(identity::CanonicalEncoder& encoder, const DispatchTarget& target) {
    const auto& value = target.variant();
    if (value.is<DirectTarget>()) {
      encoder.encodeUint8(0x01);
      return encodeDefinition(encoder, value.get<DirectTarget>().callee);
    }
    if (value.is<ConcreteMethodTarget>()) {
      encoder.encodeUint8(0x02);
      return encodeDefinition(encoder, value.get<ConcreteMethodTarget>().method);
    }
    if (value.is<ImplMethodTarget>()) {
      const auto& method = value.get<ImplMethodTarget>();
      encoder.encodeUint8(0x03);
      return encodeImpl(encoder, method.impl) && encodeDefinition(encoder, method.method);
    }
    if (value.is<WitnessMethodTarget>()) {
      const auto& method = value.get<WitnessMethodTarget>();
      encoder.encodeUint8(0x04);
      return encodeDefinition(encoder, method.witnessParameter) &&
             encodeDefinition(encoder, method.interface) &&
             encodeDefinition(encoder, method.method);
    }
    if (value.is<DynMethodTarget>()) {
      const auto& method = value.get<DynMethodTarget>();
      encoder.encodeUint8(0x05);
      return encodeDefinition(encoder, method.interface) &&
             encodeDefinition(encoder, method.method);
    }
    const auto operation = value.get<PrimitiveTarget>().operation;
    if (operation < PrimitiveOperation::UnaryPlus || operation > PrimitiveOperation::NullCoalesce) {
      return false;
    }
    encoder.encodeUint8(0x06);
    encoder.encodeUint8(static_cast<uint8_t>(operation));
    return true;
  }

  bool encodeResultTransform(identity::CanonicalEncoder& encoder,
                             const DispatchResultTransform& transform) {
    const auto& value = transform.variant();
    if (value.is<IdentityResultTransform>()) {
      encoder.encodeUint8(0x01);
      return true;
    }
    if (value.is<BooleanNotResultTransform>()) {
      encoder.encodeUint8(0x02);
      return true;
    }
    const auto relation = value.get<CompareOrderingResultTransform>().relation;
    if (relation < OrderingRelation::Less || relation > OrderingRelation::GreaterEqual) {
      return false;
    }
    encoder.encodeUint8(0x03);
    encoder.encodeUint8(static_cast<uint8_t>(relation));
    return true;
  }

  bool encodeArgument(identity::CanonicalEncoder& encoder, const DispatchArgumentPlan& argument) {
    if (!argument.sourceNode.sourceSpan.belongsTo(source)) return false;
    encodeNode(encoder, argument.sourceNode);
    if (!encodeType(encoder, argument.sourceType) || !encodeType(encoder, argument.parameterType)) {
      return false;
    }
    if (argument.adjustment == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(adjustment, argument.adjustment) {
      return encodeCoercionRecord(encoder, adjustment);
    }
    return false;
  }

  bool encodeReceiver(identity::CanonicalEncoder& encoder,
                      const zc::Maybe<DispatchReceiverPlan>& receiver) {
    if (receiver == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(value, receiver) {
      if (value.role < DispatchReceiverRole::ExplicitFirstArgument ||
          value.role > DispatchReceiverRole::IndexBase ||
          value.passing < checked::ReceiverMode::Static ||
          value.passing > checked::ReceiverMode::Move) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(value.role));
      encoder.encodeUint8(static_cast<uint8_t>(value.passing));
      return encodeArgument(encoder, value.value) &&
             encodeReceiverAdjustmentRecord(encoder, value.adjustment);
    }
    return false;
  }

  bool encodeSubstitution(identity::CanonicalEncoder& encoder,
                          const zc::Maybe<checked::CanonicalSubstitutionId>& substitution) {
    if (substitution == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(value, substitution) { return encodeStoreRecord(encoder, value); }
    return false;
  }

  bool encodeWitnesses(identity::CanonicalEncoder& encoder,
                       const zc::Maybe<checked::WitnessArgumentsId>& witnesses) {
    if (witnesses == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(value, witnesses) { return encodeWitnessRecord(encoder, value); }
    return false;
  }

  bool encodeOptionalType(identity::CanonicalEncoder& encoder,
                          const zc::Maybe<identity::SemanticTypeId>& type) {
    if (type == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(value, type) { return encodeType(encoder, value); }
    return false;
  }

  const identity::SemanticIdentityRegistrySet& registries;
  const type::SemanticTypeStore& semanticTypes;
  const checked::VerifiedCheckedFacts& checkedFacts;
  const identity::SourceFileKey& source;
};

bool validateDefinitionIdentity(identity::DefId definition,
                                const identity::SemanticIdentityRegistrySet& registries,
                                zc::Maybe<identity::IdentityInvariant>& failure, uint32_t ordinal) {
  const auto result = registries.definitions().validate(definition);
  if (result == identity::FrozenRegistryFailure::None) return true;
  failure = registryInvariant(result, identity::IdentityAllocationPhase::Definition, ordinal);
  return false;
}

bool validateImplIdentity(identity::ImplId implementation,
                          const identity::SemanticIdentityRegistrySet& registries,
                          zc::Maybe<identity::IdentityInvariant>& failure, uint32_t ordinal) {
  const auto result = registries.impls().validate(implementation);
  if (result == identity::FrozenRegistryFailure::None) return true;
  failure = registryInvariant(result, identity::IdentityAllocationPhase::Impl, ordinal);
  return false;
}

bool validateTypeIdentity(identity::SemanticTypeId semanticType,
                          const type::SemanticTypeStore& semanticTypes,
                          zc::Maybe<identity::IdentityInvariant>& failure) {
  auto result = semanticTypes.get(semanticType);
  if (result.is<type::SemanticTypeLookup>()) return true;
  failure = result.get<identity::IdentityInvariant>().clone();
  return false;
}

bool validateCoercionIdentities(const checked::CoercionAdjustment& adjustment,
                                const DispatchFactsVerificationInput& input,
                                zc::Maybe<identity::IdentityInvariant>& failure,
                                bool& storeMismatch, uint32_t ordinal) {
  if (!validateTypeIdentity(adjustment.source, input.semanticTypes, failure) ||
      !validateTypeIdentity(adjustment.destination, input.semanticTypes, failure)) {
    return false;
  }
  for (const auto& step : adjustment.steps) {
    const auto& value = step.variant();
    if (value.is<checked::UnionInjectStep>()) {
      if (!validateTypeIdentity(value.get<checked::UnionInjectStep>().alternative,
                                input.semanticTypes, failure)) {
        return false;
      }
    } else if (value.is<checked::DynEraseStep>()) {
      const auto& erase = value.get<checked::DynEraseStep>();
      if (!validateDefinitionIdentity(erase.interface.interface, input.registries, failure,
                                      ordinal) ||
          !validateImplIdentity(erase.impl, input.registries, failure, ordinal)) {
        return false;
      }
      for (const auto type : erase.interface.arguments) {
        if (!validateTypeIdentity(type, input.semanticTypes, failure)) return false;
      }
      if (!input.checkedFacts.witnessStore().contains(erase.witnesses)) {
        storeMismatch = true;
        return false;
      }
    } else if (value.is<checked::DynUpcastStep>()) {
      for (const auto definition : value.get<checked::DynUpcastStep>().path) {
        if (!validateDefinitionIdentity(definition, input.registries, failure, ordinal)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool validateArgumentIdentities(const DispatchArgumentPlan& argument,
                                const DispatchFactsVerificationInput& input,
                                zc::Maybe<identity::IdentityInvariant>& failure,
                                bool& storeMismatch, uint32_t ordinal) {
  if (!validateTypeIdentity(argument.sourceType, input.semanticTypes, failure) ||
      !validateTypeIdentity(argument.parameterType, input.semanticTypes, failure)) {
    return false;
  }
  ZC_IF_SOME(adjustment, argument.adjustment) {
    if (!validateCoercionIdentities(adjustment, input, failure, storeMismatch, ordinal)) {
      return false;
    }
  }
  return true;
}

bool validateFactIdentities(const DispatchFact& fact, const DispatchFactsVerificationInput& input,
                            zc::Maybe<identity::IdentityInvariant>& failure, bool& storeMismatch,
                            uint32_t ordinal) {
  const auto& target = fact.target.variant();
  if (target.is<DirectTarget>()) {
    if (!validateDefinitionIdentity(target.get<DirectTarget>().callee, input.registries, failure,
                                    ordinal)) {
      return false;
    }
  } else if (target.is<ConcreteMethodTarget>()) {
    if (!validateDefinitionIdentity(target.get<ConcreteMethodTarget>().method, input.registries,
                                    failure, ordinal)) {
      return false;
    }
  } else if (target.is<ImplMethodTarget>()) {
    const auto& method = target.get<ImplMethodTarget>();
    if (!validateImplIdentity(method.impl, input.registries, failure, ordinal) ||
        !validateDefinitionIdentity(method.method, input.registries, failure, ordinal)) {
      return false;
    }
  } else if (target.is<WitnessMethodTarget>()) {
    const auto& method = target.get<WitnessMethodTarget>();
    if (!validateDefinitionIdentity(method.witnessParameter, input.registries, failure, ordinal) ||
        !validateDefinitionIdentity(method.interface, input.registries, failure, ordinal) ||
        !validateDefinitionIdentity(method.method, input.registries, failure, ordinal)) {
      return false;
    }
  } else if (target.is<DynMethodTarget>()) {
    const auto& method = target.get<DynMethodTarget>();
    if (!validateDefinitionIdentity(method.interface, input.registries, failure, ordinal) ||
        !validateDefinitionIdentity(method.method, input.registries, failure, ordinal)) {
      return false;
    }
  }

  if (!validateTypeIdentity(fact.successType, input.semanticTypes, failure) ||
      !validateTypeIdentity(fact.resultType, input.semanticTypes, failure)) {
    return false;
  }
  ZC_IF_SOME(raises, fact.raises) {
    if (!validateTypeIdentity(raises, input.semanticTypes, failure)) return false;
  }
  ZC_IF_SOME(substitution, fact.substitutions) {
    if (!input.checkedFacts.substitutionStore().contains(substitution)) {
      storeMismatch = true;
      return false;
    }
  }
  ZC_IF_SOME(witnesses, fact.witnesses) {
    if (!input.checkedFacts.witnessStore().contains(witnesses)) {
      storeMismatch = true;
      return false;
    }
  }
  ZC_IF_SOME(receiver, fact.receiver) {
    if (!validateArgumentIdentities(receiver.value, input, failure, storeMismatch, ordinal) ||
        !validateTypeIdentity(receiver.adjustment.source, input.semanticTypes, failure) ||
        !validateTypeIdentity(receiver.adjustment.destination, input.semanticTypes, failure)) {
      return false;
    }
  }
  for (const auto& argument : fact.arguments) {
    if (!validateArgumentIdentities(argument, input, failure, storeMismatch, ordinal)) {
      return false;
    }
  }
  return true;
}

zc::Maybe<const checked::CheckedNodeKey&> projectionFor(
    ast::NodeId sourceNode, zc::ArrayPtr<const DispatchNodeProjection> projections) {
  for (const auto& projection : projections) {
    if (projection.sourceNode == sourceNode) return projection.checkedNode;
  }
  return zc::none;
}

zc::Maybe<DispatchArgumentPlan> cloneArgument(
    const checked::CheckedArgumentFact& argument,
    zc::ArrayPtr<const DispatchNodeProjection> projections) {
  auto projection = projectionFor(argument.sourceNode, projections);
  ZC_IF_SOME(node, projection) {
    return DispatchArgumentPlan{cloneNode(node), argument.sourceType, argument.parameterType,
                                cloneCoercion(argument.adjustment)};
  }
  return zc::none;
}

checked::ReceiverAdjustment cloneReceiverAdjustment(const checked::ReceiverAdjustment& adjustment) {
  zc::Vector<checked::ReceiverAdjustmentStep> steps(adjustment.steps.size());
  for (const auto step : adjustment.steps) steps.add(step);
  return checked::ReceiverAdjustment{adjustment.source, adjustment.destination, zc::mv(steps),
                                     adjustment.sourceSpan.clone()};
}

DispatchTarget targetFor(const checked::SelectedCallable& selected) {
  const auto& value = selected.variant();
  if (value.is<checked::DirectCallable>()) {
    return DispatchTarget(DirectTarget{value.get<checked::DirectCallable>().callee});
  }
  if (value.is<checked::ConcreteMethodCallable>()) {
    return DispatchTarget(
        ConcreteMethodTarget{value.get<checked::ConcreteMethodCallable>().method});
  }
  if (value.is<checked::ImplMethodCallable>()) {
    const auto& method = value.get<checked::ImplMethodCallable>();
    return DispatchTarget(ImplMethodTarget{method.impl, method.method});
  }
  if (value.is<checked::WitnessMethodCallable>()) {
    const auto& method = value.get<checked::WitnessMethodCallable>();
    return DispatchTarget(
        WitnessMethodTarget{method.witnessParameter, method.interface, method.method});
  }
  if (value.is<checked::DynMethodCallable>()) {
    const auto& method = value.get<checked::DynMethodCallable>();
    return DispatchTarget(DynMethodTarget{method.interface, method.method});
  }
  return DispatchTarget(PrimitiveTarget{value.get<checked::PrimitiveCallable>().operation});
}

zc::Maybe<PrimitiveOperation> compoundStem(CompoundAssignmentOperation operation) {
  switch (operation) {
    case CompoundAssignmentOperation::AddAssign:
      return PrimitiveOperation::Add;
    case CompoundAssignmentOperation::SubAssign:
      return PrimitiveOperation::Sub;
    case CompoundAssignmentOperation::MulAssign:
      return PrimitiveOperation::Mul;
    case CompoundAssignmentOperation::DivAssign:
      return PrimitiveOperation::Div;
    case CompoundAssignmentOperation::RemAssign:
      return PrimitiveOperation::Rem;
    case CompoundAssignmentOperation::PowAssign:
      return PrimitiveOperation::Pow;
    case CompoundAssignmentOperation::ShlAssign:
      return PrimitiveOperation::Shl;
    case CompoundAssignmentOperation::ShrAssign:
      return PrimitiveOperation::Shr;
    case CompoundAssignmentOperation::UShrAssign:
      return PrimitiveOperation::UShr;
    case CompoundAssignmentOperation::BitAndAssign:
      return PrimitiveOperation::BitAnd;
    case CompoundAssignmentOperation::BitOrAssign:
      return PrimitiveOperation::BitOr;
    case CompoundAssignmentOperation::BitXorAssign:
      return PrimitiveOperation::BitXor;
    case CompoundAssignmentOperation::LogicalAndAssign:
      return PrimitiveOperation::LogicalAnd;
    case CompoundAssignmentOperation::LogicalOrAssign:
      return PrimitiveOperation::LogicalOr;
    case CompoundAssignmentOperation::NullCoalesceAssign:
      return PrimitiveOperation::NullCoalesce;
  }
  return zc::none;
}

DispatchResultTransform resultTransformFor(const DispatchSiteRequirement& requirement,
                                           const checked::SelectedCallable& selected) {
  if (selected.variant().is<checked::PrimitiveCallable>()) {
    return DispatchResultTransform(IdentityResultTransform{});
  }
  ZC_IF_SOME(operation, requirement.operation) {
    switch (operation) {
      case PrimitiveOperation::Ne:
        return DispatchResultTransform(BooleanNotResultTransform{});
      case PrimitiveOperation::Lt:
        return DispatchResultTransform(CompareOrderingResultTransform{OrderingRelation::Less});
      case PrimitiveOperation::Le:
        return DispatchResultTransform(CompareOrderingResultTransform{OrderingRelation::LessEqual});
      case PrimitiveOperation::Gt:
        return DispatchResultTransform(CompareOrderingResultTransform{OrderingRelation::Greater});
      case PrimitiveOperation::Ge:
        return DispatchResultTransform(
            CompareOrderingResultTransform{OrderingRelation::GreaterEqual});
      default:
        break;
    }
  }
  return DispatchResultTransform(IdentityResultTransform{});
}

bool sameTransform(const DispatchResultTransform& left, const DispatchResultTransform& right) {
  const auto& leftValue = left.variant();
  const auto& rightValue = right.variant();
  if (leftValue.is<IdentityResultTransform>()) { return rightValue.is<IdentityResultTransform>(); }
  if (leftValue.is<BooleanNotResultTransform>()) {
    return rightValue.is<BooleanNotResultTransform>();
  }
  return rightValue.is<CompareOrderingResultTransform>() &&
         leftValue.get<CompareOrderingResultTransform>().relation ==
             rightValue.get<CompareOrderingResultTransform>().relation;
}

bool validRequirementShape(const DispatchSiteRequirement& requirement) {
  if (requirement.siteKind < DispatchSiteKind::Call ||
      requirement.siteKind > DispatchSiteKind::NullCoalescing) {
    return false;
  }
  const bool hasOperation = requirement.operation != zc::none;
  const bool hasCompound = requirement.compoundOperation != zc::none;
  const bool hasRole = requirement.receiverRole != zc::none;
  switch (requirement.siteKind) {
    case DispatchSiteKind::Call:
      return !hasOperation && !hasCompound && hasRole;
    case DispatchSiteKind::UnaryOperator:
    case DispatchSiteKind::BinaryOperator:
    case DispatchSiteKind::NullCoalescing:
      return hasOperation && !hasCompound && hasRole;
    case DispatchSiteKind::Index:
      return !hasOperation && !hasCompound && hasRole;
    case DispatchSiteKind::CompoundAssignment:
      return hasOperation && hasCompound && hasRole;
  }
  return false;
}

bool sameOptionalType(const zc::Maybe<identity::SemanticTypeId>& left,
                      const zc::Maybe<identity::SemanticTypeId>& right) {
  if ((left == zc::none) != (right == zc::none)) return false;
  if (left == zc::none) return true;
  bool same = false;
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) { same = leftValue == rightValue; }
  }
  return same;
}

template <typename Value>
bool sameOptionalHandle(const zc::Maybe<Value>& left, const zc::Maybe<Value>& right) {
  if ((left == zc::none) != (right == zc::none)) return false;
  if (left == zc::none) return true;
  bool same = false;
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) { same = leftValue == rightValue; }
  }
  return same;
}

bool sameTarget(const DispatchTarget& dispatch, const checked::SelectedCallable& selected) {
  const auto& left = dispatch.variant();
  const auto& right = selected.variant();
  if (left.is<DirectTarget>()) {
    return right.is<checked::DirectCallable>() &&
           left.get<DirectTarget>().callee == right.get<checked::DirectCallable>().callee;
  }
  if (left.is<ConcreteMethodTarget>()) {
    return right.is<checked::ConcreteMethodCallable>() &&
           left.get<ConcreteMethodTarget>().method ==
               right.get<checked::ConcreteMethodCallable>().method;
  }
  if (left.is<ImplMethodTarget>()) {
    return right.is<checked::ImplMethodCallable>() &&
           left.get<ImplMethodTarget>().impl == right.get<checked::ImplMethodCallable>().impl &&
           left.get<ImplMethodTarget>().method == right.get<checked::ImplMethodCallable>().method;
  }
  if (left.is<WitnessMethodTarget>()) {
    return right.is<checked::WitnessMethodCallable>() &&
           left.get<WitnessMethodTarget>().witnessParameter ==
               right.get<checked::WitnessMethodCallable>().witnessParameter &&
           left.get<WitnessMethodTarget>().interface ==
               right.get<checked::WitnessMethodCallable>().interface &&
           left.get<WitnessMethodTarget>().method ==
               right.get<checked::WitnessMethodCallable>().method;
  }
  if (left.is<DynMethodTarget>()) {
    return right.is<checked::DynMethodCallable>() &&
           left.get<DynMethodTarget>().interface ==
               right.get<checked::DynMethodCallable>().interface &&
           left.get<DynMethodTarget>().method == right.get<checked::DynMethodCallable>().method;
  }
  return right.is<checked::PrimitiveCallable>() &&
         left.get<PrimitiveTarget>().operation == right.get<checked::PrimitiveCallable>().operation;
}

bool sameCoercion(const zc::Maybe<checked::CoercionAdjustment>& left,
                  const zc::Maybe<checked::CoercionAdjustment>& right, FactEncoder& encoder) {
  if ((left == zc::none) != (right == zc::none)) return false;
  if (left == zc::none) return true;
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  bool encoded = false;
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) {
      encoded = encoder.encodeCoercionRecord(leftEncoder, leftValue) &&
                encoder.encodeCoercionRecord(rightEncoder, rightValue);
    }
  }
  if (!encoded) return false;
  auto leftBytes = leftEncoder.finish();
  auto rightBytes = rightEncoder.finish();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameReceiverAdjustment(const checked::ReceiverAdjustment& left,
                            const checked::ReceiverAdjustment& right, FactEncoder& encoder) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  if (!encoder.encodeReceiverAdjustmentRecord(leftEncoder, left) ||
      !encoder.encodeReceiverAdjustmentRecord(rightEncoder, right)) {
    return false;
  }
  auto leftBytes = leftEncoder.finish();
  auto rightBytes = rightEncoder.finish();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameArgument(const DispatchArgumentPlan& dispatch, const checked::CheckedArgumentFact& checked,
                  zc::ArrayPtr<const DispatchNodeProjection> projections, FactEncoder& encoder) {
  auto projected = projectionFor(checked.sourceNode, projections);
  bool sameProjected = false;
  ZC_IF_SOME(value, projected) { sameProjected = sameNode(dispatch.sourceNode, value); }
  return sameProjected && dispatch.sourceType == checked.sourceType &&
         dispatch.parameterType == checked.parameterType &&
         sameCoercion(dispatch.adjustment, checked.adjustment, encoder);
}

bool targetMatchesRequirement(const DispatchTarget& target,
                              const DispatchSiteRequirement& requirement) {
  if (!target.variant().is<PrimitiveTarget>()) {
    return requirement.siteKind != DispatchSiteKind::NullCoalescing;
  }
  const auto selected = target.variant().get<PrimitiveTarget>().operation;
  if (requirement.siteKind == DispatchSiteKind::Call) return false;
  if (requirement.siteKind == DispatchSiteKind::Index) {
    return selected == PrimitiveOperation::Index || selected == PrimitiveOperation::IndexMut;
  }
  bool matches = false;
  ZC_IF_SOME(expected, requirement.operation) { matches = selected == expected; }
  return matches;
}

bool matchesEnvelope(const DispatchFact& fact, const checked::CheckedCallEnvelope& envelope,
                     const DispatchSiteRequirement& requirement,
                     zc::ArrayPtr<const DispatchNodeProjection> projections, FactEncoder& encoder) {
  auto expectedTransform = resultTransformFor(requirement, envelope.selected);
  if (!sameTarget(fact.target, envelope.selected) || fact.successType != envelope.successType ||
      fact.resultType != envelope.resultType ||
      !sameOptionalHandle(fact.substitutions, envelope.substitutions) ||
      !sameOptionalHandle(fact.witnesses, envelope.witnesses) ||
      !sameOptionalType(fact.raises, envelope.raises) ||
      fact.arguments.size() != envelope.arguments.size() ||
      !targetMatchesRequirement(fact.target, requirement) ||
      !sameTransform(fact.resultTransform, expectedTransform)) {
    return false;
  }
  if (fact.raises == zc::none && fact.resultType != fact.successType) return false;
  for (size_t index = 0; index < fact.arguments.size(); ++index) {
    if (!sameArgument(fact.arguments[index], envelope.arguments[index], projections, encoder)) {
      return false;
    }
  }
  if ((fact.receiver == zc::none) != (envelope.receiver == zc::none) ||
      (fact.receiver == zc::none) != (envelope.receiverMode == zc::none) ||
      (fact.receiver == zc::none) != (envelope.receiverAdjustment == zc::none)) {
    return false;
  }
  if (fact.receiver != zc::none) {
    bool matches = false;
    ZC_IF_SOME(receiver, fact.receiver) {
      ZC_IF_SOME(argument, envelope.receiver) {
        ZC_IF_SOME(mode, envelope.receiverMode) {
          ZC_IF_SOME(adjustment, envelope.receiverAdjustment) {
            matches = receiver.passing == mode && requirement.receiverRole != zc::none &&
                      sameArgument(receiver.value, argument, projections, encoder) &&
                      sameReceiverAdjustment(receiver.adjustment, adjustment, encoder);
            ZC_IF_SOME(role, requirement.receiverRole) {
              matches = matches && receiver.role == role;
            }
          }
        }
      }
    }
    if (!matches) return false;
  }
  return true;
}

zc::Maybe<const checked::CheckedCallEnvelope&> checkedEnvelope(
    ast::NodeId sourceNode, const checked::VerifiedCheckedFacts& facts,
    zc::Maybe<const identity::SourceSpan&>& span) {
  for (const auto& entry : facts.calls().entries()) {
    if (entry.key == sourceNode) {
      span = entry.value.sourceSpan;
      return entry.value.invocation;
    }
  }
  for (const auto& entry : facts.compoundAssignments().entries()) {
    if (entry.key == sourceNode) {
      span = entry.value.sourceSpan;
      return entry.value.invocation;
    }
  }
  return zc::none;
}

zc::Maybe<const DispatchSiteRequirement&> requirementFor(
    ast::NodeId node, zc::ArrayPtr<const DispatchSiteRequirement> requirements) {
  for (const auto& requirement : requirements) {
    if (requirement.sourceNode == node) return requirement;
  }
  return zc::none;
}

bool sameOwner(const zc::Maybe<identity::DefId>& left, const zc::Maybe<identity::DefId>& right) {
  return sameOptionalHandle(left, right);
}

zc::Maybe<DispatchReceiverRole> cloneReceiverRole(const zc::Maybe<DispatchReceiverRole>& role) {
  ZC_IF_SOME(value, role) { return value; }
  return zc::none;
}

bool subtreeContains(const ast::Tree& tree, ast::NodeId root, ast::NodeId target) {
  bool contains = false;
  ast::visitTreePreOrder(tree, root, [&](ast::NodeId node, const ast::Node&) {
    if (node == target) contains = true;
  });
  return contains;
}

bool ownerFor(const binder::VerifiedBoundModuleInput& boundModule, ast::NodeId sourceNode,
              zc::Maybe<identity::DefId>& owner) {
  bool found = false;
  size_t bestDepth = 0;
  identity::DefId best;
  for (const auto& definition : boundModule.definitions().definitions()) {
    if (!subtreeContains(boundModule.tree(), definition.node, sourceNode)) continue;
    const size_t depth = definition.record.owners().size();
    if (!found || depth > bestDepth) {
      found = true;
      bestDepth = depth;
      best = definition.definition;
      continue;
    }
    if (depth == bestDepth && definition.definition != best) return false;
  }
  if (found) owner = best;
  return true;
}

zc::Maybe<PrimitiveOperation> primitiveOperationFor(const ast::Node& syntax) {
  zc::Maybe<OperatorKind> operation;
  if (syntax.kind == ast::SyntaxKind::UnaryExpression) {
    operation = OperatorKind::fromUnary(
        static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]));
  } else if (syntax.kind == ast::SyntaxKind::BinaryExpr) {
    operation = OperatorKind::fromBinary(
        static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]));
  } else if (syntax.kind == ast::SyntaxKind::PostfixExpression) {
    operation = OperatorKind::fromPostfix(
        static_cast<ast::PostfixOperatorKind>(syntax.payload.words[ast::kPostfixExpressionOpWord]));
  } else {
    return zc::none;
  }
  ZC_IF_SOME(value, operation) {
    if (value.variant().is<PrimitiveOperation>()) {
      return value.variant().get<PrimitiveOperation>();
    }
  }
  return zc::none;
}

zc::Maybe<CompoundAssignmentOperation> compoundOperationFor(const ast::Node& syntax) {
  if (syntax.kind != ast::SyntaxKind::AssignmentExpr) return zc::none;
  auto operation = OperatorKind::fromAssignment(
      static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]));
  ZC_IF_SOME(value, operation) {
    if (value.variant().is<CompoundAssignmentOperation>()) {
      return value.variant().get<CompoundAssignmentOperation>();
    }
  }
  return zc::none;
}

zc::Maybe<DispatchSiteRequirement> siteRequirementFor(
    const binder::VerifiedBoundModuleInput& boundModule,
    const checked::NodeFactRequirement& bodyRequirement) {
  const auto& tree = boundModule.tree();
  if (!tree.contains(bodyRequirement.node)) return zc::none;
  const auto& syntax = tree.node(bodyRequirement.node);
  zc::Maybe<identity::DefId> owner;
  if (!ownerFor(boundModule, bodyRequirement.node, owner)) return zc::none;

  DispatchSiteKind siteKind = DispatchSiteKind::Call;
  zc::Maybe<DispatchReceiverRole> receiverRole;
  zc::Maybe<PrimitiveOperation> operation;
  zc::Maybe<CompoundAssignmentOperation> compoundOperation;
  if (bodyRequirement.group == checked::CheckedFactGroup::CompoundAssignment) {
    compoundOperation = compoundOperationFor(syntax);
    if (compoundOperation == zc::none) return zc::none;
    ZC_IF_SOME(value, compoundOperation) { operation = compoundStem(value); }
    if (operation == zc::none) return zc::none;
    siteKind = DispatchSiteKind::CompoundAssignment;
    receiverRole = DispatchReceiverRole::OperatorLeftHandSide;
  } else if (bodyRequirement.group == checked::CheckedFactGroup::Call) {
    switch (syntax.kind) {
      case ast::SyntaxKind::CallExpression: {
        const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
        const bool memberCall =
            tree.contains(callee) && tree.node(callee).kind == ast::SyntaxKind::MemberExpression;
        receiverRole = memberCall ? DispatchReceiverRole::ImplicitSelf
                                  : DispatchReceiverRole::ExplicitFirstArgument;
        break;
      }
      case ast::SyntaxKind::ImportCallExpression:
        receiverRole = DispatchReceiverRole::ExplicitFirstArgument;
        break;
      case ast::SyntaxKind::UnaryExpression:
      case ast::SyntaxKind::PostfixExpression:
        siteKind = DispatchSiteKind::UnaryOperator;
        receiverRole = DispatchReceiverRole::OperatorOperand;
        operation = primitiveOperationFor(syntax);
        break;
      case ast::SyntaxKind::BinaryExpr:
        siteKind = DispatchSiteKind::BinaryOperator;
        receiverRole = DispatchReceiverRole::OperatorLeftHandSide;
        operation = primitiveOperationFor(syntax);
        break;
      case ast::SyntaxKind::IndexExpression:
        siteKind = DispatchSiteKind::Index;
        receiverRole = DispatchReceiverRole::IndexBase;
        break;
      case ast::SyntaxKind::NullCoalesceExpr:
        siteKind = DispatchSiteKind::NullCoalescing;
        receiverRole = DispatchReceiverRole::OperatorLeftHandSide;
        operation = PrimitiveOperation::NullCoalesce;
        break;
      default:
        return zc::none;
    }
  } else {
    return zc::none;
  }
  DispatchSiteRequirement requirement{bodyRequirement.node,
                                      cloneNode(bodyRequirement.key),
                                      cloneOwner(owner),
                                      siteKind,
                                      cloneReceiverRole(receiverRole),
                                      cloneOperation(operation),
                                      cloneCompoundOperation(compoundOperation)};
  if (!validRequirementShape(requirement)) return zc::none;
  return zc::mv(requirement);
}

}  // namespace

struct VerifiedDispatchSiteInventory::Impl final {
  Impl(identity::SemanticContextBrand semanticContext, identity::ModuleId module,
       identity::SemanticContextFingerprint&& semanticFingerprint,
       const identity::Sha256Digest& sourceContentDigest,
       const binder::ParsedModuleReceipt& parsedModuleReceipt,
       zc::Vector<DispatchSiteRequirement>&& requirements,
       zc::Vector<DispatchNodeProjection>&& nodeProjections)
      : semanticContext(semanticContext),
        module(module),
        semanticFingerprint(zc::mv(semanticFingerprint)),
        sourceContentDigest(sourceContentDigest),
        parsedModuleReceipt(parsedModuleReceipt),
        requirements(zc::mv(requirements)),
        nodeProjections(zc::mv(nodeProjections)) {}

  identity::SemanticContextBrand semanticContext;
  identity::ModuleId module;
  identity::SemanticContextFingerprint semanticFingerprint;
  identity::Sha256Digest sourceContentDigest;
  binder::ParsedModuleReceipt parsedModuleReceipt;
  zc::Vector<DispatchSiteRequirement> requirements;
  zc::Vector<DispatchNodeProjection> nodeProjections;
};

VerifiedDispatchSiteInventory::VerifiedDispatchSiteInventory(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedDispatchSiteInventory::~VerifiedDispatchSiteInventory() noexcept(false) = default;
VerifiedDispatchSiteInventory::VerifiedDispatchSiteInventory(
    VerifiedDispatchSiteInventory&&) noexcept = default;
VerifiedDispatchSiteInventory& VerifiedDispatchSiteInventory::operator=(
    VerifiedDispatchSiteInventory&&) noexcept = default;
identity::SemanticContextBrand VerifiedDispatchSiteInventory::semanticContext() const noexcept {
  return impl->semanticContext;
}
identity::ModuleId VerifiedDispatchSiteInventory::module() const noexcept { return impl->module; }
const identity::SemanticContextFingerprint& VerifiedDispatchSiteInventory::semanticFingerprint()
    const noexcept {
  return impl->semanticFingerprint;
}
const identity::Sha256Digest& VerifiedDispatchSiteInventory::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}
const binder::ParsedModuleReceipt& VerifiedDispatchSiteInventory::parsedModuleReceipt()
    const noexcept {
  return impl->parsedModuleReceipt;
}
zc::ArrayPtr<const DispatchSiteRequirement> VerifiedDispatchSiteInventory::requirements()
    const noexcept {
  return impl->requirements.asPtr();
}
zc::ArrayPtr<const DispatchNodeProjection> VerifiedDispatchSiteInventory::nodeProjections()
    const noexcept {
  return impl->nodeProjections.asPtr();
}

DispatchSiteInventoryBuildResult DispatchSiteInventoryBuilder::build(
    const binder::VerifiedBoundModuleInput& boundModule,
    const body::VerifiedBodyFactRequirementInventory& bodyRequirements) {
  if (boundModule.semanticContext() != bodyRequirements.semanticContext() ||
      boundModule.module() != bodyRequirements.module() ||
      boundModule.parsedModule().contentDigest() != bodyRequirements.sourceContentDigest() ||
      boundModule.parsedModule().receipt().digest() !=
          bodyRequirements.parsedModuleReceipt().digest()) {
    return rejected(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                    boundModule.module(), 0);
  }

  zc::Vector<DispatchNodeProjection> projections;
  for (const auto& bodyRequirement : bodyRequirements.nodeRequirements()) {
    bool found = false;
    for (const auto& projection : projections) {
      if (projection.sourceNode != bodyRequirement.node) continue;
      if (!sameNode(projection.checkedNode, bodyRequirement.key)) {
        return rejected(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                        boundModule.module(), bodyRequirement.key.schemaPreorder, zc::none,
                        maybeNode(bodyRequirement.key), maybeSpan(bodyRequirement.key.sourceSpan));
      }
      found = true;
      break;
    }
    if (!found) {
      projections.add(DispatchNodeProjection{bodyRequirement.node, cloneNode(bodyRequirement.key)});
    }
  }

  zc::Vector<DispatchSiteRequirement> requirements;
  for (const auto& bodyRequirement : bodyRequirements.nodeRequirements()) {
    if (bodyRequirement.group != checked::CheckedFactGroup::Call &&
        bodyRequirement.group != checked::CheckedFactGroup::CompoundAssignment) {
      continue;
    }
    for (const auto& existing : requirements) {
      if (existing.sourceNode == bodyRequirement.node) {
        return rejected(DispatchInvariantKind::AdditionalFact, DispatchInvariantStage::Input,
                        boundModule.module(), bodyRequirement.key.schemaPreorder, zc::none,
                        maybeNode(bodyRequirement.key), maybeSpan(bodyRequirement.key.sourceSpan));
      }
    }
    auto requirement = siteRequirementFor(boundModule, bodyRequirement);
    if (requirement == zc::none) {
      return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                      boundModule.module(), bodyRequirement.key.schemaPreorder, zc::none,
                      maybeNode(bodyRequirement.key), maybeSpan(bodyRequirement.key.sourceSpan));
    }
    ZC_IF_SOME(value, requirement) { requirements.add(zc::mv(value)); }
  }

  return VerifiedDispatchSiteInventory(zc::heap<VerifiedDispatchSiteInventory::Impl>(
      boundModule.semanticContext(), boundModule.module(),
      boundModule.semanticFingerprint().clone(), boundModule.parsedModule().contentDigest(),
      boundModule.parsedModule().receipt(), zc::mv(requirements), zc::mv(projections)));
}

struct DispatchFactsCandidate::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId module,
       const checked::CheckedFactsRevision& checkedFactsRevision,
       zc::Vector<DispatchFactCandidateEntry>&& facts)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        facts(zc::mv(facts)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checked::CheckedFactsRevision checkedFactsRevision;
  zc::Vector<DispatchFactCandidateEntry> facts;
};

DispatchFactsCandidate::DispatchFactsCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId module,
    const checked::CheckedFactsRevision& checkedFactsRevision,
    zc::Vector<DispatchFactCandidateEntry>&& facts)
    : impl(zc::heap<Impl>(semanticContext, zc::mv(contextFingerprint), module, checkedFactsRevision,
                          zc::mv(facts))) {}
DispatchFactsCandidate::~DispatchFactsCandidate() noexcept(false) = default;
DispatchFactsCandidate::DispatchFactsCandidate(DispatchFactsCandidate&&) noexcept = default;
DispatchFactsCandidate& DispatchFactsCandidate::operator=(DispatchFactsCandidate&&) noexcept =
    default;

DispatchFactsRevision::DispatchFactsRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}
const identity::Sha256Digest& DispatchFactsRevision::digest() const noexcept { return value; }

zc::Maybe<DispatchFactsRevision> DispatchFactsRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const checked::CheckedFactsRevision& checkedFactsRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> completeCanonicalRecords) {
  return computeFramedDigest(contextFingerprint, expandedModuleKey, checkedFactsRevision.digest(),
                             completeCanonicalRecords);
}

zc::Maybe<DispatchFactsRevision> DispatchFactsRevision::computeFramedDigest(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& checkedFactsRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> completeCanonicalRecords) {
  if (expandedModuleKey.size() == 0) return zc::none;
  zc::Vector<zc::ArrayPtr<const uint8_t>> sorted(completeCanonicalRecords.size());
  for (const auto record : completeCanonicalRecords) {
    if (record.size() == 0) return zc::none;
    sorted.add(record);
  }
  for (size_t index = 1; index < sorted.size(); ++index) {
    const auto current = sorted[index];
    size_t insertion = index;
    while (insertion != 0 && lessBytes(current, sorted[insertion - 1])) {
      sorted[insertion] = sorted[insertion - 1];
      --insertion;
    }
    sorted[insertion] = current;
  }
  for (size_t index = 1; index < sorted.size(); ++index) {
    if (sameBytes(sorted[index - 1], sorted[index])) return zc::none;
  }

  zc::Vector<uint8_t> preimage;
  constexpr zc::StringPtr domain = "zom.dispatch-facts-revision.v1"_zcc;
  for (const auto character : domain) preimage.add(static_cast<uint8_t>(character));
  preimage.add(0);
  append(preimage, contextFingerprint.bytes());
  appendByteString(preimage, expandedModuleKey);
  append(preimage, checkedFactsRevision.bytes());
  appendUint64(preimage, sorted.size());
  for (const auto record : sorted) appendByteString(preimage, record);
  ZC_IF_SOME(digest, identity::sha256(preimage.asPtr())) { return DispatchFactsRevision(digest); }
  return zc::none;
}

struct VerifiedDispatchFacts::Impl final {
  Impl(DispatchFactsRevision revision, identity::SemanticContextBrand semanticContext,
       identity::ModuleId module, const checked::CheckedFactsRevision& checkedFactsRevision,
       zc::Vector<VerifiedDispatchFact>&& facts)
      : revision(revision),
        semanticContext(semanticContext),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        facts(zc::mv(facts)) {}
  DispatchFactsRevision revision;
  identity::SemanticContextBrand semanticContext;
  identity::ModuleId module;
  checked::CheckedFactsRevision checkedFactsRevision;
  zc::Vector<VerifiedDispatchFact> facts;
};

VerifiedDispatchFacts::VerifiedDispatchFacts(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedDispatchFacts::~VerifiedDispatchFacts() noexcept(false) = default;
VerifiedDispatchFacts::VerifiedDispatchFacts(VerifiedDispatchFacts&&) noexcept = default;
VerifiedDispatchFacts& VerifiedDispatchFacts::operator=(VerifiedDispatchFacts&&) noexcept = default;
const DispatchFactsRevision& VerifiedDispatchFacts::revision() const noexcept {
  return impl->revision;
}
identity::SemanticContextBrand VerifiedDispatchFacts::semanticContext() const noexcept {
  return impl->semanticContext;
}
identity::ModuleId VerifiedDispatchFacts::module() const noexcept { return impl->module; }
const checked::CheckedFactsRevision& VerifiedDispatchFacts::checkedFactsRevision() const noexcept {
  return impl->checkedFactsRevision;
}
zc::ArrayPtr<const VerifiedDispatchFact> VerifiedDispatchFacts::facts() const noexcept {
  return impl->facts.asPtr();
}

zc::Maybe<zc::Array<uint8_t>> DispatchFactCanonicalCodec::encode(
    const checked::CheckedNodeKey& checkedNode, const DispatchFact& fact,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes,
    const checked::VerifiedCheckedFacts& checkedFacts) {
  auto module = registries.modules().lookup(checkedFacts.module());
  if (module == zc::none || checkedFacts.semanticContext() != registries.context() ||
      checkedFacts.semanticContext() != semanticTypes.context()) {
    return zc::none;
  }
  FactEncoder encoder(registries, semanticTypes, checkedFacts, checkedNode.sourceSpan.source());
  return encoder.encode(checkedNode, fact);
}

DispatchFactsBuildResult DispatchFactsBuilder::build(
    const VerifiedDispatchSiteInventory& inventory,
    const identity::SemanticContextFingerprint& contextFingerprint,
    const checked::CheckedEvidenceLease& checkedLease,
    const checked::VerifiedCheckedFacts& checkedFacts,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  if (inventory.semanticContext() != registries.context() ||
      inventory.semanticContext() != semanticTypes.context() ||
      inventory.semanticContext() != checkedFacts.semanticContext() ||
      inventory.module() != checkedFacts.module() || inventory.module() != checkedLease.module() ||
      inventory.semanticFingerprint().digest() != contextFingerprint.digest() ||
      checkedLease.revision().digest() != checkedFacts.revision().digest()) {
    return rejected(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                    inventory.module(), 0);
  }

  zc::Vector<DispatchFactCandidateEntry> facts(inventory.requirements().size());
  for (const auto& requirement : inventory.requirements()) {
    if (!validRequirementShape(requirement)) {
      return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                      inventory.module(), requirement.checkedNode.schemaPreorder,
                      cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                      maybeSpan(requirement.checkedNode.sourceSpan));
    }
    zc::Maybe<const identity::SourceSpan&> checkedSpan;
    auto envelope = checkedEnvelope(requirement.sourceNode, checkedFacts, checkedSpan);
    if (envelope == zc::none || checkedSpan == zc::none) {
      return rejected(DispatchInvariantKind::MissingFact, DispatchInvariantStage::Construction,
                      inventory.module(), requirement.checkedNode.schemaPreorder,
                      cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                      maybeSpan(requirement.checkedNode.sourceSpan));
    }

    zc::Maybe<DispatchFact> fact;
    ZC_IF_SOME(call, envelope) {
      zc::Maybe<DispatchReceiverPlan> receiver;
      const bool hasReceiver = call.receiver != zc::none;
      if (hasReceiver != (call.receiverMode != zc::none) ||
          hasReceiver != (call.receiverAdjustment != zc::none)) {
        return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                        inventory.module(), requirement.checkedNode.schemaPreorder,
                        cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                        maybeSpan(requirement.checkedNode.sourceSpan));
      }
      if (hasReceiver) {
        bool copied = false;
        ZC_IF_SOME(argument, call.receiver) {
          auto value = cloneArgument(argument, inventory.nodeProjections());
          ZC_IF_SOME(mode, call.receiverMode) {
            ZC_IF_SOME(adjustment, call.receiverAdjustment) {
              ZC_IF_SOME(role, requirement.receiverRole) {
                ZC_IF_SOME(argumentPlan, value) {
                  receiver = DispatchReceiverPlan{role, mode, zc::mv(argumentPlan),
                                                  cloneReceiverAdjustment(adjustment)};
                  copied = true;
                }
              }
            }
          }
        }
        if (!copied) {
          return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                          inventory.module(), requirement.checkedNode.schemaPreorder,
                          cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                          maybeSpan(requirement.checkedNode.sourceSpan));
        }
      }

      zc::Vector<DispatchArgumentPlan> arguments(call.arguments.size());
      for (const auto& argument : call.arguments) {
        auto copied = cloneArgument(argument, inventory.nodeProjections());
        if (copied == zc::none) {
          return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                          inventory.module(), requirement.checkedNode.schemaPreorder,
                          cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                          maybeSpan(requirement.checkedNode.sourceSpan));
        }
        ZC_IF_SOME(value, copied) { arguments.add(zc::mv(value)); }
      }
      ZC_IF_SOME(span, checkedSpan) {
        fact = DispatchFact{targetFor(call.selected),
                            resultTransformFor(requirement, call.selected),
                            zc::mv(receiver),
                            zc::mv(arguments),
                            call.successType,
                            call.resultType,
                            call.substitutions,
                            call.witnesses,
                            call.raises,
                            span.clone()};
      }
    }
    if (fact == zc::none) {
      return rejected(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Construction,
                      inventory.module(), requirement.checkedNode.schemaPreorder,
                      cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                      maybeSpan(requirement.checkedNode.sourceSpan));
    }
    ZC_IF_SOME(value, fact) {
      auto canonical = DispatchFactCanonicalCodec::encode(requirement.checkedNode, value,
                                                          registries, semanticTypes, checkedFacts);
      if (canonical == zc::none) {
        return rejected(DispatchInvariantKind::CanonicalCodecMismatch,
                        DispatchInvariantStage::Encoding, inventory.module(),
                        requirement.checkedNode.schemaPreorder, cloneOwner(requirement.owner),
                        maybeNode(requirement.checkedNode), maybeSpan(value.sourceSpan));
      }
      ZC_IF_SOME(record, canonical) {
        facts.add(DispatchFactCandidateEntry{
            requirement.sourceNode, cloneNode(requirement.checkedNode),
            cloneOwner(requirement.owner), zc::mv(value), zc::mv(record)});
      }
    }
  }

  return DispatchFactsCandidate(inventory.semanticContext(), contextFingerprint.clone(),
                                inventory.module(), checkedFacts.revision(), zc::mv(facts));
}

DispatchVerificationResult DispatchFactsVerifier::verify(
    DispatchFactsCandidate&& candidate, const DispatchFactsVerificationInput& input) {
  const auto moduleFailure = input.registries.modules().validate(candidate.impl->module);
  if (moduleFailure != identity::FrozenRegistryFailure::None) {
    return reject(registryInvariant(moduleFailure, identity::IdentityAllocationPhase::Module, 0));
  }
  auto module = input.registries.modules().lookup(candidate.impl->module);
  if (module == zc::none) {
    return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                  candidate.impl->module, 0);
  }
  const bool staleCandidate =
      candidate.impl->checkedFactsRevision.digest() != input.checkedFacts.revision().digest();
  const bool staleLease =
      input.checkedLease.revision().digest() != input.checkedFacts.revision().digest() ||
      input.checkedLease.module() != input.checkedFacts.module();
  if (candidate.impl->semanticContext != input.registries.context() ||
      candidate.impl->semanticContext != input.semanticTypes.context() ||
      candidate.impl->semanticContext != input.checkedFacts.semanticContext() ||
      candidate.impl->module != input.module ||
      candidate.impl->module != input.checkedFacts.module() ||
      candidate.impl->contextFingerprint.digest() != input.contextFingerprint.digest() ||
      staleCandidate || staleLease ||
      input.registries.sourceFiles().find(input.source) == zc::none) {
    zc::Maybe<identity::Sha256Digest> expected = input.checkedFacts.revision().digest();
    zc::Maybe<identity::Sha256Digest> actual = candidate.impl->checkedFactsRevision.digest();
    if (staleLease) actual = input.checkedLease.revision().digest();
    return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                  candidate.impl->module, 0, zc::none, zc::none, zc::none, zc::Vector<uint32_t>(),
                  zc::mv(expected), zc::mv(actual));
  }

  auto verifyWithModule = [&](const identity::ModuleKey& moduleKey) -> DispatchVerificationResult {
    for (size_t index = 0; index < input.nodeProjections.size(); ++index) {
      const auto& projection = input.nodeProjections[index];
      if (!projection.sourceNode || projection.checkedNode.schemaPreorder == UINT32_MAX ||
          !input.source.belongsTo(moduleKey.crate()) ||
          !projection.checkedNode.sourceSpan.belongsTo(input.source)) {
        return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                      candidate.impl->module, static_cast<uint32_t>(index), zc::none,
                      maybeNode(projection.checkedNode),
                      maybeSpan(projection.checkedNode.sourceSpan));
      }
      for (size_t previous = 0; previous < index; ++previous) {
        if (input.nodeProjections[previous].sourceNode == projection.sourceNode ||
            sameNode(input.nodeProjections[previous].checkedNode, projection.checkedNode)) {
          return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                        candidate.impl->module, static_cast<uint32_t>(index), zc::none,
                        maybeNode(projection.checkedNode),
                        maybeSpan(projection.checkedNode.sourceSpan));
        }
      }
    }
    for (size_t index = 0; index < input.requirements.size(); ++index) {
      const auto& requirement = input.requirements[index];
      ZC_IF_SOME(owner, requirement.owner) {
        const auto ownerFailure = input.registries.definitions().validate(owner);
        if (ownerFailure != identity::FrozenRegistryFailure::None) {
          return reject(registryInvariant(ownerFailure,
                                          identity::IdentityAllocationPhase::Definition,
                                          static_cast<uint32_t>(index)));
        }
      }
      auto projected = projectionFor(requirement.sourceNode, input.nodeProjections);
      bool projectionMatches = false;
      ZC_IF_SOME(value, projected) { projectionMatches = sameNode(value, requirement.checkedNode); }
      if (!validRequirementShape(requirement) || !requirement.sourceNode || !projectionMatches ||
          !input.source.belongsTo(moduleKey.crate()) ||
          !requirement.checkedNode.sourceSpan.belongsTo(input.source)) {
        return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                      candidate.impl->module, static_cast<uint32_t>(index),
                      cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                      maybeSpan(requirement.checkedNode.sourceSpan));
      }
      for (size_t previous = 0; previous < index; ++previous) {
        if (input.requirements[previous].sourceNode == requirement.sourceNode ||
            sameNode(input.requirements[previous].checkedNode, requirement.checkedNode)) {
          return reject(DispatchInvariantKind::AdditionalFact, DispatchInvariantStage::Input,
                        candidate.impl->module, static_cast<uint32_t>(index),
                        cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                        maybeSpan(requirement.checkedNode.sourceSpan));
        }
      }
    }

    FactEncoder encoder(input.registries, input.semanticTypes, input.checkedFacts, input.source);
    for (size_t index = 0; index < candidate.impl->facts.size(); ++index) {
      const auto& entry = candidate.impl->facts[index];
      ZC_IF_SOME(owner, entry.owner) {
        const auto ownerFailure = input.registries.definitions().validate(owner);
        if (ownerFailure != identity::FrozenRegistryFailure::None) {
          return reject(registryInvariant(ownerFailure,
                                          identity::IdentityAllocationPhase::Definition,
                                          static_cast<uint32_t>(index)));
        }
      }
      zc::Maybe<identity::IdentityInvariant> identityFailure;
      bool storeMismatch = false;
      if (!validateFactIdentities(entry.fact, input, identityFailure, storeMismatch,
                                  static_cast<uint32_t>(index))) {
        ZC_IF_SOME(failure, identityFailure) { return reject(zc::mv(failure)); }
        if (storeMismatch) {
          return reject(DispatchInvariantKind::InputMismatch, DispatchInvariantStage::Input,
                        candidate.impl->module, static_cast<uint32_t>(index),
                        cloneOwner(entry.owner), maybeNode(entry.checkedNode),
                        maybeSpan(entry.fact.sourceSpan));
        }
        return reject(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Verification,
                      candidate.impl->module, static_cast<uint32_t>(index), cloneOwner(entry.owner),
                      maybeNode(entry.checkedNode), maybeSpan(entry.fact.sourceSpan));
      }
      auto encoded = encoder.encode(entry.checkedNode, entry.fact);
      bool canonical = false;
      ZC_IF_SOME(bytes, encoded) {
        canonical = sameBytes(bytes.asPtr(), entry.canonicalRecord.asPtr());
      }
      if (!canonical) {
        return reject(DispatchInvariantKind::CanonicalCodecMismatch,
                      DispatchInvariantStage::Encoding, candidate.impl->module,
                      static_cast<uint32_t>(index), cloneOwner(entry.owner),
                      maybeNode(entry.checkedNode), maybeSpan(entry.fact.sourceSpan));
      }
    }

    for (size_t index = 0; index < input.requirements.size(); ++index) {
      bool found = false;
      for (const auto& entry : candidate.impl->facts) {
        if (entry.sourceNode == input.requirements[index].sourceNode) found = true;
      }
      if (!found) {
        const auto& requirement = input.requirements[index];
        return reject(DispatchInvariantKind::MissingFact, DispatchInvariantStage::Verification,
                      candidate.impl->module, static_cast<uint32_t>(index),
                      cloneOwner(requirement.owner), maybeNode(requirement.checkedNode),
                      maybeSpan(requirement.checkedNode.sourceSpan));
      }
    }

    for (size_t index = 0; index < candidate.impl->facts.size(); ++index) {
      const auto& entry = candidate.impl->facts[index];
      auto requirement = requirementFor(entry.sourceNode, input.requirements);
      if (requirement == zc::none) {
        return reject(DispatchInvariantKind::AdditionalFact, DispatchInvariantStage::Verification,
                      candidate.impl->module, static_cast<uint32_t>(index), cloneOwner(entry.owner),
                      maybeNode(entry.checkedNode), maybeSpan(entry.fact.sourceSpan));
      }
      for (size_t previous = 0; previous < index; ++previous) {
        if (candidate.impl->facts[previous].sourceNode == entry.sourceNode ||
            sameNode(candidate.impl->facts[previous].checkedNode, entry.checkedNode) ||
            sameBytes(candidate.impl->facts[previous].canonicalRecord.asPtr(),
                      entry.canonicalRecord.asPtr())) {
          return reject(DispatchInvariantKind::AdditionalFact, DispatchInvariantStage::Verification,
                        candidate.impl->module, static_cast<uint32_t>(index),
                        cloneOwner(entry.owner), maybeNode(entry.checkedNode),
                        maybeSpan(entry.fact.sourceSpan));
        }
      }
      bool exactRequirement = false;
      ZC_IF_SOME(value, requirement) {
        exactRequirement =
            sameNode(value.checkedNode, entry.checkedNode) && sameOwner(value.owner, entry.owner);
      }
      if (!exactRequirement) {
        return reject(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Verification,
                      candidate.impl->module, static_cast<uint32_t>(index), cloneOwner(entry.owner),
                      maybeNode(entry.checkedNode), maybeSpan(entry.fact.sourceSpan));
      }

      zc::Maybe<const identity::SourceSpan&> checkedSpan;
      auto envelope = checkedEnvelope(entry.sourceNode, input.checkedFacts, checkedSpan);
      bool exactEnvelope = false;
      ZC_IF_SOME(required, requirement) {
        ZC_IF_SOME(value, envelope) {
          ZC_IF_SOME(span, checkedSpan) {
            exactEnvelope =
                sameSpan(span, entry.fact.sourceSpan) &&
                matchesEnvelope(entry.fact, value, required, input.nodeProjections, encoder);
          }
        }
      }
      if (!exactEnvelope) {
        return reject(DispatchInvariantKind::InvalidFact, DispatchInvariantStage::Verification,
                      candidate.impl->module, static_cast<uint32_t>(index), cloneOwner(entry.owner),
                      maybeNode(entry.checkedNode), maybeSpan(entry.fact.sourceSpan));
      }
    }

    for (size_t index = 1; index < candidate.impl->facts.size(); ++index) {
      auto current = zc::mv(candidate.impl->facts[index]);
      size_t insertion = index;
      while (insertion != 0 &&
             lessBytes(current.canonicalRecord.asPtr(),
                       candidate.impl->facts[insertion - 1].canonicalRecord.asPtr())) {
        candidate.impl->facts[insertion] = zc::mv(candidate.impl->facts[insertion - 1]);
        --insertion;
      }
      candidate.impl->facts[insertion] = zc::mv(current);
    }
    zc::Vector<zc::ArrayPtr<const uint8_t>> records(candidate.impl->facts.size());
    for (const auto& entry : candidate.impl->facts) records.add(entry.canonicalRecord.asPtr());
    auto moduleBytes = moduleKey.encode();
    auto revision =
        DispatchFactsRevision::computeFramed(input.contextFingerprint.digest(), moduleBytes.asPtr(),
                                             input.checkedFacts.revision(), records.asPtr());
    if (revision == zc::none) {
      return reject(DispatchInvariantKind::CanonicalCodecMismatch, DispatchInvariantStage::Encoding,
                    candidate.impl->module, 0);
    }

    zc::Vector<VerifiedDispatchFact> verified(candidate.impl->facts.size());
    for (auto& entry : candidate.impl->facts) {
      verified.add(VerifiedDispatchFact{zc::mv(entry.checkedNode), zc::mv(entry.owner),
                                        zc::mv(entry.fact), zc::mv(entry.canonicalRecord)});
    }
    ZC_IF_SOME(value, revision) {
      return VerifiedDispatchFacts(zc::heap<VerifiedDispatchFacts::Impl>(
          value, candidate.impl->semanticContext, candidate.impl->module,
          candidate.impl->checkedFactsRevision, zc::mv(verified)));
    }
    ZC_UNREACHABLE
  };
  ZC_IF_SOME(moduleKey, module) { return verifyWithModule(moduleKey); }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::dispatch
