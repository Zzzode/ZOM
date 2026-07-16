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

#include "zomlang/compiler/type/semantic-type-key.h"

#include "zc/core/vector.h"

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

class Encoder final {
public:
  explicit Encoder(const SemanticTypeKeyResolver& resolver) : resolver(resolver) {}

  bool encode(identity::CanonicalEncoder& output, const TypeData& data) {
    output.encodeUint8(static_cast<uint8_t>(data.tag()));
    switch (data.tag()) {
      case TypeDataTag::Primitive: {
        const auto kind = data.get<PrimitiveTypeData>().kind;
        if (!validPrimitive(kind)) return false;
        output.encodeUint8(static_cast<uint8_t>(kind));
        return true;
      }
      case TypeDataTag::Tuple: {
        const auto& values = data.get<TupleTypeData>().elements;
        return values.size() >= 2 && encodeTypes(output, values.asPtr());
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
        return encodeDefinition(output, value.definition) &&
               encodeTypes(output, value.arguments.asPtr());
      }
      case TypeDataTag::TypeParameter:
        return encodeDefinition(output, data.get<TypeParameterTypeData>().parameter);
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
        return encodeDefinition(output, data.get<InterfaceSelfTypeData>().interface);
    }
    return false;
  }

private:
  zc::Maybe<zc::Array<uint8_t>> definitionBytes(identity::DefId id) const {
    if (!id.isValid()) return zc::none;
    identity::CanonicalEncoder output;
    if (!resolver.encodeDefinition(output, id)) return zc::none;
    auto bytes = output.finish();
    if (bytes.size() == 0) return zc::none;
    return bytes;
  }

  bool encodeDefinition(identity::CanonicalEncoder& output, identity::DefId id) const {
    ZC_IF_SOME(bytes, definitionBytes(id)) {
      append(output, bytes.asPtr());
      return true;
    }
    return false;
  }

  zc::Maybe<zc::Array<uint8_t>> typeBytes(identity::SemanticTypeId id) {
    if (!id.isValid()) return zc::none;
    for (const auto activeId : active) {
      if (activeId == id) return zc::none;
    }
    ZC_IF_SOME(data, resolver.resolve(id)) {
      active.add(id);
      identity::CanonicalEncoder output;
      const auto valid = encode(output, data);
      active.removeLast();
      if (valid) return output.finish();
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
    if (values.size() < 2) return false;
    zc::Vector<zc::Array<uint8_t>> encoded;
    for (const auto id : values) {
      ZC_IF_SOME(child, resolver.resolve(id)) {
        if ((isUnion && child.tag() == TypeDataTag::Union) ||
            (!isUnion && child.tag() == TypeDataTag::Intersection)) {
          return false;
        }
        if (child.tag() == TypeDataTag::Primitive) {
          const auto kind = child.get<PrimitiveTypeData>().kind;
          if (!validPrimitive(kind) ||
              (isUnion && (kind == PrimitiveKind::Never || kind == PrimitiveKind::Any)) ||
              (!isUnion && (kind == PrimitiveKind::Never || kind == PrimitiveKind::Any))) {
            return false;
          }
        }
      }
      else { return false; }
      ZC_IF_SOME(bytes, typeBytes(id)) {
        if (encoded.size() != 0 && compareBytes(encoded.back().asPtr(), bytes.asPtr()) >= 0) {
          return false;
        }
        encoded.add(zc::mv(bytes));
      }
      else { return false; }
    }
    output.encodeSequenceSize(encoded.size());
    for (const auto& bytes : encoded) { append(output, bytes.asPtr()); }
    return true;
  }

  bool encodeObject(identity::CanonicalEncoder& output, const ObjectTypeData& object) {
    zc::Array<uint8_t> previousName;
    output.encodeSequenceSize(object.fields.size());
    for (const auto& field : object.fields) {
      if (!validMutability(field.mutability) || !validPresence(field.presence)) return false;
      identity::CanonicalEncoder nameEncoder;
      field.name.encode(nameEncoder);
      auto name = nameEncoder.finish();
      if (previousName.size() != 0 && compareBytes(previousName.asPtr(), name.asPtr()) >= 0) {
        return false;
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
    if (!validMutability(mutability)) return false;
    output.encodeUint8(static_cast<uint8_t>(mutability));
    return encodeType(output, type);
  }

  bool encodeInterface(identity::CanonicalEncoder& output,
                       const InterfaceInstantiation& interface) {
    return encodeDefinition(output, interface.interface) &&
           encodeTypes(output, interface.arguments.asPtr());
  }

  zc::Maybe<zc::Array<uint8_t>> existentialInterfaceBytes(
      const ExistentialInterfaceData& interface) {
    identity::CanonicalEncoder output;
    if (!encodeDefinition(output, interface.definition) ||
        !encodeTypes(output, interface.arguments.asPtr())) {
      return zc::none;
    }
    return output.finish();
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
          return false;
        }
        previous = zc::mv(bytes);
        append(output, previous.asPtr());
      }
      else { return false; }
    }

    if (!encodeDefinitions(output, value.markers.asPtr())) return false;
    output.encodeSequenceSize(value.associatedBindings.size());
    zc::Array<uint8_t> previousAssociated;
    for (const auto& binding : value.associatedBindings) {
      ZC_IF_SOME(key, definitionBytes(binding.associated)) {
        if (previousAssociated.size() != 0 &&
            compareBytes(previousAssociated.asPtr(), key.asPtr()) >= 0) {
          return false;
        }
        previousAssociated = zc::mv(key);
        append(output, previousAssociated.asPtr());
      }
      else { return false; }
      if (!encodeType(output, binding.type)) return false;
    }
    return true;
  }

  bool encodeDefinitions(identity::CanonicalEncoder& output,
                         zc::ArrayPtr<const identity::DefId> values) const {
    output.encodeSequenceSize(values.size());
    zc::Array<uint8_t> previous;
    for (const auto id : values) {
      ZC_IF_SOME(bytes, definitionBytes(id)) {
        if (previous.size() != 0 && compareBytes(previous.asPtr(), bytes.asPtr()) >= 0)
          return false;
        previous = zc::mv(bytes);
        append(output, previous.asPtr());
      }
      else { return false; }
    }
    return true;
  }

  const SemanticTypeKeyResolver& resolver;
  zc::Vector<identity::SemanticTypeId> active;
};

}  // namespace

zc::Maybe<SemanticTypeKey> encodeSemanticTypeKeyV1(const TypeData& data,
                                                   const SemanticTypeKeyResolver& resolver) {
  identity::CanonicalEncoder output;
  append(output, "zom.semantic-type-key.v1"_zc.asBytes());
  output.encodeUint8(0x00);
  Encoder encoder(resolver);
  if (!encoder.encode(output, data)) return zc::none;
  return SemanticTypeKey(output.finish());
}

}  // namespace zomlang::compiler::type::semantic
