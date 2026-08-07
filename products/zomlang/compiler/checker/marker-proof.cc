// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/marker-proof.h"

#include <cstdint>

#include "zc/core/array.h"
#include "zomlang/compiler/checker/body-checker.h"

namespace zomlang::compiler::checker::marker {
namespace {

using signature::CheckerInvariantFact;
using signature::CheckerInvariantKind;
using signature::CheckerInvariantStage;
using signature::CheckerVerificationFailure;
using signature::MarkerComponentEvidence;
using signature::MarkerComponentStep;
using signature::MarkerFact;
using signature::MarkerFactKey;
using signature::MarkerPolicy;
using signature::MarkerStructuralSubject;
using signature::Polarity;
using signature::SemanticSignature;

constexpr uint32_t kMaximumRebuildDepth = 256;
constexpr uint32_t kMaximumRebuildNodes = 65536;

bool sameKey(const MarkerFactKey& left, const MarkerFactKey& right) {
  return left.marker == right.marker && left.subject == right.subject;
}

CheckerVerificationFailure invariant(identity::ModuleId module, identity::DefId marker,
                                     CheckerInvariantKind kind, uint32_t ordinal = 0) {
  zc::Maybe<identity::DefId> owner = marker;
  zc::Maybe<ast::NodeId> noNode;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  return CheckerVerificationFailure(CheckerInvariantFact{
      kind, CheckerInvariantStage::Verification, module, zc::mv(owner), zc::mv(noNode),
      zc::mv(noSpan), zc::mv(noPath), zc::mv(noExpected), zc::mv(noActual), ordinal});
}

MarkerProofResult reject(identity::ModuleId module, identity::DefId marker,
                         CheckerInvariantKind kind, uint32_t ordinal = 0) {
  zc::Vector<CheckerVerificationFailure> failures;
  failures.add(invariant(module, marker, kind, ordinal));
  return MarkerProofInvariantRejected{zc::mv(failures)};
}

bool contains(zc::ArrayPtr<const MarkerStructuralSubject> values,
              MarkerStructuralSubject expected) {
  for (const auto value : values) {
    if (value == expected) return true;
  }
  return false;
}

bool contains(zc::ArrayPtr<const signature::PrimitiveKind> values,
              signature::PrimitiveKind expected) {
  for (const auto value : values) {
    if (value == expected) return true;
  }
  return false;
}

zc::Maybe<identity::DefId> referenceMarker(const MarkerPolicy& policy,
                                           signature::Mutability mutability) {
  for (const auto& requirement : policy.referenceRequirements) {
    if (requirement.mutability == mutability) return requirement.requiredMarker;
  }
  return zc::none;
}

zc::Maybe<const SemanticSignature&> localSignature(
    const signature::VerifiedSignatureFacts& signatures, identity::DefId definition) {
  for (const auto& value : signatures.signatures()) {
    if (value.definition == definition) return value;
  }
  return zc::none;
}

struct TypeSubstitution final {
  identity::GenericParameterKey parameter;
  identity::SemanticTypeId argument;
};

class ComponentTypeRebuilder final {
public:
  ComponentTypeRebuilder(const type::SemanticTypeStore& semanticTypes,
                         SemanticTypeInterningCapability& componentInterner,
                         zc::ArrayPtr<const TypeSubstitution> substitutions) noexcept
      : semanticTypes(semanticTypes),
        componentInterner(componentInterner),
        substitutions(substitutions) {}

  zc::Maybe<identity::SemanticTypeId> rebuild(identity::SemanticTypeId source) {
    uint32_t nodes = 0;
    return rebuild(source, 0, nodes);
  }

private:
  zc::Maybe<identity::SemanticTypeId> publish(type::semantic::TypeData&& data) {
    auto admitted = semanticTypes.canonicalizeClosed(zc::mv(data));
    if (!admitted.is<type::semantic::CanonicalTypeData>()) return zc::none;
    auto interned =
        componentInterner.intern(zc::mv(admitted).get<type::semantic::CanonicalTypeData>());
    if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
    return interned.get<type::SemanticTypeInterned>().id;
  }

