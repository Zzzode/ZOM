// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/borrow/borrow-interface.h"

#include "zc/core/string.h"

namespace zomlang::compiler::checker::borrow {
namespace {

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

void appendDomain(zc::Vector<uint8_t>& output, zc::StringPtr domain) {
  for (const auto byte : domain) { output.add(static_cast<uint8_t>(byte)); }
  output.add(0);
}

void appendUint32(zc::Vector<uint8_t>& output, uint32_t value) {
  for (uint32_t index = 0; index < 4; ++index) {
    output.add(static_cast<uint8_t>((value >> (24U - index * 8U)) & 0xffU));
  }
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

bool less(const BorrowInputRegion& left, const BorrowInputRegion& right) {
  if (left.tag() != right.tag()) {
    return static_cast<uint8_t>(left.tag()) < static_cast<uint8_t>(right.tag());
  }
  return left.tag() == BorrowInputRegionTag::Parameter &&
         left.parameterIndex() < right.parameterIndex();
}

bool less(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

void appendInput(zc::Vector<uint8_t>& output, const BorrowInputRegion& input) {
  output.add(static_cast<uint8_t>(input.tag()));
  if (input.tag() == BorrowInputRegionTag::Parameter) {
    appendUint32(output, input.parameterIndex());
  }
}

}  // namespace

BorrowInputRegion::BorrowInputRegion(BorrowInputRegionTag tag, uint32_t parameterIndex) noexcept
    : tagValue(tag), parameterIndexValue(parameterIndex) {}

BorrowInputRegion BorrowInputRegion::receiver() noexcept {
  return BorrowInputRegion(BorrowInputRegionTag::Receiver, 0);
}

BorrowInputRegion BorrowInputRegion::parameter(uint32_t index) noexcept {
  return BorrowInputRegion(BorrowInputRegionTag::Parameter, index);
}

BorrowInputRegionTag BorrowInputRegion::tag() const noexcept { return tagValue; }
uint32_t BorrowInputRegion::parameterIndex() const noexcept { return parameterIndexValue; }

bool BorrowInputRegion::operator==(const BorrowInputRegion& other) const noexcept {
  return tagValue == other.tagValue && (tagValue == BorrowInputRegionTag::Receiver ||
                                        parameterIndexValue == other.parameterIndexValue);
}

BorrowReturnRelation::BorrowReturnRelation(BorrowReturnRelationTag tag,
                                           BorrowInputRegion source) noexcept
    : tagValue(tag), sourceValue(source) {}

BorrowReturnRelation BorrowReturnRelation::none() noexcept {
  return BorrowReturnRelation(BorrowReturnRelationTag::None, BorrowInputRegion::receiver());
}

BorrowReturnRelation BorrowReturnRelation::directRoot(BorrowInputRegion source) noexcept {
  return BorrowReturnRelation(BorrowReturnRelationTag::DirectRoot, source);
}

BorrowReturnRelationTag BorrowReturnRelation::tag() const noexcept { return tagValue; }
const BorrowInputRegion& BorrowReturnRelation::source() const noexcept { return sourceValue; }

BorrowSignatureSummary BorrowSignatureSummary::clone() const {
  zc::Vector<BorrowInputRegion> inputs(directInputs.size());
  for (const auto& input : directInputs) { inputs.add(input); }
  return BorrowSignatureSummary{callable, zc::mv(inputs), returnRelation};
}

BorrowSignatureFailure BorrowSignatureFailure::clone() const {
  return BorrowSignatureFailure{kind, callable, primarySpan.clone(), declarationSpan.clone(),
                                traversalOrdinal};
}

zc::Maybe<zc::Array<uint8_t>> BorrowSignatureCanonicalCodec::encodeFramed(
    const BorrowSignatureSummary& summary, zc::ArrayPtr<const uint8_t> expandedCallableKey) {
  if (expandedCallableKey.size() == 0) { return zc::none; }
  for (size_t index = 0; index < summary.directInputs.size(); ++index) {
    if (index != 0 && !less(summary.directInputs[index - 1], summary.directInputs[index])) {
      return zc::none;
    }
  }
  if (summary.returnRelation.tag() == BorrowReturnRelationTag::DirectRoot) {
    bool found = false;
    for (const auto& input : summary.directInputs) {
      if (input == summary.returnRelation.source()) { found = true; }
    }
    if (!found) { return zc::none; }
  }

  zc::Vector<uint8_t> output;
  appendDomain(output, "zom.borrow-signature-summary"_zc);
  appendByteString(output, expandedCallableKey);
  appendUint64(output, summary.directInputs.size());
  for (const auto& input : summary.directInputs) { appendInput(output, input); }
  output.add(static_cast<uint8_t>(summary.returnRelation.tag()));
  if (summary.returnRelation.tag() == BorrowReturnRelationTag::DirectRoot) {
    appendInput(output, summary.returnRelation.source());
  }
  return output.releaseAsArray();
}

zc::Maybe<zc::Array<uint8_t>> BorrowSignatureCanonicalCodec::encode(
    const BorrowSignatureSummary& summary, const CheckerIdentityAuthority& identities) {
  auto definition = identities.definition(summary.callable);
  ZC_IF_SOME(value, definition) {
    const auto bytes = value.key().encode();
    return encodeFramed(summary, bytes.asPtr());
  }
  return zc::none;
}

BorrowInterfaceRevision::BorrowInterfaceRevision(const identity::Sha256Digest& value) noexcept
    : value(value) {}

const identity::Sha256Digest& BorrowInterfaceRevision::digest() const noexcept { return value; }

zc::Maybe<BorrowInterfaceRevision> BorrowInterfaceRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& signatureFactsRevision,
    const identity::Sha256Digest& importedSignatureViewRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> summaryRecords) {
  if (expandedModuleKey.size() == 0) { return zc::none; }
  zc::Vector<uint8_t> output;
  appendDomain(output, "zom.borrow-interface"_zc);
  append(output, contextFingerprint.bytes());
  appendByteString(output, expandedModuleKey);
  append(output, signatureFactsRevision.bytes());
  append(output, importedSignatureViewRevision.bytes());
  appendUint64(output, summaryRecords.size());
  for (size_t index = 0; index < summaryRecords.size(); ++index) {
    if (summaryRecords[index].size() == 0 ||
        (index != 0 && !less(summaryRecords[index - 1], summaryRecords[index]))) {
      return zc::none;
    }
    appendByteString(output, summaryRecords[index]);
  }
  ZC_IF_SOME(digest, identity::sha256(output.asPtr())) { return BorrowInterfaceRevision(digest); }
  return zc::none;
}

namespace {

using ClassifyResult =
    zc::OneOf<BorrowShape, identity::IdentityInvariant, signature::CheckerInvariantFact>;

struct TypeSubstitution final {
  identity::GenericParameterKey parameter;
  identity::SemanticTypeId argument;
};

zc::Maybe<const signature::SemanticSignature&> findSignature(
    identity::DefId definition, const BorrowInterfaceBuildInput& input) {
  for (const auto& candidate : input.definitions) {
    if (candidate.definition == definition) { return candidate; }
  }
  for (const auto& candidate : input.supportDefinitions) {
    if (candidate.definition == definition) { return candidate; }
  }
  return zc::none;
}

signature::CheckerInvariantFact checkerFailure(const BorrowInterfaceBuildInput& input,
                                               signature::CheckerInvariantKind kind,
                                               zc::Maybe<identity::DefId>&& owner,
                                               zc::Maybe<identity::SourceSpan>&& span,
                                               uint32_t ordinal) {
  return signature::CheckerInvariantFact{kind,
                                         signature::CheckerInvariantStage::Signature,
                                         input.module,
                                         zc::mv(owner),
                                         zc::none,
                                         zc::mv(span),
                                         zc::Vector<uint32_t>(),
                                         zc::none,
                                         zc::none,
                                         ordinal};
}

BorrowShape contained(BorrowShape shape) {
  return shape == BorrowShape::DirectRootRegion ? BorrowShape::NestedRegion : shape;
}

BorrowShape join(BorrowShape left, BorrowShape right) {
  left = contained(left);
  right = contained(right);
  const auto rank = [](BorrowShape shape) {
    switch (shape) {
      case BorrowShape::NoRegion:
        return uint8_t{0};
      case BorrowShape::NestedRegion:
        return uint8_t{1};
      case BorrowShape::ParametricRegion:
        return uint8_t{2};
      case BorrowShape::OpaqueRegion:
        return uint8_t{3};
      case BorrowShape::DirectRootRegion:
        return uint8_t{1};
    }
    ZC_UNREACHABLE
  };
  return rank(left) < rank(right) ? right : left;
}

zc::Maybe<identity::SemanticTypeId> substitute(const identity::GenericParameterKey& parameter,
                                               zc::ArrayPtr<const TypeSubstitution> substitutions) {
  for (const auto& candidate : substitutions) {
    if (candidate.parameter == parameter) { return candidate.argument; }
  }
  return zc::none;
}

ClassifyResult classifyType(identity::SemanticTypeId typeId, const BorrowInterfaceBuildInput& input,
                            zc::ArrayPtr<const TypeSubstitution> substitutions,
                            zc::Vector<identity::DefId>& activeNominals, identity::DefId owner,
                            uint32_t ordinal) {
  auto lookup = input.semanticTypes.get(typeId);
  if (lookup.is<identity::IdentityInvariant>()) {
    return zc::mv(lookup.get<identity::IdentityInvariant>());
  }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  using namespace type::semantic;
  switch (data.tag()) {
    case TypeDataTag::Primitive:
    case TypeDataTag::RawPointer:
      return BorrowShape::NoRegion;
    case TypeDataTag::Reference:
      return BorrowShape::DirectRootRegion;
    case TypeDataTag::TypeParameter: {
      const auto& parameter = data.get<TypeParameterTypeData>().parameter;
      ZC_IF_SOME(argument, substitute(parameter, substitutions)) {
        return classifyType(argument, input, substitutions, activeNominals, owner, ordinal);
      }
      return BorrowShape::ParametricRegion;
    }
    case TypeDataTag::Function:
    case TypeDataTag::Existential:
    case TypeDataTag::InterfaceBound:
    case TypeDataTag::InterfaceSelf:
      return BorrowShape::OpaqueRegion;
    case TypeDataTag::Slice:
    case TypeDataTag::Intersection: {
      zc::Maybe<identity::DefId> ownerValue = owner;
      return checkerFailure(input, signature::CheckerInvariantKind::InvalidFact, zc::mv(ownerValue),
                            zc::none, ordinal);
    }
    case TypeDataTag::Tuple: {
      BorrowShape result = BorrowShape::NoRegion;
      for (const auto child : data.get<TupleTypeData>().elements) {
        auto classified = classifyType(child, input, substitutions, activeNominals, owner, ordinal);
        if (!classified.is<BorrowShape>()) { return classified; }
        result = join(result, classified.get<BorrowShape>());
      }
      return result;
    }
    case TypeDataTag::Object: {
      BorrowShape result = BorrowShape::NoRegion;
      for (const auto& field : data.get<ObjectTypeData>().fields) {
        auto classified =
            classifyType(field.type, input, substitutions, activeNominals, owner, ordinal);
        if (!classified.is<BorrowShape>()) { return classified; }
        result = join(result, classified.get<BorrowShape>());
      }
      return result;
    }
    case TypeDataTag::DynamicArray: {
      auto classified = classifyType(data.get<DynamicArrayTypeData>().element, input, substitutions,
                                     activeNominals, owner, ordinal);
      if (!classified.is<BorrowShape>()) { return classified; }
      return contained(classified.get<BorrowShape>());
    }
    case TypeDataTag::FixedArray: {
      auto classified = classifyType(data.get<FixedArrayTypeData>().element, input, substitutions,
                                     activeNominals, owner, ordinal);
      if (!classified.is<BorrowShape>()) { return classified; }
      return contained(classified.get<BorrowShape>());
    }
    case TypeDataTag::Union: {
      BorrowShape result = BorrowShape::NoRegion;
      for (const auto child : data.get<UnionTypeData>().alternatives) {
        auto classified = classifyType(child, input, substitutions, activeNominals, owner, ordinal);
        if (!classified.is<BorrowShape>()) { return classified; }
        result = join(result, classified.get<BorrowShape>());
      }
      return result;
    }
    case TypeDataTag::Nominal: {
      const auto& nominalType = data.get<NominalTypeData>();
      for (const auto active : activeNominals) {
        if (active == nominalType.definition) { return BorrowShape::NoRegion; }
      }
      auto nominal = findSignature(nominalType.definition, input);
      if (nominal == zc::none) {
        zc::Maybe<identity::DefId> ownerValue = owner;
        return checkerFailure(input, signature::CheckerInvariantKind::MissingRequiredFact,
                              zc::mv(ownerValue), zc::none, ordinal);
      }
      zc::Maybe<const signature::NominalSignature&> nominalPayload;
      ZC_IF_SOME(nominalValue, nominal) {
        if (!nominalValue.payload.variant().is<signature::NominalSignature>()) {
          zc::Maybe<identity::DefId> ownerValue = owner;
          return checkerFailure(input, signature::CheckerInvariantKind::InvalidFact,
                                zc::mv(ownerValue), zc::none, ordinal);
        }
        nominalPayload = nominalValue.payload.variant().get<signature::NominalSignature>();
      }
      ZC_IF_SOME(payload, nominalPayload) {
        if (payload.genericParameters.size() != nominalType.arguments.size()) {
          zc::Maybe<identity::DefId> ownerValue = owner;
          return checkerFailure(input, signature::CheckerInvariantKind::InvalidFact,
                                zc::mv(ownerValue), zc::none, ordinal);
        }
        zc::Vector<TypeSubstitution> nested(substitutions.size() +
                                            payload.genericParameters.size());
        for (const auto& item : substitutions) {
          nested.add(TypeSubstitution{item.parameter.clone(), item.argument});
        }
        for (size_t index = 0; index < payload.genericParameters.size(); ++index) {
          nested.add(TypeSubstitution{payload.genericParameters[index].parameter.clone(),
                                      nominalType.arguments[index]});
        }
        activeNominals.add(nominalType.definition);
        BorrowShape result = BorrowShape::NoRegion;
        for (const auto field : payload.fields) {
          auto fieldSignature = findSignature(field, input);
          if (fieldSignature == zc::none) {
            activeNominals.removeLast();
            zc::Maybe<identity::DefId> ownerValue = owner;
            return checkerFailure(input, signature::CheckerInvariantKind::MissingRequiredFact,
                                  zc::mv(ownerValue), zc::none, ordinal);
          }
          identity::SemanticTypeId fieldType;
          ZC_IF_SOME(fieldValue, fieldSignature) {
            if (!fieldValue.payload.variant().is<signature::ValueSignature>()) {
              activeNominals.removeLast();
              zc::Maybe<identity::DefId> ownerValue = owner;
              return checkerFailure(input, signature::CheckerInvariantKind::InvalidFact,
                                    zc::mv(ownerValue), zc::none, ordinal);
            }
            fieldType = fieldValue.payload.variant().get<signature::ValueSignature>().type;
          }
          auto classified =
              classifyType(fieldType, input, nested.asPtr(), activeNominals, owner, ordinal);
          if (!classified.is<BorrowShape>()) {
            activeNominals.removeLast();
            return classified;
          }
          result = join(result, classified.get<BorrowShape>());
        }
        activeNominals.removeLast();
        return result;
      }
      ZC_UNREACHABLE
    }
  }
  ZC_UNREACHABLE
}

struct EncodedSummary final {
  BorrowSignatureSummary summary;
  zc::Array<uint8_t> bytes;
};

bool bytesLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return less(left, right);
}

template <typename Value, typename Less>
void insertionSort(zc::Vector<Value>& values, Less&& comparator) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && comparator(current, values[insertion - 1])) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

}  // namespace

