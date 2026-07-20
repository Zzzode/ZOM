// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/module-interface.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/checker/borrow-interface.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver {
namespace {

void append(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> bytes) {
  output.addAll(bytes);
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

zc::Maybe<identity::SourceSpan> cloneSpan(const zc::Maybe<identity::SourceSpan>& sourceSpan) {
  ZC_IF_SOME(value, sourceSpan) { return value.clone(); }
  return zc::none;
}

zc::Array<uint8_t> cloneBytes(zc::ArrayPtr<const uint8_t> bytes) {
  zc::Vector<uint8_t> result(bytes.size());
  result.addAll(bytes);
  return result.releaseAsArray();
}

void append(zc::Vector<uint8_t>& output, zc::Array<uint8_t>&& bytes) {
  output.addAll(bytes.asPtr());
}

bool encodeDefinition(zc::Vector<uint8_t>& output, identity::DefId definition,
                      const identity::SemanticIdentityRegistrySet& registries) {
  auto key = registries.definitions().lookup(definition);
  ZC_IF_SOME(value, key) {
    identity::CanonicalEncoder encoder;
    value.encode(encoder);
    append(output, encoder.finish());
    return true;
  }
  return false;
}

bool encodeModule(zc::Vector<uint8_t>& output, identity::ModuleId module,
                  const identity::SemanticIdentityRegistrySet& registries) {
  auto key = registries.modules().lookup(module);
  ZC_IF_SOME(value, key) {
    identity::CanonicalEncoder encoder;
    value.encode(encoder);
    append(output, encoder.finish());
    return true;
  }
  return false;
}

bool encodeBindingTarget(zc::Vector<uint8_t>& output, const binder::BindingTarget& target,
                         const identity::SemanticIdentityRegistrySet& registries) {
  const auto& value = target.value();
  if (value.is<binder::DefinitionBindingTarget>()) {
    output.add(0x01);
    return encodeDefinition(output, value.get<binder::DefinitionBindingTarget>().definition,
                            registries);
  }
  if (value.is<binder::ModuleBindingTarget>()) {
    output.add(0x02);
    return encodeModule(output, value.get<binder::ModuleBindingTarget>().module, registries);
  }
  if (value.is<binder::SemanticImportBindingTarget>()) {
    output.add(0x03);
    append(output, value.get<binder::SemanticImportBindingTarget>().binding.encode());
    return true;
  }
  return false;
}

bool encodeSignatureRootBinding(zc::Vector<uint8_t>& output, const binder::BindingTarget& binding,
                                const identity::SemanticIdentityRegistrySet& registries) {
  if (!module_interface::isSignatureRootBinding(binding)) { return false; }
  return encodeBindingTarget(output, binding, registries);
}

bool encodeVisibility(zc::Vector<uint8_t>& output, const binder::VisibilityEnvelope& visibility,
                      const identity::SemanticIdentityRegistrySet& registries) {
  const auto& value = visibility.value();
  if (value.is<binder::ModuleVisibility>()) {
    output.add(0x01);
    return encodeModule(output, value.get<binder::ModuleVisibility>().module, registries);
  }
  output.add(0x02);
  return true;
}

void encodeName(zc::Vector<uint8_t>& output, const binder::BindingNameKey& name) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(name.nameSpace()));
  name.name().encode(encoder);
  append(output, encoder.finish());
}

void encodeSpan(zc::Vector<uint8_t>& output, const identity::SourceSpan& span) {
  identity::CanonicalEncoder encoder;
  span.encode(encoder);
  append(output, encoder.finish());
}

void encodeOptionalSpan(zc::Vector<uint8_t>& output, const zc::Maybe<identity::SourceSpan>& span) {
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, span) {
    encoder.encodeSome();
    value.encode(encoder);
    append(output, encoder.finish());
    return;
  }
  encoder.encodeNone();
  append(output, encoder.finish());
}

bool encodeTypeEnrichedTarget(zc::Vector<uint8_t>& output, const TypeEnrichedBindingTarget& target,
                              const identity::SemanticIdentityRegistrySet& registries,
                              const type::SemanticTypeStore& semanticTypes) {
  const auto& value = target.variant();
  if (value.is<DefinitionTypeEnrichedTarget>()) {
    const auto& definition = value.get<DefinitionTypeEnrichedTarget>();
    output.add(0x01);
    if (!encodeDefinition(output, definition.definition, registries)) { return false; }
    auto definitionRecord = registries.definitions().lookupRecord(definition.definition);
    if (definitionRecord == zc::none) { return false; }
    ZC_IF_SOME(record, definitionRecord) {
      auto owningModule = registries.modules().find(record.module());
      if (owningModule == zc::none) { return false; }
      ZC_IF_SOME(module, owningModule) {
        auto signature = checker::signature::SignatureFactsCanonicalCodec::encodeSignature(
            definition.signature, module, registries, semanticTypes);
        ZC_IF_SOME(bytes, signature) {
          append(output, zc::mv(bytes));
          return true;
        }
      }
    }
    return false;
  }
  output.add(0x02);
  const auto& module = value.get<ModuleTypeEnrichedTarget>();
  if (!encodeModule(output, module.module, registries)) { return false; }
  output.addAll(module.surfaceRevision.digest().bytes());
  return true;
}

bool canonicalLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

template <typename Value>
struct EncodedProjection final {
  Value value;
  zc::Array<uint8_t> encoded;
};

template <typename Value>
void sortEncoded(zc::Vector<EncodedProjection<Value>>& values) {
  for (size_t outer = 1; outer < values.size(); ++outer) {
    size_t index = outer;
    while (index != 0 &&
           canonicalLess(values[index].encoded.asPtr(), values[index - 1].encoded.asPtr())) {
      auto temporary = zc::mv(values[index]);
      values[index] = zc::mv(values[index - 1]);
      values[index - 1] = zc::mv(temporary);
      --index;
    }
  }
}

bool sameName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return left.nameSpace() == right.nameSpace() && left.name() == right.name();
}

bool containsDefinition(zc::ArrayPtr<const identity::DefId> values, identity::DefId value) {
  for (const auto candidate : values) {
    if (candidate == value) { return true; }
  }
  return false;
}

void addDefinition(zc::Vector<identity::DefId>& values, identity::DefId value) {
  if (!containsDefinition(values.asPtr(), value)) { values.add(value); }
}

zc::Maybe<const checker::signature::SemanticSignature&> findSignature(
    const module_interface::AuthorizedSignatureBundle& bundle, identity::DefId definition) {
  for (const auto& signature : bundle.definitions) {
    if (signature.definition == definition) { return signature; }
  }
  for (const auto& signature : bundle.supportDefinitions) {
    if (signature.definition == definition) { return signature; }
  }
  return zc::none;
}

class SignatureReferenceCollector final {
public:
  SignatureReferenceCollector(const type::SemanticTypeStore& semanticTypes,
                              const identity::SemanticIdentityRegistrySet& registries,
                              zc::Vector<identity::DefId>& definitions) noexcept
      : semanticTypes(semanticTypes), registries(registries), definitions(definitions) {}