  zc::Maybe<identity::SemanticTypeId> rebuild(identity::SemanticTypeId source, uint32_t depth,
                                              uint32_t& nodes) {
    if (depth > kMaximumRebuildDepth || nodes == kMaximumRebuildNodes) return zc::none;
    ++nodes;
    auto lookup = semanticTypes.get(source);
    if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
    const auto& data = lookup.get<type::SemanticTypeLookup>().data();
    if (data.is<type::semantic::PrimitiveTypeData>()) return source;
    if (data.is<type::semantic::TypeParameterTypeData>()) {
      const auto& parameter = data.get<type::semantic::TypeParameterTypeData>().parameter;
      for (const auto& substitution : substitutions) {
        if (substitution.parameter == parameter) return substitution.argument;
      }
      return zc::none;
    }
    if (data.is<type::semantic::InterfaceSelfTypeData>()) return zc::none;

    const auto rebuildSequence = [&](zc::ArrayPtr<const identity::SemanticTypeId> values)
        -> zc::Maybe<zc::Vector<identity::SemanticTypeId>> {
      zc::Vector<identity::SemanticTypeId> rebuilt(values.size());
      for (const auto value : values) {
        auto item = rebuild(value, depth + 1, nodes);
        if (item == zc::none) return zc::none;
        ZC_IF_SOME(id, item) { rebuilt.add(id); }
      }
      return rebuilt;
    };

    if (data.is<type::semantic::TupleTypeData>()) {
      auto elements = rebuildSequence(data.get<type::semantic::TupleTypeData>().elements.asPtr());
      if (elements == zc::none) return zc::none;
      ZC_IF_SOME(value, elements) {
        return publish(type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(value)}));
      }
    }
    if (data.is<type::semantic::ObjectTypeData>()) {
      zc::Vector<type::semantic::ObjectFieldData> fields;
      for (const auto& field : data.get<type::semantic::ObjectTypeData>().fields) {
        auto fieldType = rebuild(field.type, depth + 1, nodes);
        if (fieldType == zc::none) return zc::none;
        ZC_IF_SOME(value, fieldType) {
          fields.add(type::semantic::ObjectFieldData{field.name.clone(), value, field.mutability,
                                                     field.presence});
        }
      }
      return publish(type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(fields)}));
    }
    if (data.is<type::semantic::DynamicArrayTypeData>()) {
      auto element =
          rebuild(data.get<type::semantic::DynamicArrayTypeData>().element, depth + 1, nodes);
      if (element == zc::none) return zc::none;
      ZC_IF_SOME(value, element) {
        return publish(type::semantic::TypeData(type::semantic::DynamicArrayTypeData{value}));
      }
    }
    if (data.is<type::semantic::SliceTypeData>()) {
      auto element = rebuild(data.get<type::semantic::SliceTypeData>().element, depth + 1, nodes);
      if (element == zc::none) return zc::none;
      ZC_IF_SOME(value, element) {
        return publish(type::semantic::TypeData(type::semantic::SliceTypeData{value}));
      }
    }
    if (data.is<type::semantic::FixedArrayTypeData>()) {
      const auto& array = data.get<type::semantic::FixedArrayTypeData>();
      auto element = rebuild(array.element, depth + 1, nodes);
      if (element == zc::none) return zc::none;
      ZC_IF_SOME(value, element) {
        return publish(
            type::semantic::TypeData(type::semantic::FixedArrayTypeData{value, array.length}));
      }
    }
    if (data.is<type::semantic::FunctionTypeData>()) {
      const auto& function = data.get<type::semantic::FunctionTypeData>();
      auto parameters = rebuildSequence(function.parameters.asPtr());
      auto success = rebuild(function.success, depth + 1, nodes);
      if (parameters == zc::none || success == zc::none) return zc::none;
      zc::Maybe<identity::SemanticTypeId> raises;
      ZC_IF_SOME(value, function.raises) {
        auto rebuilt = rebuild(value, depth + 1, nodes);
        if (rebuilt == zc::none) return zc::none;
        ZC_IF_SOME(id, rebuilt) { raises = id; }
      }
      ZC_IF_SOME(parameterValues, parameters) {
        ZC_IF_SOME(successValue, success) {
          return publish(type::semantic::TypeData(type::semantic::FunctionTypeData{
              zc::mv(parameterValues), successValue, zc::mv(raises)}));
        }
      }
    }
    if (data.is<type::semantic::NominalTypeData>()) {
      const auto& nominal = data.get<type::semantic::NominalTypeData>();
      auto arguments = rebuildSequence(nominal.arguments.asPtr());
      if (arguments == zc::none) return zc::none;
      ZC_IF_SOME(value, arguments) {
        return publish(type::semantic::TypeData(
            type::semantic::NominalTypeData{nominal.definition, zc::mv(value)}));
      }
    }
    if (data.is<type::semantic::UnionTypeData>()) {
      auto alternatives =
          rebuildSequence(data.get<type::semantic::UnionTypeData>().alternatives.asPtr());
      if (alternatives == zc::none) return zc::none;
      ZC_IF_SOME(value, alternatives) {
        return publish(type::semantic::TypeData(type::semantic::UnionTypeData{zc::mv(value)}));
      }
    }
    if (data.is<type::semantic::IntersectionTypeData>()) {
      auto conjuncts =
          rebuildSequence(data.get<type::semantic::IntersectionTypeData>().conjuncts.asPtr());
      if (conjuncts == zc::none) return zc::none;
      ZC_IF_SOME(value, conjuncts) {
        return publish(
            type::semantic::TypeData(type::semantic::IntersectionTypeData{zc::mv(value)}));
      }
    }
    if (data.is<type::semantic::ReferenceTypeData>()) {
      const auto& reference = data.get<type::semantic::ReferenceTypeData>();
      auto referent = rebuild(reference.referent, depth + 1, nodes);
      if (referent == zc::none) return zc::none;
      ZC_IF_SOME(value, referent) {
        return publish(type::semantic::TypeData(
            type::semantic::ReferenceTypeData{reference.mutability, value}));
      }
    }
    if (data.is<type::semantic::RawPointerTypeData>()) {
      const auto& pointer = data.get<type::semantic::RawPointerTypeData>();
      auto pointee = rebuild(pointer.pointee, depth + 1, nodes);
      if (pointee == zc::none) return zc::none;
      ZC_IF_SOME(value, pointee) {
        return publish(type::semantic::TypeData(
            type::semantic::RawPointerTypeData{pointer.mutability, value}));
      }
    }
    if (data.is<type::semantic::ExistentialTypeData>()) {
      const auto& existential = data.get<type::semantic::ExistentialTypeData>();
      auto rebuildInterface = [&](const type::semantic::ExistentialInterfaceData& interface)
          -> zc::Maybe<type::semantic::ExistentialInterfaceData> {
        auto arguments = rebuildSequence(interface.arguments.asPtr());
        if (arguments == zc::none) return zc::none;
        ZC_IF_SOME(value, arguments) {
          return type::semantic::ExistentialInterfaceData{interface.definition, zc::mv(value)};
        }
        return zc::none;
      };
      auto principal = rebuildInterface(existential.principal);
      if (principal == zc::none) return zc::none;
      zc::Vector<type::semantic::ExistentialInterfaceData> additional;
      for (const auto& interface : existential.additionalInterfaces) {
        auto rebuilt = rebuildInterface(interface);
        if (rebuilt == zc::none) return zc::none;
        ZC_IF_SOME(value, rebuilt) { additional.add(zc::mv(value)); }
      }
      zc::Vector<identity::DefId> markers(existential.markers.size());
      for (const auto marker : existential.markers) { markers.add(marker); }
      zc::Vector<type::semantic::AssociatedTypeBindingData> bindings;
      for (const auto& binding : existential.associatedBindings) {
        auto bindingType = rebuild(binding.type, depth + 1, nodes);
        if (bindingType == zc::none) return zc::none;
        ZC_IF_SOME(value, bindingType) {
          bindings.add(type::semantic::AssociatedTypeBindingData{binding.associated, value});
        }
      }
      ZC_IF_SOME(principalValue, principal) {
        return publish(type::semantic::TypeData(type::semantic::ExistentialTypeData{
            zc::mv(principalValue), zc::mv(additional), zc::mv(markers), zc::mv(bindings)}));
      }
    }
    if (data.is<type::semantic::InterfaceBoundTypeData>()) {
      const auto& interface = data.get<type::semantic::InterfaceBoundTypeData>().interface;
      auto arguments = rebuildSequence(interface.arguments.asPtr());
      if (arguments == zc::none) return zc::none;
      ZC_IF_SOME(value, arguments) {
        return publish(type::semantic::TypeData(type::semantic::InterfaceBoundTypeData{
            type::semantic::InterfaceInstantiation{interface.interface, zc::mv(value)}}));
      }
    }
    return zc::none;
  }

  const type::SemanticTypeStore& semanticTypes;
  SemanticTypeInterningCapability& componentInterner;
  zc::ArrayPtr<const TypeSubstitution> substitutions;
};

}  // namespace

