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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/type/semantic-type-key.h"

#include "zc/core/vector.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::type::semantic {
namespace {

void append(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  for (const auto byte : bytes) { encoder.encodeUint8(byte); }
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const auto count = zc::min(left.size(), right.size());
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  return left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
}

bool validPrimitive(PrimitiveKind value) {
  const auto tag = static_cast<uint8_t>(value);
  return tag >= 0x01 && tag <= 0x13;
}

bool validMutability(Mutability value) {
  return value == Mutability::Const || value == Mutability::Mutable;
}

bool validPresence(FieldPresence value) {
  return value == FieldPresence::Required || value == FieldPresence::Optional;
}

enum class DefinitionRole : uint8_t { Nominal, Interface, AssociatedType };

bool definitionKindMatches(identity::DefinitionKind kind, DefinitionRole role) {
  switch (role) {
    case DefinitionRole::Nominal:
      return kind == identity::DefinitionKind::Class || kind == identity::DefinitionKind::Struct ||
             kind == identity::DefinitionKind::Enum || kind == identity::DefinitionKind::Error;
    case DefinitionRole::Interface:
      return kind == identity::DefinitionKind::Interface;
    case DefinitionRole::AssociatedType:
      return kind == identity::DefinitionKind::AssociatedType;
  }
  ZC_UNREACHABLE
}

struct AdmissionFailure final {
  identity::IdentityInvariantKind kind;
  identity::IdentityAllocationPhase phase;
};

}  // namespace

class StoreBoundTypeEncoder final {
public:
  explicit StoreBoundTypeEncoder(const SemanticTypeStore& store) : store(store) {}

  zc::Maybe<SemanticTypeKey> encodeKey(const TypeData& data) {
    identity::CanonicalEncoder output;
    append(output, "zom.semantic-type-key"_zc.asBytes());
    output.encodeUint8(0x00);
    if (!encode(output, data)) return zc::none;
    return SemanticTypeKey(output.finish());
  }

  AdmissionFailure rejection() const noexcept {
    ZC_IF_SOME(value, failure) { return value; }
    return AdmissionFailure{identity::IdentityInvariantKind::InvalidClosedValue,
                            identity::IdentityAllocationPhase::SemanticType};
  }

private:
  void reject(identity::IdentityInvariantKind kind, identity::IdentityAllocationPhase phase) {
    if (failure == zc::none) { failure = AdmissionFailure{kind, phase}; }
  }

  void reject(const zc::Maybe<identity::IdentityInvariantKind>& value,
              identity::IdentityAllocationPhase phase) {
    ZC_IF_SOME(kind, value) { reject(kind, phase); }
  }

  bool rejectClosedValue() {
    reject(identity::IdentityInvariantKind::InvalidClosedValue,
           identity::IdentityAllocationPhase::SemanticType);
    return false;
  }

  bool rejectNonCanonical() {
    reject(identity::IdentityInvariantKind::NonCanonicalEncoding,
           identity::IdentityAllocationPhase::SemanticType);
    return false;
  }