struct VerifiedBorrowInterfaceSurface::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
       signature::SignatureFactsRevision signatureRevision,
       cross_module::ImportedSignatureViewRevision importedRevision,
       zc::Vector<BorrowSignatureSummary>&& summaries, BorrowInterfaceRevision revision)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        signatureRevision(signatureRevision),
        importedRevision(importedRevision),
        summaries(zc::mv(summaries)),
        revision(revision) {}

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  signature::SignatureFactsRevision signatureRevision;
  cross_module::ImportedSignatureViewRevision importedRevision;
  zc::Vector<BorrowSignatureSummary> summaries;
  BorrowInterfaceRevision revision;
};

VerifiedBorrowInterfaceSurface::VerifiedBorrowInterfaceSurface(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBorrowInterfaceSurface::~VerifiedBorrowInterfaceSurface() noexcept(false) = default;
VerifiedBorrowInterfaceSurface::VerifiedBorrowInterfaceSurface(
    VerifiedBorrowInterfaceSurface&&) noexcept = default;
VerifiedBorrowInterfaceSurface& VerifiedBorrowInterfaceSurface::operator=(
    VerifiedBorrowInterfaceSurface&&) noexcept = default;
identity::SemanticContextBrand VerifiedBorrowInterfaceSurface::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::ContextFingerprint& VerifiedBorrowInterfaceSurface::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedBorrowInterfaceSurface::module() const noexcept { return impl->module; }
const signature::SignatureFactsRevision& VerifiedBorrowInterfaceSurface::signatureFactsRevision()
    const noexcept {
  return impl->signatureRevision;
}
const cross_module::ImportedSignatureViewRevision&
VerifiedBorrowInterfaceSurface::importedSignatureViewRevision() const noexcept {
  return impl->importedRevision;
}
zc::ArrayPtr<const BorrowSignatureSummary> VerifiedBorrowInterfaceSurface::summaries()
    const noexcept {
  return impl->summaries;
}
const BorrowInterfaceRevision& VerifiedBorrowInterfaceSurface::revision() const noexcept {
  return impl->revision;
}
VerifiedBorrowInterfaceSurface VerifiedBorrowInterfaceSurface::clone() const {
  zc::Vector<BorrowSignatureSummary> summaries;
  for (const auto& summary : impl->summaries) { summaries.add(summary.clone()); }
  return VerifiedBorrowInterfaceSurface(zc::heap<Impl>(
      impl->semanticContext, impl->contextFingerprint.clone(), impl->module,
      impl->signatureRevision, impl->importedRevision, zc::mv(summaries), impl->revision));
}

BorrowInterfaceBuildResult BorrowInterfaceBuilder::build(const BorrowInterfaceBuildInput& input) {
  if (!input.semanticContext.isValid() || !input.module.belongsTo(input.semanticContext) ||
      input.identities.semanticContext() != input.semanticContext ||
      input.identities.module(input.module) == zc::none ||
      input.semanticTypes.context() != input.semanticContext) {
    zc::Vector<signature::CheckerVerificationFailure> failures;
    failures.add(signature::CheckerVerificationFailure(checkerFailure(
        input, signature::CheckerInvariantKind::InputReceiptMismatch, zc::none, zc::none, 0)));
    return BorrowInterfaceInvariantRejected{zc::mv(failures)};
  }

  zc::Vector<EncodedSummary> encoded;
  zc::Vector<BorrowSignatureFailure> sourceFailures;
  uint32_t ordinal = 0;
  const auto process = [&](const signature::SemanticSignature& semanticSignature)
      -> zc::Maybe<signature::CheckerVerificationFailure> {
    if (!semanticSignature.payload.variant().is<signature::CallableSignature>()) {
      return zc::none;
    }
    for (const auto& existing : encoded) {
      if (existing.summary.callable == semanticSignature.definition) {
        return signature::CheckerVerificationFailure(checkerFailure(
            input, signature::CheckerInvariantKind::AdditionalFact,
            zc::Maybe<identity::DefId>(semanticSignature.definition),
            zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
      }
    }
    const auto& callable = semanticSignature.payload.variant().get<signature::CallableSignature>();
    zc::Vector<BorrowInputRegion> directInputs;
    bool hasReceiver = false;
    ZC_IF_SOME(receiver, callable.receiver) {
      const auto& scope = semanticSignature.scope.variant();
      if (!scope.is<signature::MemberSignatureScope>()) {
        return signature::CheckerVerificationFailure(checkerFailure(
            input, signature::CheckerInvariantKind::InvalidFact,
            zc::Maybe<identity::DefId>(semanticSignature.definition),
            zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
      }
      const auto owner = scope.get<signature::MemberSignatureScope>().owner;
      auto ownerSignature = findSignature(owner, input);
      if (ownerSignature == zc::none) {
        return signature::CheckerVerificationFailure(checkerFailure(
            input, signature::CheckerInvariantKind::InvalidFact,
            zc::Maybe<identity::DefId>(semanticSignature.definition),
            zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
      }
      ZC_IF_SOME(ownerValue, ownerSignature) {
        const auto& payload = ownerValue.payload.variant();
        if (!payload.is<signature::NominalSignature>() &&
            !payload.is<signature::InterfaceSignature>()) {
          return signature::CheckerVerificationFailure(checkerFailure(
              input, signature::CheckerInvariantKind::InvalidFact,
              zc::Maybe<identity::DefId>(semanticSignature.definition),
              zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
        }
      }
      if (receiver.mode == signature::ReceiverMode::Shared ||
          receiver.mode == signature::ReceiverMode::Mutable) {
        directInputs.add(BorrowInputRegion::receiver());
        hasReceiver = true;
      }
    }

    bool hiddenInputRegion = false;
    bool hasNonNoRegionInput = hasReceiver;
    zc::Vector<identity::DefId> activeNominals;
    const zc::ArrayPtr<const TypeSubstitution> noSubstitutions;
    for (size_t index = 0; index < callable.parameters.size(); ++index) {
      const auto& parameter = callable.parameters[index];
      auto classified = classifyType(parameter.type, input, noSubstitutions, activeNominals,
                                     semanticSignature.definition, ordinal);
      if (classified.is<identity::IdentityInvariant>()) {
        return signature::CheckerVerificationFailure(
            zc::mv(classified.get<identity::IdentityInvariant>()));
      }
      if (classified.is<signature::CheckerInvariantFact>()) {
        return signature::CheckerVerificationFailure(
            zc::mv(classified.get<signature::CheckerInvariantFact>()));
      }
      const auto shape = classified.get<BorrowShape>();
      hasNonNoRegionInput = hasNonNoRegionInput || shape != BorrowShape::NoRegion;
      hiddenInputRegion = hiddenInputRegion || shape == BorrowShape::NestedRegion ||
                          shape == BorrowShape::ParametricRegion ||
                          shape == BorrowShape::OpaqueRegion;
      if (parameter.mode == signature::ParameterMode::SharedReference ||
          parameter.mode == signature::ParameterMode::MutableReference) {
        auto parameterType = input.semanticTypes.get(parameter.type);
        if (!parameterType.is<type::SemanticTypeLookup>() ||
            !parameterType.get<type::SemanticTypeLookup>()
                 .data()
                 .is<type::semantic::ReferenceTypeData>()) {
          return signature::CheckerVerificationFailure(checkerFailure(
              input, signature::CheckerInvariantKind::InvalidFact,
              zc::Maybe<identity::DefId>(semanticSignature.definition),
              zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
        }
        const auto required = parameter.mode == signature::ParameterMode::MutableReference
                                  ? type::semantic::Mutability::Mutable
                                  : type::semantic::Mutability::Const;
        if (parameterType.get<type::SemanticTypeLookup>()
                .data()
                .get<type::semantic::ReferenceTypeData>()
                .mutability != required) {
          return signature::CheckerVerificationFailure(checkerFailure(
              input, signature::CheckerInvariantKind::InvalidFact,
              zc::Maybe<identity::DefId>(semanticSignature.definition),
              zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
        }
        if (index > 0xffffffffULL) {
          return signature::CheckerVerificationFailure(checkerFailure(
              input, signature::CheckerInvariantKind::InvalidFact,
              zc::Maybe<identity::DefId>(semanticSignature.definition),
              zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
        }
        directInputs.add(BorrowInputRegion::parameter(static_cast<uint32_t>(index)));
      }
    }

    auto resultShape = classifyType(callable.success, input, noSubstitutions, activeNominals,
                                    semanticSignature.definition, ordinal);
    if (resultShape.is<identity::IdentityInvariant>()) {
      return signature::CheckerVerificationFailure(
          zc::mv(resultShape.get<identity::IdentityInvariant>()));
    }
    if (resultShape.is<signature::CheckerInvariantFact>()) {
      return signature::CheckerVerificationFailure(
          zc::mv(resultShape.get<signature::CheckerInvariantFact>()));
    }

    const auto result = resultShape.get<BorrowShape>();
    if (callable.abi != zc::none && (hasNonNoRegionInput || result != BorrowShape::NoRegion)) {
      sourceFailures.add(BorrowSignatureFailure{
          BorrowSignatureFailureKind::UnverifiedExternContract, semanticSignature.definition,
          semanticSignature.declarationSpan.clone(), semanticSignature.declarationSpan.clone(),
          ordinal++});
      return zc::none;
    }

    auto returnRelation = BorrowReturnRelation::none();
    if (result == BorrowShape::DirectRootRegion) {
      if (hiddenInputRegion) {
        sourceFailures.add(BorrowSignatureFailure{
            BorrowSignatureFailureKind::UnexpressibleResult, semanticSignature.definition,
            semanticSignature.declarationSpan.clone(), semanticSignature.declarationSpan.clone(),
            ordinal++});
        return zc::none;
      }
      if (directInputs.size() == 1) {
        returnRelation = BorrowReturnRelation::directRoot(directInputs[0]);
      } else if (directInputs.size() > 1 && hasReceiver) {
        returnRelation = BorrowReturnRelation::directRoot(BorrowInputRegion::receiver());
      } else {
        sourceFailures.add(BorrowSignatureFailure{
            BorrowSignatureFailureKind::AmbiguousDirectResult, semanticSignature.definition,
            semanticSignature.declarationSpan.clone(), semanticSignature.declarationSpan.clone(),
            ordinal++});
        return zc::none;
      }
    } else if (result != BorrowShape::NoRegion) {
      sourceFailures.add(BorrowSignatureFailure{
          BorrowSignatureFailureKind::UnexpressibleResult, semanticSignature.definition,
          semanticSignature.declarationSpan.clone(), semanticSignature.declarationSpan.clone(),
          ordinal++});
      return zc::none;
    }

    BorrowSignatureSummary summary{semanticSignature.definition, zc::mv(directInputs),
                                   returnRelation};
    auto bytes = BorrowSignatureCanonicalCodec::encode(summary, input.identities);
    if (bytes == zc::none) {
      return signature::CheckerVerificationFailure(checkerFailure(
          input, signature::CheckerInvariantKind::CanonicalCodecMismatch,
          zc::Maybe<identity::DefId>(semanticSignature.definition),
          zc::Maybe<identity::SourceSpan>(semanticSignature.declarationSpan.clone()), ordinal));
    }
    ZC_IF_SOME(value, bytes) { encoded.add(EncodedSummary{zc::mv(summary), zc::mv(value)}); }
    ++ordinal;
    return zc::none;
  };

  for (const auto& definition : input.definitions) {
    ZC_IF_SOME(failure, process(definition)) {
      zc::Vector<signature::CheckerVerificationFailure> failures;
      failures.add(zc::mv(failure));
      return BorrowInterfaceInvariantRejected{zc::mv(failures)};
    }
  }
  for (const auto& definition : input.supportDefinitions) {
    ZC_IF_SOME(failure, process(definition)) {
      zc::Vector<signature::CheckerVerificationFailure> failures;
      failures.add(zc::mv(failure));
      return BorrowInterfaceInvariantRejected{zc::mv(failures)};
    }
  }
  if (sourceFailures.size() != 0) { return BorrowInterfaceSourceRejected{zc::mv(sourceFailures)}; }

  insertionSort(encoded, [](const auto& left, const auto& right) {
    return bytesLess(left.bytes.asPtr(), right.bytes.asPtr());
  });
  zc::Vector<zc::ArrayPtr<const uint8_t>> recordViews(encoded.size());
  for (const auto& item : encoded) { recordViews.add(item.bytes.asPtr()); }
  auto moduleKey = input.identities.module(input.module);
  if (moduleKey == zc::none) {
    zc::Vector<signature::CheckerVerificationFailure> failures;
    failures.add(signature::CheckerVerificationFailure(checkerFailure(
        input, signature::CheckerInvariantKind::InputReceiptMismatch, zc::none, zc::none, 0)));
    return BorrowInterfaceInvariantRejected{zc::mv(failures)};
  }
  ZC_IF_SOME(module, moduleKey) {
    const auto moduleBytes = module.key().encode();
    auto revision = BorrowInterfaceRevision::computeFramed(
        input.contextFingerprint.digest(), moduleBytes.asPtr(),
        input.signatureFactsRevision.digest(), input.importedSignatureViewRevision.digest(),
        recordViews.asPtr());
    if (revision == zc::none) {
      zc::Vector<signature::CheckerVerificationFailure> failures;
      failures.add(signature::CheckerVerificationFailure(checkerFailure(
          input, signature::CheckerInvariantKind::CanonicalCodecMismatch, zc::none, zc::none, 0)));
      return BorrowInterfaceInvariantRejected{zc::mv(failures)};
    }
    zc::Vector<BorrowSignatureSummary> summaries(encoded.size());
    for (auto& item : encoded) { summaries.add(zc::mv(item.summary)); }
    ZC_IF_SOME(revisionValue, revision) {
      return VerifiedBorrowInterfaceSurface(zc::heap<VerifiedBorrowInterfaceSurface::Impl>(
          input.semanticContext, input.contextFingerprint.clone(), input.module,
          input.signatureFactsRevision, input.importedSignatureViewRevision, zc::mv(summaries),
          revisionValue));
    }
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::borrow