struct SemanticTypeInterningCapability::Impl final {
  explicit Impl(type::SemanticTypeStore& semanticTypes) noexcept : semanticTypes(semanticTypes) {}

  type::SemanticTypeStore& semanticTypes;
};

SemanticTypeInterningCapability::SemanticTypeInterningCapability(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
SemanticTypeInterningCapability::~SemanticTypeInterningCapability() noexcept(false) = default;
SemanticTypeInterningCapability::SemanticTypeInterningCapability(
    SemanticTypeInterningCapability&&) noexcept = default;
SemanticTypeInterningCapability& SemanticTypeInterningCapability::operator=(
    SemanticTypeInterningCapability&&) noexcept = default;

type::SemanticTypeInternResult SemanticTypeInterningCapability::intern(
    type::semantic::CanonicalTypeData&& canonical) {
  return impl->semanticTypes.intern(zc::mv(canonical));
}

struct MarkerProofInput::Impl final {
  Impl(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
       const CheckerIdentityAuthority& identities,
       const signature::VerifiedMarkerPolicyRegistry& policy,
       const signature::VerifiedSignatureFacts& localSignatures,
       const cross_module::ImportedSignatureView& importedSignatures,
       const coherence::FrozenCoherenceView& coherence,
       const type::SemanticTypeStore& semanticTypes,
       SemanticTypeInterningCapability&& componentInterner) noexcept
      : boundModule(boundModule),
        identities(identities),
        policy(policy),
        localSignatures(localSignatures),
        importedSignatures(importedSignatures),
        coherence(coherence),
        semanticTypes(semanticTypes),
        componentInterner(zc::mv(componentInterner)) {}

  const driver::module_graph_query::CheckerBoundModuleView& boundModule;
  const CheckerIdentityAuthority& identities;
  const signature::VerifiedMarkerPolicyRegistry& policy;
  const signature::VerifiedSignatureFacts& localSignatures;
  const cross_module::ImportedSignatureView& importedSignatures;
  const coherence::FrozenCoherenceView& coherence;
  const type::SemanticTypeStore& semanticTypes;
  SemanticTypeInterningCapability componentInterner;
};

MarkerProofInput::MarkerProofInput(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
MarkerProofInput::~MarkerProofInput() noexcept(false) = default;
MarkerProofInput::MarkerProofInput(MarkerProofInput&&) noexcept = default;
MarkerProofInput& MarkerProofInput::operator=(MarkerProofInput&&) noexcept = default;

zc::Maybe<MarkerProofInput> MarkerProofInput::from(
    const body::BodyCheckingInput& bodyInput,
    const signature::VerifiedMarkerPolicyRegistry& policy) {
  const auto context = bodyInput.boundModule.semanticContext();
  const auto module = bodyInput.boundModule.module();
  const auto& parsedModule = bodyInput.boundModule.parsedModule();
  if (!context.isValid() || policy.semanticContext() != context ||
      bodyInput.identities.semanticContext() != context ||
      bodyInput.signatureFacts.semanticContext() != context ||
      bodyInput.importedSignatures.semanticContext() != context ||
      bodyInput.coherence.semanticContext() != context ||
      bodyInput.semanticTypes.context() != context ||
      bodyInput.requirements.semanticContext() != context ||
      bodyInput.signatureFacts.module() != module ||
      bodyInput.importedSignatures.requester() != module ||
      bodyInput.requirements.module() != module ||
      bodyInput.identities.boundModule(module) == zc::none ||
      bodyInput.boundModule.bindingSurface().revision().digest() !=
          bodyInput.signatureFacts.bindingSurfaceRevision().digest() ||
      bodyInput.boundModule.semanticFingerprint().digest() !=
          policy.contextFingerprint().digest() ||
      bodyInput.boundModule.semanticFingerprint().digest() !=
          bodyInput.signatureFacts.contextFingerprint().digest() ||
      bodyInput.boundModule.semanticFingerprint().digest() !=
          bodyInput.importedSignatures.contextFingerprint().digest() ||
      bodyInput.boundModule.semanticFingerprint().digest() !=
          bodyInput.coherence.contextFingerprint().digest() ||
      parsedModule.contentDigest() != bodyInput.signatureFacts.sourceContentDigest() ||
      parsedModule.contentDigest() != bodyInput.requirements.sourceContentDigest() ||
      parsedModule.receipt().digest() != bodyInput.signatureFacts.parsedModuleReceipt().digest() ||
      parsedModule.receipt().digest() != bodyInput.requirements.parsedModuleReceipt().digest() ||
      bodyInput.signatureFacts.markerPolicyRegistryRevision().digest() !=
          policy.revision().digest() ||
      bodyInput.coherence.markerPolicyRegistryRevision().digest() != policy.revision().digest()) {
    return zc::none;
  }
  SemanticTypeInterningCapability componentInterner(
      zc::heap<SemanticTypeInterningCapability::Impl>(bodyInput.semanticTypes));
  return MarkerProofInput(zc::heap<MarkerProofInput::Impl>(
      bodyInput.boundModule, bodyInput.identities, policy, bodyInput.signatureFacts,
      bodyInput.importedSignatures, bodyInput.coherence, bodyInput.semanticTypes,
      zc::mv(componentInterner)));
}

struct MarkerProofEngine::Impl final {
  explicit Impl(MarkerProofInput&& input) noexcept
      : boundModule(input.impl->boundModule),
        identities(input.impl->identities),
        policy(input.impl->policy),
        localSignatures(input.impl->localSignatures),
        importedSignatures(input.impl->importedSignatures),
        coherence(input.impl->coherence),
        semanticTypes(input.impl->semanticTypes),
        componentInterner(zc::mv(input.impl->componentInterner)) {}

  zc::Maybe<const SemanticSignature&> signature(identity::DefId definition) const {
    auto local = localSignature(localSignatures, definition);
    auto imported = importedSignatures.supportDefinition(definition);
    if (local != zc::none && imported != zc::none) return zc::none;
    ZC_IF_SOME(value, local) { return value; }
    ZC_IF_SOME(value, imported) { return value; }
    return zc::none;
  }

  zc::Maybe<identity::DefId> materializedDefinitionForType(identity::DefId definition) const {
    auto entry = identities.definition(definition);
    ZC_IF_SOME(value, entry) { return value.handle(); }
    return zc::none;
  }

  zc::Maybe<const identity::DefinitionKey&> definitionKey(identity::DefId definition) const {
    auto entry = identities.definition(definition);
    ZC_IF_SOME(value, entry) { return value.key(); }
    return zc::none;
  }

  bool genericSubstitutions(identity::DefId definition,
                            const signature::NominalSignature& nominalSignature,
                            const type::semantic::NominalTypeData& subject,
                            zc::Vector<TypeSubstitution>& output) const {
    if (nominalSignature.genericParameters.size() != subject.arguments.size()) return false;
    auto ownerKey = definitionKey(definition);
    if (ownerKey == zc::none) return false;
    for (size_t index = 0; index < nominalSignature.genericParameters.size(); ++index) {
      const auto& generic = nominalSignature.genericParameters[index];
      if (generic.index != index) return false;
      for (size_t previous = 0; previous < index; ++previous) {
        if (nominalSignature.genericParameters[previous].parameter == generic.parameter) {
          return false;
        }
      }
      auto parameter = identities.genericParameter(generic.parameter);
      if (parameter == zc::none) return false;
      ZC_IF_SOME(value, parameter) {
        const auto& record = value.record();
        auto owner = record.owner().definitionKey();
        if (record.kind() != identity::GenericParameterKind::Type || record.ordinal() != index ||
            owner == zc::none) {
          return false;
        }
        bool matchesOwner = false;
        ZC_IF_SOME(key, owner) {
          ZC_IF_SOME(expected, ownerKey) { matchesOwner = key == expected; }
        }
        if (!matchesOwner) return false;
      }
      output.add(TypeSubstitution{generic.parameter.clone(), subject.arguments[index]});
    }
    return true;
  }

  MarkerProofResult resolveComponent(identity::DefId marker, identity::SemanticTypeId type,
                                     MarkerComponentStep&& step, zc::Vector<MarkerFactKey>& active,
                                     zc::Vector<MarkerComponentEvidence>& components) {
    auto support = resolve(marker, type, active);
    if (support.is<MarkerProofInvariantRejected>()) {
      return zc::mv(support).get<MarkerProofInvariantRejected>();
    }
    if (!support.is<MarkerProofPositive>()) return MarkerProofUnsatisfied{};
    const auto& proof = support.get<MarkerProofPositive>().proof;
    const MarkerFactKey expected{marker, type};
    if (!sameKey(proof.key, expected) || proof.polarity != Polarity::Positive) {
      return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }
    zc::Vector<MarkerComponentStep> path;
    path.add(zc::mv(step));
    components.add(MarkerComponentEvidence{zc::mv(path), type, proof.key});
    return MarkerProofUnsatisfied{};
  }

  MarkerProofResult structural(identity::DefId marker, identity::SemanticTypeId subject,
                               const MarkerPolicy& markerPolicy,
                               const type::semantic::TypeData& data,
                               zc::Vector<MarkerFactKey>& active) {
    zc::Vector<MarkerComponentEvidence> components;
    const auto addComponent = [&](identity::DefId requiredMarker,
                                  identity::SemanticTypeId componentType,
                                  MarkerComponentStep&& step) -> MarkerProofResult {
      const auto before = components.size();
      auto result =
          resolveComponent(requiredMarker, componentType, zc::mv(step), active, components);
      if (result.is<MarkerProofInvariantRejected>()) return result;
      if (components.size() != before + 1) return MarkerProofUnsatisfied{};
      return MarkerProofPositive{MarkerFact{
          MarkerFactKey{marker, subject}, Polarity::Positive,
          signature::MarkerEvidence(
              signature::StructuralMarkerEvidence{zc::Vector<MarkerComponentEvidence>()}),
          zc::none}};
    };

    if (data.is<type::semantic::TupleTypeData>()) {
      if (!contains(markerPolicy.structuralSubjects.asPtr(), MarkerStructuralSubject::Tuple)) {
        return MarkerProofUnsatisfied{};
      }
      const auto& elements = data.get<type::semantic::TupleTypeData>().elements;
      for (size_t index = 0; index < elements.size(); ++index) {
        if (index > UINT32_MAX) return MarkerProofUnsatisfied{};
        auto result = addComponent(
            marker, elements[index],
            MarkerComponentStep(signature::TupleElementStep{static_cast<uint32_t>(index)}));
        if (!result.is<MarkerProofPositive>()) return result;
      }
    } else if (data.is<type::semantic::ObjectTypeData>()) {
      if (!contains(markerPolicy.structuralSubjects.asPtr(), MarkerStructuralSubject::Object)) {
        return MarkerProofUnsatisfied{};
      }
      for (const auto& field : data.get<type::semantic::ObjectTypeData>().fields) {
        auto result =
            addComponent(marker, field.type,
                         MarkerComponentStep(signature::ObjectFieldStep{field.name.clone()}));
        if (!result.is<MarkerProofPositive>()) return result;
      }
    } else if (data.is<type::semantic::FixedArrayTypeData>()) {
      if (!contains(markerPolicy.structuralSubjects.asPtr(), MarkerStructuralSubject::FixedArray)) {
        return MarkerProofUnsatisfied{};
      }
      auto result = addComponent(marker, data.get<type::semantic::FixedArrayTypeData>().element,
                                 MarkerComponentStep(signature::ArrayElementStep{}));
      if (!result.is<MarkerProofPositive>()) return result;
    } else if (data.is<type::semantic::ReferenceTypeData>()) {
      const auto& reference = data.get<type::semantic::ReferenceTypeData>();
      auto requiredMarker = referenceMarker(markerPolicy, reference.mutability);
      if (requiredMarker == zc::none) return MarkerProofUnsatisfied{};
      ZC_IF_SOME(value, requiredMarker) {
        auto result = addComponent(value, reference.referent,
                                   MarkerComponentStep(signature::ReferenceReferentStep{}));
        if (!result.is<MarkerProofPositive>()) return result;
      }
    } else if (data.is<type::semantic::NominalTypeData>()) {
      const auto& nominalType = data.get<type::semantic::NominalTypeData>();
      auto nominalDefinition = materializedDefinitionForType(nominalType.definition);
      if (nominalDefinition == zc::none) {
        return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
      }
      identity::DefId definition;
      ZC_IF_SOME(value, nominalDefinition) { definition = value; }
      auto signatureValue = signature(definition);
      if (signatureValue == zc::none) {
        return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
      }
      ZC_IF_SOME(selected, signatureValue) {
        if (!selected.payload.variant().is<signature::NominalSignature>()) {
          return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
        }
        const bool isStruct = selected.definitionKind == identity::DefinitionKind::Struct;
        const bool isEnum = selected.definitionKind == identity::DefinitionKind::Enum;
        if ((!isStruct && !isEnum) ||
            (isStruct && !contains(markerPolicy.structuralSubjects.asPtr(),
                                   MarkerStructuralSubject::NominalStruct)) ||
            (isEnum && !contains(markerPolicy.structuralSubjects.asPtr(),
                                 MarkerStructuralSubject::NominalEnum))) {
          return MarkerProofUnsatisfied{};
        }
        const auto& nominalSignature =
            selected.payload.variant().get<signature::NominalSignature>();
        zc::Vector<TypeSubstitution> substitutions;
        if (!genericSubstitutions(definition, nominalSignature, nominalType, substitutions)) {
          return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
        }
        ComponentTypeRebuilder rebuilder(semanticTypes, componentInterner, substitutions.asPtr());
        if (isStruct) {
          for (const auto field : nominalSignature.fields) {
            auto fieldSignature = signature(field);
            if (fieldSignature == zc::none) {
              return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
            }
            ZC_IF_SOME(fieldValue, fieldSignature) {
              if (fieldValue.definitionKind != identity::DefinitionKind::Field ||
                  !fieldValue.payload.variant().is<signature::ValueSignature>() ||
                  !fieldValue.scope.variant().is<signature::MemberSignatureScope>() ||
                  fieldValue.scope.variant().get<signature::MemberSignatureScope>().owner !=
                      definition) {
                return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
              }
              auto component = rebuilder.rebuild(
                  fieldValue.payload.variant().get<signature::ValueSignature>().type);
              if (component == zc::none) {
                return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
              }
              ZC_IF_SOME(value, component) {
                auto result = addComponent(marker, value,
                                           MarkerComponentStep(signature::NominalFieldStep{field}));
                if (!result.is<MarkerProofPositive>()) return result;
              }
            }
          }
        } else {
          for (const auto variant : nominalSignature.variants) {
            auto variantSignature = signature(variant);
            if (variantSignature == zc::none) {
              return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
            }
            ZC_IF_SOME(variantValue, variantSignature) {
              if (variantValue.definitionKind != identity::DefinitionKind::EnumVariant ||
                  !variantValue.payload.variant().is<signature::EnumVariantSignature>() ||
                  !variantValue.scope.variant().is<signature::EnclosedSignatureScope>() ||
                  variantValue.scope.variant().get<signature::EnclosedSignatureScope>().owner !=
                      definition) {
                return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
              }
              const auto& payload =
                  variantValue.payload.variant().get<signature::EnumVariantSignature>().payload;
              for (size_t index = 0; index < payload.size(); ++index) {
                if (index > UINT32_MAX) return MarkerProofUnsatisfied{};
                auto component = rebuilder.rebuild(payload[index]);
                if (component == zc::none) {
                  return reject(localSignatures.module(), marker,
                                CheckerInvariantKind::InvalidFact);
                }
                ZC_IF_SOME(value, component) {
                  auto result = addComponent(marker, value,
                                             MarkerComponentStep(signature::EnumVariantPayloadStep{
                                                 variant, static_cast<uint32_t>(index)}));
                  if (!result.is<MarkerProofPositive>()) return result;
                }
              }
            }
          }
        }
      }
    } else {
      return MarkerProofUnsatisfied{};
    }

    zc::Maybe<identity::SourceSpan> noSpan;
    return MarkerProofPositive{MarkerFact{
        MarkerFactKey{marker, subject}, Polarity::Positive,
        signature::MarkerEvidence(signature::StructuralMarkerEvidence{zc::mv(components)}),
        zc::mv(noSpan)}};
  }

  MarkerProofResult resolve(identity::DefId marker, identity::SemanticTypeId subject,
                            zc::Vector<MarkerFactKey>& active) {
    if (identities.definition(marker) == zc::none) {
      return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }
    auto subjectLookup = semanticTypes.get(subject);
    if (!subjectLookup.is<type::SemanticTypeLookup>()) {
      return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }
    const MarkerFactKey key{marker, subject};
    ZC_IF_SOME(explicitFact, coherence.marker(key)) {
      if (!explicitFact.evidence.variant().is<signature::ExplicitMarkerEvidence>() ||
          explicitFact.declarationSpan == zc::none) {
        return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
      }
      auto record = signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
          explicitFact, identities, semanticTypes);
      if (record == zc::none) {
        return reject(localSignatures.module(), marker,
                      CheckerInvariantKind::CanonicalCodecMismatch);
      }
      if (explicitFact.polarity == Polarity::Positive) {
        return MarkerProofPositive{explicitFact.clone()};
      }
      if (explicitFact.polarity == Polarity::Negative) {
        return MarkerProofNegative{explicitFact.clone()};
      }
      return reject(localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }

    auto markerPolicy = policy.policy(marker);
    if (markerPolicy == zc::none) return MarkerProofUnsatisfied{};
    const auto& data = subjectLookup.get<type::SemanticTypeLookup>().data();
    ZC_IF_SOME(value, markerPolicy) {
      if (data.is<type::semantic::PrimitiveTypeData>()) {
        const auto primitive = data.get<type::semantic::PrimitiveTypeData>().kind;
        if (contains(value.builtinPrimitives.asPtr(), primitive)) {
          zc::Maybe<identity::SourceSpan> noSpan;
          return MarkerProofPositive{
              MarkerFact{MarkerFactKey{marker, subject}, Polarity::Positive,
                         signature::MarkerEvidence(signature::BuiltinMarkerEvidence{primitive}),
                         zc::mv(noSpan)}};
        }
      }
      for (const auto& activeKey : active) {
        if (sameKey(activeKey, key)) return MarkerProofUnsatisfied{};
      }
      active.add(key);
      auto result = structural(marker, subject, value, data, active);
      active.removeLast();
      return result;
    }
    return MarkerProofUnsatisfied{};
  }

  MarkerProofResult resolve(identity::DefId marker, identity::SemanticTypeId subject) {
    zc::Vector<MarkerFactKey> active;
    return resolve(marker, subject, active);
  }

  const driver::module_graph_query::CheckerBoundModuleView& boundModule;
  const CheckerIdentityAuthority& identities;
  const signature::VerifiedMarkerPolicyRegistry& policy;
  const signature::VerifiedSignatureFacts& localSignatures;
  const cross_module::ImportedSignatureView& importedSignatures;
  const coherence::FrozenCoherenceView& coherence;
  const type::SemanticTypeStore& semanticTypes;
  SemanticTypeInterningCapability componentInterner;
};

MarkerProofEngine::MarkerProofEngine(MarkerProofInput&& input)
    : impl(zc::heap<Impl>(zc::mv(input))) {}
MarkerProofEngine::~MarkerProofEngine() noexcept(false) = default;

MarkerProofResult MarkerProofEngine::prove(identity::DefId marker,
                                           identity::SemanticTypeId subject) {
  auto produced = impl->resolve(marker, subject);
  auto verified = impl->resolve(marker, subject);
  if (produced.which() != verified.which()) {
    return reject(impl->localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
  }
  if (produced.is<MarkerProofPositive>()) {
    auto producedRecord = signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        produced.get<MarkerProofPositive>().proof, impl->identities, impl->semanticTypes);
    auto verifiedRecord = signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        verified.get<MarkerProofPositive>().proof, impl->identities, impl->semanticTypes);
    if (producedRecord == zc::none || verifiedRecord == zc::none) {
      return reject(impl->localSignatures.module(), marker,
                    CheckerInvariantKind::CanonicalCodecMismatch);
    }
    bool recordsMatch = false;
    ZC_IF_SOME(left, producedRecord) {
      ZC_IF_SOME(right, verifiedRecord) { recordsMatch = left.asPtr() == right.asPtr(); }
    }
    if (!recordsMatch) {
      return reject(impl->localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }
  } else if (produced.is<MarkerProofNegative>()) {
    auto producedRecord = signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        produced.get<MarkerProofNegative>().explicitFact, impl->identities, impl->semanticTypes);
    auto verifiedRecord = signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        verified.get<MarkerProofNegative>().explicitFact, impl->identities, impl->semanticTypes);
    if (producedRecord == zc::none || verifiedRecord == zc::none) {
      return reject(impl->localSignatures.module(), marker,
                    CheckerInvariantKind::CanonicalCodecMismatch);
    }
    bool recordsMatch = false;
    ZC_IF_SOME(left, producedRecord) {
      ZC_IF_SOME(right, verifiedRecord) { recordsMatch = left.asPtr() == right.asPtr(); }
    }
    if (!recordsMatch) {
      return reject(impl->localSignatures.module(), marker, CheckerInvariantKind::InvalidFact);
    }
  }
  return produced;
}

}  // namespace zomlang::compiler::checker::marker