  bool collect(const checker::signature::SemanticSignature& signature) {
    const auto& scope = signature.scope.variant();
    if (scope.is<checker::signature::MemberSignatureScope>()) {
      addDefinition(definitions, scope.get<checker::signature::MemberSignatureScope>().owner);
    } else if (scope.is<checker::signature::EnclosedSignatureScope>()) {
      addDefinition(definitions, scope.get<checker::signature::EnclosedSignatureScope>().owner);
    }
    for (const auto& attribute : signature.attributes) {
      addDefinition(definitions, attribute.target);
    }

    const auto& payload = signature.payload.variant();
    if (payload.is<checker::signature::CallableSignature>()) {
      const auto& value = payload.get<checker::signature::CallableSignature>();
      if (!collectGenericParameters(value.genericParameters.asPtr(), signature.definition)) {
        return false;
      }
      ZC_IF_SOME(receiver, value.receiver) {
        ZC_IF_SOME(owner, callableParameterOwner(receiver.parameter)) {
          if (owner != signature.definition) return false;
          addDefinition(definitions, owner);
        } else {
          return false;
        }
      }
      for (const auto& parameter : value.parameters) {
        ZC_IF_SOME(owner, callableParameterOwner(parameter.parameter)) {
          if (owner != signature.definition) return false;
          addDefinition(definitions, owner);
        } else {
          return false;
        }
        if (!collectType(parameter.type)) return false;
      }
      if (!collectType(value.success)) return false;
      ZC_IF_SOME(raises, value.raises) {
        if (!collectType(raises)) return false;
      }
      return true;
    }
    if (payload.is<checker::signature::NominalSignature>()) {
      const auto& value = payload.get<checker::signature::NominalSignature>();
      if (!collectGenericParameters(value.genericParameters.asPtr(), signature.definition)) {
        return false;
      }
      ZC_IF_SOME(base, value.base) {
        if (!collectType(base)) return false;
      }
      for (const auto& interface : value.interfaces) {
        if (!collectInterface(interface)) return false;
      }
      for (const auto field : value.fields) { addDefinition(definitions, field); }
      for (const auto variant : value.variants) { addDefinition(definitions, variant); }
      for (const auto member : value.members) { addDefinition(definitions, member); }
      return true;
    }
    if (payload.is<checker::signature::InterfaceSignature>()) {
      const auto& value = payload.get<checker::signature::InterfaceSignature>();
      if (!collectGenericParameters(value.genericParameters.asPtr(), signature.definition)) {
        return false;
      }
      for (const auto& parent : value.parents) {
        if (!collectInterface(parent)) return false;
      }
      for (const auto member : value.members) { addDefinition(definitions, member); }
      for (const auto associated : value.associatedTypes) {
        addDefinition(definitions, associated);
      }
      for (const auto& cause : value.objectSafetyCauses) {
        const auto& causeValue = cause.variant();
        if (causeValue.is<checker::signature::UnsafeSuperinterfaceCause>()) {
          addDefinition(definitions,
                        causeValue.get<checker::signature::UnsafeSuperinterfaceCause>().interface);
        } else if (causeValue.is<checker::signature::GenericMethodCause>()) {
          addDefinition(definitions,
                        causeValue.get<checker::signature::GenericMethodCause>().method);
        } else if (causeValue.is<checker::signature::ReturnsSelfCause>()) {
          addDefinition(definitions, causeValue.get<checker::signature::ReturnsSelfCause>().method);
        } else if (causeValue.is<checker::signature::MovesSelfCause>()) {
          addDefinition(definitions, causeValue.get<checker::signature::MovesSelfCause>().method);
        } else if (causeValue.is<checker::signature::StaticMethodCause>()) {
          addDefinition(definitions,
                        causeValue.get<checker::signature::StaticMethodCause>().method);
        } else if (causeValue.is<checker::signature::GenericAssociatedTypeCause>()) {
          addDefinition(
              definitions,
              causeValue.get<checker::signature::GenericAssociatedTypeCause>().associated);
        } else {
          const auto& unsized = causeValue.get<checker::signature::UnsizedParameterCause>();
          addDefinition(definitions, unsized.method);
          ZC_IF_SOME(owner, callableParameterOwner(unsized.parameter)) {
            if (owner != unsized.method) return false;
            addDefinition(definitions, owner);
          } else {
            return false;
          }
          if (!collectType(unsized.type)) return false;
        }
      }
      return true;
    }
    if (payload.is<checker::signature::TypeAliasSignature>()) {
      const auto& value = payload.get<checker::signature::TypeAliasSignature>();
      return collectGenericParameters(value.genericParameters.asPtr(), signature.definition) &&
             collectType(value.target);
    }
    if (payload.is<checker::signature::AssociatedTypeSignature>()) {
      const auto& value = payload.get<checker::signature::AssociatedTypeSignature>();
      if (!collectGenericParameters(value.genericParameters.asPtr(), signature.definition)) {
        return false;
      }
      for (const auto& bound : value.bounds) {
        if (!collectInterface(bound)) return false;
      }
      for (const auto marker : value.markerBounds) { addDefinition(definitions, marker); }
      ZC_IF_SOME(defaultType, value.defaultType) {
        if (!collectType(defaultType)) return false;
      }
      return true;
    }
    if (payload.is<checker::signature::ValueSignature>()) {
      const auto& value = payload.get<checker::signature::ValueSignature>();
      if (!collectType(value.type)) return false;
      ZC_IF_SOME(constant, value.constantValue) {
        zc::Vector<identity::DefId> referenced;
        constant.appendReferencedDefinitions(referenced);
        for (const auto definition : referenced) { addDefinition(definitions, definition); }
      }
      return true;
    }
    if (payload.is<checker::signature::EnumVariantSignature>()) {
      for (const auto type : payload.get<checker::signature::EnumVariantSignature>().payload) {
        if (!collectType(type)) return false;
      }
      return true;
    }
    return false;
  }

private:
  bool containsType(identity::SemanticTypeId value) const {
    for (const auto candidate : visitedTypes) {
      if (candidate == value) return true;
    }
    return false;
  }