  bool encode(identity::CanonicalEncoder& output, const TypeData& data) {
    output.encodeUint8(static_cast<uint8_t>(data.tag()));
    switch (data.tag()) {
      case TypeDataTag::Primitive: {
        const auto kind = data.get<PrimitiveTypeData>().kind;
        if (!validPrimitive(kind)) return rejectClosedValue();
        output.encodeUint8(static_cast<uint8_t>(kind));
        return true;
      }
      case TypeDataTag::Tuple: {
        const auto& values = data.get<TupleTypeData>().elements;
        return values.size() >= 2 ? encodeTypes(output, values.asPtr()) : rejectClosedValue();
      }
      case TypeDataTag::Object:
        return encodeObject(output, data.get<ObjectTypeData>());
      case TypeDataTag::DynamicArray:
        return encodeType(output, data.get<DynamicArrayTypeData>().element);
      case TypeDataTag::Slice:
        return encodeType(output, data.get<SliceTypeData>().element);
      case TypeDataTag::FixedArray: {
        const auto& value = data.get<FixedArrayTypeData>();
        if (!encodeType(output, value.element)) return false;
        output.encodeUint64(value.length);
        return true;
      }
      case TypeDataTag::Function:
        return encodeFunction(output, data.get<FunctionTypeData>());
      case TypeDataTag::Nominal: {
        const auto& value = data.get<NominalTypeData>();
        return encodeDefinition(output, value.definition, DefinitionRole::Nominal) &&
               encodeTypes(output, value.arguments.asPtr());
      }
      case TypeDataTag::TypeParameter: {
        const auto& parameter = data.get<TypeParameterTypeData>().parameter;
        const auto validation = store.validateGenericParameterForAdmission(parameter);
        if (validation != zc::none) {
          reject(validation, identity::IdentityAllocationPhase::GenericParameter);
          return false;
        }
        parameter.encode(output);
        return true;
      }
      case TypeDataTag::Union:
        return encodeSet(output, data.get<UnionTypeData>().alternatives.asPtr(), true);
      case TypeDataTag::Intersection:
        return encodeSet(output, data.get<IntersectionTypeData>().conjuncts.asPtr(), false);
      case TypeDataTag::Reference: {
        const auto& value = data.get<ReferenceTypeData>();
        return encodeQualified(output, value.mutability, value.referent);
      }
      case TypeDataTag::RawPointer: {
        const auto& value = data.get<RawPointerTypeData>();
        return encodeQualified(output, value.mutability, value.pointee);
      }
      case TypeDataTag::Existential:
        return encodeExistential(output, data.get<ExistentialTypeData>());
      case TypeDataTag::InterfaceBound:
        return encodeInterface(output, data.get<InterfaceBoundTypeData>().interface);
      case TypeDataTag::InterfaceSelf:
        return encodeDefinition(output, data.get<InterfaceSelfTypeData>().interface,
                                DefinitionRole::Interface);
    }
    return rejectClosedValue();
  }

  zc::Maybe<zc::Array<uint8_t>> definitionBytes(identity::DefId id, DefinitionRole role) {
    const auto validation = store.validateDefinitionForAdmission(id);
    if (validation != zc::none) {
      reject(validation, identity::IdentityAllocationPhase::Definition);
      return zc::none;
    }
    ZC_IF_SOME(record, store.definitionRecordForAdmission(id)) {
      if (!definitionKindMatches(record.kind(), role)) {
        reject(identity::IdentityInvariantKind::InvalidClosedValue,
               identity::IdentityAllocationPhase::SemanticType);
        return zc::none;
      }
      ZC_IF_SOME(key, store.definitionKeyForAdmission(id)) {
        identity::CanonicalEncoder output;
        key.encode(output);
        return output.finish();
      }
    }
    reject(identity::IdentityInvariantKind::InvalidHandle,
           identity::IdentityAllocationPhase::Definition);
    return zc::none;
  }

  bool encodeDefinition(identity::CanonicalEncoder& output, identity::DefId id,
                        DefinitionRole role) {
    ZC_IF_SOME(bytes, definitionBytes(id, role)) {
      append(output, bytes.asPtr());
      return true;
    }
    return false;
  }

  zc::Maybe<zc::Array<uint8_t>> typeBytes(identity::SemanticTypeId id) {
    const auto validation = store.validateTypeForAdmission(id);
    if (validation != zc::none) {
      reject(validation, identity::IdentityAllocationPhase::SemanticType);
      return zc::none;
    }
    for (const auto activeId : active) {
      if (activeId == id) {
        rejectNonCanonical();
        return zc::none;
      }
    }
    ZC_IF_SOME(data, store.typeDataForAdmission(id)) {
      active.add(id);
      identity::CanonicalEncoder output;
      const auto valid = encode(output, data);
      active.removeLast();
      if (valid) return output.finish();
    }
    if (failure == zc::none) {
      reject(identity::IdentityInvariantKind::InvalidHandle,
             identity::IdentityAllocationPhase::SemanticType);
    }
    return zc::none;
  }

  bool encodeType(identity::CanonicalEncoder& output, identity::SemanticTypeId id) {
    ZC_IF_SOME(bytes, typeBytes(id)) {
      append(output, bytes.asPtr());
      return true;
    }
    return false;
  }

  bool encodeTypes(identity::CanonicalEncoder& output,
                   zc::ArrayPtr<const identity::SemanticTypeId> values) {
    output.encodeSequenceSize(values.size());
    for (const auto id : values) {
      if (!encodeType(output, id)) return false;
    }
    return true;
  }

  bool encodeSet(identity::CanonicalEncoder& output,
                 zc::ArrayPtr<const identity::SemanticTypeId> values, bool isUnion) {
    if (values.size() < 2) return rejectClosedValue();
    zc::Vector<zc::Array<uint8_t>> encoded;
    for (const auto id : values) {
      ZC_IF_SOME(child, store.typeDataForAdmission(id)) {
        if ((isUnion && child.tag() == TypeDataTag::Union) ||
            (!isUnion && child.tag() == TypeDataTag::Intersection)) {
          return rejectClosedValue();
        }
        if (child.tag() == TypeDataTag::Primitive) {
          const auto kind = child.get<PrimitiveTypeData>().kind;
          if (!validPrimitive(kind) || kind == PrimitiveKind::Never || kind == PrimitiveKind::Any) {
            return rejectClosedValue();
          }
        }
      } else {
        const auto validation = store.validateTypeForAdmission(id);
        reject(validation, identity::IdentityAllocationPhase::SemanticType);
        return false;
      }
      ZC_IF_SOME(bytes, typeBytes(id)) {
        if (encoded.size() != 0 && compareBytes(encoded.back().asPtr(), bytes.asPtr()) >= 0) {
          return rejectNonCanonical();
        }
        encoded.add(zc::mv(bytes));
      } else {
        return false;
      }
    }
    output.encodeSequenceSize(encoded.size());
    for (const auto& bytes : encoded) { append(output, bytes.asPtr()); }
    return true;
  }

  bool encodeObject(identity::CanonicalEncoder& output, const ObjectTypeData& object) {
    zc::Array<uint8_t> previousName;
    output.encodeSequenceSize(object.fields.size());
    for (const auto& field : object.fields) {
      if (!validMutability(field.mutability) || !validPresence(field.presence)) {
        return rejectClosedValue();
      }
      identity::CanonicalEncoder nameEncoder;
      field.name.encode(nameEncoder);
      auto name = nameEncoder.finish();
      if (previousName.size() != 0 && compareBytes(previousName.asPtr(), name.asPtr()) >= 0) {
        return rejectNonCanonical();
      }
      previousName = zc::mv(name);
      append(output, previousName.asPtr());
      if (!encodeType(output, field.type)) return false;
      output.encodeUint8(static_cast<uint8_t>(field.mutability));
      output.encodeUint8(static_cast<uint8_t>(field.presence));
    }
    return true;
  }

  bool encodeFunction(identity::CanonicalEncoder& output, const FunctionTypeData& function) {
    if (!encodeTypes(output, function.parameters.asPtr()) ||
        !encodeType(output, function.success)) {
      return false;
    }
    ZC_IF_SOME(raises, function.raises) {
      output.encodeSome();
      return encodeType(output, raises);
    }
    output.encodeNone();
    return true;
  }

  bool encodeQualified(identity::CanonicalEncoder& output, Mutability mutability,
                       identity::SemanticTypeId type) {
    if (!validMutability(mutability)) return rejectClosedValue();
    output.encodeUint8(static_cast<uint8_t>(mutability));
    return encodeType(output, type);
  }