  zc::Maybe<identity::DefId> genericParameterOwner(const identity::GenericParameterKey& key) const {
    ZC_IF_SOME(parameter, registries.genericParameters().find(key)) {
      ZC_IF_SOME(authority, registries.genericParameters().lookupAuthority(parameter)) {
        if (!authority.verify()) return zc::none;
        ZC_IF_SOME(owner, authority.record().owner().definitionKey()) {
          return registries.definitions().find(owner);
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<identity::DefId> callableParameterOwner(
      const identity::CallableParameterKey& key) const {
    ZC_IF_SOME(parameter, registries.callableParameters().find(key)) {
      ZC_IF_SOME(authority, registries.callableParameters().lookupAuthority(parameter)) {
        if (!authority.verify()) return zc::none;
        return registries.definitions().find(authority.record().owner());
      }
    }
    return zc::none;
  }

  bool collectInterface(const checker::signature::InterfaceInstantiation& interface) {
    addDefinition(definitions, interface.interface);
    for (const auto argument : interface.arguments) {
      if (!collectType(argument)) return false;
    }
    return true;
  }

  bool collectGenericParameters(
      zc::ArrayPtr<const checker::signature::GenericParameterSignature> parameters,
      identity::DefId expectedOwner) {
    for (const auto& parameter : parameters) {
      ZC_IF_SOME(owner, genericParameterOwner(parameter.parameter)) {
        if (owner != expectedOwner) return false;
        addDefinition(definitions, owner);
      } else {
        return false;
      }
      for (const auto& bound : parameter.bounds) {
        if (!collectInterface(bound)) return false;
      }
      for (const auto marker : parameter.markerBounds) { addDefinition(definitions, marker); }
      ZC_IF_SOME(defaultType, parameter.defaultType) {
        if (!collectType(defaultType)) return false;
      }
    }
    return true;
  }

  bool collectType(identity::SemanticTypeId typeId) {
    if (containsType(typeId)) return true;
    visitedTypes.add(typeId);
    auto lookup = semanticTypes.get(typeId);
    if (!lookup.is<type::SemanticTypeLookup>()) return false;
    const auto& data = lookup.get<type::SemanticTypeLookup>().data();
    using namespace type::semantic;
    if (data.is<PrimitiveTypeData>()) return true;
    if (data.is<TupleTypeData>()) {
      for (const auto element : data.get<TupleTypeData>().elements) {
        if (!collectType(element)) return false;
      }
      return true;
    }
    if (data.is<ObjectTypeData>()) {
      for (const auto& field : data.get<ObjectTypeData>().fields) {
        if (!collectType(field.type)) return false;
      }
      return true;
    }
    if (data.is<DynamicArrayTypeData>()) {
      return collectType(data.get<DynamicArrayTypeData>().element);
    }
    if (data.is<SliceTypeData>()) return collectType(data.get<SliceTypeData>().element);
    if (data.is<FixedArrayTypeData>()) {
      return collectType(data.get<FixedArrayTypeData>().element);
    }
    if (data.is<FunctionTypeData>()) {
      const auto& function = data.get<FunctionTypeData>();
      for (const auto parameter : function.parameters) {
        if (!collectType(parameter)) return false;
      }
      if (!collectType(function.success)) return false;
      ZC_IF_SOME(raises, function.raises) {
        if (!collectType(raises)) return false;
      }
      return true;
    }
    if (data.is<NominalTypeData>()) {
      const auto& nominal = data.get<NominalTypeData>();
      addDefinition(definitions, nominal.definition);
      for (const auto argument : nominal.arguments) {
        if (!collectType(argument)) return false;
      }
      return true;
    }
    if (data.is<TypeParameterTypeData>()) {
      ZC_IF_SOME(owner, genericParameterOwner(data.get<TypeParameterTypeData>().parameter)) {
        addDefinition(definitions, owner);
        return true;
      }
      return false;
    }
    if (data.is<UnionTypeData>()) {
      for (const auto alternative : data.get<UnionTypeData>().alternatives) {
        if (!collectType(alternative)) return false;
      }
      return true;
    }
    if (data.is<IntersectionTypeData>()) {
      for (const auto conjunct : data.get<IntersectionTypeData>().conjuncts) {
        if (!collectType(conjunct)) return false;
      }
      return true;
    }
    if (data.is<ReferenceTypeData>()) {
      return collectType(data.get<ReferenceTypeData>().referent);
    }
    if (data.is<RawPointerTypeData>()) {
      return collectType(data.get<RawPointerTypeData>().pointee);
    }
    if (data.is<ExistentialTypeData>()) {
      const auto& existential = data.get<ExistentialTypeData>();
      addDefinition(definitions, existential.principal.definition);
      for (const auto argument : existential.principal.arguments) {
        if (!collectType(argument)) return false;
      }
      for (const auto& interface : existential.additionalInterfaces) {
        addDefinition(definitions, interface.definition);
        for (const auto argument : interface.arguments) {
          if (!collectType(argument)) return false;
        }
      }
      for (const auto marker : existential.markers) { addDefinition(definitions, marker); }
      for (const auto& binding : existential.associatedBindings) {
        addDefinition(definitions, binding.associated);
        if (!collectType(binding.type)) return false;
      }
      return true;
    }
    if (data.is<InterfaceBoundTypeData>()) {
      return collectInterface(data.get<InterfaceBoundTypeData>().interface);
    }
    if (data.is<InterfaceSelfTypeData>()) {
      addDefinition(definitions, data.get<InterfaceSelfTypeData>().interface);
      return true;
    }
    return false;
  }

  const type::SemanticTypeStore& semanticTypes;
  const identity::SemanticIdentityRegistrySet& registries;
  zc::Vector<identity::DefId>& definitions;
  zc::Vector<identity::SemanticTypeId> visitedTypes;
};

bool collectSignatureReferences(const checker::signature::SemanticSignature& signature,
                                const type::SemanticTypeStore& semanticTypes,
                                const identity::SemanticIdentityRegistrySet& registries,
                                zc::Vector<identity::DefId>& definitions) {
  return SignatureReferenceCollector(semanticTypes, registries, definitions).collect(signature);
}

bool lookupBelongsToRoot(const checker::signature::SemanticSignature& signature,
                         zc::ArrayPtr<const identity::DefId> rootDefinitions,
                         const module_interface::AuthorizedSignatureBundle& bundle) {
  if (containsDefinition(rootDefinitions, signature.definition)) { return true; }
  identity::DefId current = signature.definition;
  zc::Vector<identity::DefId> visited;
  for (size_t depth = 0; depth <= bundle.definitions.size(); ++depth) {
    if (containsDefinition(visited.asPtr(), current)) { return false; }
    visited.add(current);
    ZC_IF_SOME(currentSignature, findSignature(bundle, current)) {
      const auto& scope = currentSignature.scope.variant();
      if (!scope.is<checker::signature::MemberSignatureScope>()) { return false; }
      current = scope.get<checker::signature::MemberSignatureScope>().owner;
      if (containsDefinition(rootDefinitions, current)) { return true; }
      continue;
    }
    return false;
  }
  return false;
}

zc::Maybe<identity::ModuleId> owningModule(
    identity::DefId definition, const identity::SemanticIdentityRegistrySet& registries) {
  ZC_IF_SOME(record, registries.definitions().lookupRecord(definition)) {
    return registries.modules().find(record.module());
  }
  return zc::none;
}

zc::Maybe<identity::ModuleId> owningModule(
    identity::ImplId implementation, const identity::SemanticIdentityRegistrySet& registries) {
  ZC_IF_SOME(record, registries.impls().lookupRecord(implementation)) {
    return registries.modules().find(record.module());
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> encodeSignature(
    const checker::signature::SemanticSignature& signature,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(module, owningModule(signature.definition, registries)) {
    return checker::signature::SignatureFactsCanonicalCodec::encodeSignature(
        signature, module, registries, semanticTypes);
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> encodeImportedModuleTarget(
    const checker::cross_module::ImportedModuleTarget& target,
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<uint8_t> output;
  encodeName(output, target.name);
  if (!encodeModule(output, target.module, registries)) { return zc::none; }
  append(output, target.surfaceRevision.digest().bytes());
  return output.releaseAsArray();
}

bool sameVisibility(const binder::VisibilityEnvelope& left,
                    const binder::VisibilityEnvelope& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::ExternalVisibility>()) {
    return rightValue.is<binder::ExternalVisibility>();
  }
  return rightValue.is<binder::ModuleVisibility>() &&
         leftValue.get<binder::ModuleVisibility>().module ==
             rightValue.get<binder::ModuleVisibility>().module;
}

zc::Maybe<const checker::signature::SemanticSignature&> findAvailableSignature(
    const checker::signature::VerifiedSignatureFacts& local,
    const checker::cross_module::ImportedSignatureView& imported, identity::DefId definition) {
  for (const auto& signature : local.signatures()) {
    if (signature.definition == definition) { return signature; }
  }
  ZC_IF_SOME(signature, imported.lookupDefinition(definition)) { return signature; }
  return imported.supportDefinition(definition);
}

bool lookupBelongsToAvailableRoot(const checker::signature::SemanticSignature& signature,
                                  zc::ArrayPtr<const identity::DefId> rootDefinitions,
                                  const checker::signature::VerifiedSignatureFacts& local,
                                  const checker::cross_module::ImportedSignatureView& imported) {
  if (containsDefinition(rootDefinitions, signature.definition)) { return true; }
  identity::DefId current = signature.definition;
  zc::Vector<identity::DefId> visited;
  size_t limit = local.signatures().size() + 1;
  for (const auto& module : imported.modules()) {
    limit += module.lookupDefinitions().size() + module.supportDefinitions().size();
  }
  for (size_t depth = 0; depth < limit; ++depth) {
    if (containsDefinition(visited.asPtr(), current)) { return false; }
    visited.add(current);
    ZC_IF_SOME(currentSignature, findAvailableSignature(local, imported, current)) {
      const auto& scope = currentSignature.scope.variant();
      if (!scope.is<checker::signature::MemberSignatureScope>()) { return false; }
      current = scope.get<checker::signature::MemberSignatureScope>().owner;
      if (containsDefinition(rootDefinitions, current)) { return true; }
      continue;
    }
    return false;
  }
  return false;
}

zc::Maybe<binder::ExportSurfaceRevision> findModuleTargetRevision(
    const binder::ExportSurfaceEntry& entry, identity::ModuleId target,
    const binder::VerifiedBindingMetadata& bindings,
    const checker::cross_module::ImportedSignatureView& imported) {
  const auto& bindingIdentity = entry.bindingIdentity.value();
  if (bindingIdentity.is<binder::DefinitionBindingTarget>()) {
    const auto binding = bindingIdentity.get<binder::DefinitionBindingTarget>().definition;
    for (const auto& alias : bindings.moduleAliases()) {
      if (alias.alias == binding && alias.canonicalTarget == target) {
        return alias.targetRevision;
      }
    }
  } else if (bindingIdentity.is<binder::SemanticImportBindingTarget>()) {
    const auto& semantic = bindingIdentity.get<binder::SemanticImportBindingTarget>().binding;
    for (const auto& import : bindings.imports()) {
      if (import.binding != semantic) { continue; }
      const auto& canonical = import.canonicalTarget.value();
      if (canonical.is<binder::ModuleBindingTarget>() &&
          canonical.get<binder::ModuleBindingTarget>().module == target) {
        return import.sourceRevision;
      }
    }
  }
  for (const auto& module : imported.modules()) {
    ZC_IF_SOME(moduleTarget, module.moduleTarget(entry.name)) {
      if (moduleTarget.module == target) { return moduleTarget.surfaceRevision; }
    }
  }
  return zc::none;
}

ModuleInterfaceBuildResult rejectInterface(identity::ModuleId module,
                                           ModuleInterfaceInvariantKind kind,
                                           ModuleInterfaceInvariantStage stage,
                                           uint32_t structuralField,
                                           zc::Maybe<identity::Sha256Digest> expected = zc::none,
                                           zc::Maybe<identity::Sha256Digest> actual = zc::none) {
  zc::Vector<uint32_t> path(1);
  path.add(structuralField);
  zc::Vector<ModuleInterfaceInvariantFact> failures(1);
  failures.add(ModuleInterfaceInvariantFact{kind, stage, module, zc::none, zc::none, zc::mv(path),
                                            expected, actual, 0});
  return ModuleInterfaceInvariantRejected{zc::mv(failures)};
}

template <typename Value>
bool containsProjectedDefinition(zc::ArrayPtr<const EncodedProjection<Value>> values,
                                 identity::DefId definition) {
  for (const auto& value : values) {
    if (value.value.definition == definition) { return true; }
  }
  return false;
}

}  // namespace

TypeEnrichedBindingTarget TypeEnrichedBindingTarget::clone() const {
  if (value.is<DefinitionTypeEnrichedTarget>()) {
    const auto& definition = value.get<DefinitionTypeEnrichedTarget>();
    return TypeEnrichedBindingTarget(
        DefinitionTypeEnrichedTarget{definition.definition, definition.signature.clone()});
  }
  return TypeEnrichedBindingTarget(value.get<ModuleTypeEnrichedTarget>());
}

VisibleBinding VisibleBinding::clone() const {
  return VisibleBinding{bindingIdentity.clone(), name.clone(),
                        target.clone(),          visibility.clone(),
                        bindingSpan.clone(),     canonicalDeclarationSpan.clone(),
                        cloneSpan(aliasSpan)};
}

ExportedBinding ExportedBinding::clone() const {
  return ExportedBinding{bindingIdentity.clone(), name.clone(),
                         target.clone(),          visibility.clone(),
                         bindingSpan.clone(),     canonicalDeclarationSpan.clone(),
                         cloneSpan(aliasSpan),    exportSpan.clone()};
}

zc::Maybe<zc::Array<uint8_t>> ModuleInterfaceCanonicalCodec::encodeSignatureRoot(
    const module_interface::SignatureRootAuthorization& root,
    const identity::SemanticIdentityRegistrySet& registries) {
  zc::Vector<uint8_t> output;
  if (!encodeSignatureRootBinding(output, root.binding, registries) ||
      !encodeDefinition(output, root.canonicalDefinition, registries) ||
      !encodeVisibility(output, root.visibility, registries) ||
      !encodeModule(output, root.sourceModule, registries)) {
    return zc::none;
  }
  append(output, root.bindingSurfaceRevision.digest().bytes());
  const auto& origin = root.origin.variant();
  if (origin.is<module_interface::LocalSignatureAuthorization>()) {
    output.add(0x01);
  } else {
    output.add(0x02);
    append(output, origin.get<module_interface::ImportedSignatureAuthorization>()
                       .interfaceRevision.digest()
                       .bytes());
  }
  return output.releaseAsArray();
}

zc::Maybe<zc::Array<uint8_t>> ModuleInterfaceCanonicalCodec::encodeVisibleBinding(
    const VisibleBinding& binding, const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  zc::Vector<uint8_t> output;
  if (!encodeBindingTarget(output, binding.bindingIdentity, registries)) { return zc::none; }
  encodeName(output, binding.name);
  if (!encodeTypeEnrichedTarget(output, binding.target, registries, semanticTypes) ||
      !encodeVisibility(output, binding.visibility, registries)) {
    return zc::none;
  }
  encodeSpan(output, binding.bindingSpan);
  encodeSpan(output, binding.canonicalDeclarationSpan);
  encodeOptionalSpan(output, binding.aliasSpan);
  return output.releaseAsArray();
}

zc::Maybe<zc::Array<uint8_t>> ModuleInterfaceCanonicalCodec::encodeExportedBinding(
    const ExportedBinding& binding, const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  zc::Vector<uint8_t> output;
  if (!encodeBindingTarget(output, binding.bindingIdentity, registries)) { return zc::none; }
  encodeName(output, binding.name);
  if (!encodeTypeEnrichedTarget(output, binding.target, registries, semanticTypes) ||
      !encodeVisibility(output, binding.visibility, registries)) {
    return zc::none;
  }
  encodeSpan(output, binding.bindingSpan);
  encodeSpan(output, binding.canonicalDeclarationSpan);
  encodeOptionalSpan(output, binding.aliasSpan);
  encodeSpan(output, binding.exportSpan);
  return output.releaseAsArray();
}

struct VerifiedModuleInterface::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       module_interface::ModuleInterfaceRevision revision, identity::PackageId package,
       identity::CrateId crate, identity::ModuleId module,
       const identity::Sha256Digest& sourceContentDigest,
       binder::VerifiedExportSurface&& bindingSurface,
       checker::signature::SignatureFactsRevision signatureFactsRevision,
       checker::signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision,
       checker::cross_module::ImportedSignatureViewRevision importedSignatureViewRevision,
       checker::borrow::VerifiedBorrowInterfaceSurface&& borrowSurface,
       module_interface::AuthorizedSignatureBundle&& signatures,
       zc::Vector<VisibleBinding>&& visibleBindings, zc::Vector<ExportedBinding>&& exportedBindings,
       zc::Vector<checker::signature::ImplHead>&& implHeads,
       zc::Vector<zc::Array<uint8_t>>&& implHeadRecords,
       zc::Vector<checker::signature::MarkerFact>&& markerFacts,
       zc::Vector<zc::Array<uint8_t>>&& markerFactRecords)
      : semanticContext(semanticContext),
        revision(revision),
        package(package),
        crate(crate),
        module(module),
        sourceContentDigest(sourceContentDigest),
        bindingSurface(zc::mv(bindingSurface)),
        signatureFactsRevision(signatureFactsRevision),
        markerPolicyRegistryRevision(markerPolicyRegistryRevision),
        importedSignatureViewRevision(importedSignatureViewRevision),
        borrowSurface(zc::mv(borrowSurface)),
        signatures(zc::mv(signatures)),
        visibleBindings(zc::mv(visibleBindings)),
        exportedBindings(zc::mv(exportedBindings)),
        implHeads(zc::mv(implHeads)),
        implHeadRecords(zc::mv(implHeadRecords)),
        markerFacts(zc::mv(markerFacts)),
        markerFactRecords(zc::mv(markerFactRecords)) {}

  identity::SemanticContextBrand semanticContext;
  module_interface::ModuleInterfaceRevision revision;
  identity::PackageId package;
  identity::CrateId crate;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  binder::VerifiedExportSurface bindingSurface;
  checker::signature::SignatureFactsRevision signatureFactsRevision;
  checker::signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision;
  checker::cross_module::ImportedSignatureViewRevision importedSignatureViewRevision;
  checker::borrow::VerifiedBorrowInterfaceSurface borrowSurface;
  module_interface::AuthorizedSignatureBundle signatures;
  zc::Vector<VisibleBinding> visibleBindings;
  zc::Vector<ExportedBinding> exportedBindings;
  zc::Vector<checker::signature::ImplHead> implHeads;
  zc::Vector<zc::Array<uint8_t>> implHeadRecords;
  zc::Vector<checker::signature::MarkerFact> markerFacts;
  zc::Vector<zc::Array<uint8_t>> markerFactRecords;
};

VerifiedModuleInterface::VerifiedModuleInterface(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedModuleInterface::~VerifiedModuleInterface() noexcept(false) = default;
VerifiedModuleInterface::VerifiedModuleInterface(VerifiedModuleInterface&&) noexcept = default;
VerifiedModuleInterface& VerifiedModuleInterface::operator=(VerifiedModuleInterface&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedModuleInterface::semanticContext() const noexcept {
  return impl->semanticContext;
}
const module_interface::ModuleInterfaceRevision& VerifiedModuleInterface::revision()
    const noexcept {
  return impl->revision;
}
identity::PackageId VerifiedModuleInterface::package() const noexcept { return impl->package; }
identity::CrateId VerifiedModuleInterface::crate() const noexcept { return impl->crate; }
identity::ModuleId VerifiedModuleInterface::module() const noexcept { return impl->module; }
const identity::Sha256Digest& VerifiedModuleInterface::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}
const binder::VerifiedExportSurface& VerifiedModuleInterface::bindingSurface() const noexcept {
  return impl->bindingSurface;
}
const checker::signature::SignatureFactsRevision& VerifiedModuleInterface::signatureFactsRevision()
    const noexcept {
  return impl->signatureFactsRevision;
}
const checker::signature::MarkerPolicyRegistryRevision&
VerifiedModuleInterface::markerPolicyRegistryRevision() const noexcept {
  return impl->markerPolicyRegistryRevision;
}
const checker::cross_module::ImportedSignatureViewRevision&
VerifiedModuleInterface::importedSignatureViewRevision() const noexcept {
  return impl->importedSignatureViewRevision;
}
const checker::borrow::VerifiedBorrowInterfaceSurface& VerifiedModuleInterface::borrowSurface()
    const noexcept {
  return impl->borrowSurface;
}
const module_interface::AuthorizedSignatureBundle& VerifiedModuleInterface::signatures()
    const noexcept {
  return impl->signatures;
}
zc::ArrayPtr<const VisibleBinding> VerifiedModuleInterface::visibleBindings() const noexcept {
  return impl->visibleBindings;
}
zc::ArrayPtr<const ExportedBinding> VerifiedModuleInterface::exportedBindings() const noexcept {
  return impl->exportedBindings;
}
zc::ArrayPtr<const checker::signature::ImplHead> VerifiedModuleInterface::coherenceImplHeads()
    const noexcept {
  return impl->implHeads;
}
zc::ArrayPtr<const checker::signature::MarkerFact> VerifiedModuleInterface::markerFacts()
    const noexcept {
  return impl->markerFacts;
}

checker::coherence::CoherenceModuleInput VerifiedModuleInterface::projectCoherenceInput() const {
  zc::Vector<checker::signature::ImplHead> implHeads(impl->implHeads.size());
  zc::Vector<zc::Array<uint8_t>> implHeadRecords(impl->implHeadRecords.size());
  for (size_t index = 0; index < impl->implHeads.size(); ++index) {
    implHeads.add(impl->implHeads[index].clone());
    implHeadRecords.add(cloneBytes(impl->implHeadRecords[index].asPtr()));
  }
  zc::Vector<checker::signature::MarkerFact> markerFacts(impl->markerFacts.size());
  zc::Vector<zc::Array<uint8_t>> markerFactRecords(impl->markerFactRecords.size());
  for (size_t index = 0; index < impl->markerFacts.size(); ++index) {
    markerFacts.add(impl->markerFacts[index].clone());
    markerFactRecords.add(cloneBytes(impl->markerFactRecords[index].asPtr()));
  }
  return checker::coherence::CoherenceModuleInput::publish(
      impl->module, impl->revision, impl->markerPolicyRegistryRevision, zc::mv(implHeads),
      zc::mv(implHeadRecords), zc::mv(markerFacts), zc::mv(markerFactRecords));
}

zc::Maybe<checker::cross_module::ImportedSignatureModule>
VerifiedModuleInterface::projectImportedSignatures(
    const binder::VerifiedBoundModuleInput& requester,
    checker::cross_module::SignatureViewOrigin origin,
    zc::ArrayPtr<const checker::cross_module::ImportedDefinitionBindingSelection>
        definitionBindings,
    zc::ArrayPtr<const checker::cross_module::ImportedModuleTargetSelection> moduleTargetNames,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) const {
  if (requester.module() == impl->module || requester.semanticContext() != impl->semanticContext ||
      impl->semanticContext != registries.context() ||
      impl->semanticContext != semanticTypes.context() ||
      registries.modules().lookup(requester.module()) == zc::none ||
      registries.modules().lookup(impl->module) == zc::none ||
      impl->bindingSurface.sourceModule() != impl->module) {
    return zc::none;
  }
  switch (origin) {
    case checker::cross_module::SignatureViewOrigin::ExplicitImport:
    case checker::cross_module::SignatureViewOrigin::NamespaceImport:
    case checker::cross_module::SignatureViewOrigin::Prelude:
      break;
    default:
      return zc::none;
  }

  for (size_t index = 0; index < definitionBindings.size(); ++index) {
    if (!module_interface::isSignatureRootBinding(definitionBindings[index].requesterBinding)) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (module_interface::sameSignatureRootBinding(definitionBindings[prior].requesterBinding,
                                                     definitionBindings[index].requesterBinding)) {
        return zc::none;
      }
    }
  }
  for (size_t index = 0; index < moduleTargetNames.size(); ++index) {
    for (size_t prior = 0; prior < index; ++prior) {
      if (sameName(moduleTargetNames[prior].requesterName,
                   moduleTargetNames[index].requesterName)) {
        return zc::none;
      }
    }
  }

  zc::Vector<EncodedProjection<module_interface::SignatureRootAuthorization>> rootRecords(
      definitionBindings.size());
  zc::Vector<identity::DefId> rootDefinitions(definitionBindings.size());
  for (const auto& selection : definitionBindings) {
    zc::Maybe<identity::DefId> canonicalDefinition;
    for (const auto& exported : impl->exportedBindings) {
      const auto& identityValue = exported.bindingIdentity.value();
      const auto& targetValue = exported.target.variant();
      if (identityValue.is<binder::DefinitionBindingTarget>() &&
          identityValue.get<binder::DefinitionBindingTarget>().definition ==
              selection.sourceBinding &&
          targetValue.is<DefinitionTypeEnrichedTarget>()) {
        canonicalDefinition = targetValue.get<DefinitionTypeEnrichedTarget>().definition;
        break;
      }
    }
    if (canonicalDefinition == zc::none) { return zc::none; }

    zc::Maybe<const module_interface::SignatureRootAuthorization&> sourceRoot;
    auto sourceBindingTarget = binder::BindingTarget::definition(selection.sourceBinding);
    for (const auto& root : impl->signatures.roots) {
      ZC_IF_SOME(definition, canonicalDefinition) {
        if (module_interface::sameSignatureRootBinding(root.binding, sourceBindingTarget) &&
            root.canonicalDefinition == definition) {
          sourceRoot = root;
          break;
        }
      }
    }
    if (sourceRoot == zc::none) { return zc::none; }

    zc::Maybe<const binder::ExportSurfaceEntry&> requesterEntry;
    for (const auto& entry : requester.bindingSurface().visibleEntries()) {
      const auto& canonical = entry.canonicalTarget.value();
      ZC_IF_SOME(definition, canonicalDefinition) {
        if (module_interface::sameSignatureRootBinding(entry.bindingIdentity,
                                                       selection.requesterBinding) &&
            canonical.is<binder::DefinitionBindingTarget>() &&
            canonical.get<binder::DefinitionBindingTarget>().definition == definition) {
          requesterEntry = entry;
          break;
        }
      }
    }

    bool exactAuthorization = false;
    switch (selection.authorizationOrigin) {
      case checker::cross_module::SignatureViewOrigin::ExplicitImport:
        if (!selection.requesterBinding.value().is<binder::SemanticImportBindingTarget>()) {
          return zc::none;
        }
        for (const auto& import : requester.bindings().imports()) {
          if (import.binding == selection.requesterBinding.value()
                                    .get<binder::SemanticImportBindingTarget>()
                                    .binding &&
              import.sourceModule == impl->module &&
              import.sourceRevision.digest() == impl->bindingSurface.revision().digest()) {
            exactAuthorization = true;
            break;
          }
        }
        break;
      case checker::cross_module::SignatureViewOrigin::NamespaceImport:
        if (!module_interface::sameSignatureRootBinding(selection.requesterBinding,
                                                        sourceBindingTarget)) {
          return zc::none;
        }
        for (const auto& alias : requester.resolvedModuleAliases()) {
          if (alias.targetModule() == impl->module &&
              alias.targetRevision().digest() == impl->bindingSurface.revision().digest()) {
            exactAuthorization = true;
            break;
          }
        }
        break;
      case checker::cross_module::SignatureViewOrigin::Prelude:
        ZC_IF_SOME(prelude, requester.preludeSurface()) {
          if (prelude.sourceModule() == impl->module &&
              prelude.sourceRevision().digest() == impl->bindingSurface.revision().digest()) {
            for (const auto& scope : requester.bindings().scopes()) {
              if (scope.kind != binder::ScopeKind::Module) { continue; }
              for (const auto& entry : scope.bindings) {
                const auto& binding = entry.binding.bindingIdentity.value();
                if (entry.binding.origin == binder::BindingOrigin::Prelude &&
                    module_interface::sameSignatureRootBinding(entry.binding.bindingIdentity,
                                                               selection.requesterBinding) &&
                    binding.is<binder::DefinitionBindingTarget>()) {
                  exactAuthorization = true;
                  break;
                }
              }
              break;
            }
          }
        }
        break;
    }
    if (!exactAuthorization || (selection.authorizationOrigin !=
                                    checker::cross_module::SignatureViewOrigin::NamespaceImport &&
                                requesterEntry == zc::none)) {
      return zc::none;
    }

    ZC_IF_SOME(root, sourceRoot) {
      if (root.bindingSurfaceRevision.digest() != impl->bindingSurface.revision().digest()) {
        return zc::none;
      }
      binder::VisibilityEnvelope visibility = root.visibility.clone();
      ZC_IF_SOME(entry, requesterEntry) { visibility = entry.visibility.clone(); }
      {
        module_interface::SignatureRootAuthorization importedRoot{
            selection.requesterBinding.clone(),
            root.canonicalDefinition,
            zc::mv(visibility),
            root.sourceModule,
            root.bindingSurfaceRevision,
            module_interface::SignatureAuthorizationOrigin(
                module_interface::ImportedSignatureAuthorization{impl->revision})};
        auto encoded = ModuleInterfaceCanonicalCodec::encodeSignatureRoot(importedRoot, registries);
        ZC_IF_SOME(bytes, encoded) {
          rootDefinitions.add(importedRoot.canonicalDefinition);
          rootRecords.add(EncodedProjection<module_interface::SignatureRootAuthorization>{
              zc::mv(importedRoot), zc::mv(bytes)});
        } else {
          return zc::none;
        }
      }
    }
  }
  sortEncoded(rootRecords);

  zc::Vector<EncodedProjection<checker::cross_module::ImportedModuleTarget>> targetRecords(
      moduleTargetNames.size());
  for (const auto& requestedName : moduleTargetNames) {
    zc::Maybe<checker::cross_module::ImportedModuleTarget> selected;
    if (requestedName.authorizationOrigin ==
        checker::cross_module::SignatureViewOrigin::NamespaceImport) {
      for (const auto& alias : requester.resolvedModuleAliases()) {
        if (sameName(alias.localName(), requestedName.requesterName) &&
            alias.targetModule() == impl->module &&
            alias.targetRevision().digest() == impl->bindingSurface.revision().digest()) {
          selected = checker::cross_module::ImportedModuleTarget{
              requestedName.requesterName.clone(), impl->module, impl->bindingSurface.revision()};
          break;
        }
      }
    }
    for (const auto& exported : impl->exportedBindings) {
      if (selected != zc::none) { break; }
      const auto& target = exported.target.variant();
      if (sameName(exported.name, requestedName.sourceName) &&
          target.is<ModuleTypeEnrichedTarget>()) {
        const auto& module = target.get<ModuleTypeEnrichedTarget>();
        selected = checker::cross_module::ImportedModuleTarget{
            requestedName.requesterName.clone(), module.module, module.surfaceRevision};
        break;
      }
    }
    if (selected == zc::none) { return zc::none; }
    ZC_IF_SOME(target, selected) {
      bool exactRequesterTarget = requestedName.authorizationOrigin ==
                                      checker::cross_module::SignatureViewOrigin::NamespaceImport &&
                                  target.module == impl->module;
      for (const auto& entry : requester.bindingSurface().visibleEntries()) {
        const auto& canonical = entry.canonicalTarget.value();
        if (sameName(entry.name, requestedName.requesterName) &&
            canonical.is<binder::ModuleBindingTarget>() &&
            canonical.get<binder::ModuleBindingTarget>().module == target.module) {
          const auto& binding = entry.bindingIdentity.value();
          if (requestedName.authorizationOrigin ==
              checker::cross_module::SignatureViewOrigin::ExplicitImport) {
            if (binding.is<binder::SemanticImportBindingTarget>()) {
              const auto& semantic = binding.get<binder::SemanticImportBindingTarget>().binding;
              for (const auto& import : requester.bindings().imports()) {
                if (import.binding == semantic && import.sourceModule == impl->module &&
                    import.sourceRevision.digest() == impl->bindingSurface.revision().digest()) {
                  exactRequesterTarget = true;
                  break;
                }
              }
            }
          } else if (requestedName.authorizationOrigin ==
                     checker::cross_module::SignatureViewOrigin::Prelude) {
            if (binding.is<binder::DefinitionBindingTarget>()) {
              for (const auto& scope : requester.bindings().scopes()) {
                if (scope.kind != binder::ScopeKind::Module) { continue; }
                for (const auto& binding : scope.bindings) {
                  if (sameName(binding.name, requestedName.requesterName) &&
                      binding.binding.origin == binder::BindingOrigin::Prelude) {
                    exactRequesterTarget = true;
                    break;
                  }
                }
                break;
              }
            }
          } else {
            exactRequesterTarget = true;
          }
          break;
        }
      }
      if (!exactRequesterTarget) { return zc::none; }
      auto encoded = encodeImportedModuleTarget(target, registries);
      ZC_IF_SOME(bytes, encoded) {
        targetRecords.add(EncodedProjection<checker::cross_module::ImportedModuleTarget>{
            zc::mv(target), zc::mv(bytes)});
      } else {
        return zc::none;
      }
    }
  }
  sortEncoded(targetRecords);

  zc::Vector<checker::signature::SemanticSignature> lookupValues;
  for (const auto& signature : impl->signatures.definitions) {
    if (lookupBelongsToRoot(signature, rootDefinitions.asPtr(), impl->signatures)) {
      lookupValues.add(signature.clone());
    }
  }
  for (const auto definition : rootDefinitions) {
    bool found = false;
    for (const auto& signature : lookupValues) {
      if (signature.definition == definition) {
        found = true;
        break;
      }
    }
    if (!found) { return zc::none; }
  }

  zc::Vector<identity::DefId> requiredDefinitions;
  for (const auto& signature : lookupValues) {
    if (!collectSignatureReferences(signature, semanticTypes, registries, requiredDefinitions)) {
      return zc::none;
    }
  }
  zc::Vector<checker::signature::SemanticSignature> supportValues;
  for (size_t cursor = 0; cursor < requiredDefinitions.size(); ++cursor) {
    const auto definition = requiredDefinitions[cursor];
    bool alreadyPresent = false;
    for (const auto& signature : lookupValues) {
      if (signature.definition == definition) {
        alreadyPresent = true;
        break;
      }
    }
    if (!alreadyPresent) {
      for (const auto& signature : supportValues) {
        if (signature.definition == definition) {
          alreadyPresent = true;
          break;
        }
      }
    }
    if (alreadyPresent) { continue; }
    ZC_IF_SOME(signature, findSignature(impl->signatures, definition)) {
      supportValues.add(signature.clone());
      if (!collectSignatureReferences(signature, semanticTypes, registries, requiredDefinitions)) {
        return zc::none;
      }
    } else {
      return zc::none;
    }
  }

  zc::Vector<EncodedProjection<checker::signature::SemanticSignature>> lookupRecords(
      lookupValues.size());
  for (auto& signature : lookupValues) {
    auto encoded = encodeSignature(signature, registries, semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      lookupRecords.add(EncodedProjection<checker::signature::SemanticSignature>{zc::mv(signature),
                                                                                 zc::mv(bytes)});
    } else {
      return zc::none;
    }
  }
  sortEncoded(lookupRecords);
  zc::Vector<EncodedProjection<checker::signature::SemanticSignature>> supportRecords(
      supportValues.size());
  for (auto& signature : supportValues) {
    auto encoded = encodeSignature(signature, registries, semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      supportRecords.add(EncodedProjection<checker::signature::SemanticSignature>{zc::mv(signature),
                                                                                  zc::mv(bytes)});
    } else {
      return zc::none;
    }
  }
  sortEncoded(supportRecords);

  zc::Vector<zc::ArrayPtr<const uint8_t>> rootBytes(rootRecords.size());
  for (const auto& record : rootRecords) { rootBytes.add(record.encoded.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> lookupBytes(lookupRecords.size());
  for (const auto& record : lookupRecords) { lookupBytes.add(record.encoded.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> supportBytes(supportRecords.size());
  for (const auto& record : supportRecords) { supportBytes.add(record.encoded.asPtr()); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> targetBytes(targetRecords.size());
  for (const auto& record : targetRecords) { targetBytes.add(record.encoded.asPtr()); }
  identity::CanonicalEncoder sourceEncoder;
  ZC_IF_SOME(sourceKey, registries.modules().lookup(impl->module)) {
    sourceKey.encode(sourceEncoder);
  }
  auto sourceBytes = sourceEncoder.finish();
  auto moduleRecord = checker::cross_module::ImportedSignatureModuleCanonicalCodec::encodeFramed(
      origin, sourceBytes.asPtr(), impl->revision.digest(),
      impl->bindingSurface.revision().digest(), rootBytes.asPtr(), lookupBytes.asPtr(),
      supportBytes.asPtr(), targetBytes.asPtr());
  if (moduleRecord == zc::none) { return zc::none; }

  zc::Vector<module_interface::SignatureRootAuthorization> roots(rootRecords.size());
  for (auto& record : rootRecords) { roots.add(zc::mv(record.value)); }
  zc::Vector<checker::signature::SemanticSignature> definitions(lookupRecords.size());
  for (auto& record : lookupRecords) { definitions.add(zc::mv(record.value)); }
  zc::Vector<checker::signature::SemanticSignature> supportDefinitions(supportRecords.size());
  for (auto& record : supportRecords) { supportDefinitions.add(zc::mv(record.value)); }
  zc::Vector<checker::cross_module::ImportedModuleTarget> moduleTargets(targetRecords.size());
  for (auto& record : targetRecords) { moduleTargets.add(zc::mv(record.value)); }

  ZC_IF_SOME(record, moduleRecord) {
    return checker::cross_module::ImportedSignatureModule::publish(
        impl->semanticContext, requester.module(), origin, impl->module, impl->revision,
        impl->bindingSurface.revision(), zc::mv(roots), zc::mv(definitions),
        zc::mv(supportDefinitions), zc::mv(moduleTargets), zc::mv(record));
  }
  return zc::none;
}

ModuleInterfaceBuildResult ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput&& input) {
  const auto module = input.boundModule.module();
  const auto& fingerprint = input.boundModule.semanticFingerprint();
  const auto& sourceDigest = input.boundModule.parsedModule().contentDigest();
  const auto& surface = input.boundModule.bindingSurface();
  if (!input.boundModule.semanticContext().isValid() ||
      input.boundModule.semanticContext() != input.signatureFacts.semanticContext() ||
      input.boundModule.semanticContext() != input.importedSignatures.semanticContext() ||
      input.boundModule.semanticContext() != input.markerPolicies.semanticContext() ||
      input.boundModule.semanticContext() != input.borrowSurface.semanticContext() ||
      input.boundModule.semanticContext() != input.registries.context() ||
      input.boundModule.semanticContext() != input.semanticTypes.context()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 0);
  }
  if (fingerprint.digest() != input.signatureFacts.contextFingerprint().digest() ||
      fingerprint.digest() != input.importedSignatures.contextFingerprint().digest() ||
      fingerprint.digest() != input.markerPolicies.contextFingerprint().digest() ||
      fingerprint.digest() != input.borrowSurface.contextFingerprint().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 1, fingerprint.digest());
  }
  if (input.signatureFacts.module() != module || input.importedSignatures.requester() != module ||
      input.borrowSurface.module() != module || surface.sourceModule() != module ||
      surface.sourcePackage() != input.boundModule.package()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 2);
  }
  if (sourceDigest != input.signatureFacts.sourceContentDigest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 3, sourceDigest,
                           input.signatureFacts.sourceContentDigest());
  }
  if (input.boundModule.parsedModule().receipt().digest() !=
      input.signatureFacts.parsedModuleReceipt().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 4,
                           input.boundModule.parsedModule().receipt().digest(),
                           input.signatureFacts.parsedModuleReceipt().digest());
  }
  if (surface.revision().digest() != input.signatureFacts.bindingSurfaceRevision().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 5, surface.revision().digest(),
                           input.signatureFacts.bindingSurfaceRevision().digest());
  }
  if (input.signatureFacts.markerPolicyRegistryRevision().digest() !=
      input.markerPolicies.revision().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 6,
                           input.markerPolicies.revision().digest(),
                           input.signatureFacts.markerPolicyRegistryRevision().digest());
  }
  if (input.borrowSurface.signatureFactsRevision().digest() !=
          input.signatureFacts.revision().digest() ||
      input.borrowSurface.importedSignatureViewRevision().digest() !=
          input.importedSignatures.revision().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 7);
  }
  auto packageKey = input.registries.packages().lookup(input.boundModule.package());
  auto crateKey = input.registries.crates().lookup(input.boundModule.crate());
  auto moduleKey = input.registries.modules().lookup(module);
  if (packageKey == zc::none || crateKey == zc::none || moduleKey == zc::none) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                           ModuleInterfaceInvariantStage::Input, 8);
  }
  ZC_IF_SOME(packageValue, packageKey) {
    ZC_IF_SOME(crateValue, crateKey) {
      ZC_IF_SOME(moduleValue, moduleKey) {
        auto moduleCrate = moduleValue.crate().encode();
        auto registeredCrate = crateValue.encode();
        auto cratePackage = crateValue.package().encode();
        auto registeredPackage = packageValue.encode();
        if (!sameBytes(moduleCrate.asPtr(), registeredCrate.asPtr()) ||
            !sameBytes(cratePackage.asPtr(), registeredPackage.asPtr())) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::InputMismatch,
                                 ModuleInterfaceInvariantStage::Input, 8);
        }
      }
    }
  }

  zc::Vector<module_interface::SignatureRootAuthorization> roots;
  zc::Vector<identity::DefId> rootDefinitions;
  for (const auto& entry : surface.visibleEntries()) {
    const auto& canonical = entry.canonicalTarget.value();
    if (!canonical.is<binder::DefinitionBindingTarget>()) { continue; }
    const auto& binding = entry.bindingIdentity.value();
    if (!module_interface::isSignatureRootBinding(entry.bindingIdentity)) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                             ModuleInterfaceInvariantStage::Projection, 9);
    }
    const auto canonicalDefinition = canonical.get<binder::DefinitionBindingTarget>().definition;
    auto owner = owningModule(canonicalDefinition, input.registries);
    if (owner == zc::none) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                             ModuleInterfaceInvariantStage::Projection, 10);
    }
    ZC_IF_SOME(ownerModule, owner) {
      if (ownerModule == module) {
        if (!binding.is<binder::DefinitionBindingTarget>()) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                                 ModuleInterfaceInvariantStage::Verification, 10);
        }
        const auto bindingDefinition = binding.get<binder::DefinitionBindingTarget>().definition;
        auto bindingOwner = owningModule(bindingDefinition, input.registries);
        if (bindingOwner == zc::none) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                                 ModuleInterfaceInvariantStage::Projection, 10);
        }
        ZC_IF_SOME(localBindingOwner, bindingOwner) {
          if (localBindingOwner != module) {
            return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                                   ModuleInterfaceInvariantStage::Verification, 10);
          }
        }
        bool hasLocalSignature = false;
        for (const auto& signature : input.signatureFacts.signatures()) {
          if (signature.definition == canonicalDefinition) {
            hasLocalSignature = true;
            break;
          }
        }
        if (!hasLocalSignature) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                                 ModuleInterfaceInvariantStage::Projection, 10);
        }
        roots.add(module_interface::SignatureRootAuthorization{
            entry.bindingIdentity.clone(), canonicalDefinition, entry.visibility.clone(), module,
            surface.revision(),
            module_interface::SignatureAuthorizationOrigin(
                module_interface::LocalSignatureAuthorization{})});
      } else {
        zc::Maybe<const module_interface::SignatureRootAuthorization&> importedRoot;
        zc::Maybe<const checker::cross_module::ImportedSignatureModule&> sourceModule;
        for (const auto& importedModule : input.importedSignatures.modules()) {
          ZC_IF_SOME(candidate, importedModule.authorization(entry.bindingIdentity)) {
            if (importedRoot != zc::none) {
              return rejectInterface(module, ModuleInterfaceInvariantKind::AdditionalProjection,
                                     ModuleInterfaceInvariantStage::Projection, 11);
            }
            importedRoot = candidate;
            sourceModule = importedModule;
          }
        }
        if (importedRoot == zc::none || sourceModule == zc::none) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                                 ModuleInterfaceInvariantStage::Projection, 11);
        }
        ZC_IF_SOME(root, importedRoot) {
          ZC_IF_SOME(importedModule, sourceModule) {
            const auto& origin = root.origin.variant();
            if (root.canonicalDefinition != canonicalDefinition ||
                root.sourceModule != ownerModule ||
                !sameVisibility(root.visibility, entry.visibility) ||
                !origin.is<module_interface::ImportedSignatureAuthorization>() ||
                origin.get<module_interface::ImportedSignatureAuthorization>()
                        .interfaceRevision.digest() !=
                    importedModule.interfaceRevision().digest() ||
                root.bindingSurfaceRevision.digest() !=
                    importedModule.bindingSurfaceRevision().digest()) {
              return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                                     ModuleInterfaceInvariantStage::Verification, 12);
            }
            ZC_IF_SOME(signature, importedModule.lookupDefinition(canonicalDefinition)) {
              (void)signature;
            } else {
              return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                                     ModuleInterfaceInvariantStage::Projection, 12);
            }
            roots.add(module_interface::SignatureRootAuthorization{
                entry.bindingIdentity.clone(), canonicalDefinition, entry.visibility.clone(),
                root.sourceModule, surface.revision(),
                module_interface::SignatureAuthorizationOrigin(
                    module_interface::ImportedSignatureAuthorization{
                        importedModule.interfaceRevision()})});
          }
        }
      }
    }
    addDefinition(rootDefinitions, canonicalDefinition);
  }

  zc::Vector<checker::signature::SemanticSignature> lookupDefinitions;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (lookupBelongsToAvailableRoot(signature, rootDefinitions.asPtr(), input.signatureFacts,
                                     input.importedSignatures)) {
      lookupDefinitions.add(signature.clone());
    }
  }
  for (const auto& importedModule : input.importedSignatures.modules()) {
    for (const auto& signature : importedModule.lookupDefinitions()) {
      if (!lookupBelongsToAvailableRoot(signature, rootDefinitions.asPtr(), input.signatureFacts,
                                        input.importedSignatures)) {
        continue;
      }
      bool duplicate = false;
      for (const auto& existing : lookupDefinitions) {
        if (existing.definition == signature.definition) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) { lookupDefinitions.add(signature.clone()); }
    }
  }
  for (const auto definition : rootDefinitions) {
    bool found = false;
    for (const auto& signature : lookupDefinitions) {
      if (signature.definition == definition) {
        found = true;
        break;
      }
    }
    if (!found) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                             ModuleInterfaceInvariantStage::Projection, 13);
    }
  }

  zc::Vector<identity::DefId> requiredDefinitions;
  for (const auto& signature : lookupDefinitions) {
    if (!collectSignatureReferences(signature, input.semanticTypes, input.registries,
                                    requiredDefinitions)) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                             ModuleInterfaceInvariantStage::Verification, 14);
    }
  }
  zc::Vector<checker::signature::SemanticSignature> supportDefinitions;
  for (size_t cursor = 0; cursor < requiredDefinitions.size(); ++cursor) {
    const auto definition = requiredDefinitions[cursor];
    bool present = false;
    for (const auto& signature : lookupDefinitions) {
      if (signature.definition == definition) {
        present = true;
        break;
      }
    }
    if (!present) {
      for (const auto& signature : supportDefinitions) {
        if (signature.definition == definition) {
          present = true;
          break;
        }
      }
    }
    if (present) { continue; }
    ZC_IF_SOME(signature,
               findAvailableSignature(input.signatureFacts, input.importedSignatures, definition)) {
      supportDefinitions.add(signature.clone());
      if (!collectSignatureReferences(signature, input.semanticTypes, input.registries,
                                      requiredDefinitions)) {
        return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                               ModuleInterfaceInvariantStage::Verification, 15);
      }
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                             ModuleInterfaceInvariantStage::Projection, 15);
    }
  }

  auto rebuiltBorrowSurface =
      checker::borrow::BorrowInterfaceBuilder::build(checker::borrow::BorrowInterfaceBuildInput{
          input.boundModule.semanticContext(), fingerprint, module, input.signatureFacts.revision(),
          input.importedSignatures.revision(), lookupDefinitions.asPtr(),
          supportDefinitions.asPtr(), input.registries, input.semanticTypes});
  if (!rebuiltBorrowSurface.is<checker::borrow::VerifiedBorrowInterfaceSurface>() ||
      rebuiltBorrowSurface.get<checker::borrow::VerifiedBorrowInterfaceSurface>()
              .revision()
              .digest() != input.borrowSurface.revision().digest()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                           ModuleInterfaceInvariantStage::Verification, 16,
                           input.borrowSurface.revision().digest());
  }

  zc::Vector<EncodedProjection<module_interface::SignatureRootAuthorization>> rootRecords(
      roots.size());
  for (auto& root : roots) {
    auto encoded = ModuleInterfaceCanonicalCodec::encodeSignatureRoot(root, input.registries);
    ZC_IF_SOME(bytes, encoded) {
      rootRecords.add(EncodedProjection<module_interface::SignatureRootAuthorization>{
          zc::mv(root), zc::mv(bytes)});
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                             ModuleInterfaceInvariantStage::Encoding, 16);
    }
  }
  sortEncoded(rootRecords);
  zc::Vector<EncodedProjection<checker::signature::SemanticSignature>> definitionRecords(
      lookupDefinitions.size());
  for (auto& signature : lookupDefinitions) {
    auto encoded = encodeSignature(signature, input.registries, input.semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      definitionRecords.add(EncodedProjection<checker::signature::SemanticSignature>{
          zc::mv(signature), zc::mv(bytes)});
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                             ModuleInterfaceInvariantStage::Encoding, 17);
    }
  }
  sortEncoded(definitionRecords);
  zc::Vector<EncodedProjection<checker::signature::SemanticSignature>> supportRecords(
      supportDefinitions.size());
  for (auto& signature : supportDefinitions) {
    auto encoded = encodeSignature(signature, input.registries, input.semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      supportRecords.add(EncodedProjection<checker::signature::SemanticSignature>{zc::mv(signature),
                                                                                  zc::mv(bytes)});
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                             ModuleInterfaceInvariantStage::Encoding, 18);
    }
  }
  sortEncoded(supportRecords);

  auto findProjectedSignature =
      [&](identity::DefId definition) -> zc::Maybe<const checker::signature::SemanticSignature&> {
    for (const auto& record : definitionRecords) {
      if (record.value.definition == definition) { return record.value; }
    }
    return zc::none;
  };
  zc::Vector<EncodedProjection<VisibleBinding>> visibleRecords(surface.visibleEntries().size());
  zc::Vector<EncodedProjection<ExportedBinding>> exportedRecords(surface.exports().size());
  for (const auto& entry : surface.visibleEntries()) {
    zc::Maybe<TypeEnrichedBindingTarget> projectedTarget;
    const auto& canonical = entry.canonicalTarget.value();
    if (canonical.is<binder::DefinitionBindingTarget>()) {
      const auto definition = canonical.get<binder::DefinitionBindingTarget>().definition;
      ZC_IF_SOME(signature, findProjectedSignature(definition)) {
        projectedTarget =
            TypeEnrichedBindingTarget(DefinitionTypeEnrichedTarget{definition, signature.clone()});
      }
    } else {
      const auto targetModule = canonical.get<binder::ModuleBindingTarget>().module;
      ZC_IF_SOME(revision,
                 findModuleTargetRevision(entry, targetModule, input.boundModule.bindings(),
                                          input.importedSignatures)) {
        projectedTarget =
            TypeEnrichedBindingTarget(ModuleTypeEnrichedTarget{targetModule, revision});
      }
    }
    if (projectedTarget == zc::none) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                             ModuleInterfaceInvariantStage::Projection, 19);
    }
    ZC_IF_SOME(target, projectedTarget) {
      VisibleBinding visible{entry.bindingIdentity.clone(),
                             entry.name.clone(),
                             zc::mv(target),
                             entry.visibility.clone(),
                             entry.bindingSpan.clone(),
                             entry.canonicalDeclarationSpan.clone(),
                             cloneSpan(entry.aliasSpan)};
      auto encoded = ModuleInterfaceCanonicalCodec::encodeVisibleBinding(visible, input.registries,
                                                                         input.semanticTypes);
      ZC_IF_SOME(bytes, encoded) {
        visibleRecords.add(EncodedProjection<VisibleBinding>{visible.clone(), zc::mv(bytes)});
      } else {
        return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                               ModuleInterfaceInvariantStage::Encoding, 19);
      }
      if (entry.exported) {
        if (entry.exportSpan == zc::none ||
            !entry.visibility.value().is<binder::ExternalVisibility>()) {
          return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                                 ModuleInterfaceInvariantStage::Projection, 20);
        }
        ZC_IF_SOME(exportSpan, entry.exportSpan) {
          ExportedBinding exported{
              entry.bindingIdentity.clone(), entry.name.clone(),
              visible.target.clone(),        entry.visibility.clone(),
              entry.bindingSpan.clone(),     entry.canonicalDeclarationSpan.clone(),
              cloneSpan(entry.aliasSpan),    exportSpan.clone()};
          auto exportBytes = ModuleInterfaceCanonicalCodec::encodeExportedBinding(
              exported, input.registries, input.semanticTypes);
          ZC_IF_SOME(bytes, exportBytes) {
            exportedRecords.add(
                EncodedProjection<ExportedBinding>{zc::mv(exported), zc::mv(bytes)});
          } else {
            return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                                   ModuleInterfaceInvariantStage::Encoding, 21);
          }
        }
      }
    }
  }
  if (exportedRecords.size() != surface.exports().size()) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::MissingProjection,
                           ModuleInterfaceInvariantStage::Projection, 22);
  }
  sortEncoded(visibleRecords);
  sortEncoded(exportedRecords);

  zc::Vector<EncodedProjection<checker::signature::ImplHead>> implRecords(
      input.signatureFacts.implHeads().size());
  for (size_t index = 0; index < input.signatureFacts.implHeads().size(); ++index) {
    const auto& head = input.signatureFacts.implHeads()[index];
    auto owner = owningModule(head.impl, input.registries);
    if (owner == zc::none) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                             ModuleInterfaceInvariantStage::Verification, 23);
    }
    ZC_IF_SOME(ownerModule, owner) {
      if (ownerModule != module) {
        return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                               ModuleInterfaceInvariantStage::Verification, 23);
      }
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (input.signatureFacts.implHeads()[prior].impl == head.impl) {
        return rejectInterface(module, ModuleInterfaceInvariantKind::AdditionalProjection,
                               ModuleInterfaceInvariantStage::Verification, 23);
      }
    }
    auto encoded = checker::signature::SignatureFactsCanonicalCodec::encodeImplHead(
        head, input.registries, input.semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      implRecords.add(EncodedProjection<checker::signature::ImplHead>{head.clone(), zc::mv(bytes)});
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                             ModuleInterfaceInvariantStage::Encoding, 23);
    }
  }
  sortEncoded(implRecords);
  zc::Vector<EncodedProjection<checker::signature::MarkerFact>> markerRecords(
      input.signatureFacts.markerFacts().size());
  for (size_t index = 0; index < input.signatureFacts.markerFacts().size(); ++index) {
    const auto& fact = input.signatureFacts.markerFacts()[index];
    const auto& evidence = fact.evidence.variant();
    if (!evidence.is<checker::signature::ExplicitMarkerEvidence>() ||
        fact.declarationSpan == zc::none) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                             ModuleInterfaceInvariantStage::Verification, 24);
    }
    const auto implementation = evidence.get<checker::signature::ExplicitMarkerEvidence>().impl;
    auto owner = owningModule(implementation, input.registries);
    if (owner == zc::none) {
      return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                             ModuleInterfaceInvariantStage::Verification, 24);
    }
    ZC_IF_SOME(ownerModule, owner) {
      if (ownerModule != module) {
        return rejectInterface(module, ModuleInterfaceInvariantKind::InvalidProjection,
                               ModuleInterfaceInvariantStage::Verification, 24);
      }
    }
    for (size_t prior = 0; prior < index; ++prior) {
      const auto& priorKey = input.signatureFacts.markerFacts()[prior].key;
      if (priorKey.marker == fact.key.marker && priorKey.subject == fact.key.subject) {
        return rejectInterface(module, ModuleInterfaceInvariantKind::AdditionalProjection,
                               ModuleInterfaceInvariantStage::Verification, 24);
      }
    }
    auto encoded = checker::signature::SignatureFactsCanonicalCodec::encodeMarkerFact(
        fact, input.registries, input.semanticTypes);
    ZC_IF_SOME(bytes, encoded) {
      markerRecords.add(
          EncodedProjection<checker::signature::MarkerFact>{fact.clone(), zc::mv(bytes)});
    } else {
      return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                             ModuleInterfaceInvariantStage::Encoding, 24);
    }
  }
  sortEncoded(markerRecords);

  auto recordViews = [](const auto& records) {
    zc::Vector<zc::ArrayPtr<const uint8_t>> views(records.size());
    for (const auto& record : records) { views.add(record.encoded.asPtr()); }
    return views;
  };
  auto rootViews = recordViews(rootRecords);
  auto definitionViews = recordViews(definitionRecords);
  auto supportViews = recordViews(supportRecords);
  auto visibleViews = recordViews(visibleRecords);
  auto exportedViews = recordViews(exportedRecords);
  auto implViews = recordViews(implRecords);
  auto markerViews = recordViews(markerRecords);
  identity::CanonicalEncoder moduleEncoder;
  ZC_IF_SOME(moduleKey, input.registries.modules().lookup(module)) {
    moduleKey.encode(moduleEncoder);
  }
  auto moduleBytes = moduleEncoder.finish();
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      fingerprint.digest(), moduleBytes.asPtr(), sourceDigest, surface.revision().digest(),
      input.signatureFacts.revision().digest(), input.markerPolicies.revision().digest(),
      input.importedSignatures.revision().digest(), input.borrowSurface.revision().digest(),
      rootViews.asPtr(), definitionViews.asPtr(), supportViews.asPtr(), visibleViews.asPtr(),
      exportedViews.asPtr(), implViews.asPtr(), markerViews.asPtr());
  if (revision == zc::none) {
    return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                           ModuleInterfaceInvariantStage::Encoding, 25);
  }

  zc::Vector<module_interface::SignatureRootAuthorization> finalRoots(rootRecords.size());
  for (auto& record : rootRecords) { finalRoots.add(zc::mv(record.value)); }
  zc::Vector<checker::signature::SemanticSignature> finalDefinitions(definitionRecords.size());
  for (auto& record : definitionRecords) { finalDefinitions.add(zc::mv(record.value)); }
  zc::Vector<checker::signature::SemanticSignature> finalSupport(supportRecords.size());
  for (auto& record : supportRecords) { finalSupport.add(zc::mv(record.value)); }
  zc::Vector<VisibleBinding> finalVisible(visibleRecords.size());
  for (auto& record : visibleRecords) { finalVisible.add(zc::mv(record.value)); }
  zc::Vector<ExportedBinding> finalExported(exportedRecords.size());
  for (auto& record : exportedRecords) { finalExported.add(zc::mv(record.value)); }
  zc::Vector<checker::signature::ImplHead> finalImpls(implRecords.size());
  zc::Vector<zc::Array<uint8_t>> finalImplRecords(implRecords.size());
  for (auto& record : implRecords) {
    finalImpls.add(zc::mv(record.value));
    finalImplRecords.add(zc::mv(record.encoded));
  }
  zc::Vector<checker::signature::MarkerFact> finalMarkers(markerRecords.size());
  zc::Vector<zc::Array<uint8_t>> finalMarkerRecords(markerRecords.size());
  for (auto& record : markerRecords) {
    finalMarkers.add(zc::mv(record.value));
    finalMarkerRecords.add(zc::mv(record.encoded));
  }

  ZC_IF_SOME(finalRevision, revision) {
    return VerifiedModuleInterface(zc::heap<VerifiedModuleInterface::Impl>(
        input.boundModule.semanticContext(), finalRevision, input.boundModule.package(),
        input.boundModule.crate(), module, sourceDigest, surface.clone(),
        input.signatureFacts.revision(), input.markerPolicies.revision(),
        input.importedSignatures.revision(), zc::mv(input.borrowSurface),
        module_interface::AuthorizedSignatureBundle{zc::mv(finalRoots), zc::mv(finalDefinitions),
                                                    zc::mv(finalSupport)},
        zc::mv(finalVisible), zc::mv(finalExported), zc::mv(finalImpls), zc::mv(finalImplRecords),
        zc::mv(finalMarkers), zc::mv(finalMarkerRecords)));
  }
  return rejectInterface(module, ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                         ModuleInterfaceInvariantStage::Encoding, 25);
}

}  // namespace zomlang::compiler::driver