  bool encodeInterface(identity::CanonicalEncoder& output,
                       const InterfaceInstantiation& interface) {
    return encodeDefinition(output, interface.interface, DefinitionRole::Interface) &&
           encodeTypes(output, interface.arguments.asPtr());
  }

  zc::Maybe<zc::Array<uint8_t>> existentialInterfaceBytes(
      const ExistentialInterfaceData& interface) {
    identity::CanonicalEncoder output;
    if (!encodeDefinition(output, interface.definition, DefinitionRole::Interface) ||
        !encodeTypes(output, interface.arguments.asPtr())) {
      return zc::none;
    }
    return output.finish();
  }

  bool validateMarkerFacts(zc::ArrayPtr<const identity::DefId> markers) {
    if (markers.size() == 0) return true;
    for (const auto marker : markers) {
      if (definitionBytes(marker, DefinitionRole::Interface) == zc::none) return false;
    }
    return rejectClosedValue();
  }

  bool encodeExistential(identity::CanonicalEncoder& output, const ExistentialTypeData& value) {
    zc::Array<uint8_t> principal;
    ZC_IF_SOME(bytes, existentialInterfaceBytes(value.principal)) { principal = zc::mv(bytes); }
    if (principal.size() == 0) return false;
    append(output, principal.asPtr());

    zc::Array<uint8_t> previous;
    output.encodeSequenceSize(value.additionalInterfaces.size());
    for (const auto& interface : value.additionalInterfaces) {
      ZC_IF_SOME(bytes, existentialInterfaceBytes(interface)) {
        if (compareBytes(principal.asPtr(), bytes.asPtr()) == 0 ||
            (previous.size() != 0 && compareBytes(previous.asPtr(), bytes.asPtr()) >= 0)) {
          return rejectNonCanonical();
        }
        previous = zc::mv(bytes);
        append(output, previous.asPtr());
      } else {
        return false;
      }
    }

    if (!validateMarkerFacts(value.markers.asPtr())) return false;
    output.encodeSequenceSize(0);
    output.encodeSequenceSize(value.associatedBindings.size());
    zc::Array<uint8_t> previousAssociated;
    for (const auto& binding : value.associatedBindings) {
      ZC_IF_SOME(key, definitionBytes(binding.associated, DefinitionRole::AssociatedType)) {
        if (previousAssociated.size() != 0 &&
            compareBytes(previousAssociated.asPtr(), key.asPtr()) >= 0) {
          return rejectNonCanonical();
        }
        previousAssociated = zc::mv(key);
        append(output, previousAssociated.asPtr());
      } else {
        return false;
      }
      if (!encodeType(output, binding.type)) return false;
    }
    return true;
  }

  const SemanticTypeStore& store;
  zc::Vector<identity::SemanticTypeId> active;
  zc::Maybe<AdmissionFailure> failure;
};

}  // namespace zomlang::compiler::type::semantic

namespace zomlang::compiler::type {
namespace {

SemanticTypeAdmissionResult admissionInvariant(identity::IdentityInvariantKind kind,
                                               identity::IdentityAllocationPhase phase) {
  zc::Maybe<zc::Array<uint8_t>> noStructuralKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant =
      identity::IdentityInvariant::from(kind, phase, zc::mv(noStructuralKey), zc::mv(noRange),
                                        identity::IdentityApiSite::CanonicalEncode, 0);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

}  // namespace

SemanticTypeAdmissionResult SemanticTypeStore::canonicalizeClosed(semantic::TypeData&& data) const {
  semantic::StoreBoundTypeEncoder encoder(*this);
  auto key = encoder.encodeKey(data);
  ZC_IF_SOME(value, key) {
    return semantic::CanonicalTypeData(context(), zc::mv(data), zc::mv(value));
  }

  const auto rejection = encoder.rejection();
  return admissionInvariant(rejection.kind, rejection.phase);
}

}  // namespace zomlang::compiler::type
