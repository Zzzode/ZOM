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

#include "zomlang/compiler/checker/signature-facts.h"

#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/binder/verified-bound-module-input.h"
#include "zomlang/compiler/checker/scalar-literal-facts.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::checker::signature {
namespace {

struct ConstInteger final {
  CanonicalInteger value;
};
struct ConstFloat32 final {
  uint32_t bits;
};
struct ConstFloat64 final {
  uint64_t bits;
};
struct ConstBool final {
  bool value;
};
struct ConstChar final {
  uint32_t scalar;
};
struct ConstString final {
  zc::Array<uint8_t> bytes;
};
struct ConstNull final {};
struct ConstUnit final {};
struct ConstTuple final {
  zc::Vector<CanonicalConstValue> values;
};
struct ConstArray final {
  zc::Vector<CanonicalConstValue> values;
};
struct ConstObject final {
  zc::Vector<ConstObjectField> fields;
};
struct ConstEnum final {
  identity::DefId variant;
  zc::Vector<CanonicalConstValue> payload;
};

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return !lessBytes(left, right) && !lessBytes(right, left);
}

bool sameTypeHead(const CanonicalTypeHead& left, const CanonicalTypeHead& right) {
  const auto& leftValue = left.variant();
  const auto& rightValue = right.variant();
  if (leftValue.which() != rightValue.which()) return false;
  if (leftValue.is<BlanketTypeHead>() || leftValue.is<ObjectTypeHead>() ||
      leftValue.is<DynamicArrayTypeHead>() || leftValue.is<SliceTypeHead>() ||
      leftValue.is<FixedArrayTypeHead>()) {
    return true;
  }
  if (leftValue.is<PrimitiveTypeHead>()) {
    return leftValue.get<PrimitiveTypeHead>().primitive ==
           rightValue.get<PrimitiveTypeHead>().primitive;
  }
  if (leftValue.is<TupleTypeHead>()) {
    return leftValue.get<TupleTypeHead>().arity == rightValue.get<TupleTypeHead>().arity;
  }
  if (leftValue.is<FunctionTypeHead>()) {
    const auto& leftFunction = leftValue.get<FunctionTypeHead>();
    const auto& rightFunction = rightValue.get<FunctionTypeHead>();
    return leftFunction.arity == rightFunction.arity &&
           leftFunction.hasRaises == rightFunction.hasRaises;
  }
  if (leftValue.is<NominalTypeHead>()) {
    return leftValue.get<NominalTypeHead>().definition ==
           rightValue.get<NominalTypeHead>().definition;
  }
  if (leftValue.is<UnionTypeHead>()) {
    return leftValue.get<UnionTypeHead>().arity == rightValue.get<UnionTypeHead>().arity;
  }
  if (leftValue.is<IntersectionTypeHead>()) {
    return leftValue.get<IntersectionTypeHead>().arity ==
           rightValue.get<IntersectionTypeHead>().arity;
  }
  if (leftValue.is<ReferenceTypeHead>()) {
    return leftValue.get<ReferenceTypeHead>().mutability ==
           rightValue.get<ReferenceTypeHead>().mutability;
  }
  if (leftValue.is<RawPointerTypeHead>()) {
    return leftValue.get<RawPointerTypeHead>().mutability ==
           rightValue.get<RawPointerTypeHead>().mutability;
  }
  return leftValue.get<ExistentialTypeHead>().interface ==
         rightValue.get<ExistentialTypeHead>().interface;
}

zc::Maybe<uint32_t> schemaPreorder(const ast::Tree& tree, ast::NodeId target) {
  uint64_t ordinal = 0;
  zc::Maybe<uint32_t> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node&) {
    if (node == target && ordinal <= UINT32_MAX) { result = static_cast<uint32_t>(ordinal); }
    ++ordinal;
  });
  return result;
}

zc::Maybe<SignatureSourceFailureRef> signatureSourceFailure(
    SignatureSourceDiagnostic diagnostic, const binder::VerifiedBoundModuleInput& input,
    ast::NodeId owner, ast::NodeId primary, uint32_t itemOrdinal = 0) {
  const auto& tree = input.tree();
  if (!tree.contains(owner) || !tree.contains(primary)) return zc::none;
  auto ownerOrdinal = schemaPreorder(tree, owner);
  auto siteOrdinal = schemaPreorder(tree, primary);
  auto span = input.parsedModule().spanFor(tree.node(primary).range);
  if (ownerOrdinal == zc::none || siteOrdinal == zc::none || span == zc::none) return zc::none;
  uint32_t ownerValue = 0;
  uint32_t siteValue = 0;
  ZC_IF_SOME(value, ownerOrdinal) { ownerValue = value; }
  ZC_IF_SOME(value, siteOrdinal) { siteValue = value; }
  ZC_IF_SOME(value, span) {
    return SignatureSourceFailureRef{diagnostic, primary, zc::mv(value),
                                     zc::Vector<SignatureSourceArgument>(),
                                     SignatureEmitterOrdinal{ownerValue, siteValue, itemOrdinal}};
  }
  return zc::none;
}

void encodeRaw(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const uint8_t> bytes) {
  for (const auto byte : bytes) { encoder.encodeUint8(byte); }
}

void encodeAscii(identity::CanonicalEncoder& encoder, zc::StringPtr value) {
  for (const auto character : value) { encoder.encodeUint8(static_cast<uint8_t>(character)); }
}

template <typename T>
bool isKnownEnum(T value, T first, T last) {
  const auto encoded = static_cast<uint64_t>(value);
  return encoded >= static_cast<uint64_t>(first) && encoded <= static_cast<uint64_t>(last);
}

bool decodeAscii(identity::CanonicalDecoder& decoder, zc::StringPtr expected) {
  for (const auto character : expected) {
    auto decoded = decoder.decodeUint8();
    if (decoded == zc::none) return false;
    ZC_IF_SOME(value, decoded) {
      if (value != static_cast<uint8_t>(character)) return false;
    }
  }
  return true;
}

class PatternByteDecoder final {
public:
  PatternByteDecoder(identity::CanonicalDecoder& decoder,
                     const identity::SemanticIdentityRegistrySet& registries) noexcept
      : decoder(decoder), registries(registries) {}

  zc::Maybe<TypeKeyPattern> decodePattern(uint32_t depth = 0) {
    if (depth > kMaximumPatternDepth || remainingNodes == 0) return zc::none;
    --remainingNodes;
    auto tag = decoder.decodeUint8();
    if (tag == zc::none) return zc::none;
    ZC_IF_SOME(value, tag) {
      switch (static_cast<TypeKeyPatternTag>(value)) {
        case TypeKeyPatternTag::Primitive:
          return decodePrimitive();
        case TypeKeyPatternTag::Tuple:
          return decodeTuple(depth);
        case TypeKeyPatternTag::Object:
          return decodeObject(depth);
        case TypeKeyPatternTag::DynamicArray:
          return decodeUnary(depth, &TypeKeyPattern::dynamicArray);
        case TypeKeyPatternTag::Slice:
          return decodeUnary(depth, &TypeKeyPattern::slice);
        case TypeKeyPatternTag::FixedArray:
          return decodeFixedArray(depth);
        case TypeKeyPatternTag::Function:
          return decodeFunction(depth);
        case TypeKeyPatternTag::Nominal:
          return decodeNominal(depth);
        case TypeKeyPatternTag::TypeParameter:
          return decodeTypeParameter();
        case TypeKeyPatternTag::Union:
          return decodeCollection(depth, &TypeKeyPattern::unionOf);
        case TypeKeyPatternTag::Intersection:
          return decodeCollection(depth, &TypeKeyPattern::intersection);
        case TypeKeyPatternTag::Reference:
          return decodeQualifiedUnary(depth, &TypeKeyPattern::reference);
        case TypeKeyPatternTag::RawPointer:
          return decodeQualifiedUnary(depth, &TypeKeyPattern::rawPointer);
        case TypeKeyPatternTag::Existential:
          return decodeExistential(depth);
        case TypeKeyPatternTag::InterfaceBound:
          return decodeInterfaceBound(depth);
        case TypeKeyPatternTag::InterfaceSelf:
          return decodeInterfaceSelf();
        case TypeKeyPatternTag::Parameter:
          return decodeParameter();
      }
    }
    return zc::none;
  }

  zc::Maybe<PatternInterfaceInstantiation> decodePatternInterface(uint32_t depth = 0) {
    auto interface = decodeDefinition();
    if (interface == zc::none) return zc::none;
    auto arguments = decodePatternSequence(depth);
    if (arguments == zc::none) return zc::none;
    ZC_IF_SOME(interfaceValue, interface) {
      ZC_IF_SOME(argumentValues, arguments) {
        return PatternInterfaceInstantiation{interfaceValue, zc::mv(argumentValues)};
      }
    }
    return zc::none;
  }

private:
  static constexpr uint32_t kMaximumPatternDepth = 256;
  static constexpr uint64_t kMaximumPatternNodes = 65536;

  using UnaryFactory = TypeKeyPattern (*)(TypeKeyPattern&&);
  using QualifiedUnaryFactory = TypeKeyPattern (*)(Mutability, TypeKeyPattern&&);
  using CollectionFactory = TypeKeyPattern (*)(zc::Vector<TypeKeyPattern>&&);

  zc::Maybe<TypeKeyPattern> decodePrimitive() {
    auto kind = decoder.decodeUint8();
    ZC_IF_SOME(value, kind) {
      const auto decoded = static_cast<PrimitiveKind>(value);
      if (isKnownEnum(decoded, PrimitiveKind::I8, PrimitiveKind::Null)) {
        return TypeKeyPattern::primitive(decoded);
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeTuple(uint32_t depth) {
    auto elements = decodePatternSequence(depth);
    ZC_IF_SOME(values, elements) { return TypeKeyPattern::tuple(zc::mv(values)); }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeObject(uint32_t depth) {
    auto count = decoder.decodeSequenceSize(kMaximumPatternNodes);
    if (count == zc::none) return zc::none;
    ZC_IF_SOME(size, count) {
      zc::Vector<PatternObjectField> fields(size);
      for (uint64_t index = 0; index < size; ++index) {
        auto name = identity::SemanticIdentifier::decodeCanonical(decoder);
        auto type = decodePattern(depth + 1);
        auto mutability = decodeMutability();
        auto presence = decodeFieldPresence();
        if (name == zc::none || type == zc::none || mutability == zc::none ||
            presence == zc::none) {
          return zc::none;
        }
        ZC_IF_SOME(nameValue, name) {
          ZC_IF_SOME(typeValue, type) {
            ZC_IF_SOME(mutabilityValue, mutability) {
              ZC_IF_SOME(presenceValue, presence) {
                fields.add(PatternObjectField{zc::mv(nameValue), zc::mv(typeValue), mutabilityValue,
                                              presenceValue});
              }
            }
          }
        }
      }
      return TypeKeyPattern::object(zc::mv(fields));
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeUnary(uint32_t depth, UnaryFactory factory) {
    auto child = decodePattern(depth + 1);
    ZC_IF_SOME(value, child) { return factory(zc::mv(value)); }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeFixedArray(uint32_t depth) {
    auto element = decodePattern(depth + 1);
    auto length = decoder.decodeUint64();
    if (element == zc::none || length == zc::none) return zc::none;
    ZC_IF_SOME(elementValue, element) {
      ZC_IF_SOME(lengthValue, length) {
        return TypeKeyPattern::fixedArray(zc::mv(elementValue), lengthValue);
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeFunction(uint32_t depth) {
    auto parameters = decodePatternSequence(depth);
    auto success = decodePattern(depth + 1);
    auto optionalTag = decoder.decodeUint8();
    if (parameters == zc::none || success == zc::none || optionalTag == zc::none) {
      return zc::none;
    }
    zc::Maybe<TypeKeyPattern> raises;
    ZC_IF_SOME(value, optionalTag) {
      if (value == 0x01) {
        auto decodedRaises = decodePattern(depth + 1);
        if (decodedRaises == zc::none) return zc::none;
        ZC_IF_SOME(raisesValue, decodedRaises) { raises = zc::mv(raisesValue); }
      } else if (value != 0x00) {
        return zc::none;
      }
    }
    ZC_IF_SOME(parameterValues, parameters) {
      ZC_IF_SOME(successValue, success) {
        return TypeKeyPattern::function(
            PatternFunctionType{zc::mv(parameterValues), zc::mv(successValue), zc::mv(raises)});
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeNominal(uint32_t depth) {
    auto definition = decodeDefinition();
    auto arguments = decodePatternSequence(depth);
    if (definition == zc::none || arguments == zc::none) return zc::none;
    ZC_IF_SOME(definitionValue, definition) {
      ZC_IF_SOME(argumentValues, arguments) {
        return TypeKeyPattern::nominal(definitionValue, zc::mv(argumentValues));
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeTypeParameter() {
    auto bytes = decoder.decodeBytes(32);
    if (bytes == zc::none) return zc::none;
    ZC_IF_SOME(value, bytes) {
      auto key = identity::GenericParameterKey::fromBytes(value.asPtr());
      if (key == zc::none) return zc::none;
      ZC_IF_SOME(keyValue, key) {
        if (registries.genericParameters().find(keyValue) == zc::none) return zc::none;
        return TypeKeyPattern::typeParameter(zc::mv(keyValue));
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeCollection(uint32_t depth, CollectionFactory factory) {
    auto values = decodePatternSequence(depth);
    ZC_IF_SOME(decoded, values) { return factory(zc::mv(decoded)); }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeQualifiedUnary(uint32_t depth, QualifiedUnaryFactory factory) {
    auto mutability = decodeMutability();
    auto child = decodePattern(depth + 1);
    if (mutability == zc::none || child == zc::none) return zc::none;
    ZC_IF_SOME(mutabilityValue, mutability) {
      ZC_IF_SOME(childValue, child) { return factory(mutabilityValue, zc::mv(childValue)); }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeExistential(uint32_t depth) {
    auto principal = decodeExistentialInterface(depth);
    auto additional = decodeExistentialInterfaces(depth);
    auto markers = decodeDefinitions();
    auto bindings = decodeAssociatedBindings(depth);
    if (principal == zc::none || additional == zc::none || markers == zc::none ||
        bindings == zc::none) {
      return zc::none;
    }
    ZC_IF_SOME(principalValue, principal) {
      ZC_IF_SOME(additionalValues, additional) {
        ZC_IF_SOME(markerValues, markers) {
          ZC_IF_SOME(bindingValues, bindings) {
            return TypeKeyPattern::existential(
                PatternExistentialType{zc::mv(principalValue), zc::mv(additionalValues),
                                       zc::mv(markerValues), zc::mv(bindingValues)});
          }
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeInterfaceBound(uint32_t depth) {
    auto interface = decodePatternInterface(depth);
    ZC_IF_SOME(value, interface) { return TypeKeyPattern::interfaceBound(zc::mv(value)); }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeInterfaceSelf() {
    auto interface = decodeDefinition();
    ZC_IF_SOME(value, interface) { return TypeKeyPattern::interfaceSelf(value); }
    return zc::none;
  }

  zc::Maybe<TypeKeyPattern> decodeParameter() {
    auto index = decoder.decodeUint32();
    ZC_IF_SOME(value, index) { return TypeKeyPattern::parameter(value); }
    return zc::none;
  }

  zc::Maybe<zc::Vector<TypeKeyPattern>> decodePatternSequence(uint32_t depth) {
    auto count = decoder.decodeSequenceSize(kMaximumPatternNodes);
    if (count == zc::none) return zc::none;
    ZC_IF_SOME(size, count) {
      zc::Vector<TypeKeyPattern> patterns(size);
      for (uint64_t index = 0; index < size; ++index) {
        auto pattern = decodePattern(depth + 1);
        if (pattern == zc::none) return zc::none;
        ZC_IF_SOME(value, pattern) { patterns.add(zc::mv(value)); }
      }
      return patterns;
    }
    return zc::none;
  }

  zc::Maybe<PatternExistentialInterface> decodeExistentialInterface(uint32_t depth) {
    auto definition = decodeDefinition();
    auto arguments = decodePatternSequence(depth);
    if (definition == zc::none || arguments == zc::none) return zc::none;
    ZC_IF_SOME(definitionValue, definition) {
      ZC_IF_SOME(argumentValues, arguments) {
        return PatternExistentialInterface{definitionValue, zc::mv(argumentValues)};
      }
    }
    return zc::none;
  }

  zc::Maybe<zc::Vector<PatternExistentialInterface>> decodeExistentialInterfaces(uint32_t depth) {
    auto count = decoder.decodeSequenceSize(kMaximumPatternNodes);
    if (count == zc::none) return zc::none;
    ZC_IF_SOME(size, count) {
      zc::Vector<PatternExistentialInterface> interfaces(size);
      for (uint64_t index = 0; index < size; ++index) {
        auto interface = decodeExistentialInterface(depth);
        if (interface == zc::none) return zc::none;
        ZC_IF_SOME(value, interface) { interfaces.add(zc::mv(value)); }
      }
      return interfaces;
    }
    return zc::none;
  }

  zc::Maybe<zc::Vector<identity::DefId>> decodeDefinitions() {
    auto count = decoder.decodeSequenceSize(kMaximumPatternNodes);
    if (count == zc::none) return zc::none;
    ZC_IF_SOME(size, count) {
      zc::Vector<identity::DefId> definitions(size);
      for (uint64_t index = 0; index < size; ++index) {
        auto definition = decodeDefinition();
        if (definition == zc::none) return zc::none;
        ZC_IF_SOME(value, definition) { definitions.add(value); }
      }
      return definitions;
    }
    return zc::none;
  }

  zc::Maybe<zc::Vector<PatternAssociatedTypeBinding>> decodeAssociatedBindings(uint32_t depth) {
    auto count = decoder.decodeSequenceSize(kMaximumPatternNodes);
    if (count == zc::none) return zc::none;
    ZC_IF_SOME(size, count) {
      zc::Vector<PatternAssociatedTypeBinding> bindings(size);
      for (uint64_t index = 0; index < size; ++index) {
        auto associated = decodeDefinition();
        auto type = decodePattern(depth + 1);
        if (associated == zc::none || type == zc::none) return zc::none;
        ZC_IF_SOME(associatedValue, associated) {
          ZC_IF_SOME(typeValue, type) {
            bindings.add(PatternAssociatedTypeBinding{associatedValue, zc::mv(typeValue)});
          }
        }
      }
      return bindings;
    }
    return zc::none;
  }

  zc::Maybe<identity::DefId> decodeDefinition() {
    auto bytes = decoder.decodeBytes(32);
    if (bytes == zc::none) return zc::none;
    ZC_IF_SOME(value, bytes) {
      auto key = identity::DefinitionKey::fromBytes(value.asPtr());
      ZC_IF_SOME(keyValue, key) { return registries.definitions().find(keyValue); }
    }
    return zc::none;
  }

  zc::Maybe<Mutability> decodeMutability() {
    auto value = decoder.decodeUint8();
    ZC_IF_SOME(encoded, value) {
      const auto decoded = static_cast<Mutability>(encoded);
      if (isKnownEnum(decoded, Mutability::Const, Mutability::Mutable)) return decoded;
    }
    return zc::none;
  }

  zc::Maybe<FieldPresence> decodeFieldPresence() {
    auto value = decoder.decodeUint8();
    ZC_IF_SOME(encoded, value) {
      const auto decoded = static_cast<FieldPresence>(encoded);
      if (isKnownEnum(decoded, FieldPresence::Required, FieldPresence::Optional)) return decoded;
    }
    return zc::none;
  }

  identity::CanonicalDecoder& decoder;
  const identity::SemanticIdentityRegistrySet& registries;
  uint64_t remainingNodes = kMaximumPatternNodes;
};

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

identity::IdentityInvariant invalidContextInvariant(uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto result = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Context,
      zc::mv(noStructural), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

struct EncodedSignature final {
  zc::Array<uint8_t> bytes;
};

enum class RecordFailureKind : uint8_t { InvalidFact, CanonicalCodec };
struct RecordFailure final {
  RecordFailureKind kind;
  uint32_t ordinal;
};

using RecordEncodingResult =
    zc::OneOf<EncodedSignature, identity::IdentityInvariant, RecordFailure>;

bool isCallableDefinition(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
      return true;
    default:
      return false;
  }
}

bool isNominalDefinition(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Class:
    case identity::DefinitionKind::Struct:
    case identity::DefinitionKind::Enum:
    case identity::DefinitionKind::Error:
      return true;
    default:
      return false;
  }
}

bool isValueDefinition(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Field:
    case identity::DefinitionKind::Constant:
    case identity::DefinitionKind::Static:
      return true;
    default:
      return false;
  }
}

bool payloadMatchesDefinition(const SemanticSignature& signature) {
  const auto& payload = signature.payload.variant();
  if (payload.is<CallableSignature>()) return isCallableDefinition(signature.definitionKind);
  if (payload.is<NominalSignature>()) return isNominalDefinition(signature.definitionKind);
  if (payload.is<InterfaceSignature>()) {
    return signature.definitionKind == identity::DefinitionKind::Interface;
  }
  if (payload.is<TypeAliasSignature>()) {
    return signature.definitionKind == identity::DefinitionKind::TypeAlias;
  }
  if (payload.is<AssociatedTypeSignature>()) {
    return signature.definitionKind == identity::DefinitionKind::AssociatedType;
  }
  if (payload.is<ValueSignature>()) return isValueDefinition(signature.definitionKind);
  if (payload.is<EnumVariantSignature>()) {
    return signature.definitionKind == identity::DefinitionKind::EnumVariant;
  }
  return false;
}

CheckerInvariantFact checkerInvariant(CheckerInvariantKind kind, identity::ModuleId module,
                                      uint32_t ordinal, uint32_t field = 0) {
  zc::Maybe<identity::DefId> noOwner;
  zc::Maybe<ast::NodeId> noNode;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> path;
  if (field != 0) { path.add(field); }
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  return CheckerInvariantFact{kind,
                              CheckerInvariantStage::Signature,
                              module,
                              zc::mv(noOwner),
                              zc::mv(noNode),
                              zc::mv(noSpan),
                              zc::mv(path),
                              zc::mv(noExpected),
                              zc::mv(noActual),
                              ordinal};
}

SignatureFactsVerificationResult reject(CheckerInvariantFact&& fact) {
  zc::Vector<CheckerVerificationFailure> failures;
  failures.add(CheckerVerificationFailure(zc::mv(fact)));
  return SignatureFactsInvariantRejected{zc::mv(failures)};
}

SignatureFactsVerificationResult reject(identity::IdentityInvariant&& fact) {
  zc::Vector<CheckerVerificationFailure> failures;
  failures.add(CheckerVerificationFailure(zc::mv(fact)));
  return SignatureFactsInvariantRejected{zc::mv(failures)};
}

SignatureFactsInvariantRejected buildReject(CheckerInvariantFact&& fact) {
  zc::Vector<CheckerVerificationFailure> failures;
  failures.add(CheckerVerificationFailure(zc::mv(fact)));
  return SignatureFactsInvariantRejected{zc::mv(failures)};
}

SignatureFactsInvariantRejected buildReject(identity::IdentityInvariant&& fact) {
  zc::Vector<CheckerVerificationFailure> failures;
  failures.add(CheckerVerificationFailure(zc::mv(fact)));
  return SignatureFactsInvariantRejected{zc::mv(failures)};
}

bool sameSourceKey(const identity::SourceFileKey& left, const identity::SourceFileKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameDefinitionKey(const identity::DefinitionKey& left, const identity::DefinitionKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameImplKey(const identity::ImplKey& left, const identity::ImplKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return sameSourceKey(left.source(), right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
}

bool sameDefinitionSite(const binder::DefinitionSite& left, const binder::DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DeclarationDefinitionSite>()) {
    return rightValue.is<binder::DeclarationDefinitionSite>() &&
           leftValue.get<binder::DeclarationDefinitionSite>().node ==
               rightValue.get<binder::DeclarationDefinitionSite>().node;
  }
  if (!rightValue.is<binder::PatternBindingSite>()) { return false; }
  const auto& leftPattern = leftValue.get<binder::PatternBindingSite>();
  const auto& rightPattern = rightValue.get<binder::PatternBindingSite>();
  if (leftPattern.introducer != rightPattern.introducer ||
      leftPattern.patternPath.size() != rightPattern.patternPath.size()) {
    return false;
  }
  for (size_t index = 0; index < leftPattern.patternPath.size(); ++index) {
    if (leftPattern.patternPath[index] != rightPattern.patternPath[index]) { return false; }
  }
  return true;
}

bool bindingInventoryMatches(const binder::VerifiedBoundModuleInput& input,
                             const binder::VerifiedBindingMetadata& metadata,
                             const identity::SemanticIdentityRegistrySet& registries) {
  const auto frozenDefinitions = input.definitions().definitions();
  const auto boundDefinitions = metadata.definitions();
  if (frozenDefinitions.size() != boundDefinitions.size()) { return false; }
  for (const auto& frozen : frozenDefinitions) {
    zc::Maybe<const binder::DefinitionFact&> match;
    for (const auto& bound : boundDefinitions) {
      if (bound.identity == frozen.definition) {
        if (match != zc::none) { return false; }
        match = bound;
      }
    }
    if (match == zc::none) { return false; }
    ZC_IF_SOME(bound, match) {
      if (bound.kind != frozen.record.kind() || !sameDefinitionSite(bound.site, frozen.site) ||
          !sameSpan(bound.source, frozen.source)) {
        return false;
      }
    }
    auto registered = registries.definitions().lookup(frozen.definition);
    auto registeredRecord = registries.definitions().lookupRecord(frozen.definition);
    if (registered == zc::none || registeredRecord == zc::none) { return false; }
    ZC_IF_SOME(key, registered) {
      if (!sameDefinitionKey(key, frozen.key)) return false;
    }
    ZC_IF_SOME(record, registeredRecord) {
      if (record.kind() != frozen.record.kind()) return false;
    }
  }

  const auto frozenImpls = input.definitions().impls();
  const auto boundImpls = metadata.impls();
  if (frozenImpls.size() != boundImpls.size()) { return false; }
  for (const auto& frozen : frozenImpls) {
    zc::Maybe<const binder::ImplBindingFact&> match;
    for (const auto& bound : boundImpls) {
      if (bound.occurrence == frozen.occurrence) {
        if (match != zc::none) { return false; }
        match = bound;
      }
    }
    if (match == zc::none) { return false; }
    ZC_IF_SOME(bound, match) {
      if (bound.authority != frozen.authority || bound.node != frozen.node ||
          !sameSpan(bound.source, frozen.source)) {
        return false;
      }
    }
    auto registered = registries.impls().lookup(frozen.authority);
    if (registered == zc::none) { return false; }
    ZC_IF_SOME(key, registered) {
      if (!sameImplKey(key, frozen.key.implementation())) { return false; }
    }
  }
  return true;
}

bool isSignatureBearingDefinition(identity::DefinitionKind kind) {
  switch (kind) {
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
    case identity::DefinitionKind::Class:
    case identity::DefinitionKind::Struct:
    case identity::DefinitionKind::Interface:
    case identity::DefinitionKind::Enum:
    case identity::DefinitionKind::Error:
    case identity::DefinitionKind::TypeAlias:
    case identity::DefinitionKind::AssociatedType:
    case identity::DefinitionKind::Field:
    case identity::DefinitionKind::EnumVariant:
    case identity::DefinitionKind::Constant:
    case identity::DefinitionKind::Static:
      return true;
    case identity::DefinitionKind::ModuleAlias:
    case identity::DefinitionKind::Parameter:
    case identity::DefinitionKind::TypeParameter:
    case identity::DefinitionKind::Local:
    case identity::DefinitionKind::PatternBinding:
    case identity::DefinitionKind::Closure:
      return false;
  }
  ZC_UNREACHABLE
}

zc::Maybe<ast::BindingDeclarationKind> declarationKindForDeclarator(const ast::Tree& tree,
                                                                    ast::NodeId declarator) {
  zc::Maybe<ast::BindingDeclarationKind> result;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::LetStmt) return;
    const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
    if (!tree.contains(declarations) ||
        tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
      return;
    }
    const auto& list = tree.node(declarations);
    const ast::NodeList values{list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                               list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
    if (!tree.contains(values)) return;
    for (const auto value : tree.list(values)) {
      if (value != declarator) continue;
      if (result != zc::none) {
        result = zc::none;
        return;
      }
      result =
          static_cast<ast::BindingDeclarationKind>(syntax.payload.words[ast::kLetStmtKindWord]);
    }
  });
  return result;
}

zc::Maybe<checked::CheckedNodeKey> checkedNodeKey(
    const binder::VerifiedBoundModuleInput& boundModule, ast::NodeId target) {
  if (!boundModule.tree().contains(target)) return zc::none;
  zc::Maybe<uint32_t> preorder;
  uint32_t next = 0;
  ast::visitTreePreOrder(boundModule.tree(), boundModule.tree().root(),
                         [&](ast::NodeId node, const ast::Node&) {
                           if (node == target && preorder == zc::none) preorder = next;
                           ++next;
                         });
  auto span = boundModule.parsedModule().spanFor(boundModule.tree().node(target).range);
  if (preorder == zc::none || span == zc::none) return zc::none;
  ZC_IF_SOME(ordinal, preorder) {
    ZC_IF_SOME(sourceSpan, span) {
      return checked::CheckedNodeKey{static_cast<uint32_t>(boundModule.tree().node(target).kind),
                                     ordinal, sourceSpan.clone()};
    }
  }
  return zc::none;
}

zc::Maybe<const binder::DefinitionFact&> findDefinitionFact(
    const binder::VerifiedBindingMetadata& metadata, identity::DefId definition) {
  zc::Maybe<const binder::DefinitionFact&> result;
  for (const auto& fact : metadata.definitions()) {
    if (fact.identity != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = fact;
  }
  return result;
}

zc::Maybe<identity::DefId> resolvedDefinitionAtNode(const binder::VerifiedBindingMetadata& metadata,
                                                    ast::NodeId node);

zc::Maybe<const binder::ScopeRecord&> findScope(const binder::VerifiedBindingMetadata& metadata,
                                                binder::ScopeId scope) {
  for (const auto& value : metadata.scopes()) {
    if (value.id == scope) return value;
  }
  return zc::none;
}

zc::Maybe<SignatureScope> signatureScope(const binder::VerifiedBoundModuleInput& input,
                                         const binder::VerifiedBindingMetadata& metadata,
                                         const binder::DefinitionFact& definition,
                                         identity::ModuleId module) {
  auto scope = findScope(metadata, definition.declaringScope);
  if (scope == zc::none) return zc::none;
  ZC_IF_SOME(value, scope) {
    const auto& owner = value.owner.value();
    if (value.kind == binder::ScopeKind::Module && owner.is<binder::ModuleScopeOwner>() &&
        owner.get<binder::ModuleScopeOwner>().module == module) {
      return SignatureScope(ModuleDefinitionSignatureScope{});
    }
    if (owner.is<binder::DefinitionScopeOwner>()) {
      const auto ownerDefinition = owner.get<binder::DefinitionScopeOwner>().definition;
      if ((definition.kind == identity::DefinitionKind::Method ||
           definition.kind == identity::DefinitionKind::Field) &&
          definition.memberVisibility != zc::none) {
        MemberVisibility visibility = MemberVisibility::Private;
        ZC_IF_SOME(source, definition.memberVisibility) {
          switch (source) {
            case binder::MemberVisibility::Public:
              visibility = MemberVisibility::Public;
              break;
            case binder::MemberVisibility::Protected:
              visibility = MemberVisibility::Protected;
              break;
            case binder::MemberVisibility::Private:
              visibility = MemberVisibility::Private;
              break;
          }
        }
        return SignatureScope(MemberSignatureScope{ownerDefinition, visibility});
      }
      return SignatureScope(EnclosedSignatureScope{ownerDefinition});
    }
    if (value.kind == binder::ScopeKind::ImplBody && owner.is<binder::ImplScopeOwner>()) {
      const auto occurrence = owner.get<binder::ImplScopeOwner>().occurrence;
      for (const auto& impl : input.definitions().impls()) {
        if (impl.occurrence != occurrence || !input.tree().contains(impl.node)) continue;
        const auto& syntax = input.tree().node(impl.node);
        if (syntax.kind != ast::SyntaxKind::StandaloneImplDecl) return zc::none;
        const ast::NodeId interfaceNode(
            syntax.payload.words[ast::kStandaloneImplDeclInterfaceWord]);
        if (!input.tree().contains(interfaceNode) ||
            input.tree().node(interfaceNode).kind != ast::SyntaxKind::NamedTypeExpr) {
          return zc::none;
        }
        auto interface = resolvedDefinitionAtNode(
            metadata,
            ast::NodeId(
                input.tree().node(interfaceNode).payload.words[ast::kNamedTypeExprPathWord]));
        ZC_IF_SOME(value, interface) { return SignatureScope(EnclosedSignatureScope{value}); }
      }
    }
  }
  return zc::none;
}

bool sameDefinitionName(const identity::DeclaredDefinitionName& left,
                        const identity::DeclaredDefinitionName& right) {
  identity::CanonicalEncoder leftEncoder;
  identity::CanonicalEncoder rightEncoder;
  left.encode(leftEncoder);
  right.encode(rightEncoder);
  const auto leftBytes = leftEncoder.finish();
  const auto rightBytes = rightEncoder.finish();
  return sameBytes(leftBytes.asPtr(), rightBytes.asPtr());
}

zc::Maybe<identity::DefId> directAssociatedType(const binder::VerifiedBindingMetadata& metadata,
                                                identity::DefId interface,
                                                const identity::DeclaredDefinitionName& name) {
  zc::Maybe<identity::DefId> result;
  for (const auto& definition : metadata.definitions()) {
    if (definition.kind != identity::DefinitionKind::AssociatedType ||
        !sameDefinitionName(definition.name, name)) {
      continue;
    }
    auto scope = findScope(metadata, definition.declaringScope);
    if (scope == zc::none) continue;
    ZC_IF_SOME(value, scope) {
      const auto& owner = value.owner.value();
      if (value.kind != binder::ScopeKind::TypeBody || !owner.is<binder::DefinitionScopeOwner>() ||
          owner.get<binder::DefinitionScopeOwner>().definition != interface) {
        continue;
      }
      if (result != zc::none) return zc::none;
      result = definition.identity;
    }
  }
  return result;
}

bool isSimpleCallableDeclaration(const ast::Tree& tree, ast::NodeId declaration,
                                 identity::DefinitionKind definitionKind) {
  if (!tree.contains(declaration)) { return false; }
  const auto& syntax = tree.node(declaration);
  ast::NodeId parameters;
  if (definitionKind == identity::DefinitionKind::Function &&
      syntax.kind == ast::SyntaxKind::FunctionDecl) {
    if (tree.contains(ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord])) ||
        tree.contains(ast::NodeId(syntax.payload.words[ast::kFunctionDeclRetTyWord])) ||
        tree.contains(ast::NodeId(syntax.payload.words[ast::kFunctionDeclRaisesTyWord]))) {
      return false;
    }
    parameters = ast::NodeId(syntax.payload.words[ast::kFunctionDeclParamsIdWord]);
  } else if (definitionKind == identity::DefinitionKind::Method &&
             syntax.kind == ast::SyntaxKind::MethodDecl) {
    if (tree.contains(ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord])) ||
        tree.contains(ast::NodeId(syntax.payload.words[ast::kMethodDeclRetTyWord])) ||
        tree.contains(ast::NodeId(syntax.payload.words[ast::kMethodDeclRaisesTyWord]))) {
      return false;
    }
    parameters = ast::NodeId(syntax.payload.words[ast::kMethodDeclParamsIdWord]);
  } else {
    return false;
  }
  if (!tree.contains(parameters)) { return false; }
  const auto& parameterSyntax = tree.node(parameters);
  if (parameterSyntax.kind != ast::SyntaxKind::FunctionParameterList) { return false; }
  const ast::NodeList parameterList{
      parameterSyntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
      parameterSyntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
  return tree.contains(parameterList) && parameterList.empty();
}

struct BuiltSourceType final {
  identity::SemanticTypeId type;
  TypeKeyPattern pattern;
};

class SourceTypeBuilder final {
public:
  SourceTypeBuilder(const binder::VerifiedBoundModuleInput& boundModule,
                    const identity::SemanticIdentityRegistrySet& registries,
                    type::SemanticTypeStore& semanticTypes,
                    zc::ArrayPtr<const identity::GenericParameterId> genericParameters)
      : boundModule(boundModule),
        registries(registries),
        semanticTypes(semanticTypes),
        genericParameters(genericParameters) {}

  zc::Maybe<BuiltSourceType> build(ast::NodeId node) {
    const auto& tree = boundModule.tree();
    if (!tree.contains(node)) return zc::none;
    const auto& syntax = tree.node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::PredefinedTypeExpr:
        return buildPrimitive(syntax.payload.words[ast::kPredefinedTypeExprKindWord]);
      case ast::SyntaxKind::NamedTypeExpr:
        return buildNamed(syntax);
      case ast::SyntaxKind::TupleTypeExpr:
        return buildTuple(syntax);
      case ast::SyntaxKind::ArrayTypeExpr:
        return buildArray(syntax);
      case ast::SyntaxKind::FixedArrayTypeExpr:
        return buildFixedArray(syntax.payload.words[ast::kFixedArrayTypeExprElemWord],
                               syntax.payload.words[ast::kFixedArrayTypeExprLenExprWord]);
      case ast::SyntaxKind::SliceArrayTypeExpr:
        return buildUnary(
            ast::NodeId(syntax.payload.words[ast::kSliceArrayTypeExprElemWord]),
            [](identity::SemanticTypeId element) {
              return type::semantic::TypeData(type::semantic::SliceTypeData{element});
            },
            [](TypeKeyPattern&& element) { return TypeKeyPattern::slice(zc::mv(element)); });
      case ast::SyntaxKind::ReferenceTypeExpr:
        return buildReference(syntax, false);
      case ast::SyntaxKind::RawPointerTypeExpr:
        return buildReference(syntax, true);
      case ast::SyntaxKind::FunctionTypeExpr:
        return buildFunction(syntax);
      case ast::SyntaxKind::UnionTypeExpr:
        return buildSet(ast::NodeList{syntax.payload.words[ast::kUnionTypeExprAltsFirstWord],
                                      syntax.payload.words[ast::kUnionTypeExprAltsSizeWord]},
                        true);
      case ast::SyntaxKind::IntersectionTypeExpr:
        return buildSet(ast::NodeList{syntax.payload.words[ast::kIntersectionTypeExprAltsFirstWord],
                                      syntax.payload.words[ast::kIntersectionTypeExprAltsSizeWord]},
                        false);
      case ast::SyntaxKind::ObjectTypeExpr:
        return buildObject(syntax);
      case ast::SyntaxKind::OptionalTypeExpr:
        return buildOptional(syntax);
      default:
        return zc::none;
    }
  }

  zc::Maybe<InterfaceInstantiation> buildInterface(ast::NodeId node) {
    const auto& tree = boundModule.tree();
    if (!tree.contains(node)) return zc::none;
    const auto& syntax = tree.node(node);
    if (syntax.kind != ast::SyntaxKind::NamedTypeExpr) return zc::none;
    auto definition =
        resolvedDefinition(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]));
    if (definition == zc::none) return zc::none;
    identity::DefId interface;
    ZC_IF_SOME(value, definition) { interface = value; }
    ZC_IF_SOME(record, registries.definitions().lookupRecord(interface)) {
      if (record.kind() != identity::DefinitionKind::Interface) return zc::none;
    } else {
      return zc::none;
    }
    const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                  syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
    if (!tree.contains(arguments)) return zc::none;
    zc::Vector<identity::SemanticTypeId> argumentTypes(arguments.size);
    for (const auto argument : tree.list(arguments)) {
      auto built = build(argument);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) { argumentTypes.add(value.type); }
    }
    return InterfaceInstantiation{interface, zc::mv(argumentTypes)};
  }

  zc::Maybe<PatternInterfaceInstantiation> buildPatternInterface(ast::NodeId node) {
    const auto& tree = boundModule.tree();
    if (!tree.contains(node)) return zc::none;
    const auto& syntax = tree.node(node);
    if (syntax.kind != ast::SyntaxKind::NamedTypeExpr) return zc::none;
    auto definition =
        resolvedDefinition(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]));
    if (definition == zc::none) return zc::none;
    identity::DefId interface;
    ZC_IF_SOME(value, definition) { interface = value; }
    ZC_IF_SOME(record, registries.definitions().lookupRecord(interface)) {
      if (record.kind() != identity::DefinitionKind::Interface) return zc::none;
    } else {
      return zc::none;
    }
    const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                  syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
    if (!tree.contains(arguments)) return zc::none;
    zc::Vector<TypeKeyPattern> patterns(arguments.size);
    for (const auto argument : tree.list(arguments)) {
      auto built = build(argument);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) { patterns.add(zc::mv(value.pattern)); }
    }
    return PatternInterfaceInstantiation{interface, zc::mv(patterns)};
  }

  zc::Maybe<identity::DefId> definitionAtPath(ast::NodeId node) const {
    return resolvedDefinition(node);
  }

private:
  template <typename DataBuilder, typename PatternBuilder>
  zc::Maybe<BuiltSourceType> buildUnary(ast::NodeId child, DataBuilder dataBuilder,
                                        PatternBuilder patternBuilder) {
    auto built = build(child);
    if (built == zc::none) return zc::none;
    ZC_IF_SOME(value, built) {
      return intern(dataBuilder(value.type), patternBuilder(zc::mv(value.pattern)));
    }
    return zc::none;
  }

  zc::Maybe<BuiltSourceType> intern(type::semantic::TypeData&& data, TypeKeyPattern&& pattern) {
    auto admitted = semanticTypes.canonicalizeClosed(zc::mv(data));
    if (!admitted.is<type::semantic::CanonicalTypeData>()) return zc::none;
    auto interned = semanticTypes.intern(zc::mv(admitted.get<type::semantic::CanonicalTypeData>()));
    if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
    return BuiltSourceType{interned.get<type::SemanticTypeInterned>().id, zc::mv(pattern)};
  }

  zc::Maybe<BuiltSourceType> buildPrimitive(uint32_t kind) {
    PrimitiveKind primitive;
    switch (kind) {
      case 0:
        primitive = PrimitiveKind::I8;
        break;
      case 1:
        primitive = PrimitiveKind::I16;
        break;
      case 2:
        primitive = PrimitiveKind::I32;
        break;
      case 3:
        primitive = PrimitiveKind::I64;
        break;
      case 4:
        primitive = PrimitiveKind::U8;
        break;
      case 5:
        primitive = PrimitiveKind::U16;
        break;
      case 6:
        primitive = PrimitiveKind::U32;
        break;
      case 7:
        primitive = PrimitiveKind::U64;
        break;
      case 8:
        primitive = PrimitiveKind::F32;
        break;
      case 9:
        primitive = PrimitiveKind::F64;
        break;
      case 10:
        primitive = PrimitiveKind::Bool;
        break;
      case 11:
        primitive = PrimitiveKind::Str;
        break;
      case 12:
        primitive = PrimitiveKind::Char;
        break;
      case 13:
        primitive = PrimitiveKind::Null;
        break;
      case 14:
        primitive = PrimitiveKind::Unit;
        break;
      case 15:
        primitive = PrimitiveKind::Never;
        break;
      case 16:
        primitive = PrimitiveKind::Any;
        break;
      default:
        return zc::none;
    }
    return intern(type::semantic::TypeData(type::semantic::PrimitiveTypeData{primitive}),
                  TypeKeyPattern::primitive(primitive));
  }

  zc::Maybe<const binder::BindingTargetValue&> resolvedTarget(ast::NodeId node) const {
    zc::Maybe<const binder::BindingTargetValue&> result;
    for (const auto& resolution : boundModule.bindings().nodeBindings()) {
      if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
        continue;
      }
      const auto& target =
          resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
      if (result != zc::none) return zc::none;
      result = target;
    }
    return result;
  }

  zc::Maybe<identity::DefId> resolvedDefinition(ast::NodeId node) const {
    auto target = resolvedTarget(node);
    ZC_IF_SOME(value, target) {
      if (value.is<binder::DefinitionBindingTarget>()) {
        return value.get<binder::DefinitionBindingTarget>().definition;
      }
    }
    return zc::none;
  }

  zc::Maybe<BuiltSourceType> buildNamed(const ast::Node& syntax) {
    const auto& tree = boundModule.tree();
    const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                  syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
    if (!tree.contains(arguments)) return zc::none;
    auto resolved = resolvedTarget(ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]));
    if (resolved == zc::none) return zc::none;
    ZC_IF_SOME(target, resolved) {
      if (target.is<binder::GenericParameterBindingTarget>()) {
        const auto parameter = target.get<binder::GenericParameterBindingTarget>().parameter;
        if (arguments.size != 0) return zc::none;
        for (size_t index = 0; index < genericParameters.size(); ++index) {
          if (genericParameters[index] != parameter) continue;
          ZC_IF_SOME(key, registries.genericParameters().lookup(parameter)) {
            return intern(
                type::semantic::TypeData(type::semantic::TypeParameterTypeData{key.clone()}),
                TypeKeyPattern::parameter(static_cast<uint32_t>(index)));
          }
          return zc::none;
        }
        return zc::none;
      }
      if (!target.is<binder::DefinitionBindingTarget>()) return zc::none;
    }
    identity::DefId definition;
    ZC_IF_SOME(target, resolved) {
      definition = target.get<binder::DefinitionBindingTarget>().definition;
    }
    auto record = registries.definitions().lookupRecord(definition);
    if (record == zc::none) return zc::none;
    identity::DefinitionKind kind = identity::DefinitionKind::Local;
    ZC_IF_SOME(value, record) { kind = value.kind(); }
    if (kind == identity::DefinitionKind::TypeParameter ||
        kind == identity::DefinitionKind::Parameter) {
      return zc::none;
    }
    zc::Vector<identity::SemanticTypeId> argumentTypes(arguments.size);
    zc::Vector<TypeKeyPattern> argumentPatterns(arguments.size);
    for (const auto argument : tree.list(arguments)) {
      auto built = build(argument);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) {
        argumentTypes.add(value.type);
        argumentPatterns.add(zc::mv(value.pattern));
      }
    }
    if (kind == identity::DefinitionKind::Interface) {
      InterfaceInstantiation interface{definition, zc::mv(argumentTypes)};
      return intern(
          type::semantic::TypeData(type::semantic::InterfaceBoundTypeData{zc::mv(interface)}),
          TypeKeyPattern::interfaceBound(
              PatternInterfaceInstantiation{definition, zc::mv(argumentPatterns)}));
    }
    if (kind != identity::DefinitionKind::Class && kind != identity::DefinitionKind::Struct &&
        kind != identity::DefinitionKind::Enum && kind != identity::DefinitionKind::Error) {
      return zc::none;
    }
    return intern(type::semantic::TypeData(
                      type::semantic::NominalTypeData{definition, zc::mv(argumentTypes)}),
                  TypeKeyPattern::nominal(definition, zc::mv(argumentPatterns)));
  }

  zc::Maybe<BuiltSourceType> buildTuple(const ast::Node& syntax) {
    const auto& tree = boundModule.tree();
    const ast::NodeList elements{syntax.payload.words[ast::kTupleTypeExprElemsFirstWord],
                                 syntax.payload.words[ast::kTupleTypeExprElemsSizeWord]};
    if (!tree.contains(elements)) return zc::none;
    zc::Vector<identity::SemanticTypeId> elementTypes(elements.size);
    zc::Vector<TypeKeyPattern> elementPatterns(elements.size);
    for (const auto element : tree.list(elements)) {
      auto built = build(element);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) {
        elementTypes.add(value.type);
        elementPatterns.add(zc::mv(value.pattern));
      }
    }
    return intern(type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(elementTypes)}),
                  TypeKeyPattern::tuple(zc::mv(elementPatterns)));
  }

  zc::Maybe<uint64_t> arrayLength(ast::NodeId expression) const {
    const auto& tree = boundModule.tree();
    if (!tree.contains(expression)) return zc::none;
    const auto& syntax = tree.node(expression);
    if (syntax.kind != ast::SyntaxKind::IntLiteral) return zc::none;
    const auto base = syntax.payload.words[ast::kIntLiteralBaseWord];
    if (base != 2 && base != 8 && base != 10 && base != 16) return zc::none;
    const auto text = tree.bigInt(ast::BigIntId(syntax.payload.words[ast::kIntLiteralValueWord]));
    uint64_t value = 0;
    for (const auto character : text) {
      if (character == '_') continue;
      uint8_t digit = 0xff;
      if (character >= '0' && character <= '9') digit = static_cast<uint8_t>(character - '0');
      if (character >= 'a' && character <= 'f') {
        digit = static_cast<uint8_t>(character - 'a' + 10);
      }
      if (character >= 'A' && character <= 'F') {
        digit = static_cast<uint8_t>(character - 'A' + 10);
      }
      if (digit >= base || value > (UINT64_MAX - digit) / base) return zc::none;
      value = value * base + digit;
    }
    return value;
  }

  zc::Maybe<BuiltSourceType> buildFixedArray(uint32_t elementWord, uint32_t lengthWord) {
    auto length = arrayLength(ast::NodeId(lengthWord));
    if (length == zc::none) return zc::none;
    uint64_t value = 0;
    ZC_IF_SOME(admitted, length) { value = admitted; }
    auto element = build(ast::NodeId(elementWord));
    if (element == zc::none) return zc::none;
    ZC_IF_SOME(built, element) {
      return intern(type::semantic::TypeData(type::semantic::FixedArrayTypeData{built.type, value}),
                    TypeKeyPattern::fixedArray(zc::mv(built.pattern), value));
    }
    return zc::none;
  }

  zc::Maybe<BuiltSourceType> buildArray(const ast::Node& syntax) {
    const ast::NodeId element(syntax.payload.words[ast::kArrayTypeExprElemWord]);
    return buildUnary(
        element,
        [](identity::SemanticTypeId value) {
          return type::semantic::TypeData(type::semantic::DynamicArrayTypeData{value});
        },
        [](TypeKeyPattern&& value) { return TypeKeyPattern::dynamicArray(zc::mv(value)); });
  }

  zc::Maybe<BuiltSourceType> buildReference(const ast::Node& syntax, bool raw) {
    const auto elementWord =
        raw ? ast::kRawPointerTypeExprElemWord : ast::kReferenceTypeExprElemWord;
    const auto mutabilityWord =
        raw ? ast::kRawPointerTypeExprIsMutWord : ast::kReferenceTypeExprIsMutWord;
    const auto mutability =
        syntax.payload.words[mutabilityWord] != 0 ? Mutability::Mutable : Mutability::Const;
    auto element = build(ast::NodeId(syntax.payload.words[elementWord]));
    if (element == zc::none) return zc::none;
    ZC_IF_SOME(value, element) {
      if (raw) {
        return intern(
            type::semantic::TypeData(type::semantic::RawPointerTypeData{mutability, value.type}),
            TypeKeyPattern::rawPointer(mutability, zc::mv(value.pattern)));
      }
      return intern(
          type::semantic::TypeData(type::semantic::ReferenceTypeData{mutability, value.type}),
          TypeKeyPattern::reference(mutability, zc::mv(value.pattern)));
    }
    return zc::none;
  }

  zc::Maybe<BuiltSourceType> buildFunction(const ast::Node& syntax) {
    const auto& tree = boundModule.tree();
    const ast::NodeList parameters{syntax.payload.words[ast::kFunctionTypeExprParamsFirstWord],
                                   syntax.payload.words[ast::kFunctionTypeExprParamsSizeWord]};
    if (!tree.contains(parameters)) return zc::none;
    zc::Vector<identity::SemanticTypeId> parameterTypes(parameters.size);
    zc::Vector<TypeKeyPattern> parameterPatterns(parameters.size);
    for (const auto parameter : tree.list(parameters)) {
      auto built = build(parameter);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) {
        parameterTypes.add(value.type);
        parameterPatterns.add(zc::mv(value.pattern));
      }
    }
    auto success = build(ast::NodeId(syntax.payload.words[ast::kFunctionTypeExprRetTyWord]));
    if (success == zc::none) return zc::none;
    zc::Maybe<identity::SemanticTypeId> raisesType;
    zc::Maybe<TypeKeyPattern> raisesPattern;
    const ast::NodeId raisesNode(syntax.payload.words[ast::kFunctionTypeExprRaisesWord]);
    if (tree.contains(raisesNode)) {
      auto raises = build(raisesNode);
      if (raises == zc::none) return zc::none;
      ZC_IF_SOME(value, raises) {
        raisesType = value.type;
        raisesPattern = zc::mv(value.pattern);
      }
    }
    ZC_IF_SOME(successValue, success) {
      return intern(
          type::semantic::TypeData(type::semantic::FunctionTypeData{
              zc::mv(parameterTypes), successValue.type, zc::mv(raisesType)}),
          TypeKeyPattern::function(PatternFunctionType{
              zc::mv(parameterPatterns), zc::mv(successValue.pattern), zc::mv(raisesPattern)}));
    }
    return zc::none;
  }

  zc::Maybe<BuiltSourceType> buildSet(ast::NodeList nodes, bool isUnion) {
    const auto& tree = boundModule.tree();
    if (!tree.contains(nodes)) return zc::none;
    struct BuiltElement final {
      BuiltSourceType value;
      zc::Array<uint8_t> key;
    };
    zc::Vector<BuiltElement> elements(nodes.size);
    for (const auto node : tree.list(nodes)) {
      auto built = build(node);
      if (built == zc::none) return zc::none;
      ZC_IF_SOME(value, built) {
        auto lookup = semanticTypes.get(value.type);
        if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
        elements.add(BuiltElement{
            zc::mv(value),
            zc::heapArray<uint8_t>(lookup.get<type::SemanticTypeLookup>().key().bytes())});
      }
    }
    for (size_t index = 1; index < elements.size(); ++index) {
      auto current = zc::mv(elements[index]);
      size_t insertion = index;
      while (insertion > 0 && lessBytes(current.key.asPtr(), elements[insertion - 1].key.asPtr())) {
        elements[insertion] = zc::mv(elements[insertion - 1]);
        --insertion;
      }
      elements[insertion] = zc::mv(current);
    }
    for (size_t index = 1; index < elements.size(); ++index) {
      if (sameBytes(elements[index - 1].key.asPtr(), elements[index].key.asPtr())) {
        return zc::none;
      }
    }
    zc::Vector<identity::SemanticTypeId> types(elements.size());
    zc::Vector<TypeKeyPattern> patterns(elements.size());
    for (auto& element : elements) {
      types.add(element.value.type);
      patterns.add(zc::mv(element.value.pattern));
    }
    if (isUnion) {
      return intern(type::semantic::TypeData(type::semantic::UnionTypeData{zc::mv(types)}),
                    TypeKeyPattern::unionOf(zc::mv(patterns)));
    }
    return intern(type::semantic::TypeData(type::semantic::IntersectionTypeData{zc::mv(types)}),
                  TypeKeyPattern::intersection(zc::mv(patterns)));
  }

  zc::Maybe<BuiltSourceType> buildObject(const ast::Node& syntax) {
    const auto& tree = boundModule.tree();
    const ast::NodeList members{syntax.payload.words[ast::kObjectTypeExprMembersFirstWord],
                                syntax.payload.words[ast::kObjectTypeExprMembersSizeWord]};
    if (!tree.contains(members)) return zc::none;
    struct BuiltField final {
      type::semantic::ObjectFieldData data;
      PatternObjectField pattern;
      zc::Array<uint8_t> key;
    };
    zc::Vector<BuiltField> fields(members.size);
    for (const auto member : tree.list(members)) {
      if (!tree.contains(member)) return zc::none;
      const auto& fieldSyntax = tree.node(member);
      if (fieldSyntax.kind != ast::SyntaxKind::ObjectTypeMember) return zc::none;
      auto name = identity::SemanticIdentifier::fromSource(
          tree.ident(ast::IdentId(fieldSyntax.payload.words[ast::kObjectTypeMemberNameWord])));
      auto fieldType = build(ast::NodeId(fieldSyntax.payload.words[ast::kObjectTypeMemberTyWord]));
      if (name == zc::none || fieldType == zc::none) return zc::none;
      const auto mutability = fieldSyntax.payload.words[ast::kObjectTypeMemberIsMutWord] != 0
                                  ? Mutability::Mutable
                                  : Mutability::Const;
      const auto presence = fieldSyntax.payload.words[ast::kObjectTypeMemberIsOptionalWord] != 0
                                ? FieldPresence::Optional
                                : FieldPresence::Required;
      ZC_IF_SOME(identifier, name) {
        ZC_IF_SOME(value, fieldType) {
          identity::CanonicalEncoder encoder;
          identifier.encode(encoder);
          fields.add(BuiltField{
              type::semantic::ObjectFieldData{identifier.clone(), value.type, mutability, presence},
              PatternObjectField{zc::mv(identifier), zc::mv(value.pattern), mutability, presence},
              encoder.finish()});
        }
      }
    }
    for (size_t index = 1; index < fields.size(); ++index) {
      auto current = zc::mv(fields[index]);
      size_t insertion = index;
      while (insertion > 0 && lessBytes(current.key.asPtr(), fields[insertion - 1].key.asPtr())) {
        fields[insertion] = zc::mv(fields[insertion - 1]);
        --insertion;
      }
      fields[insertion] = zc::mv(current);
    }
    for (size_t index = 1; index < fields.size(); ++index) {
      if (sameBytes(fields[index - 1].key.asPtr(), fields[index].key.asPtr())) return zc::none;
    }
    zc::Vector<type::semantic::ObjectFieldData> data(fields.size());
    zc::Vector<PatternObjectField> patterns(fields.size());
    for (auto& field : fields) {
      data.add(zc::mv(field.data));
      patterns.add(zc::mv(field.pattern));
    }
    return intern(type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(data)}),
                  TypeKeyPattern::object(zc::mv(patterns)));
  }

  zc::Maybe<BuiltSourceType> buildOptional(const ast::Node& syntax) {
    auto inner = build(ast::NodeId(syntax.payload.words[ast::kOptionalTypeExprInnerWord]));
    auto nullType =
        intern(type::semantic::TypeData(type::semantic::PrimitiveTypeData{PrimitiveKind::Null}),
               TypeKeyPattern::primitive(PrimitiveKind::Null));
    if (inner == zc::none || nullType == zc::none) return zc::none;
    zc::Vector<BuiltSourceType> alternatives(2);
    ZC_IF_SOME(value, inner) { alternatives.add(zc::mv(value)); }
    ZC_IF_SOME(value, nullType) { alternatives.add(zc::mv(value)); }
    zc::Array<uint8_t> firstKey;
    zc::Array<uint8_t> secondKey;
    auto firstLookup = semanticTypes.get(alternatives[0].type);
    auto secondLookup = semanticTypes.get(alternatives[1].type);
    if (!firstLookup.is<type::SemanticTypeLookup>() ||
        !secondLookup.is<type::SemanticTypeLookup>()) {
      return zc::none;
    }
    firstKey = zc::heapArray<uint8_t>(firstLookup.get<type::SemanticTypeLookup>().key().bytes());
    secondKey = zc::heapArray<uint8_t>(secondLookup.get<type::SemanticTypeLookup>().key().bytes());
    if (sameBytes(firstKey.asPtr(), secondKey.asPtr())) return zc::mv(alternatives[0]);
    if (lessBytes(secondKey.asPtr(), firstKey.asPtr())) {
      auto first = zc::mv(alternatives[0]);
      alternatives[0] = zc::mv(alternatives[1]);
      alternatives[1] = zc::mv(first);
    }
    zc::Vector<identity::SemanticTypeId> types(2);
    zc::Vector<TypeKeyPattern> patterns(2);
    for (auto& alternative : alternatives) {
      types.add(alternative.type);
      patterns.add(zc::mv(alternative.pattern));
    }
    return intern(type::semantic::TypeData(type::semantic::UnionTypeData{zc::mv(types)}),
                  TypeKeyPattern::unionOf(zc::mv(patterns)));
  }

  const binder::VerifiedBoundModuleInput& boundModule;
  const identity::SemanticIdentityRegistrySet& registries;
  type::SemanticTypeStore& semanticTypes;
  zc::ArrayPtr<const identity::GenericParameterId> genericParameters;
};

struct BuiltSourceGenericParameters final {
  zc::Vector<identity::GenericParameterId> identities;
  zc::Vector<GenericParameterSignature> signatures;
};

zc::Maybe<BuiltSourceGenericParameters> buildSourceGenericParameters(
    const SignatureFactsBuildInput& input, identity::DefId owner) {
  auto ownerKey = input.registries.definitions().lookup(owner);
  if (ownerKey == zc::none) return zc::none;

  struct ParameterEntry final {
    identity::GenericParameterId parameter;
    identity::GenericParameterKey key;
    ast::NodeId node;
    uint32_t ordinal;
  };
  zc::Vector<ParameterEntry> entries;
  ZC_IF_SOME(expectedOwner, ownerKey) {
    for (const auto& entry : input.boundModule.definitions().genericParameters()) {
      if (entry.record.kind() != identity::GenericParameterKind::Type) return zc::none;
      auto parameterOwner = entry.record.owner().definitionKey();
      if (parameterOwner == zc::none) continue;
      ZC_IF_SOME(actualOwner, parameterOwner) {
        if (!sameDefinitionKey(expectedOwner, actualOwner)) continue;
      }
      entries.add(
          ParameterEntry{entry.parameter, entry.key.clone(), entry.node, entry.record.ordinal()});
    }
  }
  for (size_t index = 1; index < entries.size(); ++index) {
    auto current = zc::mv(entries[index]);
    size_t insertion = index;
    while (insertion > 0 && current.ordinal < entries[insertion - 1].ordinal) {
      entries[insertion] = zc::mv(entries[insertion - 1]);
      --insertion;
    }
    entries[insertion] = zc::mv(current);
  }
  if (entries.size() > UINT32_MAX) return zc::none;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].ordinal != index) return zc::none;
  }

  zc::Vector<identity::GenericParameterId> identities(entries.size());
  for (const auto& entry : entries) { identities.add(entry.parameter); }
  SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                identities.asPtr());
  zc::Vector<GenericParameterSignature> signatures(entries.size());
  const auto& tree = input.boundModule.tree();
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (!tree.contains(entry.node) ||
        tree.node(entry.node).kind != ast::SyntaxKind::GenericTypeParam) {
      return zc::none;
    }
    const auto& syntax = tree.node(entry.node);
    zc::Vector<InterfaceInstantiation> bounds;
    zc::Vector<identity::DefId> markerBounds;
    const ast::NodeId boundListNode(syntax.payload.words[ast::kGenericTypeParamBoundsIdWord]);
    if (tree.contains(boundListNode)) {
      const auto& boundListSyntax = tree.node(boundListNode);
      if (boundListSyntax.kind != ast::SyntaxKind::TypeParameterBoundList) return zc::none;
      const ast::NodeList boundNodes{
          boundListSyntax.payload.words[ast::kTypeParameterBoundListBoundsFirstWord],
          boundListSyntax.payload.words[ast::kTypeParameterBoundListBoundsSizeWord]};
      if (!tree.contains(boundNodes) || boundNodes.empty()) return zc::none;
      for (const auto boundNode : tree.list(boundNodes)) {
        auto boundInterface = typeBuilder.buildInterface(boundNode);
        if (boundInterface == zc::none) return zc::none;
        ZC_IF_SOME(value, boundInterface) {
          auto shape = input.markerShapes.shape(value.interface);
          if (shape == zc::none) return zc::none;
          ZC_IF_SOME(kind, shape) {
            if (kind == InterfaceMarkerShape::ClosedMarker) {
              if (!value.arguments.empty()) return zc::none;
              markerBounds.add(value.interface);
            } else if (kind == InterfaceMarkerShape::Behavior) {
              bounds.add(zc::mv(value));
            } else {
              return zc::none;
            }
          }
        }
      }
    }
    zc::Maybe<identity::SemanticTypeId> defaultType;
    const ast::NodeId defaultNode(syntax.payload.words[ast::kGenericTypeParamDefaultTyWord]);
    if (tree.contains(defaultNode)) {
      auto builtDefault = typeBuilder.build(defaultNode);
      if (builtDefault == zc::none) return zc::none;
      ZC_IF_SOME(value, builtDefault) { defaultType = value.type; }
    }
    signatures.add(GenericParameterSignature{entry.key.clone(), static_cast<uint32_t>(index),
                                             zc::mv(bounds), zc::mv(markerBounds),
                                             zc::mv(defaultType)});
  }
  return BuiltSourceGenericParameters{zc::mv(identities), zc::mv(signatures)};
}

bool sortUniqueDefinitionIds(zc::Vector<identity::DefId>& definitions,
                             const identity::SemanticIdentityRegistrySet& registries) {
  for (size_t index = 1; index < definitions.size(); ++index) {
    const auto current = definitions[index];
    auto currentKey = registries.definitions().lookup(current);
    if (currentKey == zc::none) return false;
    size_t insertion = index;
    while (insertion > 0) {
      auto previousKey = registries.definitions().lookup(definitions[insertion - 1]);
      if (previousKey == zc::none) return false;
      bool currentBeforePrevious = false;
      ZC_IF_SOME(left, currentKey) {
        ZC_IF_SOME(right, previousKey) {
          currentBeforePrevious = lessBytes(left.bytes(), right.bytes());
        }
      }
      if (!currentBeforePrevious) break;
      definitions[insertion] = definitions[insertion - 1];
      --insertion;
    }
    definitions[insertion] = current;
  }
  for (size_t index = 1; index < definitions.size(); ++index) {
    auto previousKey = registries.definitions().lookup(definitions[index - 1]);
    auto currentKey = registries.definitions().lookup(definitions[index]);
    if (previousKey == zc::none || currentKey == zc::none) return false;
    bool duplicate = false;
    ZC_IF_SOME(previous, previousKey) {
      ZC_IF_SOME(current, currentKey) { duplicate = sameDefinitionKey(previous, current); }
    }
    if (duplicate) return false;
  }
  return true;
}

zc::Maybe<CanonicalTypeHead> typeHead(identity::SemanticTypeId type,
                                      const type::SemanticTypeStore& store) {
  auto lookup = store.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (data.is<type::semantic::PrimitiveTypeData>()) {
    return CanonicalTypeHead(PrimitiveTypeHead{data.get<type::semantic::PrimitiveTypeData>().kind});
  }
  if (data.is<type::semantic::TupleTypeData>()) {
    const auto size = data.get<type::semantic::TupleTypeData>().elements.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(TupleTypeHead{static_cast<uint32_t>(size)});
  }
  if (data.is<type::semantic::ObjectTypeData>()) return CanonicalTypeHead(ObjectTypeHead{});
  if (data.is<type::semantic::DynamicArrayTypeData>()) {
    return CanonicalTypeHead(DynamicArrayTypeHead{});
  }
  if (data.is<type::semantic::SliceTypeData>()) return CanonicalTypeHead(SliceTypeHead{});
  if (data.is<type::semantic::FixedArrayTypeData>()) return CanonicalTypeHead(FixedArrayTypeHead{});
  if (data.is<type::semantic::FunctionTypeData>()) {
    const auto& function = data.get<type::semantic::FunctionTypeData>();
    if (function.parameters.size() > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(FunctionTypeHead{static_cast<uint32_t>(function.parameters.size()),
                                              function.raises != zc::none});
  }
  if (data.is<type::semantic::NominalTypeData>()) {
    return CanonicalTypeHead(
        NominalTypeHead{data.get<type::semantic::NominalTypeData>().definition});
  }
  if (data.is<type::semantic::UnionTypeData>()) {
    const auto size = data.get<type::semantic::UnionTypeData>().alternatives.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(UnionTypeHead{static_cast<uint32_t>(size)});
  }
  if (data.is<type::semantic::IntersectionTypeData>()) {
    const auto size = data.get<type::semantic::IntersectionTypeData>().conjuncts.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(IntersectionTypeHead{static_cast<uint32_t>(size)});
  }
  if (data.is<type::semantic::ReferenceTypeData>()) {
    return CanonicalTypeHead(
        ReferenceTypeHead{data.get<type::semantic::ReferenceTypeData>().mutability});
  }
  if (data.is<type::semantic::RawPointerTypeData>()) {
    return CanonicalTypeHead(
        RawPointerTypeHead{data.get<type::semantic::RawPointerTypeData>().mutability});
  }
  if (data.is<type::semantic::ExistentialTypeData>()) {
    return CanonicalTypeHead(
        ExistentialTypeHead{data.get<type::semantic::ExistentialTypeData>().principal.definition});
  }
  return CanonicalTypeHead(BlanketTypeHead{});
}

zc::Maybe<identity::ModuleId> firstRegisteredModule(
    const identity::SemanticIdentityRegistrySet& registries) {
  ZC_IF_SOME(key, registries.modules().keyAt(0)) { return registries.modules().find(key); }
  return zc::none;
}

zc::Maybe<identity::DefId> resolvedDefinitionAtNode(const binder::VerifiedBindingMetadata& metadata,
                                                    ast::NodeId node) {
  zc::Maybe<identity::DefId> result;
  for (const auto& resolution : metadata.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::DefinitionBindingTarget>() || result != zc::none) return zc::none;
    result = target.get<binder::DefinitionBindingTarget>().definition;
  }
  return result;
}

struct DirectInterfaceShape final {
  identity::DefId interface;
  identity::ModuleId module;
  ast::NodeId declaration;
  bool hasGenerics;
  bool hasBehavior;
  zc::Vector<identity::DefId> parents;
};

zc::Maybe<DirectInterfaceShape> inspectDirectInterface(
    const ast::Tree& tree, const binder::VerifiedBindingMetadata& metadata,
    const identity::SemanticIdentityRegistrySet& registries,
    const binder::FrozenDefinitionEntry& definition, identity::ModuleId module) {
  if (!tree.contains(definition.node)) { return zc::none; }
  const auto& syntax = tree.node(definition.node);
  if (syntax.kind != ast::SyntaxKind::InterfaceDecl) { return zc::none; }

  bool hasGenerics = false;
  const ast::NodeId generics(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
  if (tree.contains(generics)) {
    const auto& genericSyntax = tree.node(generics);
    if (genericSyntax.kind != ast::SyntaxKind::GenericParams) { return zc::none; }
    const ast::NodeList parameters{genericSyntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                   genericSyntax.payload.words[ast::kGenericParamsParamsSizeWord]};
    if (!tree.contains(parameters)) { return zc::none; }
    hasGenerics = !parameters.empty();
  }

  bool hasBehavior = false;
  const ast::NodeId members(syntax.payload.words[ast::kInterfaceDeclMembersIdWord]);
  if (tree.contains(members)) {
    const auto& memberSyntax = tree.node(members);
    if (memberSyntax.kind != ast::SyntaxKind::ClassMemberList) { return zc::none; }
    const ast::NodeList memberList{
        memberSyntax.payload.words[ast::kClassMemberListMembersFirstWord],
        memberSyntax.payload.words[ast::kClassMemberListMembersSizeWord]};
    if (!tree.contains(memberList)) { return zc::none; }
    hasBehavior = !memberList.empty();
  }

  zc::Vector<identity::DefId> parentDefinitions;
  const ast::NodeId parentNode(syntax.payload.words[ast::kInterfaceDeclIfacesIdWord]);
  if (tree.contains(parentNode)) {
    const auto& parentSyntax = tree.node(parentNode);
    if (parentSyntax.kind != ast::SyntaxKind::ImplIfaceList) { return zc::none; }
    const ast::NodeList parentList{parentSyntax.payload.words[ast::kImplIfaceListIfacesFirstWord],
                                   parentSyntax.payload.words[ast::kImplIfaceListIfacesSizeWord]};
    if (!tree.contains(parentList)) { return zc::none; }
    for (const auto parent : tree.list(parentList)) {
      if (!tree.contains(parent)) return zc::none;
      const auto& parentType = tree.node(parent);
      if (parentType.kind != ast::SyntaxKind::NamedTypeExpr) return zc::none;
      auto resolved = resolvedDefinitionAtNode(
          metadata, ast::NodeId(parentType.payload.words[ast::kNamedTypeExprPathWord]));
      if (resolved == zc::none) return zc::none;
      ZC_IF_SOME(value, resolved) {
        auto record = registries.definitions().lookupRecord(value);
        if (record == zc::none) return zc::none;
        ZC_IF_SOME(definitionRecord, record) {
          if (definitionRecord.kind() != identity::DefinitionKind::Interface) { return zc::none; }
        }
        for (const auto previous : parentDefinitions) {
          if (previous == value) return zc::none;
        }
        parentDefinitions.add(value);
      }
    }
  }
  return DirectInterfaceShape{definition.definition, module,      definition.node,
                              hasGenerics,           hasBehavior, zc::mv(parentDefinitions)};
}

bool encodeSortedRecordBytes(identity::CanonicalEncoder& encoder,
                             zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  zc::Vector<zc::ArrayPtr<const uint8_t>> sorted(records.size());
  for (const auto record : records) { sorted.add(record); }
  for (size_t index = 1; index < sorted.size(); ++index) {
    auto current = sorted[index];
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
  for (const auto record : sorted) { encoder.encodeByteString(record); }
  return true;
}

zc::Maybe<identity::Sha256Digest> markerShapeRevisionDigest(
    const identity::Sha256Digest& fingerprint,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.marker-shape-inventory.v0"_zcc);
  encoder.encodeUint8(0);
  encoder.encodeDigest(fingerprint);
  if (!encodeSortedRecordBytes(encoder, records)) return zc::none;
  return identity::sha256(encoder.finish().asPtr());
}

zc::Maybe<identity::Sha256Digest> markerPolicyConfigurationDigest(
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.marker-policy-configuration.v0"_zcc);
  encoder.encodeUint8(0);
  if (!encodeSortedRecordBytes(encoder, records)) return zc::none;
  return identity::sha256(encoder.finish().asPtr());
}

zc::Maybe<identity::Sha256Digest> markerPolicyRegistryDigest(
    const identity::Sha256Digest& fingerprint, const identity::Sha256Digest& configurationRevision,
    const MarkerShapeInventoryRevision& inventoryRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.marker-policy-registry.v0"_zcc);
  encoder.encodeUint8(0);
  encoder.encodeDigest(fingerprint);
  encoder.encodeDigest(configurationRevision);
  encoder.encodeDigest(inventoryRevision.digest());
  if (!encodeSortedRecordBytes(encoder, records)) return zc::none;
  return identity::sha256(encoder.finish().asPtr());
}

}  // namespace

struct CanonicalConstValue::Impl final {
  explicit Impl(ConstInteger&& value) : value(zc::mv(value)) {}
  explicit Impl(ConstFloat32 value) : value(value) {}
  explicit Impl(ConstFloat64 value) : value(value) {}
  explicit Impl(ConstBool value) : value(value) {}
  explicit Impl(ConstChar value) : value(value) {}
  explicit Impl(ConstString&& value) : value(zc::mv(value)) {}
  explicit Impl(ConstNull value) : value(value) {}
  explicit Impl(ConstUnit value) : value(value) {}
  explicit Impl(ConstTuple&& value) : value(zc::mv(value)) {}
  explicit Impl(ConstArray&& value) : value(zc::mv(value)) {}
  explicit Impl(ConstObject&& value) : value(zc::mv(value)) {}
  explicit Impl(ConstEnum&& value) : value(zc::mv(value)) {}

  zc::OneOf<ConstInteger, ConstFloat32, ConstFloat64, ConstBool, ConstChar, ConstString, ConstNull,
            ConstUnit, ConstTuple, ConstArray, ConstObject, ConstEnum>
      value;
};

CanonicalConstValue::CanonicalConstValue(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
CanonicalConstValue::~CanonicalConstValue() noexcept(false) = default;
CanonicalConstValue::CanonicalConstValue(CanonicalConstValue&&) noexcept = default;
CanonicalConstValue& CanonicalConstValue::operator=(CanonicalConstValue&&) noexcept = default;

CanonicalConstValue CanonicalConstValue::integer(CanonicalInteger&& value) {
  return CanonicalConstValue(zc::heap<Impl>(ConstInteger{zc::mv(value)}));
}
CanonicalConstValue CanonicalConstValue::float32(uint32_t bits) {
  return CanonicalConstValue(zc::heap<Impl>(ConstFloat32{bits}));
}
CanonicalConstValue CanonicalConstValue::float64(uint64_t bits) {
  return CanonicalConstValue(zc::heap<Impl>(ConstFloat64{bits}));
}
CanonicalConstValue CanonicalConstValue::boolean(bool value) {
  return CanonicalConstValue(zc::heap<Impl>(ConstBool{value}));
}
CanonicalConstValue CanonicalConstValue::character(uint32_t scalar) {
  return CanonicalConstValue(zc::heap<Impl>(ConstChar{scalar}));
}
CanonicalConstValue CanonicalConstValue::string(zc::Array<uint8_t>&& bytes) {
  return CanonicalConstValue(zc::heap<Impl>(ConstString{zc::mv(bytes)}));
}
CanonicalConstValue CanonicalConstValue::null() {
  return CanonicalConstValue(zc::heap<Impl>(ConstNull{}));
}
CanonicalConstValue CanonicalConstValue::unit() {
  return CanonicalConstValue(zc::heap<Impl>(ConstUnit{}));
}
CanonicalConstValue CanonicalConstValue::tuple(zc::Vector<CanonicalConstValue>&& values) {
  return CanonicalConstValue(zc::heap<Impl>(ConstTuple{zc::mv(values)}));
}
CanonicalConstValue CanonicalConstValue::array(zc::Vector<CanonicalConstValue>&& values) {
  return CanonicalConstValue(zc::heap<Impl>(ConstArray{zc::mv(values)}));
}
CanonicalConstValue CanonicalConstValue::object(zc::Vector<ConstObjectField>&& fields) {
  return CanonicalConstValue(zc::heap<Impl>(ConstObject{zc::mv(fields)}));
}
CanonicalConstValue CanonicalConstValue::enumeration(identity::DefId variant,
                                                     zc::Vector<CanonicalConstValue>&& payload) {
  return CanonicalConstValue(zc::heap<Impl>(ConstEnum{variant, zc::mv(payload)}));
}

CanonicalConstValueTag CanonicalConstValue::tag() const noexcept {
  if (impl->value.is<ConstInteger>()) return CanonicalConstValueTag::Integer;
  if (impl->value.is<ConstFloat32>() || impl->value.is<ConstFloat64>()) {
    return CanonicalConstValueTag::Float;
  }
  if (impl->value.is<ConstBool>()) return CanonicalConstValueTag::Bool;
  if (impl->value.is<ConstChar>()) return CanonicalConstValueTag::Char;
  if (impl->value.is<ConstString>()) return CanonicalConstValueTag::String;
  if (impl->value.is<ConstNull>()) return CanonicalConstValueTag::Null;
  if (impl->value.is<ConstUnit>()) return CanonicalConstValueTag::Unit;
  if (impl->value.is<ConstTuple>()) return CanonicalConstValueTag::Tuple;
  if (impl->value.is<ConstArray>()) return CanonicalConstValueTag::Array;
  if (impl->value.is<ConstObject>()) return CanonicalConstValueTag::Object;
  return CanonicalConstValueTag::Enum;
}

zc::Maybe<const CanonicalInteger&> CanonicalConstValue::integerValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstInteger>()) { return value.value; }
  return zc::none;
}

zc::Maybe<CanonicalFloat> CanonicalConstValue::floatValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstFloat32>()) {
    return CanonicalFloat{CanonicalFloatWidth::Bits32, value.bits};
  }
  ZC_IF_SOME(value, impl->value.tryGet<ConstFloat64>()) {
    return CanonicalFloat{CanonicalFloatWidth::Bits64, value.bits};
  }
  return zc::none;
}

zc::Maybe<bool> CanonicalConstValue::booleanValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstBool>()) { return value.value; }
  return zc::none;
}

zc::Maybe<uint32_t> CanonicalConstValue::characterValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstChar>()) { return value.scalar; }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> CanonicalConstValue::stringValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstString>()) { return value.bytes.asPtr(); }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalConstValue>> CanonicalConstValue::elements() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstTuple>()) { return value.values.asPtr(); }
  ZC_IF_SOME(value, impl->value.tryGet<ConstArray>()) { return value.values.asPtr(); }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const ConstObjectField>> CanonicalConstValue::objectFields() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstObject>()) { return value.fields.asPtr(); }
  return zc::none;
}

zc::Maybe<CanonicalEnumerationView> CanonicalConstValue::enumerationValue() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<ConstEnum>()) {
    return CanonicalEnumerationView{value.variant, value.payload.asPtr()};
  }
  return zc::none;
}

namespace {

zc::Vector<CanonicalConstValue> cloneConstValues(zc::ArrayPtr<const CanonicalConstValue> values) {
  zc::Vector<CanonicalConstValue> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

zc::Vector<InterfaceInstantiation> cloneInterfaces(
    zc::ArrayPtr<const InterfaceInstantiation> values) {
  zc::Vector<InterfaceInstantiation> result(values.size());
  for (const auto& value : values) {
    zc::Vector<identity::SemanticTypeId> arguments(value.arguments.size());
    for (const auto argument : value.arguments) { arguments.add(argument); }
    result.add(InterfaceInstantiation{value.interface, zc::mv(arguments)});
  }
  return result;
}

zc::Vector<GenericParameterSignature> cloneGenericParameters(
    zc::ArrayPtr<const GenericParameterSignature> values) {
  zc::Vector<GenericParameterSignature> result(values.size());
  for (const auto& value : values) {
    zc::Vector<identity::DefId> markerBounds(value.markerBounds.size());
    for (const auto marker : value.markerBounds) { markerBounds.add(marker); }
    result.add(GenericParameterSignature{value.parameter.clone(), value.index,
                                         cloneInterfaces(value.bounds.asPtr()),
                                         zc::mv(markerBounds), value.defaultType});
  }
  return result;
}

zc::Vector<identity::GenericParameterKey> cloneGenericParameterKeys(
    zc::ArrayPtr<const identity::GenericParameterKey> values) {
  zc::Vector<identity::GenericParameterKey> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

zc::Vector<ParameterSignature> cloneParameters(zc::ArrayPtr<const ParameterSignature> values) {
  zc::Vector<ParameterSignature> result(values.size());
  for (const auto& value : values) {
    result.add(ParameterSignature{value.parameter.clone(), value.label.clone(), value.type,
                                  value.mode, value.hasDefault});
  }
  return result;
}

zc::Maybe<ReceiverSignature> cloneReceiver(const zc::Maybe<ReceiverSignature>& value) {
  ZC_IF_SOME(receiver, value) {
    return ReceiverSignature{receiver.parameter.clone(), receiver.mode};
  }
  return zc::none;
}

template <typename T>
zc::Vector<T> clonePlainVector(zc::ArrayPtr<const T> values) {
  zc::Vector<T> result(values.size());
  for (const auto& value : values) { result.add(value); }
  return result;
}

zc::Vector<ObjectSafetyCause> cloneObjectSafetyCauses(
    zc::ArrayPtr<const ObjectSafetyCause> values) {
  zc::Vector<ObjectSafetyCause> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

zc::Vector<NormalizedAttributeFact> cloneAttributes(
    zc::ArrayPtr<const NormalizedAttributeFact> values) {
  zc::Vector<NormalizedAttributeFact> result(values.size());
  for (const auto& value : values) {
    result.add(NormalizedAttributeFact{value.target, value.attribute, value.sourceSpan.clone()});
  }
  return result;
}

zc::Vector<CanonicalConstraint> cloneConstraints(zc::ArrayPtr<const CanonicalConstraint> values) {
  zc::Vector<CanonicalConstraint> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

}  // namespace

CanonicalConstValue CanonicalConstValue::clone() const {
  const auto& value = impl->value;
  if (value.is<ConstInteger>()) {
    const auto& integerValue = value.get<ConstInteger>().value;
    return integer(CanonicalInteger{integerValue.sign,
                                    zc::heapArray<uint8_t>(integerValue.magnitude.asPtr())});
  }
  if (value.is<ConstFloat32>()) return float32(value.get<ConstFloat32>().bits);
  if (value.is<ConstFloat64>()) return float64(value.get<ConstFloat64>().bits);
  if (value.is<ConstBool>()) return boolean(value.get<ConstBool>().value);
  if (value.is<ConstChar>()) return character(value.get<ConstChar>().scalar);
  if (value.is<ConstString>()) {
    return string(zc::heapArray<uint8_t>(value.get<ConstString>().bytes.asPtr()));
  }
  if (value.is<ConstNull>()) return null();
  if (value.is<ConstUnit>()) return unit();
  if (value.is<ConstTuple>()) {
    return tuple(cloneConstValues(value.get<ConstTuple>().values.asPtr()));
  }
  if (value.is<ConstArray>()) {
    return array(cloneConstValues(value.get<ConstArray>().values.asPtr()));
  }
  if (value.is<ConstObject>()) {
    zc::Vector<ConstObjectField> fields(value.get<ConstObject>().fields.size());
    for (const auto& field : value.get<ConstObject>().fields) {
      fields.add(ConstObjectField{field.name.clone(), field.value.clone()});
    }
    return object(zc::mv(fields));
  }
  const auto& enumerationValue = value.get<ConstEnum>();
  return enumeration(enumerationValue.variant, cloneConstValues(enumerationValue.payload.asPtr()));
}

void CanonicalConstValue::appendReferencedDefinitions(zc::Vector<identity::DefId>& output) const {
  const auto& value = impl->value;
  if (value.is<ConstTuple>()) {
    for (const auto& element : value.get<ConstTuple>().values) {
      element.appendReferencedDefinitions(output);
    }
  } else if (value.is<ConstArray>()) {
    for (const auto& element : value.get<ConstArray>().values) {
      element.appendReferencedDefinitions(output);
    }
  } else if (value.is<ConstObject>()) {
    for (const auto& field : value.get<ConstObject>().fields) {
      field.value.appendReferencedDefinitions(output);
    }
  } else if (value.is<ConstEnum>()) {
    const auto& enumeration = value.get<ConstEnum>();
    output.add(enumeration.variant);
    for (const auto& element : enumeration.payload) { element.appendReferencedDefinitions(output); }
  }
}

SignatureScope SignatureScope::clone() const {
  if (value.is<ModuleDefinitionSignatureScope>()) {
    return SignatureScope(ModuleDefinitionSignatureScope{});
  }
  if (value.is<MemberSignatureScope>()) {
    return SignatureScope(value.get<MemberSignatureScope>());
  }
  return SignatureScope(value.get<EnclosedSignatureScope>());
}

ObjectSafetyCause ObjectSafetyCause::clone() const {
  if (value.is<UnsafeSuperinterfaceCause>()) {
    return ObjectSafetyCause(value.get<UnsafeSuperinterfaceCause>());
  }
  if (value.is<GenericMethodCause>()) return ObjectSafetyCause(value.get<GenericMethodCause>());
  if (value.is<ReturnsSelfCause>()) return ObjectSafetyCause(value.get<ReturnsSelfCause>());
  if (value.is<MovesSelfCause>()) return ObjectSafetyCause(value.get<MovesSelfCause>());
  if (value.is<StaticMethodCause>()) return ObjectSafetyCause(value.get<StaticMethodCause>());
  if (value.is<GenericAssociatedTypeCause>()) {
    return ObjectSafetyCause(value.get<GenericAssociatedTypeCause>());
  }
  const auto& source = value.get<UnsizedParameterCause>();
  return ObjectSafetyCause(
      UnsizedParameterCause{source.method, source.parameter.clone(), source.type});
}

SemanticSignaturePayload SemanticSignaturePayload::clone() const {
  if (value.is<CallableSignature>()) {
    const auto& source = value.get<CallableSignature>();
    return SemanticSignaturePayload(CallableSignature{
        cloneGenericParameters(source.genericParameters.asPtr()), cloneReceiver(source.receiver),
        cloneParameters(source.parameters.asPtr()), source.success, source.raises, source.abi});
  }
  if (value.is<NominalSignature>()) {
    const auto& source = value.get<NominalSignature>();
    return SemanticSignaturePayload(NominalSignature{
        cloneGenericParameters(source.genericParameters.asPtr()), source.base,
        cloneInterfaces(source.interfaces.asPtr()), clonePlainVector(source.fields.asPtr()),
        clonePlainVector(source.variants.asPtr()), clonePlainVector(source.members.asPtr())});
  }
  if (value.is<InterfaceSignature>()) {
    const auto& source = value.get<InterfaceSignature>();
    return SemanticSignaturePayload(InterfaceSignature{
        cloneGenericParameters(source.genericParameters.asPtr()),
        cloneInterfaces(source.parents.asPtr()), clonePlainVector(source.members.asPtr()),
        clonePlainVector(source.associatedTypes.asPtr()), source.markerOnly,
        cloneObjectSafetyCauses(source.objectSafetyCauses.asPtr())});
  }
  if (value.is<TypeAliasSignature>()) {
    const auto& source = value.get<TypeAliasSignature>();
    return SemanticSignaturePayload(TypeAliasSignature{
        cloneGenericParameters(source.genericParameters.asPtr()), source.target});
  }
  if (value.is<AssociatedTypeSignature>()) {
    const auto& source = value.get<AssociatedTypeSignature>();
    return SemanticSignaturePayload(
        AssociatedTypeSignature{cloneGenericParameters(source.genericParameters.asPtr()),
                                cloneInterfaces(source.bounds.asPtr()),
                                clonePlainVector(source.markerBounds.asPtr()), source.defaultType});
  }
  if (value.is<ValueSignature>()) {
    const auto& source = value.get<ValueSignature>();
    zc::Maybe<CanonicalConstValue> constantValue;
    ZC_IF_SOME(constant, source.constantValue) { constantValue = constant.clone(); }
    return SemanticSignaturePayload(ValueSignature{
        source.type, source.mutability, source.hasInitializer, zc::mv(constantValue), source.abi});
  }
  if (value.is<EnumVariantSignature>()) {
    const auto& source = value.get<EnumVariantSignature>();
    zc::Maybe<CanonicalInteger> discriminant;
    ZC_IF_SOME(integerValue, source.discriminant) {
      discriminant = CanonicalInteger{integerValue.sign,
                                      zc::heapArray<uint8_t>(integerValue.magnitude.asPtr())};
    }
    return SemanticSignaturePayload(
        EnumVariantSignature{clonePlainVector(source.payload.asPtr()), zc::mv(discriminant)});
  }
  ZC_UNREACHABLE
}

SemanticSignature SemanticSignature::clone() const {
  return SemanticSignature{definition,
                           definitionKind,
                           scope.clone(),
                           clonePlainVector(modifiers.asPtr()),
                           cloneAttributes(attributes.asPtr()),
                           payload.clone(),
                           declarationSpan.clone()};
}

namespace {

struct PatternPrimitive final {
  PrimitiveKind kind;
};
struct PatternTuple final {
  zc::Vector<TypeKeyPattern> elements;
};
struct PatternObject final {
  zc::Vector<PatternObjectField> fields;
};
struct PatternDynamicArray final {
  TypeKeyPattern element;
};
struct PatternSlice final {
  TypeKeyPattern element;
};
struct PatternFixedArray final {
  TypeKeyPattern element;
  uint64_t length;
};
struct PatternFunction final {
  PatternFunctionType function;
};
struct PatternNominal final {
  identity::DefId definition;
  zc::Vector<TypeKeyPattern> arguments;
};
struct PatternTypeParameter final {
  identity::GenericParameterKey parameter;
};
struct PatternUnion final {
  zc::Vector<TypeKeyPattern> alternatives;
};
struct PatternIntersection final {
  zc::Vector<TypeKeyPattern> conjuncts;
};
struct PatternReference final {
  Mutability mutability;
  TypeKeyPattern referent;
};
struct PatternRawPointer final {
  Mutability mutability;
  TypeKeyPattern pointee;
};
struct PatternExistential final {
  PatternExistentialType existential;
};
struct PatternInterfaceBound final {
  PatternInterfaceInstantiation interface;
};
struct PatternInterfaceSelf final {
  identity::DefId interface;
};
struct PatternParameter final {
  uint32_t index;
};

zc::Vector<TypeKeyPattern> clonePatterns(zc::ArrayPtr<const TypeKeyPattern> values) {
  zc::Vector<TypeKeyPattern> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

PatternInterfaceInstantiation clonePatternInterface(const PatternInterfaceInstantiation& value) {
  return PatternInterfaceInstantiation{value.interface, clonePatterns(value.arguments.asPtr())};
}

PatternExistentialInterface clonePatternExistentialInterface(
    const PatternExistentialInterface& value) {
  return PatternExistentialInterface{value.definition, clonePatterns(value.arguments.asPtr())};
}

PatternExistentialType clonePatternExistential(const PatternExistentialType& value) {
  zc::Vector<PatternExistentialInterface> additional(value.additionalInterfaces.size());
  for (const auto& interface : value.additionalInterfaces) {
    additional.add(clonePatternExistentialInterface(interface));
  }
  zc::Vector<PatternAssociatedTypeBinding> associated(value.associatedBindings.size());
  for (const auto& binding : value.associatedBindings) {
    associated.add(PatternAssociatedTypeBinding{binding.associated, binding.type.clone()});
  }
  return PatternExistentialType{clonePatternExistentialInterface(value.principal),
                                zc::mv(additional), clonePlainVector(value.markers.asPtr()),
                                zc::mv(associated)};
}

}  // namespace

struct TypeKeyPattern::Impl final {
  explicit Impl(PatternPrimitive value) : value(value) {}
  explicit Impl(PatternTuple&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternObject&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternDynamicArray&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternSlice&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternFixedArray&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternFunction&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternNominal&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternTypeParameter&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternUnion&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternIntersection&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternReference&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternRawPointer&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternExistential&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternInterfaceBound&& value) : value(zc::mv(value)) {}
  explicit Impl(PatternInterfaceSelf value) : value(value) {}
  explicit Impl(PatternParameter value) : value(value) {}

  zc::OneOf<PatternPrimitive, PatternTuple, PatternObject, PatternDynamicArray, PatternSlice,
            PatternFixedArray, PatternFunction, PatternNominal, PatternTypeParameter, PatternUnion,
            PatternIntersection, PatternReference, PatternRawPointer, PatternExistential,
            PatternInterfaceBound, PatternInterfaceSelf, PatternParameter>
      value;
};

TypeKeyPattern::TypeKeyPattern(zc::Own<Impl>&& input) noexcept : impl(zc::mv(input)) {}
TypeKeyPattern::~TypeKeyPattern() noexcept(false) = default;
TypeKeyPattern::TypeKeyPattern(TypeKeyPattern&&) noexcept = default;
TypeKeyPattern& TypeKeyPattern::operator=(TypeKeyPattern&&) noexcept = default;

TypeKeyPattern TypeKeyPattern::primitive(PrimitiveKind kind) {
  return TypeKeyPattern(zc::heap<Impl>(PatternPrimitive{kind}));
}
TypeKeyPattern TypeKeyPattern::tuple(zc::Vector<TypeKeyPattern>&& elements) {
  return TypeKeyPattern(zc::heap<Impl>(PatternTuple{zc::mv(elements)}));
}
TypeKeyPattern TypeKeyPattern::object(zc::Vector<PatternObjectField>&& fields) {
  return TypeKeyPattern(zc::heap<Impl>(PatternObject{zc::mv(fields)}));
}
TypeKeyPattern TypeKeyPattern::dynamicArray(TypeKeyPattern&& element) {
  return TypeKeyPattern(zc::heap<Impl>(PatternDynamicArray{zc::mv(element)}));
}
TypeKeyPattern TypeKeyPattern::slice(TypeKeyPattern&& element) {
  return TypeKeyPattern(zc::heap<Impl>(PatternSlice{zc::mv(element)}));
}
TypeKeyPattern TypeKeyPattern::fixedArray(TypeKeyPattern&& element, uint64_t length) {
  return TypeKeyPattern(zc::heap<Impl>(PatternFixedArray{zc::mv(element), length}));
}
TypeKeyPattern TypeKeyPattern::function(PatternFunctionType&& function) {
  return TypeKeyPattern(zc::heap<Impl>(PatternFunction{zc::mv(function)}));
}
TypeKeyPattern TypeKeyPattern::nominal(identity::DefId definition,
                                       zc::Vector<TypeKeyPattern>&& arguments) {
  return TypeKeyPattern(zc::heap<Impl>(PatternNominal{definition, zc::mv(arguments)}));
}
TypeKeyPattern TypeKeyPattern::typeParameter(identity::GenericParameterKey&& parameter) {
  return TypeKeyPattern(zc::heap<Impl>(PatternTypeParameter{zc::mv(parameter)}));
}
TypeKeyPattern TypeKeyPattern::unionOf(zc::Vector<TypeKeyPattern>&& alternatives) {
  return TypeKeyPattern(zc::heap<Impl>(PatternUnion{zc::mv(alternatives)}));
}
TypeKeyPattern TypeKeyPattern::intersection(zc::Vector<TypeKeyPattern>&& conjuncts) {
  return TypeKeyPattern(zc::heap<Impl>(PatternIntersection{zc::mv(conjuncts)}));
}
TypeKeyPattern TypeKeyPattern::reference(Mutability mutability, TypeKeyPattern&& referent) {
  return TypeKeyPattern(zc::heap<Impl>(PatternReference{mutability, zc::mv(referent)}));
}
TypeKeyPattern TypeKeyPattern::rawPointer(Mutability mutability, TypeKeyPattern&& pointee) {
  return TypeKeyPattern(zc::heap<Impl>(PatternRawPointer{mutability, zc::mv(pointee)}));
}
TypeKeyPattern TypeKeyPattern::existential(PatternExistentialType&& existential) {
  return TypeKeyPattern(zc::heap<Impl>(PatternExistential{zc::mv(existential)}));
}
TypeKeyPattern TypeKeyPattern::interfaceBound(PatternInterfaceInstantiation&& interface) {
  return TypeKeyPattern(zc::heap<Impl>(PatternInterfaceBound{zc::mv(interface)}));
}
TypeKeyPattern TypeKeyPattern::interfaceSelf(identity::DefId interface) {
  return TypeKeyPattern(zc::heap<Impl>(PatternInterfaceSelf{interface}));
}
TypeKeyPattern TypeKeyPattern::parameter(uint32_t index) {
  return TypeKeyPattern(zc::heap<Impl>(PatternParameter{index}));
}

TypeKeyPatternTag TypeKeyPattern::tag() const noexcept {
  const auto& value = impl->value;
  if (value.is<PatternPrimitive>()) return TypeKeyPatternTag::Primitive;
  if (value.is<PatternTuple>()) return TypeKeyPatternTag::Tuple;
  if (value.is<PatternObject>()) return TypeKeyPatternTag::Object;
  if (value.is<PatternDynamicArray>()) return TypeKeyPatternTag::DynamicArray;
  if (value.is<PatternSlice>()) return TypeKeyPatternTag::Slice;
  if (value.is<PatternFixedArray>()) return TypeKeyPatternTag::FixedArray;
  if (value.is<PatternFunction>()) return TypeKeyPatternTag::Function;
  if (value.is<PatternNominal>()) return TypeKeyPatternTag::Nominal;
  if (value.is<PatternTypeParameter>()) return TypeKeyPatternTag::TypeParameter;
  if (value.is<PatternUnion>()) return TypeKeyPatternTag::Union;
  if (value.is<PatternIntersection>()) return TypeKeyPatternTag::Intersection;
  if (value.is<PatternReference>()) return TypeKeyPatternTag::Reference;
  if (value.is<PatternRawPointer>()) return TypeKeyPatternTag::RawPointer;
  if (value.is<PatternExistential>()) return TypeKeyPatternTag::Existential;
  if (value.is<PatternInterfaceBound>()) return TypeKeyPatternTag::InterfaceBound;
  if (value.is<PatternInterfaceSelf>()) return TypeKeyPatternTag::InterfaceSelf;
  return TypeKeyPatternTag::Parameter;
}

TypeKeyPattern TypeKeyPattern::clone() const {
  const auto& value = impl->value;
  if (value.is<PatternPrimitive>()) return primitive(value.get<PatternPrimitive>().kind);
  if (value.is<PatternTuple>())
    return tuple(clonePatterns(value.get<PatternTuple>().elements.asPtr()));
  if (value.is<PatternObject>()) {
    zc::Vector<PatternObjectField> fields(value.get<PatternObject>().fields.size());
    for (const auto& field : value.get<PatternObject>().fields) {
      fields.add(PatternObjectField{field.name.clone(), field.type.clone(), field.mutability,
                                    field.presence});
    }
    return object(zc::mv(fields));
  }
  if (value.is<PatternDynamicArray>()) {
    return dynamicArray(value.get<PatternDynamicArray>().element.clone());
  }
  if (value.is<PatternSlice>()) return slice(value.get<PatternSlice>().element.clone());
  if (value.is<PatternFixedArray>()) {
    const auto& source = value.get<PatternFixedArray>();
    return fixedArray(source.element.clone(), source.length);
  }
  if (value.is<PatternFunction>()) {
    const auto& source = value.get<PatternFunction>().function;
    zc::Maybe<TypeKeyPattern> raises;
    ZC_IF_SOME(value, source.raises) { raises = value.clone(); }
    return function(PatternFunctionType{clonePatterns(source.parameters.asPtr()),
                                        source.success.clone(), zc::mv(raises)});
  }
  if (value.is<PatternNominal>()) {
    const auto& source = value.get<PatternNominal>();
    return nominal(source.definition, clonePatterns(source.arguments.asPtr()));
  }
  if (value.is<PatternTypeParameter>()) {
    return typeParameter(value.get<PatternTypeParameter>().parameter.clone());
  }
  if (value.is<PatternUnion>()) {
    return unionOf(clonePatterns(value.get<PatternUnion>().alternatives.asPtr()));
  }
  if (value.is<PatternIntersection>()) {
    return intersection(clonePatterns(value.get<PatternIntersection>().conjuncts.asPtr()));
  }
  if (value.is<PatternReference>()) {
    const auto& source = value.get<PatternReference>();
    return reference(source.mutability, source.referent.clone());
  }
  if (value.is<PatternRawPointer>()) {
    const auto& source = value.get<PatternRawPointer>();
    return rawPointer(source.mutability, source.pointee.clone());
  }
  if (value.is<PatternExistential>()) {
    return existential(clonePatternExistential(value.get<PatternExistential>().existential));
  }
  if (value.is<PatternInterfaceBound>()) {
    return interfaceBound(clonePatternInterface(value.get<PatternInterfaceBound>().interface));
  }
  if (value.is<PatternInterfaceSelf>()) {
    return interfaceSelf(value.get<PatternInterfaceSelf>().interface);
  }
  return parameter(value.get<PatternParameter>().index);
}

ImplPattern ImplPattern::clone() const {
  return ImplPattern{clonePatternInterface(interface), self.clone()};
}

TypeKeyPatternKey::TypeKeyPatternKey(zc::Array<uint8_t>&& bytes, TypeKeyPattern&& pattern) noexcept
    : value(zc::mv(bytes)), decoded(zc::heap<TypeKeyPattern>(zc::mv(pattern))) {}
zc::ArrayPtr<const uint8_t> TypeKeyPatternKey::bytes() const noexcept { return value.asPtr(); }
TypeKeyPatternKey TypeKeyPatternKey::clone() const {
  return TypeKeyPatternKey(zc::heapArray<uint8_t>(value.asPtr()), decoded->clone());
}

ImplPatternKey::ImplPatternKey(zc::Array<uint8_t>&& bytes, ImplPattern&& pattern) noexcept
    : value(zc::mv(bytes)), decoded(zc::heap<ImplPattern>(zc::mv(pattern))) {}
zc::ArrayPtr<const uint8_t> ImplPatternKey::bytes() const noexcept { return value.asPtr(); }
ImplPatternKey ImplPatternKey::clone() const {
  return ImplPatternKey(zc::heapArray<uint8_t>(value.asPtr()), decoded->clone());
}

CanonicalConstraint CanonicalConstraint::clone() const {
  if (value.is<ImplementsConstraint>()) {
    const auto& source = value.get<ImplementsConstraint>();
    zc::Vector<identity::SemanticTypeId> arguments(source.interface.arguments.size());
    for (const auto argument : source.interface.arguments) { arguments.add(argument); }
    return CanonicalConstraint(ImplementsConstraint{
        source.subject, InterfaceInstantiation{source.interface.interface, zc::mv(arguments)}});
  }
  if (value.is<MarkerConstraint>()) return CanonicalConstraint(value.get<MarkerConstraint>());
  const auto& source = value.get<ProjectionEqualsConstraint>();
  zc::Vector<identity::SemanticTypeId> arguments(source.projection.interface.arguments.size());
  for (const auto argument : source.projection.interface.arguments) { arguments.add(argument); }
  return CanonicalConstraint(ProjectionEqualsConstraint{
      ProjectionKey{
          source.projection.subject,
          InterfaceInstantiation{source.projection.interface.interface, zc::mv(arguments)},
          source.projection.associated},
      source.type});
}

CanonicalTypeHead CanonicalTypeHead::clone() const {
  if (value.is<BlanketTypeHead>()) return CanonicalTypeHead(value.get<BlanketTypeHead>());
  if (value.is<PrimitiveTypeHead>()) return CanonicalTypeHead(value.get<PrimitiveTypeHead>());
  if (value.is<TupleTypeHead>()) return CanonicalTypeHead(value.get<TupleTypeHead>());
  if (value.is<ObjectTypeHead>()) return CanonicalTypeHead(value.get<ObjectTypeHead>());
  if (value.is<DynamicArrayTypeHead>()) return CanonicalTypeHead(value.get<DynamicArrayTypeHead>());
  if (value.is<SliceTypeHead>()) return CanonicalTypeHead(value.get<SliceTypeHead>());
  if (value.is<FixedArrayTypeHead>()) return CanonicalTypeHead(value.get<FixedArrayTypeHead>());
  if (value.is<FunctionTypeHead>()) return CanonicalTypeHead(value.get<FunctionTypeHead>());
  if (value.is<NominalTypeHead>()) return CanonicalTypeHead(value.get<NominalTypeHead>());
  if (value.is<UnionTypeHead>()) return CanonicalTypeHead(value.get<UnionTypeHead>());
  if (value.is<IntersectionTypeHead>()) {
    return CanonicalTypeHead(value.get<IntersectionTypeHead>());
  }
  if (value.is<ReferenceTypeHead>()) return CanonicalTypeHead(value.get<ReferenceTypeHead>());
  if (value.is<RawPointerTypeHead>()) return CanonicalTypeHead(value.get<RawPointerTypeHead>());
  return CanonicalTypeHead(value.get<ExistentialTypeHead>());
}

ImplHead ImplHead::clone() const {
  zc::Vector<AssociatedTypeBindingData> bindings(associatedBindings.size());
  for (const auto& binding : associatedBindings) {
    bindings.add(AssociatedTypeBindingData{binding.associated, binding.type});
  }
  return ImplHead{impl,
                  pattern.clone(),
                  selfType,
                  head.clone(),
                  cloneGenericParameterKeys(genericParameters.asPtr()),
                  cloneConstraints(whereConstraints.asPtr()),
                  safety,
                  zc::mv(bindings),
                  declarationSpan.clone()};
}

MarkerComponentStep MarkerComponentStep::clone() const {
  if (value.is<TupleElementStep>()) return MarkerComponentStep(value.get<TupleElementStep>());
  if (value.is<ObjectFieldStep>()) {
    return MarkerComponentStep(ObjectFieldStep{value.get<ObjectFieldStep>().name.clone()});
  }
  if (value.is<ArrayElementStep>()) return MarkerComponentStep(value.get<ArrayElementStep>());
  if (value.is<NominalFieldStep>()) return MarkerComponentStep(value.get<NominalFieldStep>());
  if (value.is<ReferenceReferentStep>()) {
    return MarkerComponentStep(value.get<ReferenceReferentStep>());
  }
  return MarkerComponentStep(value.get<EnumVariantPayloadStep>());
}

MarkerEvidence MarkerEvidence::clone() const {
  if (value.is<ExplicitMarkerEvidence>()) {
    return MarkerEvidence(value.get<ExplicitMarkerEvidence>());
  }
  if (value.is<BuiltinMarkerEvidence>()) return MarkerEvidence(value.get<BuiltinMarkerEvidence>());
  zc::Vector<MarkerComponentEvidence> components(
      value.get<StructuralMarkerEvidence>().components.size());
  for (const auto& component : value.get<StructuralMarkerEvidence>().components) {
    zc::Vector<MarkerComponentStep> path(component.path.size());
    for (const auto& step : component.path) { path.add(step.clone()); }
    components.add(
        MarkerComponentEvidence{zc::mv(path), component.componentType, component.supportingFact});
  }
  return MarkerEvidence(StructuralMarkerEvidence{zc::mv(components)});
}

MarkerFact MarkerFact::clone() const {
  zc::Maybe<identity::SourceSpan> span;
  ZC_IF_SOME(value, declarationSpan) { span = value.clone(); }
  return MarkerFact{key, polarity, evidence.clone(), zc::mv(span)};
}

class SignatureFactsCanonicalEncoder final {
public:
  SignatureFactsCanonicalEncoder(const identity::SemanticIdentityRegistrySet& registries,
                                 const type::SemanticTypeStore& semanticTypes,
                                 const identity::ModuleKey& moduleKey)
      : registries(registries), semanticTypes(semanticTypes), moduleKey(moduleKey) {}

  SignatureFactsCanonicalEncoder(const identity::SemanticIdentityRegistrySet& registries,
                                 const type::SemanticTypeStore& semanticTypes,
                                 const identity::ModuleKey& moduleKey,
                                 const identity::SourceFileKey& sourceKey)
      : registries(registries),
        semanticTypes(semanticTypes),
        moduleKey(moduleKey),
        sourceKey(sourceKey) {}

  RecordEncodingResult encodeCanonicalConstValue(const CanonicalConstValue& value,
                                                 uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    if (!encodeConst(encoder, value, ordinal)) { return failedRecord(ordinal); }
    return EncodedSignature{encoder.finish()};
  }

  RecordEncodingResult encode(const SemanticSignature& signature, uint32_t ordinal) {
    if (!payloadMatchesDefinition(signature) || !spanBelongsToSource(signature.declarationSpan)) {
      return RecordFailure{RecordFailureKind::InvalidFact, ordinal};
    }
    auto definitionRecord = registries.definitions().lookupRecord(signature.definition);
    if (definitionRecord == zc::none) {
      return registryInvariant(registries.definitions().validate(signature.definition),
                               identity::IdentityAllocationPhase::Definition, ordinal);
    }
    bool localDefinition = false;
    ZC_IF_SOME(value, definitionRecord) {
      const auto left = value.module().encode();
      const auto right = moduleKey.encode();
      localDefinition = sameBytes(left.asPtr(), right.asPtr());
    }
    if (!localDefinition) { return RecordFailure{RecordFailureKind::InvalidFact, ordinal}; }

    identity::CanonicalEncoder encoder;
    if (!encodeDefinition(encoder, signature.definition, ordinal) ||
        !encodeDefinitionKind(encoder, signature.definitionKind) ||
        !encodeScope(encoder, signature.scope, ordinal) ||
        !encodeModifiers(encoder, signature.modifiers.asPtr()) ||
        !encodeAttributes(encoder, signature.attributes.asPtr(), ordinal) ||
        !encodePayload(encoder, signature.payload, signature.definition, signature.definitionKind,
                       ordinal) ||
        !encodeSpan(encoder, signature.declarationSpan)) {
      if (identityFailure != zc::none) {
        ZC_IF_SOME(value, identityFailure) { return zc::mv(value); }
      }
      return RecordFailure{
          invalidFact ? RecordFailureKind::InvalidFact : RecordFailureKind::CanonicalCodec,
          ordinal};
    }
    return EncodedSignature{encoder.finish()};
  }

  RecordEncodingResult encode(const ImplHead& head, uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    constexpr zc::StringPtr patternDomain = "zom.impl-pattern.v1\0"_zcc;
    auto patternHead = SignatureFactsCanonicalCodec::implPatternHead(head.pattern);
    auto semanticHead =
        SignatureFactsCanonicalCodec::canonicalTypeHead(head.selfType, semanticTypes);
    if (!spanBelongsToSource(head.declarationSpan) || !encodeImpl(encoder, head.impl, ordinal) ||
        !SignatureFactsCanonicalCodec::implPatternIsPublishable(head.pattern,
                                                                head.genericParameters.size()) ||
        head.pattern.bytes().size() <= patternDomain.size() || patternHead == zc::none ||
        semanticHead == zc::none) {
      return RecordFailure{RecordFailureKind::InvalidFact, ordinal};
    }
    ZC_IF_SOME(value, patternHead) {
      if (!sameTypeHead(value, head.head)) {
        return RecordFailure{RecordFailureKind::InvalidFact, ordinal};
      }
    }
    ZC_IF_SOME(value, semanticHead) {
      if (!sameTypeHead(value, head.head)) {
        return RecordFailure{RecordFailureKind::InvalidFact, ordinal};
      }
    }
    if (!SignatureFactsCanonicalCodec::implPatternKeyIsCanonical(head.pattern, registries)) {
      return RecordFailure{RecordFailureKind::CanonicalCodec, ordinal};
    }
    for (size_t index = 0; index < patternDomain.size(); ++index) {
      if (head.pattern.bytes()[index] != static_cast<uint8_t>(patternDomain[index])) {
        return RecordFailure{RecordFailureKind::CanonicalCodec, ordinal};
      }
    }
    encoder.encodeByteString(head.pattern.bytes());
    if (!encodeType(encoder, head.selfType, ordinal) ||
        !encodeTypeHead(encoder, head.head, ordinal)) {
      return failedRecord(ordinal);
    }
    encoder.encodeSequenceSize(head.genericParameters.size());
    for (size_t index = 0; index < head.genericParameters.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (head.genericParameters[previous] == head.genericParameters[index]) {
          return RecordFailure{RecordFailureKind::InvalidFact, ordinal};
        }
      }
      if (!encodeImplGenericParameterKey(encoder, head.genericParameters[index], head.impl,
                                         static_cast<uint32_t>(index))) {
        return failedRecord(ordinal);
      }
    }
    if (!encodeSorted(encoder, head.whereConstraints.asPtr(),
                      [&](identity::CanonicalEncoder& item, const CanonicalConstraint& constraint) {
                        return encodeConstraint(item, constraint, ordinal);
                      }) ||
        !isKnownEnum(head.safety, ImplSafety::Safe, ImplSafety::UnsafeAssertion)) {
      return failedRecord(ordinal);
    }
    encoder.encodeUint8(static_cast<uint8_t>(head.safety));
    if (!encodeSorted(
            encoder, head.associatedBindings.asPtr(),
            [&](identity::CanonicalEncoder& item, const AssociatedTypeBindingData& binding) {
              return encodeDefinition(item, binding.associated, ordinal) &&
                     encodeType(item, binding.type, ordinal);
            }) ||
        !encodeSpan(encoder, head.declarationSpan)) {
      return failedRecord(ordinal);
    }
    return EncodedSignature{encoder.finish()};
  }

  RecordEncodingResult encode(const MarkerFact& fact, uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    if (!encodeMarkerKey(encoder, fact.key, ordinal) ||
        !isKnownEnum(fact.polarity, Polarity::Positive, Polarity::Negative)) {
      return failedRecord(ordinal);
    }
    encoder.encodeUint8(static_cast<uint8_t>(fact.polarity));
    const auto& evidence = fact.evidence.variant();
    if (evidence.is<ExplicitMarkerEvidence>()) {
      encoder.encodeUint8(0x01);
      if (!encodeImpl(encoder, evidence.get<ExplicitMarkerEvidence>().impl, ordinal)) {
        return failedRecord(ordinal);
      }
    } else if (evidence.is<StructuralMarkerEvidence>()) {
      encoder.encodeUint8(0x02);
      if (!encodeSorted(
              encoder, evidence.get<StructuralMarkerEvidence>().components.asPtr(),
              [&](identity::CanonicalEncoder& item, const MarkerComponentEvidence& component) {
                return encodeMarkerComponent(item, component, ordinal);
              })) {
        return failedRecord(ordinal);
      }
    } else {
      const auto primitive = evidence.get<BuiltinMarkerEvidence>().primitive;
      if (!isKnownEnum(primitive, PrimitiveKind::I8, PrimitiveKind::Null)) {
        return RecordFailure{RecordFailureKind::CanonicalCodec, ordinal};
      }
      encoder.encodeUint8(0x03);
      encoder.encodeUint8(static_cast<uint8_t>(primitive));
    }
    if (fact.declarationSpan == zc::none) {
      encoder.encodeNone();
    } else {
      encoder.encodeSome();
      ZC_IF_SOME(span, fact.declarationSpan) {
        if (!encodeSpan(encoder, span)) { return failedRecord(ordinal); }
      }
    }
    return EncodedSignature{encoder.finish()};
  }

  RecordEncodingResult encodeCanonicalConstraint(const CanonicalConstraint& constraint,
                                                 uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    if (!encodeConstraint(encoder, constraint, ordinal)) { return failedRecord(ordinal); }
    return EncodedSignature{encoder.finish()};
  }

  RecordEncodingResult encodeInterfaceInstantiation(const InterfaceInstantiation& interface,
                                                    uint32_t ordinal) {
    identity::CanonicalEncoder encoder;
    if (!encodeInterface(encoder, interface, ordinal)) { return failedRecord(ordinal); }
    return EncodedSignature{encoder.finish()};
  }

private:
  RecordEncodingResult failedRecord(uint32_t ordinal) {
    if (identityFailure != zc::none) {
      ZC_IF_SOME(value, identityFailure) { return zc::mv(value); }
    }
    return RecordFailure{
        invalidFact ? RecordFailureKind::InvalidFact : RecordFailureKind::CanonicalCodec, ordinal};
  }

  bool encodeImpl(identity::CanonicalEncoder& encoder, identity::ImplId implementation,
                  uint32_t ordinal) {
    const auto failure = registries.impls().validate(implementation);
    if (failure != identity::FrozenRegistryFailure::None) {
      identityFailure =
          registryInvariant(failure, identity::IdentityAllocationPhase::Impl, ordinal);
      return false;
    }
    ZC_IF_SOME(key, registries.impls().lookup(implementation)) {
      key.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeMarkerKey(identity::CanonicalEncoder& encoder, const MarkerFactKey& key,
                       uint32_t ordinal) {
    return encodeDefinition(encoder, key.marker, ordinal) &&
           encodeType(encoder, key.subject, ordinal);
  }

  bool encodeConstraint(identity::CanonicalEncoder& encoder, const CanonicalConstraint& constraint,
                        uint32_t ordinal) {
    const auto& value = constraint.variant();
    if (value.is<ImplementsConstraint>()) {
      const auto& implements = value.get<ImplementsConstraint>();
      encoder.encodeUint8(0x01);
      return encodeType(encoder, implements.subject, ordinal) &&
             encodeInterface(encoder, implements.interface, ordinal);
    }
    if (value.is<MarkerConstraint>()) {
      const auto& marker = value.get<MarkerConstraint>();
      if (!isKnownEnum(marker.polarity, Polarity::Positive, Polarity::Negative)) return false;
      encoder.encodeUint8(0x02);
      if (!encodeType(encoder, marker.subject, ordinal) ||
          !encodeDefinition(encoder, marker.marker, ordinal)) {
        return false;
      }
      encoder.encodeUint8(static_cast<uint8_t>(marker.polarity));
      return true;
    }
    const auto& projection = value.get<ProjectionEqualsConstraint>();
    encoder.encodeUint8(0x03);
    return encodeType(encoder, projection.projection.subject, ordinal) &&
           encodeInterface(encoder, projection.projection.interface, ordinal) &&
           encodeDefinition(encoder, projection.projection.associated, ordinal) &&
           encodeType(encoder, projection.type, ordinal);
  }

  bool encodeTypeHead(identity::CanonicalEncoder& encoder, const CanonicalTypeHead& head,
                      uint32_t ordinal) {
    const auto& value = head.variant();
    if (value.is<BlanketTypeHead>()) {
      encoder.encodeUint8(0x01);
      return true;
    }
    if (value.is<PrimitiveTypeHead>()) {
      const auto primitive = value.get<PrimitiveTypeHead>().primitive;
      if (!isKnownEnum(primitive, PrimitiveKind::I8, PrimitiveKind::Null)) return false;
      encoder.encodeUint8(0x02);
      encoder.encodeUint8(static_cast<uint8_t>(primitive));
      return true;
    }
    if (value.is<TupleTypeHead>()) {
      encoder.encodeUint8(0x03);
      encoder.encodeUint32(value.get<TupleTypeHead>().arity);
      return true;
    }
    if (value.is<ObjectTypeHead>()) {
      encoder.encodeUint8(0x04);
      return true;
    }
    if (value.is<DynamicArrayTypeHead>()) {
      encoder.encodeUint8(0x05);
      return true;
    }
    if (value.is<SliceTypeHead>()) {
      encoder.encodeUint8(0x06);
      return true;
    }
    if (value.is<FixedArrayTypeHead>()) {
      encoder.encodeUint8(0x07);
      return true;
    }
    if (value.is<FunctionTypeHead>()) {
      const auto function = value.get<FunctionTypeHead>();
      encoder.encodeUint8(0x08);
      encoder.encodeUint32(function.arity);
      encoder.encodeBool(function.hasRaises);
      return true;
    }
    if (value.is<NominalTypeHead>()) {
      encoder.encodeUint8(0x09);
      return encodeDefinition(encoder, value.get<NominalTypeHead>().definition, ordinal);
    }
    if (value.is<UnionTypeHead>()) {
      encoder.encodeUint8(0x0a);
      encoder.encodeUint32(value.get<UnionTypeHead>().arity);
      return true;
    }
    if (value.is<IntersectionTypeHead>()) {
      encoder.encodeUint8(0x0b);
      encoder.encodeUint32(value.get<IntersectionTypeHead>().arity);
      return true;
    }
    if (value.is<ReferenceTypeHead>()) {
      const auto mutability = value.get<ReferenceTypeHead>().mutability;
      if (!isKnownEnum(mutability, Mutability::Const, Mutability::Mutable)) return false;
      encoder.encodeUint8(0x0c);
      encoder.encodeUint8(static_cast<uint8_t>(mutability));
      return true;
    }
    if (value.is<RawPointerTypeHead>()) {
      const auto mutability = value.get<RawPointerTypeHead>().mutability;
      if (!isKnownEnum(mutability, Mutability::Const, Mutability::Mutable)) return false;
      encoder.encodeUint8(0x0d);
      encoder.encodeUint8(static_cast<uint8_t>(mutability));
      return true;
    }
    encoder.encodeUint8(0x0e);
    return encodeDefinition(encoder, value.get<ExistentialTypeHead>().interface, ordinal);
  }

  bool encodeMarkerComponentStep(identity::CanonicalEncoder& encoder,
                                 const MarkerComponentStep& step, uint32_t ordinal) {
    const auto& value = step.variant();
    if (value.is<TupleElementStep>()) {
      encoder.encodeUint8(0x01);
      encoder.encodeUint32(value.get<TupleElementStep>().index);
      return true;
    }
    if (value.is<ObjectFieldStep>()) {
      encoder.encodeUint8(0x02);
      value.get<ObjectFieldStep>().name.encode(encoder);
      return true;
    }
    if (value.is<ArrayElementStep>()) {
      encoder.encodeUint8(0x03);
      return true;
    }
    if (value.is<NominalFieldStep>()) {
      encoder.encodeUint8(0x04);
      return encodeDefinition(encoder, value.get<NominalFieldStep>().field, ordinal);
    }
    if (value.is<ReferenceReferentStep>()) {
      encoder.encodeUint8(0x05);
      return true;
    }
    encoder.encodeUint8(0x06);
    const auto& payload = value.get<EnumVariantPayloadStep>();
    if (!encodeDefinition(encoder, payload.variant, ordinal)) return false;
    encoder.encodeUint32(payload.index);
    return true;
  }

  bool encodeMarkerComponent(identity::CanonicalEncoder& encoder,
                             const MarkerComponentEvidence& component, uint32_t ordinal) {
    if (component.path.size() == 0) return false;
    encoder.encodeSequenceSize(component.path.size());
    for (const auto& step : component.path) {
      if (!encodeMarkerComponentStep(encoder, step, ordinal)) return false;
    }
    return encodeType(encoder, component.componentType, ordinal) &&
           encodeMarkerKey(encoder, component.supportingFact, ordinal);
  }

  bool encodeDefinition(identity::CanonicalEncoder& encoder, identity::DefId definition,
                        uint32_t ordinal) {
    const auto failure = registries.definitions().validate(definition);
    if (failure != identity::FrozenRegistryFailure::None) {
      identityFailure =
          registryInvariant(failure, identity::IdentityAllocationPhase::Definition, ordinal);
      return false;
    }
    ZC_IF_SOME(key, registries.definitions().lookup(definition)) {
      key.encode(encoder);
      return true;
    }
    return false;
  }

  bool encodeType(identity::CanonicalEncoder& encoder, identity::SemanticTypeId type, uint32_t) {
    auto result = semanticTypes.get(type);
    if (result.is<identity::IdentityInvariant>()) {
      identityFailure = result.get<identity::IdentityInvariant>().clone();
      return false;
    }
    const auto bytes = result.get<type::SemanticTypeLookup>().key().bytes();
    constexpr zc::StringPtr domain = "zom.semantic-type-key.v1\0"_zcc;
    if (bytes.size() <= domain.size()) { return false; }
    for (size_t index = 0; index < domain.size(); ++index) {
      if (bytes[index] != static_cast<uint8_t>(domain[index])) { return false; }
    }
    encodeRaw(encoder, bytes.slice(domain.size(), bytes.size()));
    return true;
  }

  bool encodeSpan(identity::CanonicalEncoder& encoder, const identity::SourceSpan& span) {
    if (!spanBelongsToSource(span)) {
      invalidFact = true;
      return false;
    }
    span.encode(encoder);
    return true;
  }

  bool encodeDefinitionKind(identity::CanonicalEncoder& encoder, identity::DefinitionKind kind) {
    if (!identity::isDefinitionKindValue(kind)) { return false; }
    encoder.encodeUint8(static_cast<uint8_t>(kind));
    return true;
  }

  bool encodeScope(identity::CanonicalEncoder& encoder, const SignatureScope& scope,
                   uint32_t ordinal) {
    const auto& value = scope.variant();
    if (value.is<ModuleDefinitionSignatureScope>()) {
      encoder.encodeUint8(0x01);
      return true;
    }
    if (value.is<MemberSignatureScope>()) {
      const auto& member = value.get<MemberSignatureScope>();
      if (!isKnownEnum(member.visibility, MemberVisibility::Public, MemberVisibility::Private)) {
        return false;
      }
      encoder.encodeUint8(0x02);
      return encodeDefinition(encoder, member.owner, ordinal) &&
             (encoder.encodeUint8(static_cast<uint8_t>(member.visibility)), true);
    }
    encoder.encodeUint8(0x03);
    return encodeDefinition(encoder, value.get<EnclosedSignatureScope>().owner, ordinal);
  }

  bool encodeModifiers(identity::CanonicalEncoder& encoder,
                       zc::ArrayPtr<const SignatureModifier> modifiers) {
    encoder.encodeSequenceSize(modifiers.size());
    uint8_t previous = 0;
    for (const auto modifier : modifiers) {
      const auto tag = static_cast<uint8_t>(modifier);
      if (tag <= previous ||
          !isKnownEnum(modifier, SignatureModifier::Static, SignatureModifier::Abstract)) {
        return false;
      }
      previous = tag;
      encoder.encodeUint8(tag);
    }
    return true;
  }

  template <typename T, typename Encode>
  bool encodeSorted(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const T> values,
                    Encode encodeValue) {
    zc::Vector<zc::Array<uint8_t>> encoded(values.size());
    for (const auto& value : values) {
      identity::CanonicalEncoder item;
      if (!encodeValue(item, value)) { return false; }
      encoded.add(item.finish());
    }
    for (size_t index = 1; index < encoded.size(); ++index) {
      if (!lessBytes(encoded[index - 1].asPtr(), encoded[index].asPtr())) { return false; }
    }
    encoder.encodeSequenceSize(encoded.size());
    for (const auto& item : encoded) { encodeRaw(encoder, item.asPtr()); }
    return true;
  }

  bool encodeAttributes(identity::CanonicalEncoder& encoder,
                        zc::ArrayPtr<const NormalizedAttributeFact> attributes, uint32_t ordinal) {
    return encodeSorted(
        encoder, attributes,
        [&](identity::CanonicalEncoder& item, const NormalizedAttributeFact& attribute) {
          if (attribute.attribute != NormalizedAttribute::MoveReceiver) { return false; }
          if (!spanBelongsToSource(attribute.sourceSpan)) {
            invalidFact = true;
            return false;
          }
          return encodeDefinition(item, attribute.target, ordinal) &&
                 (item.encodeUint8(static_cast<uint8_t>(attribute.attribute)),
                  attribute.sourceSpan.encode(item), true);
        });
  }

  bool encodeInterface(identity::CanonicalEncoder& encoder, const InterfaceInstantiation& interface,
                       uint32_t ordinal) {
    if (!encodeDefinition(encoder, interface.interface, ordinal)) { return false; }
    encoder.encodeSequenceSize(interface.arguments.size());
    for (const auto argument : interface.arguments) {
      if (!encodeType(encoder, argument, ordinal)) return false;
    }
    return true;
  }

  bool encodeGenericParameterKey(identity::CanonicalEncoder& encoder,
                                 const identity::GenericParameterKey& key, identity::DefId owner,
                                 uint32_t index) {
    auto ownerKey = registries.definitions().lookup(owner);
    auto parameterId = registries.genericParameters().find(key);
    if (ownerKey == zc::none || parameterId == zc::none) return false;
    ZC_IF_SOME(id, parameterId) {
      auto authority = registries.genericParameters().lookupAuthority(id);
      if (authority == zc::none) return false;
      ZC_IF_SOME(value, authority) {
        if (!value.verify() || value.record().kind() != identity::GenericParameterKind::Type ||
            value.record().ordinal() != index) {
          return false;
        }
        ZC_IF_SOME(expected, ownerKey) {
          ZC_IF_SOME(actual, value.record().owner().definitionKey()) {
            if (actual != expected) return false;
            key.encode(encoder);
            return true;
          }
        }
      }
    }
    return false;
  }

  bool encodeImplGenericParameterKey(identity::CanonicalEncoder& encoder,
                                     const identity::GenericParameterKey& key,
                                     identity::ImplId owner, uint32_t index) {
    auto ownerKey = registries.impls().lookup(owner);
    auto parameterId = registries.genericParameters().find(key);
    if (ownerKey == zc::none || parameterId == zc::none) return false;
    ZC_IF_SOME(id, parameterId) {
      auto authority = registries.genericParameters().lookupAuthority(id);
      if (authority == zc::none) return false;
      ZC_IF_SOME(value, authority) {
        if (!value.verify() || value.record().kind() != identity::GenericParameterKind::Type ||
            value.record().ordinal() != index) {
          return false;
        }
        ZC_IF_SOME(expected, ownerKey) {
          ZC_IF_SOME(actual, value.record().owner().implKey()) {
            if (actual != expected) return false;
            key.encode(encoder);
            return true;
          }
        }
      }
    }
    return false;
  }

  bool encodeCallableParameterKey(identity::CanonicalEncoder& encoder,
                                  const identity::CallableParameterKey& key, identity::DefId owner,
                                  identity::CallableParameterPositionKind positionKind,
                                  const zc::Maybe<uint32_t>& positionOrdinal,
                                  bool requireExactOrdinal) {
    auto ownerKey = registries.definitions().lookup(owner);
    auto parameterId = registries.callableParameters().find(key);
    if (ownerKey == zc::none || parameterId == zc::none) return false;
    ZC_IF_SOME(id, parameterId) {
      auto authority = registries.callableParameters().lookupAuthority(id);
      if (authority == zc::none) return false;
      ZC_IF_SOME(value, authority) {
        if (!value.verify() || value.record().position().kind() != positionKind) return false;
        ZC_IF_SOME(expected, ownerKey) {
          if (value.record().owner() != expected) return false;
        }
        if (requireExactOrdinal && value.record().position().ordinal() != positionOrdinal) {
          return false;
        }
        key.encode(encoder);
        return true;
      }
    }
    return false;
  }

  bool callableHeaderMatches(identity::DefId definition, const CallableSignature& signature) const {
    ZC_IF_SOME(authority, registries.definitions().lookupAuthority(definition)) {
      if (!authority.verify()) return false;
      ZC_IF_SOME(overload, authority.overloadHeaderAuthority()) {
        const auto& header = overload.header();
        if (header.genericParameters().size() != signature.genericParameters.size() ||
            header.parameters().size() != signature.parameters.size() ||
            (header.receiver() == zc::none) != (signature.receiver == zc::none)) {
          return false;
        }
        for (size_t index = 0; index < signature.genericParameters.size(); ++index) {
          if ((header.genericParameters()[index].defaultType() == zc::none) !=
              (signature.genericParameters[index].defaultType == zc::none)) {
            return false;
          }
        }
        ZC_IF_SOME(expectedReceiver, header.receiver()) {
          ZC_IF_SOME(receiver, signature.receiver) {
            switch (expectedReceiver) {
              case identity::ReceiverShape::Shared:
                if (receiver.mode != ReceiverMode::Shared) return false;
                break;
              case identity::ReceiverShape::Mutable:
                if (receiver.mode != ReceiverMode::Mutable) return false;
                break;
              case identity::ReceiverShape::Move:
                if (receiver.mode != ReceiverMode::Move) return false;
                break;
            }
          }
        }
        for (size_t index = 0; index < signature.parameters.size(); ++index) {
          if (header.parameters()[index].label() != signature.parameters[index].label.text() ||
              header.parameters()[index].hasDefault() != signature.parameters[index].hasDefault) {
            return false;
          }
        }
        return true;
      }
    }
    return false;
  }

  bool encodeGenericParameter(identity::CanonicalEncoder& encoder,
                              const GenericParameterSignature& parameter, identity::DefId owner,
                              uint32_t ordinal) {
    if (!encodeGenericParameterKey(encoder, parameter.parameter, owner, parameter.index)) {
      return false;
    }
    encoder.encodeUint32(parameter.index);
    if (!encodeSorted(
            encoder, parameter.bounds.asPtr(),
            [&](identity::CanonicalEncoder& item, const InterfaceInstantiation& interface) {
              return encodeInterface(item, interface, ordinal);
            }) ||
        !encodeSorted(encoder, parameter.markerBounds.asPtr(),
                      [&](identity::CanonicalEncoder& item, identity::DefId marker) {
                        return encodeDefinition(item, marker, ordinal);
                      })) {
      return false;
    }
    if (parameter.defaultType == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(value, parameter.defaultType) { return encodeType(encoder, value, ordinal); }
    return false;
  }

  bool encodeGenericParameters(identity::CanonicalEncoder& encoder,
                               zc::ArrayPtr<const GenericParameterSignature> parameters,
                               identity::DefId owner, uint32_t ordinal) {
    encoder.encodeSequenceSize(parameters.size());
    for (size_t index = 0; index < parameters.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (parameters[previous].parameter == parameters[index].parameter) return false;
      }
      if (parameters[index].index != index ||
          !encodeGenericParameter(encoder, parameters[index], owner, ordinal)) {
        return false;
      }
    }
    return true;
  }

  bool encodeTypeSequence(identity::CanonicalEncoder& encoder,
                          zc::ArrayPtr<const identity::SemanticTypeId> values, uint32_t ordinal) {
    encoder.encodeSequenceSize(values.size());
    for (const auto value : values) {
      if (!encodeType(encoder, value, ordinal)) return false;
    }
    return true;
  }

  bool encodeOptionalType(identity::CanonicalEncoder& encoder,
                          const zc::Maybe<identity::SemanticTypeId>& value, uint32_t ordinal) {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(type, value) { return encodeType(encoder, type, ordinal); }
    return false;
  }

  template <typename T>
  bool encodeOptionalEnum(identity::CanonicalEncoder& encoder, const zc::Maybe<T>& value, T first,
                          T last) {
    if (value == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(item, value) {
      if (!isKnownEnum(item, first, last)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(item));
      return true;
    }
    return false;
  }

  bool encodeInteger(identity::CanonicalEncoder& encoder, const CanonicalInteger& value) {
    if (!isKnownEnum(value.sign, IntegerSign::NonNegative, IntegerSign::Negative)) return false;
    if (value.magnitude.size() == 0 && value.sign == IntegerSign::Negative) return false;
    if (value.magnitude.size() != 0 && value.magnitude[0] == 0) return false;
    encoder.encodeUint8(static_cast<uint8_t>(value.sign));
    encoder.encodeByteString(value.magnitude.asPtr());
    return true;
  }

  bool encodeConst(identity::CanonicalEncoder& encoder, const CanonicalConstValue& value,
                   uint32_t ordinal) {
    encoder.encodeUint8(static_cast<uint8_t>(value.tag()));
    const auto& variant = value.impl->value;
    if (variant.is<ConstInteger>()) {
      return encodeInteger(encoder, variant.get<ConstInteger>().value);
    }
    if (variant.is<ConstFloat32>()) {
      encoder.encodeUint8(0x01);
      encoder.encodeUint32(variant.get<ConstFloat32>().bits);
      return true;
    }
    if (variant.is<ConstFloat64>()) {
      encoder.encodeUint8(0x02);
      encoder.encodeUint64(variant.get<ConstFloat64>().bits);
      return true;
    }
    if (variant.is<ConstBool>()) {
      encoder.encodeBool(variant.get<ConstBool>().value);
      return true;
    }
    if (variant.is<ConstChar>()) {
      const auto scalar = variant.get<ConstChar>().scalar;
      if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
      encoder.encodeUint32(scalar);
      return true;
    }
    if (variant.is<ConstString>()) {
      encoder.encodeByteString(variant.get<ConstString>().bytes.asPtr());
      return true;
    }
    if (variant.is<ConstNull>() || variant.is<ConstUnit>()) return true;
    if (variant.is<ConstTuple>()) {
      const auto& values = variant.get<ConstTuple>().values;
      encoder.encodeSequenceSize(values.size());
      for (const auto& item : values) {
        if (!encodeConst(encoder, item, ordinal)) return false;
      }
      return true;
    }
    if (variant.is<ConstArray>()) {
      const auto& values = variant.get<ConstArray>().values;
      encoder.encodeSequenceSize(values.size());
      for (const auto& item : values) {
        if (!encodeConst(encoder, item, ordinal)) return false;
      }
      return true;
    }
    if (variant.is<ConstObject>()) {
      const auto& fields = variant.get<ConstObject>().fields;
      zc::Vector<zc::Array<uint8_t>> names(fields.size());
      zc::Vector<zc::Array<uint8_t>> records(fields.size());
      for (const auto& field : fields) {
        identity::CanonicalEncoder nameEncoder;
        field.name.encode(nameEncoder);
        names.add(nameEncoder.finish());
        identity::CanonicalEncoder recordEncoder;
        field.name.encode(recordEncoder);
        if (!encodeConst(recordEncoder, field.value, ordinal)) return false;
        records.add(recordEncoder.finish());
      }
      for (size_t index = 1; index < names.size(); ++index) {
        if (!lessBytes(names[index - 1].asPtr(), names[index].asPtr())) return false;
      }
      encoder.encodeSequenceSize(records.size());
      for (const auto& record : records) { encodeRaw(encoder, record.asPtr()); }
      return true;
    }
    const auto& enumeration = variant.get<ConstEnum>();
    if (!encodeDefinition(encoder, enumeration.variant, ordinal)) return false;
    encoder.encodeSequenceSize(enumeration.payload.size());
    for (const auto& item : enumeration.payload) {
      if (!encodeConst(encoder, item, ordinal)) return false;
    }
    return true;
  }

  bool encodeObjectSafetyCause(identity::CanonicalEncoder& encoder, const ObjectSafetyCause& cause,
                               uint32_t ordinal) {
    const auto& value = cause.variant();
    if (value.is<UnsafeSuperinterfaceCause>()) {
      encoder.encodeUint8(0x01);
      return encodeDefinition(encoder, value.get<UnsafeSuperinterfaceCause>().interface, ordinal);
    }
    if (value.is<GenericMethodCause>()) {
      encoder.encodeUint8(0x02);
      return encodeDefinition(encoder, value.get<GenericMethodCause>().method, ordinal);
    }
    if (value.is<ReturnsSelfCause>()) {
      encoder.encodeUint8(0x03);
      return encodeDefinition(encoder, value.get<ReturnsSelfCause>().method, ordinal);
    }
    if (value.is<MovesSelfCause>()) {
      encoder.encodeUint8(0x04);
      return encodeDefinition(encoder, value.get<MovesSelfCause>().method, ordinal);
    }
    if (value.is<StaticMethodCause>()) {
      encoder.encodeUint8(0x05);
      return encodeDefinition(encoder, value.get<StaticMethodCause>().method, ordinal);
    }
    if (value.is<GenericAssociatedTypeCause>()) {
      encoder.encodeUint8(0x06);
      return encodeDefinition(encoder, value.get<GenericAssociatedTypeCause>().associated, ordinal);
    }
    const auto& unsized = value.get<UnsizedParameterCause>();
    encoder.encodeUint8(0x07);
    zc::Maybe<uint32_t> anyOrdinaryOrdinal;
    return encodeDefinition(encoder, unsized.method, ordinal) &&
           encodeCallableParameterKey(encoder, unsized.parameter, unsized.method,
                                      identity::CallableParameterPositionKind::Ordinary,
                                      anyOrdinaryOrdinal, false) &&
           encodeType(encoder, unsized.type, ordinal);
  }

  bool encodePayload(identity::CanonicalEncoder& encoder, const SemanticSignaturePayload& payload,
                     identity::DefId definition, identity::DefinitionKind definitionKind,
                     uint32_t ordinal) {
    const auto& value = payload.variant();
    if (value.is<CallableSignature>()) {
      const auto& callable = value.get<CallableSignature>();
      encoder.encodeUint8(0x01);
      if (!callableHeaderMatches(definition, callable)) return false;
      if (!encodeGenericParameters(encoder, callable.genericParameters.asPtr(), definition,
                                   ordinal)) {
        return false;
      }
      if (callable.receiver == zc::none) {
        encoder.encodeNone();
      } else {
        encoder.encodeSome();
        ZC_IF_SOME(receiver, callable.receiver) {
          zc::Maybe<uint32_t> noOrdinal;
          if (!encodeCallableParameterKey(encoder, receiver.parameter, definition,
                                          identity::CallableParameterPositionKind::Receiver,
                                          noOrdinal, true) ||
              !isKnownEnum(receiver.mode, ReceiverMode::Static, ReceiverMode::Move)) {
            return false;
          }
          encoder.encodeUint8(static_cast<uint8_t>(receiver.mode));
        }
      }
      encoder.encodeSequenceSize(callable.parameters.size());
      for (size_t index = 0; index < callable.parameters.size(); ++index) {
        const auto& parameter = callable.parameters[index];
        for (size_t previous = 0; previous < index; ++previous) {
          if (callable.parameters[previous].parameter == parameter.parameter) return false;
        }
        zc::Maybe<uint32_t> parameterOrdinal = static_cast<uint32_t>(index);
        if (!encodeCallableParameterKey(encoder, parameter.parameter, definition,
                                        identity::CallableParameterPositionKind::Ordinary,
                                        parameterOrdinal, true)) {
          return false;
        }
        parameter.label.encode(encoder);
        if (!encodeType(encoder, parameter.type, ordinal) ||
            !isKnownEnum(parameter.mode, ParameterMode::Value, ParameterMode::MutableReference)) {
          return false;
        }
        encoder.encodeUint8(static_cast<uint8_t>(parameter.mode));
        encoder.encodeBool(parameter.hasDefault);
      }
      return encodeType(encoder, callable.success, ordinal) &&
             encodeOptionalType(encoder, callable.raises, ordinal) &&
             encodeOptionalEnum(encoder, callable.abi, ExternAbi::Cdecl, ExternAbi::ZomNative);
    }
    if (value.is<NominalSignature>()) {
      const auto& nominal = value.get<NominalSignature>();
      encoder.encodeUint8(0x02);
      if (!encodeGenericParameters(encoder, nominal.genericParameters.asPtr(), definition,
                                   ordinal) ||
          !encodeOptionalType(encoder, nominal.base, ordinal) ||
          !encodeSorted(
              encoder, nominal.interfaces.asPtr(),
              [&](identity::CanonicalEncoder& item, const InterfaceInstantiation& interface) {
                return encodeInterface(item, interface, ordinal);
              })) {
        return false;
      }
      const auto encodeDefinitions = [&](zc::ArrayPtr<const identity::DefId> definitions) {
        return encodeSorted(encoder, definitions,
                            [&](identity::CanonicalEncoder& item, identity::DefId definition) {
                              return encodeDefinition(item, definition, ordinal);
                            });
      };
      return encodeDefinitions(nominal.fields.asPtr()) &&
             encodeDefinitions(nominal.variants.asPtr()) &&
             encodeDefinitions(nominal.members.asPtr());
    }
    if (value.is<InterfaceSignature>()) {
      const auto& interface = value.get<InterfaceSignature>();
      encoder.encodeUint8(0x03);
      if (!encodeGenericParameters(encoder, interface.genericParameters.asPtr(), definition,
                                   ordinal) ||
          !encodeSorted(
              encoder, interface.parents.asPtr(),
              [&](identity::CanonicalEncoder& item, const InterfaceInstantiation& parent) {
                return encodeInterface(item, parent, ordinal);
              }) ||
          !encodeSorted(encoder, interface.members.asPtr(),
                        [&](identity::CanonicalEncoder& item, identity::DefId member) {
                          return encodeDefinition(item, member, ordinal);
                        }) ||
          !encodeSorted(encoder, interface.associatedTypes.asPtr(),
                        [&](identity::CanonicalEncoder& item, identity::DefId associated) {
                          return encodeDefinition(item, associated, ordinal);
                        })) {
        return false;
      }
      encoder.encodeBool(interface.markerOnly);
      return encodeSorted(encoder, interface.objectSafetyCauses.asPtr(),
                          [&](identity::CanonicalEncoder& item, const ObjectSafetyCause& cause) {
                            return encodeObjectSafetyCause(item, cause, ordinal);
                          });
    }
    if (value.is<TypeAliasSignature>()) {
      const auto& alias = value.get<TypeAliasSignature>();
      encoder.encodeUint8(0x04);
      return encodeGenericParameters(encoder, alias.genericParameters.asPtr(), definition,
                                     ordinal) &&
             encodeType(encoder, alias.target, ordinal);
    }
    if (value.is<AssociatedTypeSignature>()) {
      const auto& associated = value.get<AssociatedTypeSignature>();
      encoder.encodeUint8(0x05);
      return encodeGenericParameters(encoder, associated.genericParameters.asPtr(), definition,
                                     ordinal) &&
             encodeSorted(
                 encoder, associated.bounds.asPtr(),
                 [&](identity::CanonicalEncoder& item, const InterfaceInstantiation& bound) {
                   return encodeInterface(item, bound, ordinal);
                 }) &&
             encodeSorted(encoder, associated.markerBounds.asPtr(),
                          [&](identity::CanonicalEncoder& item, identity::DefId marker) {
                            return encodeDefinition(item, marker, ordinal);
                          }) &&
             encodeOptionalType(encoder, associated.defaultType, ordinal);
    }
    if (value.is<ValueSignature>()) {
      const auto& binding = value.get<ValueSignature>();
      if (!isKnownEnum(binding.mutability, Mutability::Const, Mutability::Mutable)) return false;
      if ((definitionKind == identity::DefinitionKind::Constant) !=
          (binding.constantValue != zc::none)) {
        invalidFact = true;
        return false;
      }
      if (definitionKind == identity::DefinitionKind::Constant &&
          (!binding.hasInitializer || binding.mutability != Mutability::Const)) {
        invalidFact = true;
        return false;
      }
      encoder.encodeUint8(0x06);
      if (!encodeType(encoder, binding.type, ordinal)) return false;
      encoder.encodeUint8(static_cast<uint8_t>(binding.mutability));
      encoder.encodeBool(binding.hasInitializer);
      if (binding.constantValue == zc::none) {
        encoder.encodeNone();
      } else {
        encoder.encodeSome();
        ZC_IF_SOME(constant, binding.constantValue) {
          if (!encodeConst(encoder, constant, ordinal)) return false;
        }
      }
      return encodeOptionalEnum(encoder, binding.abi, ExternAbi::Cdecl, ExternAbi::ZomNative);
    }
    if (value.is<EnumVariantSignature>()) {
      const auto& variant = value.get<EnumVariantSignature>();
      encoder.encodeUint8(0x07);
      if (!encodeTypeSequence(encoder, variant.payload.asPtr(), ordinal)) return false;
      if (variant.discriminant == zc::none) {
        encoder.encodeNone();
        return true;
      }
      encoder.encodeSome();
      ZC_IF_SOME(discriminant, variant.discriminant) {
        return encodeInteger(encoder, discriminant);
      }
      return false;
    }
    ZC_UNREACHABLE
  }

  const identity::SemanticIdentityRegistrySet& registries;
  const type::SemanticTypeStore& semanticTypes;
  const identity::ModuleKey& moduleKey;
  zc::Maybe<const identity::SourceFileKey&> sourceKey;

  bool spanBelongsToSource(const identity::SourceSpan& span) const {
    ZC_IF_SOME(source, sourceKey) { return span.belongsTo(source); }
    return false;
  }
  zc::Maybe<identity::IdentityInvariant> identityFailure;
  bool invalidFact = false;
};

bool SignatureFactsCanonicalCodec::encodePatternInterface(
    identity::CanonicalEncoder& encoder, const PatternInterfaceInstantiation& interface,
    const identity::SemanticIdentityRegistrySet& registries) {
  if (registries.definitions().validate(interface.interface) !=
      identity::FrozenRegistryFailure::None) {
    return false;
  }
  ZC_IF_SOME(key, registries.definitions().lookup(interface.interface)) {
    key.encode(encoder);
  } else {
    return false;
  }
  encoder.encodeSequenceSize(interface.arguments.size());
  for (const auto& argument : interface.arguments) {
    if (!encodePattern(encoder, argument, registries)) return false;
  }
  return true;
}

bool SignatureFactsCanonicalCodec::encodePattern(
    identity::CanonicalEncoder& encoder, const TypeKeyPattern& pattern,
    const identity::SemanticIdentityRegistrySet& registries) {
  const auto& value = pattern.impl->value;
  const auto encodeDefinition = [&](identity::CanonicalEncoder& target,
                                    identity::DefId definition) {
    if (registries.definitions().validate(definition) != identity::FrozenRegistryFailure::None) {
      return false;
    }
    ZC_IF_SOME(key, registries.definitions().lookup(definition)) {
      key.encode(target);
      return true;
    }
    return false;
  };
  const auto encodePatternSequence = [&](identity::CanonicalEncoder& target,
                                         zc::ArrayPtr<const TypeKeyPattern> patterns) {
    target.encodeSequenceSize(patterns.size());
    for (const auto& item : patterns) {
      if (!encodePattern(target, item, registries)) return false;
    }
    return true;
  };
  if (value.is<PatternPrimitive>()) {
    const auto kind = value.get<PatternPrimitive>().kind;
    if (!isKnownEnum(kind, PrimitiveKind::I8, PrimitiveKind::Null)) return false;
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Primitive));
    encoder.encodeUint8(static_cast<uint8_t>(kind));
    return true;
  }
  if (value.is<PatternTuple>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Tuple));
    return encodePatternSequence(encoder, value.get<PatternTuple>().elements.asPtr());
  }
  if (value.is<PatternObject>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Object));
    const auto& fields = value.get<PatternObject>().fields;
    zc::Vector<zc::Array<uint8_t>> keys(fields.size());
    zc::Vector<zc::Array<uint8_t>> records(fields.size());
    for (const auto& field : fields) {
      if (!isKnownEnum(field.mutability, Mutability::Const, Mutability::Mutable) ||
          !isKnownEnum(field.presence, FieldPresence::Required, FieldPresence::Optional)) {
        return false;
      }
      identity::CanonicalEncoder key;
      field.name.encode(key);
      keys.add(key.finish());
      identity::CanonicalEncoder item;
      field.name.encode(item);
      if (!encodePattern(item, field.type, registries)) return false;
      item.encodeUint8(static_cast<uint8_t>(field.mutability));
      item.encodeUint8(static_cast<uint8_t>(field.presence));
      records.add(item.finish());
    }
    for (size_t index = 1; index < keys.size(); ++index) {
      if (!lessBytes(keys[index - 1].asPtr(), keys[index].asPtr())) return false;
    }
    encoder.encodeSequenceSize(records.size());
    for (const auto& record : records) { encodeRaw(encoder, record.asPtr()); }
    return true;
  }
  if (value.is<PatternDynamicArray>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::DynamicArray));
    return encodePattern(encoder, value.get<PatternDynamicArray>().element, registries);
  }
  if (value.is<PatternSlice>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Slice));
    return encodePattern(encoder, value.get<PatternSlice>().element, registries);
  }
  if (value.is<PatternFixedArray>()) {
    const auto& fixed = value.get<PatternFixedArray>();
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::FixedArray));
    if (!encodePattern(encoder, fixed.element, registries)) return false;
    encoder.encodeUint64(fixed.length);
    return true;
  }
  if (value.is<PatternFunction>()) {
    const auto& function = value.get<PatternFunction>().function;
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Function));
    if (!encodePatternSequence(encoder, function.parameters.asPtr()) ||
        !encodePattern(encoder, function.success, registries)) {
      return false;
    }
    if (function.raises == zc::none) {
      encoder.encodeNone();
      return true;
    }
    encoder.encodeSome();
    ZC_IF_SOME(raises, function.raises) { return encodePattern(encoder, raises, registries); }
    return false;
  }
  if (value.is<PatternNominal>()) {
    const auto& nominal = value.get<PatternNominal>();
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Nominal));
    return encodeDefinition(encoder, nominal.definition) &&
           encodePatternSequence(encoder, nominal.arguments.asPtr());
  }
  if (value.is<PatternTypeParameter>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::TypeParameter));
    const auto& parameter = value.get<PatternTypeParameter>().parameter;
    if (registries.genericParameters().find(parameter) == zc::none) return false;
    parameter.encode(encoder);
    return true;
  }
  const auto encodeSortedPatterns = [&](identity::CanonicalEncoder& target,
                                        zc::ArrayPtr<const TypeKeyPattern> patterns) {
    zc::Vector<zc::Array<uint8_t>> records(patterns.size());
    for (const auto& item : patterns) {
      identity::CanonicalEncoder encoded;
      if (!encodePattern(encoded, item, registries)) return false;
      records.add(encoded.finish());
    }
    for (size_t index = 1; index < records.size(); ++index) {
      if (!lessBytes(records[index - 1].asPtr(), records[index].asPtr())) return false;
    }
    target.encodeSequenceSize(records.size());
    for (const auto& record : records) { encodeRaw(target, record.asPtr()); }
    return true;
  };
  if (value.is<PatternUnion>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Union));
    return encodeSortedPatterns(encoder, value.get<PatternUnion>().alternatives.asPtr());
  }
  if (value.is<PatternIntersection>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Intersection));
    return encodeSortedPatterns(encoder, value.get<PatternIntersection>().conjuncts.asPtr());
  }
  if (value.is<PatternReference>()) {
    const auto& reference = value.get<PatternReference>();
    if (!isKnownEnum(reference.mutability, Mutability::Const, Mutability::Mutable)) return false;
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Reference));
    encoder.encodeUint8(static_cast<uint8_t>(reference.mutability));
    return encodePattern(encoder, reference.referent, registries);
  }
  if (value.is<PatternRawPointer>()) {
    const auto& pointer = value.get<PatternRawPointer>();
    if (!isKnownEnum(pointer.mutability, Mutability::Const, Mutability::Mutable)) return false;
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::RawPointer));
    encoder.encodeUint8(static_cast<uint8_t>(pointer.mutability));
    return encodePattern(encoder, pointer.pointee, registries);
  }
  if (value.is<PatternExistential>()) {
    const auto& existential = value.get<PatternExistential>().existential;
    const auto encodeExistentialInterface = [&](identity::CanonicalEncoder& target,
                                                const PatternExistentialInterface& interface) {
      return encodeDefinition(target, interface.definition) &&
             encodePatternSequence(target, interface.arguments.asPtr());
    };
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Existential));
    if (!encodeExistentialInterface(encoder, existential.principal)) return false;
    zc::Vector<zc::Array<uint8_t>> interfaces(existential.additionalInterfaces.size());
    for (const auto& interface : existential.additionalInterfaces) {
      identity::CanonicalEncoder item;
      if (!encodeExistentialInterface(item, interface)) return false;
      interfaces.add(item.finish());
    }
    for (size_t index = 1; index < interfaces.size(); ++index) {
      if (!lessBytes(interfaces[index - 1].asPtr(), interfaces[index].asPtr())) return false;
    }
    encoder.encodeSequenceSize(interfaces.size());
    for (const auto& item : interfaces) { encodeRaw(encoder, item.asPtr()); }
    zc::Vector<zc::Array<uint8_t>> markers(existential.markers.size());
    for (const auto marker : existential.markers) {
      identity::CanonicalEncoder item;
      if (!encodeDefinition(item, marker)) return false;
      markers.add(item.finish());
    }
    for (size_t index = 1; index < markers.size(); ++index) {
      if (!lessBytes(markers[index - 1].asPtr(), markers[index].asPtr())) return false;
    }
    encoder.encodeSequenceSize(markers.size());
    for (const auto& item : markers) { encodeRaw(encoder, item.asPtr()); }
    zc::Vector<zc::Array<uint8_t>> bindingKeys(existential.associatedBindings.size());
    zc::Vector<zc::Array<uint8_t>> bindings(existential.associatedBindings.size());
    for (const auto& binding : existential.associatedBindings) {
      identity::CanonicalEncoder key;
      if (!encodeDefinition(key, binding.associated)) return false;
      bindingKeys.add(key.finish());
      identity::CanonicalEncoder item;
      if (!encodeDefinition(item, binding.associated) ||
          !encodePattern(item, binding.type, registries)) {
        return false;
      }
      bindings.add(item.finish());
    }
    for (size_t index = 1; index < bindingKeys.size(); ++index) {
      if (!lessBytes(bindingKeys[index - 1].asPtr(), bindingKeys[index].asPtr())) return false;
    }
    encoder.encodeSequenceSize(bindings.size());
    for (const auto& item : bindings) { encodeRaw(encoder, item.asPtr()); }
    return true;
  }
  if (value.is<PatternInterfaceBound>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceBound));
    return encodePatternInterface(encoder, value.get<PatternInterfaceBound>().interface,
                                  registries);
  }
  if (value.is<PatternInterfaceSelf>()) {
    encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceSelf));
    return encodeDefinition(encoder, value.get<PatternInterfaceSelf>().interface);
  }
  encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Parameter));
  encoder.encodeUint32(value.get<PatternParameter>().index);
  return true;
}

zc::Maybe<TypeKeyPatternKey> SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(
    const TypeKeyPattern& pattern, const identity::SemanticIdentityRegistrySet& registries) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.type-key-pattern.v1"_zcc);
  encoder.encodeUint8(0);
  if (!encodePattern(encoder, pattern, registries)) { return zc::none; }
  return TypeKeyPatternKey(encoder.finish(), pattern.clone());
}

zc::Maybe<TypeKeyPatternKey> SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey(
    zc::ArrayPtr<const uint8_t> bytes, const identity::SemanticIdentityRegistrySet& registries) {
  identity::CanonicalDecoder decoder(bytes);
  if (!decodeAscii(decoder, "zom.type-key-pattern.v1"_zcc)) return zc::none;
  auto delimiter = decoder.decodeUint8();
  if (delimiter == zc::none) return zc::none;
  ZC_IF_SOME(value, delimiter) {
    if (value != 0x00) return zc::none;
  }
  PatternByteDecoder patternDecoder(decoder, registries);
  auto pattern = patternDecoder.decodePattern();
  if (pattern == zc::none || !decoder.finished()) return zc::none;
  ZC_IF_SOME(value, pattern) {
    auto result = makeTypeKeyPatternKey(value, registries);
    if (result == zc::none) return zc::none;
    ZC_IF_SOME(key, result) {
      if (sameBytes(key.bytes(), bytes)) return zc::mv(key);
    }
  }
  return zc::none;
}

bool SignatureFactsCanonicalCodec::typeKeyPatternKeyIsCanonical(
    const TypeKeyPatternKey& pattern, const identity::SemanticIdentityRegistrySet& registries) {
  auto computed = makeTypeKeyPatternKey(*pattern.decoded, registries);
  if (computed == zc::none) return false;
  ZC_IF_SOME(value, computed) { return sameBytes(value.bytes(), pattern.bytes()); }
  return false;
}

zc::Maybe<ImplPatternKey> SignatureFactsCanonicalCodec::makeImplPatternKey(
    const ImplPattern& pattern, const identity::SemanticIdentityRegistrySet& registries) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.impl-pattern.v1"_zcc);
  encoder.encodeUint8(0);
  if (!encodePatternInterface(encoder, pattern.interface, registries) ||
      !encodePattern(encoder, pattern.self, registries)) {
    return zc::none;
  }
  return ImplPatternKey(encoder.finish(), pattern.clone());
}

zc::Maybe<ImplPatternKey> SignatureFactsCanonicalCodec::decodeImplPatternKey(
    zc::ArrayPtr<const uint8_t> bytes, const identity::SemanticIdentityRegistrySet& registries) {
  identity::CanonicalDecoder decoder(bytes);
  if (!decodeAscii(decoder, "zom.impl-pattern.v1"_zcc)) return zc::none;
  auto delimiter = decoder.decodeUint8();
  if (delimiter == zc::none) return zc::none;
  ZC_IF_SOME(value, delimiter) {
    if (value != 0x00) return zc::none;
  }
  PatternByteDecoder patternDecoder(decoder, registries);
  auto interface = patternDecoder.decodePatternInterface();
  auto self = patternDecoder.decodePattern();
  if (interface == zc::none || self == zc::none || !decoder.finished()) return zc::none;
  ZC_IF_SOME(interfaceValue, interface) {
    ZC_IF_SOME(selfValue, self) {
      const ImplPattern pattern{zc::mv(interfaceValue), zc::mv(selfValue)};
      auto result = makeImplPatternKey(pattern, registries);
      if (result == zc::none) return zc::none;
      ZC_IF_SOME(key, result) {
        if (sameBytes(key.bytes(), bytes)) return zc::mv(key);
      }
    }
  }
  return zc::none;
}

bool SignatureFactsCanonicalCodec::implPatternKeyIsCanonical(
    const ImplPatternKey& pattern, const identity::SemanticIdentityRegistrySet& registries) {
  auto computed = makeImplPatternKey(*pattern.decoded, registries);
  if (computed == zc::none) return false;
  ZC_IF_SOME(value, computed) { return sameBytes(value.bytes(), pattern.bytes()); }
  return false;
}

bool SignatureFactsCanonicalCodec::implPatternIsPublishable(const ImplPatternKey& patternKey,
                                                            size_t genericParameterCount) noexcept {
  if (genericParameterCount > UINT32_MAX) return false;
  const auto& pattern = *patternKey.decoded;
  const auto& outer = pattern.self.impl->value;
  if (outer.is<PatternTypeParameter>() || outer.is<PatternInterfaceBound>() ||
      outer.is<PatternInterfaceSelf>()) {
    return false;
  }
  if ((outer.is<PatternTuple>() && outer.get<PatternTuple>().elements.size() > UINT32_MAX) ||
      (outer.is<PatternFunction>() &&
       outer.get<PatternFunction>().function.parameters.size() > UINT32_MAX) ||
      (outer.is<PatternUnion>() && outer.get<PatternUnion>().alternatives.size() > UINT32_MAX) ||
      (outer.is<PatternIntersection>() &&
       outer.get<PatternIntersection>().conjuncts.size() > UINT32_MAX)) {
    return false;
  }

  zc::Vector<uint8_t> occurrences(genericParameterCount);
  for (size_t index = 0; index < genericParameterCount; ++index) { occurrences.add(0); }
  const auto check = [&](auto&& self, const TypeKeyPattern& item, bool parameterForbidden) -> bool {
    const auto& value = item.impl->value;
    const auto checkSequence = [&](zc::ArrayPtr<const TypeKeyPattern> values,
                                   bool forbidden = false) {
      for (const auto& child : values) {
        if (!self(self, child, parameterForbidden || forbidden)) return false;
      }
      return true;
    };
    const auto checkInterface = [&](const PatternInterfaceInstantiation& interface) {
      return checkSequence(interface.arguments.asPtr(), false);
    };
    if (value.is<PatternParameter>()) {
      const auto index = value.get<PatternParameter>().index;
      if (parameterForbidden || index >= genericParameterCount) return false;
      occurrences[index] = 1;
      return true;
    }
    if (value.is<PatternTypeParameter>() || value.is<PatternInterfaceSelf>()) return false;
    if (value.is<PatternTuple>()) {
      return checkSequence(value.get<PatternTuple>().elements.asPtr(), false);
    }
    if (value.is<PatternObject>()) {
      for (const auto& field : value.get<PatternObject>().fields) {
        if (!self(self, field.type, parameterForbidden)) return false;
      }
      return true;
    }
    if (value.is<PatternDynamicArray>()) {
      return self(self, value.get<PatternDynamicArray>().element, parameterForbidden);
    }
    if (value.is<PatternSlice>()) {
      return self(self, value.get<PatternSlice>().element, parameterForbidden);
    }
    if (value.is<PatternFixedArray>()) {
      return self(self, value.get<PatternFixedArray>().element, parameterForbidden);
    }
    if (value.is<PatternFunction>()) {
      const auto& function = value.get<PatternFunction>().function;
      if (!checkSequence(function.parameters.asPtr(), false) ||
          !self(self, function.success, parameterForbidden)) {
        return false;
      }
      ZC_IF_SOME(raises, function.raises) { return self(self, raises, parameterForbidden); }
      return true;
    }
    if (value.is<PatternNominal>()) {
      return checkSequence(value.get<PatternNominal>().arguments.asPtr(), false);
    }
    if (value.is<PatternUnion>()) {
      return checkSequence(value.get<PatternUnion>().alternatives.asPtr(), true);
    }
    if (value.is<PatternIntersection>()) {
      return checkSequence(value.get<PatternIntersection>().conjuncts.asPtr(), true);
    }
    if (value.is<PatternReference>()) {
      return self(self, value.get<PatternReference>().referent, parameterForbidden);
    }
    if (value.is<PatternRawPointer>()) {
      return self(self, value.get<PatternRawPointer>().pointee, parameterForbidden);
    }
    if (value.is<PatternExistential>()) {
      const auto& existential = value.get<PatternExistential>().existential;
      bool principalSharesDefinition = false;
      for (const auto& interface : existential.additionalInterfaces) {
        if (interface.definition == existential.principal.definition) {
          principalSharesDefinition = true;
          break;
        }
      }
      if (!checkSequence(existential.principal.arguments.asPtr(), principalSharesDefinition)) {
        return false;
      }
      for (const auto& interface : existential.additionalInterfaces) {
        if (!checkSequence(interface.arguments.asPtr(), true)) return false;
      }
      for (const auto& binding : existential.associatedBindings) {
        if (!self(self, binding.type, parameterForbidden)) return false;
      }
      return true;
    }
    if (value.is<PatternInterfaceBound>()) {
      return checkInterface(value.get<PatternInterfaceBound>().interface);
    }
    return true;
  };
  if (!check(check, pattern.self, false)) return false;
  for (const auto& argument : pattern.interface.arguments) {
    if (!check(check, argument, false)) return false;
  }
  for (const auto occurrence : occurrences) {
    if (occurrence == 0) return false;
  }
  return true;
}

zc::Maybe<bool> SignatureFactsCanonicalCodec::implPatternsOverlap(
    const ImplPatternKey& leftKey, const ImplPatternKey& rightKey,
    const identity::SemanticIdentityRegistrySet& registries) {
  struct TermNode final {
    bool variable;
    uint64_t variableKey;
    zc::Array<uint8_t> constructor;
    zc::Vector<uint32_t> children;
  };
  struct Equation final {
    uint32_t left;
    uint32_t right;
  };
  struct Binding final {
    uint64_t variable;
    uint32_t term;
  };

  if (!implPatternKeyIsCanonical(leftKey, registries) ||
      !implPatternKeyIsCanonical(rightKey, registries)) {
    return zc::none;
  }
  const auto& left = *leftKey.decoded;
  const auto& right = *rightKey.decoded;

  zc::Vector<TermNode> nodes;
  const auto encodeDefinition = [&](identity::CanonicalEncoder& encoder,
                                    identity::DefId definition) {
    if (registries.definitions().validate(definition) != identity::FrozenRegistryFailure::None) {
      return false;
    }
    ZC_IF_SOME(key, registries.definitions().lookup(definition)) {
      key.encode(encoder);
      return true;
    }
    return false;
  };
  const auto encodeGenericParameter = [&](identity::CanonicalEncoder& encoder,
                                          const identity::GenericParameterKey& parameter) {
    if (registries.genericParameters().find(parameter) == zc::none) return false;
    parameter.encode(encoder);
    return true;
  };
  const auto publishNode = [&](identity::CanonicalEncoder& constructor,
                               zc::Vector<uint32_t>&& children) {
    const auto index = static_cast<uint32_t>(nodes.size());
    nodes.add(TermNode{false, 0, constructor.finish(), zc::mv(children)});
    return index;
  };
  const auto publishVariable = [&](uint32_t side, uint32_t parameter) {
    const auto index = static_cast<uint32_t>(nodes.size());
    nodes.add(TermNode{true, (static_cast<uint64_t>(side) << 32U) | parameter, zc::Array<uint8_t>(),
                       zc::Vector<uint32_t>()});
    return index;
  };

  const auto flattenPattern = [&](auto&& self, const TypeKeyPattern& pattern,
                                  uint32_t side) -> zc::Maybe<uint32_t> {
    const auto& value = pattern.impl->value;
    identity::CanonicalEncoder constructor;
    zc::Vector<uint32_t> children;
    const auto appendPatterns = [&](zc::ArrayPtr<const TypeKeyPattern> patterns) {
      constructor.encodeSequenceSize(patterns.size());
      for (const auto& child : patterns) {
        auto result = self(self, child, side);
        if (result == zc::none) return false;
        ZC_IF_SOME(index, result) { children.add(index); }
      }
      return true;
    };
    const auto appendInterface = [&](const PatternInterfaceInstantiation& interface) {
      return encodeDefinition(constructor, interface.interface) &&
             appendPatterns(interface.arguments.asPtr());
    };
    if (value.is<PatternParameter>()) {
      return publishVariable(side, value.get<PatternParameter>().index);
    }
    if (value.is<PatternPrimitive>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Primitive));
      constructor.encodeUint8(static_cast<uint8_t>(value.get<PatternPrimitive>().kind));
    } else if (value.is<PatternTuple>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Tuple));
      if (!appendPatterns(value.get<PatternTuple>().elements.asPtr())) return zc::none;
    } else if (value.is<PatternObject>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Object));
      const auto& fields = value.get<PatternObject>().fields;
      constructor.encodeSequenceSize(fields.size());
      for (const auto& field : fields) {
        field.name.encode(constructor);
        constructor.encodeUint8(static_cast<uint8_t>(field.mutability));
        constructor.encodeUint8(static_cast<uint8_t>(field.presence));
        auto child = self(self, field.type, side);
        if (child == zc::none) return zc::none;
        ZC_IF_SOME(index, child) { children.add(index); }
      }
    } else if (value.is<PatternDynamicArray>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::DynamicArray));
      auto child = self(self, value.get<PatternDynamicArray>().element, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    } else if (value.is<PatternSlice>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Slice));
      auto child = self(self, value.get<PatternSlice>().element, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    } else if (value.is<PatternFixedArray>()) {
      const auto& fixed = value.get<PatternFixedArray>();
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::FixedArray));
      constructor.encodeUint64(fixed.length);
      auto child = self(self, fixed.element, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    } else if (value.is<PatternFunction>()) {
      const auto& function = value.get<PatternFunction>().function;
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Function));
      if (!appendPatterns(function.parameters.asPtr())) return zc::none;
      auto success = self(self, function.success, side);
      if (success == zc::none) return zc::none;
      ZC_IF_SOME(index, success) { children.add(index); }
      if (function.raises == zc::none) {
        constructor.encodeNone();
      } else {
        constructor.encodeSome();
        ZC_IF_SOME(raises, function.raises) {
          auto child = self(self, raises, side);
          if (child == zc::none) return zc::none;
          ZC_IF_SOME(index, child) { children.add(index); }
        }
      }
    } else if (value.is<PatternNominal>()) {
      const auto& nominal = value.get<PatternNominal>();
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Nominal));
      if (!encodeDefinition(constructor, nominal.definition) ||
          !appendPatterns(nominal.arguments.asPtr())) {
        return zc::none;
      }
    } else if (value.is<PatternTypeParameter>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::TypeParameter));
      if (!encodeGenericParameter(constructor, value.get<PatternTypeParameter>().parameter)) {
        return zc::none;
      }
    } else if (value.is<PatternUnion>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Union));
      if (!appendPatterns(value.get<PatternUnion>().alternatives.asPtr())) return zc::none;
    } else if (value.is<PatternIntersection>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Intersection));
      if (!appendPatterns(value.get<PatternIntersection>().conjuncts.asPtr())) return zc::none;
    } else if (value.is<PatternReference>()) {
      const auto& reference = value.get<PatternReference>();
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Reference));
      constructor.encodeUint8(static_cast<uint8_t>(reference.mutability));
      auto child = self(self, reference.referent, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    } else if (value.is<PatternRawPointer>()) {
      const auto& pointer = value.get<PatternRawPointer>();
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::RawPointer));
      constructor.encodeUint8(static_cast<uint8_t>(pointer.mutability));
      auto child = self(self, pointer.pointee, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    } else if (value.is<PatternExistential>()) {
      const auto& existential = value.get<PatternExistential>().existential;
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::Existential));
      if (!encodeDefinition(constructor, existential.principal.definition) ||
          !appendPatterns(existential.principal.arguments.asPtr())) {
        return zc::none;
      }
      constructor.encodeSequenceSize(existential.additionalInterfaces.size());
      for (const auto& interface : existential.additionalInterfaces) {
        if (!encodeDefinition(constructor, interface.definition) ||
            !appendPatterns(interface.arguments.asPtr())) {
          return zc::none;
        }
      }
      constructor.encodeSequenceSize(existential.markers.size());
      for (const auto marker : existential.markers) {
        if (!encodeDefinition(constructor, marker)) return zc::none;
      }
      constructor.encodeSequenceSize(existential.associatedBindings.size());
      for (const auto& binding : existential.associatedBindings) {
        if (!encodeDefinition(constructor, binding.associated)) return zc::none;
        auto child = self(self, binding.type, side);
        if (child == zc::none) return zc::none;
        ZC_IF_SOME(index, child) { children.add(index); }
      }
    } else if (value.is<PatternInterfaceBound>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceBound));
      if (!appendInterface(value.get<PatternInterfaceBound>().interface)) return zc::none;
    } else if (value.is<PatternInterfaceSelf>()) {
      constructor.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceSelf));
      if (!encodeDefinition(constructor, value.get<PatternInterfaceSelf>().interface)) {
        return zc::none;
      }
    } else {
      return zc::none;
    }
    return publishNode(constructor, zc::mv(children));
  };

  const auto flattenImpl = [&](const ImplPattern& pattern, uint32_t side) -> zc::Maybe<uint32_t> {
    identity::CanonicalEncoder constructor;
    constructor.encodeUint8(0x7f);
    if (!encodeDefinition(constructor, pattern.interface.interface)) return zc::none;
    constructor.encodeSequenceSize(pattern.interface.arguments.size());
    zc::Vector<uint32_t> children(pattern.interface.arguments.size() + 1);
    for (const auto& argument : pattern.interface.arguments) {
      auto child = flattenPattern(flattenPattern, argument, side);
      if (child == zc::none) return zc::none;
      ZC_IF_SOME(index, child) { children.add(index); }
    }
    auto self = flattenPattern(flattenPattern, pattern.self, side);
    if (self == zc::none) return zc::none;
    ZC_IF_SOME(index, self) { children.add(index); }
    return publishNode(constructor, zc::mv(children));
  };

  auto leftRoot = flattenImpl(left, 0);
  auto rightRoot = flattenImpl(right, 1);
  if (leftRoot == zc::none || rightRoot == zc::none) return zc::none;

  zc::Vector<Binding> bindings;
  const auto resolve = [&](uint32_t term) {
    uint32_t current = term;
    for (size_t depth = 0; depth <= nodes.size(); ++depth) {
      const auto& node = nodes[current];
      if (!node.variable) return current;
      zc::Maybe<uint32_t> replacement;
      for (const auto& binding : bindings) {
        if (binding.variable == node.variableKey) {
          replacement = binding.term;
          break;
        }
      }
      if (replacement == zc::none) return current;
      ZC_IF_SOME(value, replacement) { current = value; }
    }
    return current;
  };
  const auto occurs = [&](auto&& self, uint64_t variable, uint32_t term) -> bool {
    const uint32_t current = resolve(term);
    const auto& node = nodes[current];
    if (node.variable) return node.variableKey == variable;
    for (const auto child : node.children) {
      if (self(self, variable, child)) return true;
    }
    return false;
  };

  zc::Vector<Equation> pending;
  ZC_IF_SOME(leftValue, leftRoot) {
    ZC_IF_SOME(rightValue, rightRoot) { pending.add(Equation{leftValue, rightValue}); }
  }
  while (!pending.empty()) {
    const auto equation = pending.back();
    pending.removeLast();
    const uint32_t leftTerm = resolve(equation.left);
    const uint32_t rightTerm = resolve(equation.right);
    if (leftTerm == rightTerm) continue;
    const auto& leftNode = nodes[leftTerm];
    const auto& rightNode = nodes[rightTerm];
    if (leftNode.variable) {
      if (occurs(occurs, leftNode.variableKey, rightTerm)) return false;
      bindings.add(Binding{leftNode.variableKey, rightTerm});
      continue;
    }
    if (rightNode.variable) {
      if (occurs(occurs, rightNode.variableKey, leftTerm)) return false;
      bindings.add(Binding{rightNode.variableKey, leftTerm});
      continue;
    }
    if (!sameBytes(leftNode.constructor.asPtr(), rightNode.constructor.asPtr()) ||
        leftNode.children.size() != rightNode.children.size()) {
      return false;
    }
    for (size_t index = 0; index < leftNode.children.size(); ++index) {
      pending.add(Equation{leftNode.children[index], rightNode.children[index]});
    }
  }
  return true;
}

identity::DefId SignatureFactsCanonicalCodec::implPatternInterface(
    const ImplPatternKey& pattern) noexcept {
  return pattern.decoded->interface.interface;
}

zc::Maybe<CanonicalTypeHead> SignatureFactsCanonicalCodec::implPatternHead(
    const ImplPatternKey& pattern) noexcept {
  const auto& value = pattern.decoded->self.impl->value;
  if (value.is<PatternParameter>()) return CanonicalTypeHead(BlanketTypeHead{});
  if (value.is<PatternPrimitive>()) {
    return CanonicalTypeHead(PrimitiveTypeHead{value.get<PatternPrimitive>().kind});
  }
  if (value.is<PatternTuple>()) {
    const auto size = value.get<PatternTuple>().elements.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(TupleTypeHead{static_cast<uint32_t>(size)});
  }
  if (value.is<PatternObject>()) return CanonicalTypeHead(ObjectTypeHead{});
  if (value.is<PatternDynamicArray>()) return CanonicalTypeHead(DynamicArrayTypeHead{});
  if (value.is<PatternSlice>()) return CanonicalTypeHead(SliceTypeHead{});
  if (value.is<PatternFixedArray>()) return CanonicalTypeHead(FixedArrayTypeHead{});
  if (value.is<PatternFunction>()) {
    const auto& function = value.get<PatternFunction>().function;
    if (function.parameters.size() > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(FunctionTypeHead{static_cast<uint32_t>(function.parameters.size()),
                                              function.raises != zc::none});
  }
  if (value.is<PatternNominal>()) {
    return CanonicalTypeHead(NominalTypeHead{value.get<PatternNominal>().definition});
  }
  if (value.is<PatternUnion>()) {
    const auto size = value.get<PatternUnion>().alternatives.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(UnionTypeHead{static_cast<uint32_t>(size)});
  }
  if (value.is<PatternIntersection>()) {
    const auto size = value.get<PatternIntersection>().conjuncts.size();
    if (size > UINT32_MAX) return zc::none;
    return CanonicalTypeHead(IntersectionTypeHead{static_cast<uint32_t>(size)});
  }
  if (value.is<PatternReference>()) {
    return CanonicalTypeHead(ReferenceTypeHead{value.get<PatternReference>().mutability});
  }
  if (value.is<PatternRawPointer>()) {
    return CanonicalTypeHead(RawPointerTypeHead{value.get<PatternRawPointer>().mutability});
  }
  if (value.is<PatternExistential>()) {
    return CanonicalTypeHead(
        ExistentialTypeHead{value.get<PatternExistential>().existential.principal.definition});
  }
  return zc::none;
}

zc::Maybe<CanonicalTypeHead> SignatureFactsCanonicalCodec::canonicalTypeHead(
    identity::SemanticTypeId type, const type::SemanticTypeStore& semanticTypes) {
  return typeHead(type, semanticTypes);
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeSignature(
    const SemanticSignature& signature, identity::ModuleId owningModule,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(moduleKey, registries.modules().lookup(owningModule)) {
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey,
                                           signature.declarationSpan.source());
    auto result = encoder.encode(signature, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeImplHead(
    const ImplHead& head, const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(record, registries.impls().lookupRecord(head.impl)) {
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, record.module(),
                                           head.declarationSpan.source());
    auto result = encoder.encode(head, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeMarkerFact(
    const MarkerFact& fact, const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  zc::Maybe<const identity::ModuleKey&> module;
  const auto& evidence = fact.evidence.variant();
  if (evidence.is<ExplicitMarkerEvidence>()) {
    ZC_IF_SOME(record,
               registries.impls().lookupRecord(evidence.get<ExplicitMarkerEvidence>().impl)) {
      module = record.module();
    }
  }
  if (module == zc::none) {
    ZC_IF_SOME(record, registries.definitions().lookupRecord(fact.key.marker)) {
      module = record.module();
    }
  }
  ZC_IF_SOME(moduleKey, module) {
    ZC_IF_SOME(span, fact.declarationSpan) {
      SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey, span.source());
      auto result = encoder.encode(fact, 0);
      if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
    }
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey);
    auto result = encoder.encode(fact, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeConstraint(
    const CanonicalConstraint& constraint, const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(moduleKey, registries.modules().keyAt(0)) {
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey);
    auto result = encoder.encodeCanonicalConstraint(constraint, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeInterfaceInstantiation(
    const InterfaceInstantiation& interface,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(moduleKey, registries.modules().keyAt(0)) {
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey);
    auto result = encoder.encodeInterfaceInstantiation(interface, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

zc::Maybe<zc::Array<uint8_t>> SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
    const CanonicalConstValue& value, identity::ModuleId owningModule,
    const identity::SemanticIdentityRegistrySet& registries,
    const type::SemanticTypeStore& semanticTypes) {
  ZC_IF_SOME(moduleKey, registries.modules().lookup(owningModule)) {
    SignatureFactsCanonicalEncoder encoder(registries, semanticTypes, moduleKey);
    auto result = encoder.encodeCanonicalConstValue(value, 0);
    if (result.is<EncodedSignature>()) { return zc::mv(result.get<EncodedSignature>().bytes); }
  }
  return zc::none;
}

namespace {

bool scopeGraphIsValid(zc::ArrayPtr<const SemanticSignature> signatures) {
  const auto find = [&](identity::DefId id) -> zc::Maybe<const SemanticSignature&> {
    for (const auto& signature : signatures) {
      if (signature.definition == id) return signature;
    }
    return zc::none;
  };

  for (const auto& signature : signatures) {
    identity::DefId current = signature.definition;
    for (size_t depth = 0; depth <= signatures.size(); ++depth) {
      auto found = find(current);
      if (found == zc::none) return false;
      bool complete = false;
      ZC_IF_SOME(value, found) {
        const auto& scope = value.scope.variant();
        if (scope.is<ModuleDefinitionSignatureScope>()) {
          complete = true;
        } else if (scope.is<MemberSignatureScope>()) {
          const auto owner = scope.get<MemberSignatureScope>().owner;
          auto ownerSignature = find(owner);
          if (ownerSignature == zc::none) return false;
          ZC_IF_SOME(ownerValue, ownerSignature) {
            const auto& ownerPayload = ownerValue.payload.variant();
            if (!ownerPayload.is<NominalSignature>() && !ownerPayload.is<InterfaceSignature>()) {
              return false;
            }
          }
          current = owner;
        } else {
          current = scope.get<EnclosedSignatureScope>().owner;
        }
      }
      if (complete) break;
      if (depth == signatures.size()) return false;
    }
  }
  return true;
}

bool attributesAgreeWithPayload(const SemanticSignature& signature) {
  if (signature.attributes.empty()) return true;
  const auto& payload = signature.payload.variant();
  if (!payload.is<CallableSignature>()) return false;
  const auto& callable = payload.get<CallableSignature>();
  for (const auto& attribute : signature.attributes) {
    if (attribute.attribute != NormalizedAttribute::MoveReceiver) return false;
    if (callable.receiver == zc::none) return false;
    ZC_IF_SOME(receiver, callable.receiver) {
      if (receiver.mode != ReceiverMode::Move) return false;
    }
  }
  return true;
}

bool containsRequirement(zc::ArrayPtr<const SignatureDefinitionRequirement> requirements,
                         identity::DefId definition, identity::DefinitionKind kind) {
  for (const auto& requirement : requirements) {
    if (requirement.definition == definition && requirement.definitionKind == kind) return true;
  }
  return false;
}

bool containsSignature(zc::ArrayPtr<const SemanticSignature> signatures,
                       const SignatureDefinitionRequirement& requirement) {
  for (const auto& signature : signatures) {
    if (signature.definition == requirement.definition &&
        signature.definitionKind == requirement.definitionKind) {
      return true;
    }
  }
  return false;
}

bool sameMarkerKey(const MarkerFactKey& left, const MarkerFactKey& right) {
  return left.marker == right.marker && left.subject == right.subject;
}

bool containsImplRequirement(zc::ArrayPtr<const ImplHeadRequirement> requirements,
                             identity::ImplId implementation) {
  for (const auto& requirement : requirements) {
    if (requirement.implementation == implementation) return true;
  }
  return false;
}

bool containsImplHead(zc::ArrayPtr<const ImplHead> heads, const ImplHeadRequirement& requirement) {
  for (const auto& head : heads) {
    if (head.impl == requirement.implementation) return true;
  }
  return false;
}

bool containsMarkerRequirement(zc::ArrayPtr<const MarkerFactRequirement> requirements,
                               const MarkerFactKey& key) {
  for (const auto& requirement : requirements) {
    if (sameMarkerKey(requirement.key, key)) return true;
  }
  return false;
}

bool containsMarkerFact(zc::ArrayPtr<const MarkerFact> facts,
                        const MarkerFactRequirement& requirement) {
  for (const auto& fact : facts) {
    if (sameMarkerKey(fact.key, requirement.key)) return true;
  }
  return false;
}

}  // namespace

MarkerShapeInventoryRevision::MarkerShapeInventoryRevision(
    const identity::Sha256Digest& input) noexcept
    : value(input) {}

const identity::Sha256Digest& MarkerShapeInventoryRevision::digest() const noexcept {
  return value;
}
zc::Maybe<MarkerShapeInventoryRevision> MarkerShapeInventoryRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> shapeRecords) {
  auto digest = markerShapeRevisionDigest(contextFingerprint, shapeRecords);
  ZC_IF_SOME(value, digest) { return MarkerShapeInventoryRevision(value); }
  return zc::none;
}

struct VerifiedMarkerShapeInventory::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint,
       MarkerShapeInventoryRevision&& revision, zc::Vector<InterfaceMarkerShapeFact>&& shapes)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        revision(zc::mv(revision)),
        shapes(zc::mv(shapes)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  MarkerShapeInventoryRevision revision;
  zc::Vector<InterfaceMarkerShapeFact> shapes;
};

VerifiedMarkerShapeInventory::VerifiedMarkerShapeInventory(zc::Own<Impl>&& input) noexcept
    : impl(zc::mv(input)) {}
VerifiedMarkerShapeInventory::~VerifiedMarkerShapeInventory() noexcept(false) = default;
VerifiedMarkerShapeInventory::VerifiedMarkerShapeInventory(
    VerifiedMarkerShapeInventory&&) noexcept = default;
VerifiedMarkerShapeInventory& VerifiedMarkerShapeInventory::operator=(
    VerifiedMarkerShapeInventory&&) noexcept = default;

identity::SemanticContextBrand VerifiedMarkerShapeInventory::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedMarkerShapeInventory::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
const MarkerShapeInventoryRevision& VerifiedMarkerShapeInventory::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const InterfaceMarkerShapeFact> VerifiedMarkerShapeInventory::shapes() const noexcept {
  return impl->shapes.asPtr();
}
zc::Maybe<InterfaceMarkerShape> VerifiedMarkerShapeInventory::shape(
    identity::DefId interface) const noexcept {
  for (const auto& fact : impl->shapes) {
    if (fact.interface == interface) { return fact.shape; }
  }
  return zc::none;
}

MarkerPolicyReferenceConfiguration MarkerPolicyReferenceConfiguration::clone() const {
  return MarkerPolicyReferenceConfiguration{mutability, requiredMarker.clone()};
}

MarkerPolicyConfigurationEntry MarkerPolicyConfigurationEntry::clone() const {
  zc::Vector<MarkerPolicyReferenceConfiguration> references(referenceRequirements.size());
  for (const auto& reference : referenceRequirements) { references.add(reference.clone()); }
  return MarkerPolicyConfigurationEntry{
      marker.clone(), clonePlainVector(structuralSubjects.asPtr()),
      clonePlainVector(builtinPrimitives.asPtr()), zc::mv(references)};
}

MarkerPolicy MarkerPolicy::clone() const {
  return MarkerPolicy{clonePlainVector(structuralSubjects.asPtr()),
                      clonePlainVector(builtinPrimitives.asPtr()),
                      clonePlainVector(referenceRequirements.asPtr())};
}

namespace {

zc::Maybe<zc::Array<uint8_t>> encodePolicyConfigurationEntry(
    const MarkerPolicyConfigurationEntry& entry) {
  identity::CanonicalEncoder encoder;
  entry.marker.encode(encoder);
  encoder.encodeSequenceSize(entry.structuralSubjects.size());
  uint8_t previousSubject = 0;
  for (const auto subject : entry.structuralSubjects) {
    const auto tag = static_cast<uint8_t>(subject);
    if (tag <= previousSubject || !isKnownEnum(subject, MarkerStructuralSubject::Tuple,
                                               MarkerStructuralSubject::NominalEnum)) {
      return zc::none;
    }
    previousSubject = tag;
    encoder.encodeUint8(tag);
  }
  encoder.encodeSequenceSize(entry.builtinPrimitives.size());
  uint8_t previousPrimitive = 0;
  for (const auto primitive : entry.builtinPrimitives) {
    const auto tag = static_cast<uint8_t>(primitive);
    if (tag <= previousPrimitive ||
        !isKnownEnum(primitive, PrimitiveKind::I8, PrimitiveKind::Null)) {
      return zc::none;
    }
    previousPrimitive = tag;
    encoder.encodeUint8(tag);
  }
  zc::Vector<zc::Array<uint8_t>> references(entry.referenceRequirements.size());
  for (const auto& reference : entry.referenceRequirements) {
    if (!isKnownEnum(reference.mutability, Mutability::Const, Mutability::Mutable)) {
      return zc::none;
    }
    identity::CanonicalEncoder item;
    item.encodeUint8(static_cast<uint8_t>(reference.mutability));
    reference.requiredMarker.encode(item);
    references.add(item.finish());
  }
  for (size_t index = 1; index < references.size(); ++index) {
    if (!lessBytes(references[index - 1].asPtr(), references[index].asPtr())) return zc::none;
  }
  encoder.encodeSequenceSize(references.size());
  for (const auto& reference : references) { encodeRaw(encoder, reference.asPtr()); }
  return encoder.finish();
}

}  // namespace

struct MarkerPolicyConfiguration::Impl final {
  Impl(const identity::Sha256Digest& revision, zc::Vector<MarkerPolicyConfigurationEntry>&& entries)
      : revision(revision), entries(zc::mv(entries)) {}

  identity::Sha256Digest revision;
  zc::Vector<MarkerPolicyConfigurationEntry> entries;
};

MarkerPolicyConfiguration::MarkerPolicyConfiguration(zc::Own<Impl>&& input) noexcept
    : impl(zc::mv(input)) {}
MarkerPolicyConfiguration::~MarkerPolicyConfiguration() noexcept(false) = default;
MarkerPolicyConfiguration::MarkerPolicyConfiguration(MarkerPolicyConfiguration&&) noexcept =
    default;
MarkerPolicyConfiguration& MarkerPolicyConfiguration::operator=(
    MarkerPolicyConfiguration&&) noexcept = default;

zc::Maybe<MarkerPolicyConfiguration> MarkerPolicyConfiguration::from(
    zc::Vector<MarkerPolicyConfigurationEntry>&& entries) {
  struct EncodedEntry final {
    MarkerPolicyConfigurationEntry entry;
    zc::Array<uint8_t> record;
  };
  zc::Vector<EncodedEntry> encoded(entries.size());
  for (auto& entry : entries) {
    auto record = encodePolicyConfigurationEntry(entry);
    if (record == zc::none) return zc::none;
    ZC_IF_SOME(value, record) { encoded.add(EncodedEntry{zc::mv(entry), zc::mv(value)}); }
  }
  for (size_t index = 1; index < encoded.size(); ++index) {
    auto current = zc::mv(encoded[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.record.asPtr(), encoded[insertion - 1].record.asPtr())) {
      encoded[insertion] = zc::mv(encoded[insertion - 1]);
      --insertion;
    }
    encoded[insertion] = zc::mv(current);
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> records(encoded.size());
  zc::Vector<MarkerPolicyConfigurationEntry> sorted(encoded.size());
  for (auto& entry : encoded) {
    records.add(entry.record.asPtr());
    sorted.add(zc::mv(entry.entry));
  }
  auto revision = markerPolicyConfigurationDigest(records.asPtr());
  if (revision == zc::none) return zc::none;
  ZC_IF_SOME(value, revision) {
    return MarkerPolicyConfiguration(
        zc::heap<MarkerPolicyConfiguration::Impl>(value, zc::mv(sorted)));
  }
  return zc::none;
}

MarkerPolicyConfiguration MarkerPolicyConfiguration::explicitOnly() {
  auto result = from(zc::Vector<MarkerPolicyConfigurationEntry>());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const identity::Sha256Digest& MarkerPolicyConfiguration::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const MarkerPolicyConfigurationEntry> MarkerPolicyConfiguration::entries()
    const noexcept {
  return impl->entries.asPtr();
}

MarkerPolicyRegistryRevision::MarkerPolicyRegistryRevision(
    const identity::Sha256Digest& input) noexcept
    : value(input) {}

const identity::Sha256Digest& MarkerPolicyRegistryRevision::digest() const noexcept {
  return value;
}
zc::Maybe<MarkerPolicyRegistryRevision> MarkerPolicyRegistryRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    const identity::Sha256Digest& configurationRevision,
    const MarkerShapeInventoryRevision& shapeInventoryRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> entryRecords) {
  auto digest = markerPolicyRegistryDigest(contextFingerprint, configurationRevision,
                                           shapeInventoryRevision, entryRecords);
  ZC_IF_SOME(value, digest) { return MarkerPolicyRegistryRevision(value); }
  return zc::none;
}

struct VerifiedMarkerPolicyRegistry::Impl final {
  Impl(identity::SemanticContextBrand semanticContext,
       identity::SemanticContextFingerprint&& contextFingerprint,
       const identity::Sha256Digest& configurationRevision,
       const MarkerShapeInventoryRevision& shapeInventoryRevision,
       MarkerPolicyRegistryRevision&& revision, zc::Vector<MarkerPolicyEntry>&& entries)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        configurationRevision(configurationRevision),
        shapeInventoryRevision(shapeInventoryRevision),
        revision(zc::mv(revision)),
        entries(zc::mv(entries)) {}

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::Sha256Digest configurationRevision;
  MarkerShapeInventoryRevision shapeInventoryRevision;
  MarkerPolicyRegistryRevision revision;
  zc::Vector<MarkerPolicyEntry> entries;
};

VerifiedMarkerPolicyRegistry::VerifiedMarkerPolicyRegistry(zc::Own<Impl>&& input) noexcept
    : impl(zc::mv(input)) {}
VerifiedMarkerPolicyRegistry::~VerifiedMarkerPolicyRegistry() noexcept(false) = default;
VerifiedMarkerPolicyRegistry::VerifiedMarkerPolicyRegistry(
    VerifiedMarkerPolicyRegistry&&) noexcept = default;
VerifiedMarkerPolicyRegistry& VerifiedMarkerPolicyRegistry::operator=(
    VerifiedMarkerPolicyRegistry&&) noexcept = default;

identity::SemanticContextBrand VerifiedMarkerPolicyRegistry::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedMarkerPolicyRegistry::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
const identity::Sha256Digest& VerifiedMarkerPolicyRegistry::configurationRevision() const noexcept {
  return impl->configurationRevision;
}
const MarkerShapeInventoryRevision& VerifiedMarkerPolicyRegistry::shapeInventoryRevision()
    const noexcept {
  return impl->shapeInventoryRevision;
}
const MarkerPolicyRegistryRevision& VerifiedMarkerPolicyRegistry::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const MarkerPolicyEntry> VerifiedMarkerPolicyRegistry::entries() const noexcept {
  return impl->entries.asPtr();
}
zc::Maybe<const MarkerPolicy&> VerifiedMarkerPolicyRegistry::policy(
    identity::DefId marker) const noexcept {
  for (const auto& entry : impl->entries) {
    if (entry.marker == marker) return entry.policy;
  }
  return zc::none;
}

MarkerShapeInventoryBuildResult MarkerShapeInventoryBuilder::build(
    identity::SemanticContextBrand semanticContext,
    const identity::SemanticContextFingerprint& contextFingerprint,
    zc::ArrayPtr<const MarkerShapeModuleInput> modules,
    const identity::SemanticIdentityRegistrySet& registries) {
  if (!semanticContext.isValid()) { return buildReject(invalidContextInvariant(0)); }
  auto fallbackModule = firstRegisteredModule(registries);
  if (fallbackModule == zc::none) {
    return buildReject(registryInvariant(identity::FrozenRegistryFailure::InvalidHandle,
                                         identity::IdentityAllocationPhase::Module, 0));
  }
  identity::ModuleId diagnosticModule;
  ZC_IF_SOME(value, fallbackModule) { diagnosticModule = value; }
  if (registries.context() != semanticContext || modules.size() != registries.modules().size()) {
    return buildReject(
        checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, diagnosticModule, 0));
  }

  for (size_t slot = 0; slot < registries.modules().size(); ++slot) {
    auto key = registries.modules().keyAt(slot);
    if (key == zc::none) {
      return buildReject(
          checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, diagnosticModule, 0));
    }
    identity::ModuleId expected;
    ZC_IF_SOME(value, key) {
      ZC_IF_SOME(handle, registries.modules().find(value)) { expected = handle; }
    }
    size_t occurrences = 0;
    for (const auto& module : modules) {
      if (module.boundModule.module() == expected) { ++occurrences; }
    }
    if (occurrences != 1) {
      return buildReject(checkerInvariant(occurrences == 0
                                              ? CheckerInvariantKind::MissingRequiredFact
                                              : CheckerInvariantKind::AdditionalFact,
                                          diagnosticModule, static_cast<uint32_t>(slot)));
    }
  }

  struct EncodedShape final {
    InterfaceMarkerShapeFact fact;
    zc::Array<uint8_t> bytes;
  };
  zc::Vector<DirectInterfaceShape> directShapes;
  for (const auto& module : modules) {
    diagnosticModule = module.boundModule.module();
    const auto moduleFailure = registries.modules().validate(diagnosticModule);
    if (moduleFailure != identity::FrozenRegistryFailure::None) {
      return buildReject(
          registryInvariant(moduleFailure, identity::IdentityAllocationPhase::Module, 0));
    }
    if (module.boundModule.semanticContext() != semanticContext ||
        module.boundModule.semanticFingerprint().digest() != contextFingerprint.digest() ||
        module.boundModule.bindings().semanticContext() != semanticContext ||
        module.boundModule.bindings().module() != diagnosticModule ||
        module.boundModule.bindingSurface().sourceModule() != diagnosticModule ||
        !bindingInventoryMatches(module.boundModule, module.boundModule.bindings(), registries)) {
      return buildReject(
          checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, diagnosticModule, 0));
    }
    for (const auto& definition : module.boundModule.definitions().definitions()) {
      if (definition.record.kind() != identity::DefinitionKind::Interface) { continue; }
      auto shape = inspectDirectInterface(module.boundModule.tree(), module.boundModule.bindings(),
                                          registries, definition, diagnosticModule);
      if (shape == zc::none) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact,
                                            diagnosticModule, definition.node.value));
      }
      ZC_IF_SOME(value, shape) { directShapes.add(zc::mv(value)); }
    }
  }

  size_t expectedInterfaces = 0;
  for (size_t slot = 0; slot < registries.definitions().size(); ++slot) {
    ZC_IF_SOME(record, registries.definitions().recordAt(slot)) {
      if (record.kind() == identity::DefinitionKind::Interface) { ++expectedInterfaces; }
    }
  }
  if (directShapes.size() != expectedInterfaces) {
    return buildReject(checkerInvariant(directShapes.size() < expectedInterfaces
                                            ? CheckerInvariantKind::MissingRequiredFact
                                            : CheckerInvariantKind::AdditionalFact,
                                        diagnosticModule, 0));
  }

  zc::Vector<zc::Maybe<InterfaceMarkerShape>> classified(directShapes.size());
  for (size_t index = 0; index < directShapes.size(); ++index) {
    classified.add(directShapes[index].hasBehavior
                       ? zc::Maybe<InterfaceMarkerShape>(InterfaceMarkerShape::Behavior)
                       : zc::Maybe<InterfaceMarkerShape>());
  }
  size_t remaining = 0;
  for (const auto& shape : classified) {
    if (shape == zc::none) ++remaining;
  }
  while (remaining != 0) {
    bool progressed = false;
    for (size_t index = 0; index < directShapes.size(); ++index) {
      if (classified[index] != zc::none) continue;
      bool parentsReady = true;
      bool inheritedBehavior = false;
      for (const auto parent : directShapes[index].parents) {
        size_t parentIndex = directShapes.size();
        for (size_t candidate = 0; candidate < directShapes.size(); ++candidate) {
          if (directShapes[candidate].interface == parent) {
            parentIndex = candidate;
            break;
          }
        }
        if (parentIndex == directShapes.size()) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact,
                                              directShapes[index].module,
                                              directShapes[index].declaration.value));
        }
        if (classified[parentIndex] == zc::none) {
          parentsReady = false;
          break;
        }
        ZC_IF_SOME(parentShape, classified[parentIndex]) {
          inheritedBehavior = inheritedBehavior || parentShape == InterfaceMarkerShape::Behavior;
        }
      }
      if (!parentsReady) continue;
      classified[index] = inheritedBehavior ? InterfaceMarkerShape::Behavior
                          : directShapes[index].hasGenerics
                              ? InterfaceMarkerShape::GenericMarkerShape
                              : InterfaceMarkerShape::ClosedMarker;
      --remaining;
      progressed = true;
    }
    if (!progressed) {
      return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, diagnosticModule, 0));
    }
  }

  zc::Vector<EncodedShape> encodedShapes(directShapes.size());
  for (size_t index = 0; index < directShapes.size(); ++index) {
    auto key = registries.definitions().lookup(directShapes[index].interface);
    if (key == zc::none || classified[index] == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact,
                                          directShapes[index].module,
                                          directShapes[index].declaration.value));
    }
    ZC_IF_SOME(definitionKey, key) {
      ZC_IF_SOME(shape, classified[index]) {
        identity::CanonicalEncoder encoder;
        definitionKey.encode(encoder);
        encoder.encodeUint8(static_cast<uint8_t>(shape));
        encodedShapes.add(EncodedShape{
            InterfaceMarkerShapeFact{directShapes[index].interface, shape}, encoder.finish()});
      }
    }
  }

  for (size_t index = 1; index < encodedShapes.size(); ++index) {
    auto current = zc::mv(encodedShapes[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.bytes.asPtr(), encodedShapes[insertion - 1].bytes.asPtr())) {
      encodedShapes[insertion] = zc::mv(encodedShapes[insertion - 1]);
      --insertion;
    }
    encodedShapes[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < encodedShapes.size(); ++index) {
    if (sameBytes(encodedShapes[index - 1].bytes.asPtr(), encodedShapes[index].bytes.asPtr())) {
      return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                          diagnosticModule, static_cast<uint32_t>(index)));
    }
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> recordViews(encodedShapes.size());
  zc::Vector<InterfaceMarkerShapeFact> shapeFacts(encodedShapes.size());
  for (auto& shape : encodedShapes) {
    recordViews.add(shape.bytes.asPtr());
    shapeFacts.add(shape.fact);
  }
  auto revision =
      MarkerShapeInventoryRevision::computeFramed(contextFingerprint.digest(), recordViews.asPtr());
  if (revision == zc::none) {
    return buildReject(
        checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, diagnosticModule, 0));
  }
  ZC_IF_SOME(value, revision) {
    return VerifiedMarkerShapeInventory(zc::heap<VerifiedMarkerShapeInventory::Impl>(
        semanticContext, contextFingerprint.clone(), zc::mv(value), zc::mv(shapeFacts)));
  }
  ZC_UNREACHABLE
}

MarkerPolicyRegistryBuildResult MarkerPolicyRegistryBuilder::build(
    const MarkerPolicyConfiguration& configuration,
    const VerifiedMarkerShapeInventory& shapeInventory,
    zc::ArrayPtr<const identity::ModuleId> authorizedPreludeModules,
    const identity::SemanticIdentityRegistrySet& registries) {
  auto diagnosticModule = firstRegisteredModule(registries);
  if (diagnosticModule == zc::none) {
    return buildReject(registryInvariant(identity::FrozenRegistryFailure::InvalidHandle,
                                         identity::IdentityAllocationPhase::Module, 0));
  }
  identity::ModuleId module;
  ZC_IF_SOME(value, diagnosticModule) { module = value; }
  if (registries.context() != shapeInventory.semanticContext()) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }
  for (size_t index = 0; index < authorizedPreludeModules.size(); ++index) {
    const auto failure = registries.modules().validate(authorizedPreludeModules[index]);
    if (failure != identity::FrozenRegistryFailure::None) {
      return buildReject(registryInvariant(failure, identity::IdentityAllocationPhase::Module,
                                           static_cast<uint32_t>(index)));
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (authorizedPreludeModules[previous] == authorizedPreludeModules[index]) {
        return buildReject(checkerInvariant(CheckerInvariantKind::AdditionalFact, module,
                                            static_cast<uint32_t>(index)));
      }
    }
  }
  const auto isPreludeOwned = [&](const identity::DefinitionKey& key) {
    auto definition = registries.definitions().find(key);
    if (definition == zc::none) return false;
    zc::Maybe<const identity::DefinitionIdentityRecord&> record;
    ZC_IF_SOME(value, definition) { record = registries.definitions().lookupRecord(value); }
    if (record == zc::none) return false;
    zc::Maybe<identity::ModuleId> owner;
    ZC_IF_SOME(value, record) { owner = registries.modules().find(value.module()); }
    if (owner == zc::none) return false;
    ZC_IF_SOME(value, owner) {
      for (const auto authorized : authorizedPreludeModules) {
        if (authorized == value) return true;
      }
    }
    return false;
  };
  zc::Vector<MarkerPolicyEntry> entries(configuration.entries().size());
  zc::Vector<zc::Array<uint8_t>> records(configuration.entries().size());
  for (size_t index = 0; index < configuration.entries().size(); ++index) {
    const auto& configured = configuration.entries()[index];
    auto marker = registries.definitions().find(configured.marker);
    if (marker == zc::none || !isPreludeOwned(configured.marker)) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                          static_cast<uint32_t>(index)));
    }
    identity::DefId markerId;
    ZC_IF_SOME(value, marker) { markerId = value; }
    auto shape = shapeInventory.shape(markerId);
    if (shape == zc::none || shape != InterfaceMarkerShape::ClosedMarker) {
      return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                          static_cast<uint32_t>(index)));
    }
    zc::Vector<MarkerPolicyReferenceRequirement> references(
        configured.referenceRequirements.size());
    for (const auto& reference : configured.referenceRequirements) {
      auto required = registries.definitions().find(reference.requiredMarker);
      if (required == zc::none || !isPreludeOwned(reference.requiredMarker)) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                            static_cast<uint32_t>(index)));
      }
      identity::DefId requiredId;
      ZC_IF_SOME(value, required) { requiredId = value; }
      auto requiredShape = shapeInventory.shape(requiredId);
      if (requiredShape == zc::none || requiredShape != InterfaceMarkerShape::ClosedMarker) {
        return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                            static_cast<uint32_t>(index)));
      }
      references.add(MarkerPolicyReferenceRequirement{reference.mutability, requiredId});
    }
    entries.add(MarkerPolicyEntry{
        markerId,
        MarkerPolicy{clonePlainVector(configured.structuralSubjects.asPtr()),
                     clonePlainVector(configured.builtinPrimitives.asPtr()), zc::mv(references)}});
    auto record = encodePolicyConfigurationEntry(configured);
    if (record == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module,
                                          static_cast<uint32_t>(index)));
    }
    ZC_IF_SOME(value, record) { records.add(zc::mv(value)); }
  }
  zc::Vector<zc::ArrayPtr<const uint8_t>> recordViews(records.size());
  for (const auto& record : records) { recordViews.add(record.asPtr()); }
  auto revision = MarkerPolicyRegistryRevision::computeFramed(
      shapeInventory.contextFingerprint().digest(), configuration.revision(),
      shapeInventory.revision(), recordViews.asPtr());
  if (revision == zc::none) {
    return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module, 0));
  }
  ZC_IF_SOME(value, revision) {
    return VerifiedMarkerPolicyRegistry(zc::heap<VerifiedMarkerPolicyRegistry::Impl>(
        shapeInventory.semanticContext(), shapeInventory.contextFingerprint().clone(),
        configuration.revision(), shapeInventory.revision(), zc::mv(value), zc::mv(entries)));
  }
  ZC_UNREACHABLE
}

SignatureFactsRevision::SignatureFactsRevision(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}

const identity::Sha256Digest& SignatureFactsRevision::digest() const noexcept { return value; }

zc::Maybe<SignatureFactsRevision> SignatureFactsRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint,
    zc::ArrayPtr<const uint8_t> expandedOwningModule,
    const identity::Sha256Digest& sourceContentDigest,
    const identity::Sha256Digest& bindingSurfaceRevision,
    const identity::Sha256Digest& markerPolicyRegistryRevision,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
    zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords) {
  identity::CanonicalEncoder encoder;
  encodeAscii(encoder, "zom.signature-facts-revision.v1"_zcc);
  encoder.encodeUint8(0);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedOwningModule);
  encoder.encodeDigest(sourceContentDigest);
  encoder.encodeDigest(bindingSurfaceRevision);
  encoder.encodeDigest(markerPolicyRegistryRevision);

  const auto encodeRecords = [&](zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> records) {
    zc::Vector<zc::ArrayPtr<const uint8_t>> sorted(records.size());
    for (const auto record : records) { sorted.add(record); }
    for (size_t index = 1; index < sorted.size(); ++index) {
      auto current = sorted[index];
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
    for (const auto record : sorted) { encoder.encodeByteString(record); }
    return true;
  };

  if (!encodeRecords(signatureRecords) || !encodeRecords(implHeadRecords) ||
      !encodeRecords(markerFactRecords)) {
    return zc::none;
  }
  auto digest = identity::sha256(encoder.finish().asPtr());
  ZC_IF_SOME(value, digest) { return SignatureFactsRevision(value); }
  return zc::none;
}

struct VerifiedSignatureFacts::Impl final {
  Impl(SignatureFactsCandidate&& candidate, SignatureFactsRevision&& revision)
      : revision(zc::mv(revision)),
        semanticContext(candidate.semanticContext),
        contextFingerprint(zc::mv(candidate.contextFingerprint)),
        module(candidate.module),
        sourceContentDigest(candidate.sourceContentDigest),
        parsedModuleReceipt(candidate.parsedModuleReceipt),
        bindingSurfaceRevision(candidate.bindingSurfaceRevision),
        markerPolicyRegistryRevision(candidate.markerPolicyRegistryRevision),
        signatures(zc::mv(candidate.signatures)),
        implHeads(zc::mv(candidate.implHeads)),
        markerFacts(zc::mv(candidate.markerFacts)) {}

  SignatureFactsRevision revision;
  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  identity::Sha256Digest sourceContentDigest;
  binder::ParsedModuleReceipt parsedModuleReceipt;
  binder::ExportSurfaceRevision bindingSurfaceRevision;
  MarkerPolicyRegistryRevision markerPolicyRegistryRevision;
  zc::Vector<SemanticSignature> signatures;
  zc::Vector<ImplHead> implHeads;
  zc::Vector<MarkerFact> markerFacts;
};

VerifiedSignatureFacts::VerifiedSignatureFacts(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedSignatureFacts::~VerifiedSignatureFacts() noexcept(false) = default;
VerifiedSignatureFacts::VerifiedSignatureFacts(VerifiedSignatureFacts&&) noexcept = default;
VerifiedSignatureFacts& VerifiedSignatureFacts::operator=(VerifiedSignatureFacts&&) noexcept =
    default;

const SignatureFactsRevision& VerifiedSignatureFacts::revision() const noexcept {
  return impl->revision;
}
const MarkerPolicyRegistryRevision& VerifiedSignatureFacts::markerPolicyRegistryRevision()
    const noexcept {
  return impl->markerPolicyRegistryRevision;
}
identity::SemanticContextBrand VerifiedSignatureFacts::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedSignatureFacts::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedSignatureFacts::module() const noexcept { return impl->module; }
const identity::Sha256Digest& VerifiedSignatureFacts::sourceContentDigest() const noexcept {
  return impl->sourceContentDigest;
}
const binder::ParsedModuleReceipt& VerifiedSignatureFacts::parsedModuleReceipt() const noexcept {
  return impl->parsedModuleReceipt;
}
const binder::ExportSurfaceRevision& VerifiedSignatureFacts::bindingSurfaceRevision()
    const noexcept {
  return impl->bindingSurfaceRevision;
}
zc::ArrayPtr<const SemanticSignature> VerifiedSignatureFacts::signatures() const noexcept {
  return impl->signatures.asPtr();
}
zc::ArrayPtr<const ImplHead> VerifiedSignatureFacts::implHeads() const noexcept {
  return impl->implHeads.asPtr();
}
zc::ArrayPtr<const MarkerFact> VerifiedSignatureFacts::markerFacts() const noexcept {
  return impl->markerFacts.asPtr();
}

SignatureFactsVerificationResult SignatureFactsVerifier::verify(
    SignatureFactsCandidate&& candidate, const SignatureFactsVerificationInput& input) {
  if (!candidate.semanticContext.isValid()) { return reject(invalidContextInvariant(0)); }

  const auto moduleFailure = input.registries.modules().validate(candidate.module);
  if (moduleFailure != identity::FrozenRegistryFailure::None) {
    return reject(registryInvariant(moduleFailure, identity::IdentityAllocationPhase::Module, 0));
  }
  for (size_t index = 0; index < candidate.signatures.size(); ++index) {
    const auto failure =
        input.registries.definitions().validate(candidate.signatures[index].definition);
    if (failure != identity::FrozenRegistryFailure::None) {
      return reject(registryInvariant(failure, identity::IdentityAllocationPhase::Definition,
                                      static_cast<uint32_t>(index)));
    }
  }
  for (size_t index = 0; index < candidate.implHeads.size(); ++index) {
    const auto failure = input.registries.impls().validate(candidate.implHeads[index].impl);
    if (failure != identity::FrozenRegistryFailure::None) {
      return reject(registryInvariant(failure, identity::IdentityAllocationPhase::Impl,
                                      static_cast<uint32_t>(index)));
    }
  }
  for (size_t index = 0; index < candidate.markerFacts.size(); ++index) {
    const auto markerFailure =
        input.registries.definitions().validate(candidate.markerFacts[index].key.marker);
    if (markerFailure != identity::FrozenRegistryFailure::None) {
      return reject(registryInvariant(markerFailure, identity::IdentityAllocationPhase::Definition,
                                      static_cast<uint32_t>(index)));
    }
    auto subject = input.semanticTypes.get(candidate.markerFacts[index].key.subject);
    if (subject.is<identity::IdentityInvariant>()) {
      return reject(zc::mv(subject.get<identity::IdentityInvariant>()));
    }
  }

  if (candidate.semanticContext != input.semanticContext || candidate.module != input.module ||
      candidate.contextFingerprint.digest() != input.contextFingerprint.digest() ||
      candidate.sourceContentDigest != input.sourceContentDigest ||
      candidate.parsedModuleReceipt.digest() != input.parsedModuleReceipt.digest() ||
      candidate.bindingSurfaceRevision.digest() != input.bindingSurfaceRevision.digest() ||
      candidate.markerPolicyRegistryRevision.digest() !=
          input.markerPolicyRegistryRevision.digest()) {
    return reject(
        checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, candidate.module, 0));
  }

  auto moduleKey = input.registries.modules().lookup(candidate.module);
  if (moduleKey == zc::none || input.registries.sourceFiles().find(input.source) == zc::none) {
    return reject(registryInvariant(identity::FrozenRegistryFailure::InvalidHandle,
                                    identity::IdentityAllocationPhase::Module, 0));
  }

  zc::Vector<zc::Array<uint8_t>> records(candidate.signatures.size());
  zc::Vector<zc::Array<uint8_t>> implRecords(candidate.implHeads.size());
  zc::Vector<zc::Array<uint8_t>> markerRecords(candidate.markerFacts.size());
  ZC_IF_SOME(owningModule, moduleKey) {
    if (!input.source.belongsTo(owningModule.crate())) {
      return reject(
          checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, candidate.module, 0));
    }
    SignatureFactsCanonicalEncoder encoder(input.registries, input.semanticTypes, owningModule,
                                           input.source);
    for (size_t index = 0; index < candidate.signatures.size(); ++index) {
      if (!attributesAgreeWithPayload(candidate.signatures[index])) {
        return reject(checkerInvariant(CheckerInvariantKind::InvalidFact, candidate.module,
                                       static_cast<uint32_t>(index)));
      }
      auto result = encoder.encode(candidate.signatures[index], static_cast<uint32_t>(index));
      if (result.is<identity::IdentityInvariant>()) {
        return reject(zc::mv(result.get<identity::IdentityInvariant>()));
      }
      if (result.is<RecordFailure>()) {
        const auto failure = result.get<RecordFailure>();
        return reject(checkerInvariant(failure.kind == RecordFailureKind::CanonicalCodec
                                           ? CheckerInvariantKind::CanonicalCodecMismatch
                                           : CheckerInvariantKind::InvalidFact,
                                       candidate.module, failure.ordinal));
      }
      records.add(zc::mv(result.get<EncodedSignature>().bytes));
    }
    for (size_t index = 0; index < candidate.implHeads.size(); ++index) {
      auto result = encoder.encode(candidate.implHeads[index], static_cast<uint32_t>(index));
      if (result.is<identity::IdentityInvariant>()) {
        return reject(zc::mv(result.get<identity::IdentityInvariant>()));
      }
      if (result.is<RecordFailure>()) {
        const auto failure = result.get<RecordFailure>();
        return reject(checkerInvariant(failure.kind == RecordFailureKind::CanonicalCodec
                                           ? CheckerInvariantKind::CanonicalCodecMismatch
                                           : CheckerInvariantKind::InvalidFact,
                                       candidate.module, failure.ordinal));
      }
      implRecords.add(zc::mv(result.get<EncodedSignature>().bytes));
    }
    for (size_t index = 0; index < candidate.markerFacts.size(); ++index) {
      const auto& fact = candidate.markerFacts[index];
      if (!fact.evidence.variant().is<ExplicitMarkerEvidence>() ||
          fact.declarationSpan == zc::none) {
        return reject(checkerInvariant(CheckerInvariantKind::InvalidFact, candidate.module,
                                       static_cast<uint32_t>(index)));
      }
      auto result = encoder.encode(fact, static_cast<uint32_t>(index));
      if (result.is<identity::IdentityInvariant>()) {
        return reject(zc::mv(result.get<identity::IdentityInvariant>()));
      }
      if (result.is<RecordFailure>()) {
        const auto failure = result.get<RecordFailure>();
        return reject(checkerInvariant(failure.kind == RecordFailureKind::CanonicalCodec
                                           ? CheckerInvariantKind::CanonicalCodecMismatch
                                           : CheckerInvariantKind::InvalidFact,
                                       candidate.module, failure.ordinal));
      }
      markerRecords.add(zc::mv(result.get<EncodedSignature>().bytes));
    }

    const auto recordsAreStrictlySorted = [](zc::ArrayPtr<const zc::Array<uint8_t>> values) {
      for (size_t index = 1; index < values.size(); ++index) {
        if (!lessBytes(values[index - 1].asPtr(), values[index].asPtr())) return false;
      }
      return true;
    };
    if (!recordsAreStrictlySorted(records.asPtr()) ||
        !recordsAreStrictlySorted(implRecords.asPtr()) ||
        !recordsAreStrictlySorted(markerRecords.asPtr())) {
      return reject(
          checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, candidate.module, 0));
    }

    const auto requiredRecordsAreStrictlySorted = [](const auto& requirements) {
      for (size_t index = 1; index < requirements.size(); ++index) {
        if (!lessBytes(requirements[index - 1].canonicalRecord.asPtr(),
                       requirements[index].canonicalRecord.asPtr())) {
          return false;
        }
      }
      return true;
    };
    if (!requiredRecordsAreStrictlySorted(input.requiredSignatures) ||
        !requiredRecordsAreStrictlySorted(input.requiredImplHeads) ||
        !requiredRecordsAreStrictlySorted(input.requiredMarkerFacts)) {
      return reject(
          checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, candidate.module, 0));
    }
    if (records.size() < input.requiredSignatures.size() ||
        implRecords.size() < input.requiredImplHeads.size() ||
        markerRecords.size() < input.requiredMarkerFacts.size()) {
      return reject(
          checkerInvariant(CheckerInvariantKind::MissingRequiredFact, candidate.module, 0));
    }
    if (records.size() > input.requiredSignatures.size() ||
        implRecords.size() > input.requiredImplHeads.size() ||
        markerRecords.size() > input.requiredMarkerFacts.size()) {
      return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
    }
    for (size_t index = 0; index < records.size(); ++index) {
      if (!sameBytes(records[index].asPtr(),
                     input.requiredSignatures[index].canonicalRecord.asPtr())) {
        return reject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                       candidate.module, static_cast<uint32_t>(index)));
      }
    }
    for (size_t index = 0; index < implRecords.size(); ++index) {
      if (!sameBytes(implRecords[index].asPtr(),
                     input.requiredImplHeads[index].canonicalRecord.asPtr())) {
        return reject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                       candidate.module, static_cast<uint32_t>(index)));
      }
    }
    for (size_t index = 0; index < markerRecords.size(); ++index) {
      if (!sameBytes(markerRecords[index].asPtr(),
                     input.requiredMarkerFacts[index].canonicalRecord.asPtr())) {
        return reject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                       candidate.module, static_cast<uint32_t>(index)));
      }
    }

    for (size_t index = 0; index < candidate.signatures.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (candidate.signatures[previous].definition == candidate.signatures[index].definition) {
          return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module,
                                         static_cast<uint32_t>(index)));
        }
      }
    }
    for (size_t index = 0; index < candidate.implHeads.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (candidate.implHeads[previous].impl == candidate.implHeads[index].impl) {
          return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module,
                                         static_cast<uint32_t>(index)));
        }
      }
    }
    for (size_t index = 0; index < candidate.markerFacts.size(); ++index) {
      for (size_t previous = 0; previous < index; ++previous) {
        if (sameMarkerKey(candidate.markerFacts[previous].key, candidate.markerFacts[index].key)) {
          return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module,
                                         static_cast<uint32_t>(index)));
        }
      }
    }

    if (candidate.signatures.size() < input.sourceSignatureCensus.size() ||
        candidate.implHeads.size() + candidate.markerFacts.size() < input.sourceImplCensus.size()) {
      return reject(
          checkerInvariant(CheckerInvariantKind::MissingRequiredFact, candidate.module, 0));
    }
    if (candidate.signatures.size() > input.sourceSignatureCensus.size() ||
        candidate.implHeads.size() + candidate.markerFacts.size() > input.sourceImplCensus.size()) {
      return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
    }
    for (const auto& expected : input.sourceSignatureCensus) {
      size_t matches = 0;
      for (const auto& signature : candidate.signatures) {
        if (signature.definition == expected.definition &&
            signature.definitionKind == expected.definitionKind) {
          ++matches;
        }
      }
      if (matches != 1) {
        return reject(checkerInvariant(matches == 0 ? CheckerInvariantKind::MissingRequiredFact
                                                    : CheckerInvariantKind::AdditionalFact,
                                       candidate.module, 0));
      }
    }
    for (const auto& signature : candidate.signatures) {
      bool found = false;
      for (const auto& expected : input.sourceSignatureCensus) {
        if (signature.definition == expected.definition &&
            signature.definitionKind == expected.definitionKind) {
          found = true;
          break;
        }
      }
      if (!found) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }
    for (const auto& expected : input.sourceImplCensus) {
      size_t behaviorMatches = 0;
      size_t markerMatches = 0;
      for (const auto& head : candidate.implHeads) {
        if (head.impl == expected.implementation) ++behaviorMatches;
      }
      for (const auto& fact : candidate.markerFacts) {
        if (fact.evidence.variant().get<ExplicitMarkerEvidence>().impl == expected.implementation) {
          ++markerMatches;
        }
      }
      const bool exactBehavior = expected.kind == ImplAuthorityKind::Behavior &&
                                 behaviorMatches == 1 && markerMatches == 0;
      const bool exactMarker =
          expected.kind == ImplAuthorityKind::Marker && behaviorMatches == 0 && markerMatches == 1;
      if (!exactBehavior && !exactMarker) {
        return reject(checkerInvariant(behaviorMatches + markerMatches == 0
                                           ? CheckerInvariantKind::MissingRequiredFact
                                           : CheckerInvariantKind::InvalidFact,
                                       candidate.module, 0));
      }
    }
    for (const auto& head : candidate.implHeads) {
      bool found = false;
      for (const auto& expected : input.sourceImplCensus) {
        if (expected.implementation == head.impl && expected.kind == ImplAuthorityKind::Behavior) {
          found = true;
          break;
        }
      }
      if (!found) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }
    for (const auto& fact : candidate.markerFacts) {
      const auto implementation = fact.evidence.variant().get<ExplicitMarkerEvidence>().impl;
      bool found = false;
      for (const auto& expected : input.sourceImplCensus) {
        if (expected.implementation == implementation &&
            expected.kind == ImplAuthorityKind::Marker) {
          found = true;
          break;
        }
      }
      if (!found) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }

    for (const auto& requirement : input.requiredSignatures) {
      if (!containsSignature(candidate.signatures.asPtr(), requirement)) {
        return reject(
            checkerInvariant(CheckerInvariantKind::MissingRequiredFact, candidate.module, 0));
      }
    }
    for (const auto& signature : candidate.signatures) {
      if (!containsRequirement(input.requiredSignatures, signature.definition,
                               signature.definitionKind)) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }
    for (const auto& requirement : input.requiredImplHeads) {
      if (!containsImplHead(candidate.implHeads.asPtr(), requirement)) {
        return reject(
            checkerInvariant(CheckerInvariantKind::MissingRequiredFact, candidate.module, 0));
      }
    }
    for (const auto& head : candidate.implHeads) {
      if (!containsImplRequirement(input.requiredImplHeads, head.impl)) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }
    for (const auto& requirement : input.requiredMarkerFacts) {
      if (!containsMarkerFact(candidate.markerFacts.asPtr(), requirement)) {
        return reject(
            checkerInvariant(CheckerInvariantKind::MissingRequiredFact, candidate.module, 0));
      }
    }
    for (const auto& fact : candidate.markerFacts) {
      if (!containsMarkerRequirement(input.requiredMarkerFacts, fact.key)) {
        return reject(checkerInvariant(CheckerInvariantKind::AdditionalFact, candidate.module, 0));
      }
    }
    if (!scopeGraphIsValid(candidate.signatures.asPtr())) {
      return reject(checkerInvariant(CheckerInvariantKind::InvalidFact, candidate.module, 0));
    }

    zc::Vector<zc::ArrayPtr<const uint8_t>> recordViews(records.size());
    for (const auto& record : records) { recordViews.add(record.asPtr()); }
    zc::Vector<zc::ArrayPtr<const uint8_t>> implRecordViews(implRecords.size());
    for (const auto& record : implRecords) { implRecordViews.add(record.asPtr()); }
    zc::Vector<zc::ArrayPtr<const uint8_t>> markerRecordViews(markerRecords.size());
    for (const auto& record : markerRecords) { markerRecordViews.add(record.asPtr()); }
    auto revision = SignatureFactsRevision::computeFramed(
        candidate.contextFingerprint.digest(), owningModule.encode().asPtr(),
        candidate.sourceContentDigest, candidate.bindingSurfaceRevision.digest(),
        candidate.markerPolicyRegistryRevision.digest(), recordViews.asPtr(),
        implRecordViews.asPtr(), markerRecordViews.asPtr());
    if (revision == zc::none) {
      return reject(
          checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, candidate.module, 0));
    }
    ZC_IF_SOME(value, revision) {
      return VerifiedSignatureFacts(
          zc::heap<VerifiedSignatureFacts::Impl>(zc::mv(candidate), zc::mv(value)));
    }
  }
  ZC_UNREACHABLE
}

SignatureFactsBuildResult SignatureFactsBuilder::build(const SignatureFactsBuildInput& input) {
  const auto context = input.registries.context();
  const auto module = input.boundModule.module();
  if (!context.isValid()) { return buildReject(invalidContextInvariant(0)); }

  const auto packageFailure = input.registries.packages().validate(input.boundModule.package());
  if (packageFailure != identity::FrozenRegistryFailure::None) {
    return buildReject(
        registryInvariant(packageFailure, identity::IdentityAllocationPhase::Package, 0));
  }
  const auto crateFailure = input.registries.crates().validate(input.boundModule.crate());
  if (crateFailure != identity::FrozenRegistryFailure::None) {
    return buildReject(
        registryInvariant(crateFailure, identity::IdentityAllocationPhase::Crate, 0));
  }
  const auto moduleFailure = input.registries.modules().validate(module);
  if (moduleFailure != identity::FrozenRegistryFailure::None) {
    return buildReject(
        registryInvariant(moduleFailure, identity::IdentityAllocationPhase::Module, 0));
  }
  const auto sourceFailure =
      input.registries.sourceFiles().validate(input.boundModule.parsedModule().sourceFile());
  if (sourceFailure != identity::FrozenRegistryFailure::None) {
    return buildReject(
        registryInvariant(sourceFailure, identity::IdentityAllocationPhase::Source, 0));
  }
  for (size_t index = 0; index < input.boundModule.bindings().definitions().size(); ++index) {
    const auto failure = input.registries.definitions().validate(
        input.boundModule.bindings().definitions()[index].identity);
    if (failure != identity::FrozenRegistryFailure::None) {
      return buildReject(registryInvariant(failure, identity::IdentityAllocationPhase::Definition,
                                           static_cast<uint32_t>(index)));
    }
  }
  for (size_t index = 0; index < input.boundModule.bindings().impls().size(); ++index) {
    const auto failure =
        input.registries.impls().validate(input.boundModule.bindings().impls()[index].authority);
    if (failure != identity::FrozenRegistryFailure::None) {
      return buildReject(registryInvariant(failure, identity::IdentityAllocationPhase::Impl,
                                           static_cast<uint32_t>(index)));
    }
  }

  if (input.boundModule.semanticContext() != context ||
      input.boundModule.bindings().semanticContext() != context ||
      input.semanticTypes.context() != context || input.boundModule.bindings().module() != module ||
      input.boundModule.bindingSurface().sourceModule() != module ||
      input.boundModule.bindingSurface().sourcePackage() != input.boundModule.package()) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }

  auto moduleKey = input.registries.modules().lookup(module);
  auto sourceKey =
      input.registries.sourceFiles().lookup(input.boundModule.parsedModule().sourceFile());
  if (moduleKey == zc::none || sourceKey == zc::none) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }
  ZC_IF_SOME(owner, moduleKey) {
    ZC_IF_SOME(source, sourceKey) {
      if (!sameSourceKey(input.boundModule.parsedModule().source(), source) ||
          !source.belongsTo(owner.crate()) ||
          !input.boundModule.parsedModule().rootSpan().belongsTo(source)) {
        return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
      }
    }
  }

  auto sourceSnapshot =
      input.registries.sourceSnapshot(input.boundModule.parsedModule().sourceFile());
  if (sourceSnapshot == zc::none) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }
  ZC_IF_SOME(snapshot, sourceSnapshot) {
    if (snapshot.contentDigest() != input.boundModule.parsedModule().contentDigest() ||
        snapshot.bytes().size() != input.boundModule.parsedModule().byteLength()) {
      return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
    }
  }

  if (!bindingInventoryMatches(input.boundModule, input.boundModule.bindings(), input.registries)) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }

  if (input.markerShapes.semanticContext() != context ||
      input.markerShapes.contextFingerprint().digest() !=
          input.boundModule.semanticFingerprint().digest() ||
      input.markerPolicies.semanticContext() != context ||
      input.markerPolicies.contextFingerprint().digest() !=
          input.boundModule.semanticFingerprint().digest() ||
      input.markerPolicies.shapeInventoryRevision().digest() !=
          input.markerShapes.revision().digest()) {
    return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module, 0));
  }

  zc::Maybe<identity::SemanticTypeId> unitType;
  zc::Vector<SignatureSourceFailureRef> sourceFailures;
  zc::Vector<SignatureAdvisoryRef> advisories;
  zc::Vector<identity::DefId> failedInterfaces;
  struct BuiltSignature final {
    SemanticSignature signature;
    SignatureDefinitionRequirement requirement;
    zc::Array<uint8_t> definitionKey;
  };
  zc::Vector<BuiltSignature> built;
  const auto& tree = input.boundModule.tree();
  struct BuiltSignatureCensusEntry final {
    SignatureDefinitionCensusEntry entry;
    zc::Array<uint8_t> key;
  };
  zc::Vector<BuiltSignatureCensusEntry> builtSignatureCensus;
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (isSignatureBearingDefinition(definition.record.kind())) {
      builtSignatureCensus.add(BuiltSignatureCensusEntry{
          SignatureDefinitionCensusEntry{definition.definition, definition.record.kind()},
          definition.key.encode()});
    }
  }
  for (size_t index = 1; index < builtSignatureCensus.size(); ++index) {
    auto current = zc::mv(builtSignatureCensus[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.key.asPtr(), builtSignatureCensus[insertion - 1].key.asPtr())) {
      builtSignatureCensus[insertion] = zc::mv(builtSignatureCensus[insertion - 1]);
      --insertion;
    }
    builtSignatureCensus[insertion] = zc::mv(current);
  }
  zc::Vector<SignatureDefinitionCensusEntry> sourceSignatureCensus(builtSignatureCensus.size());
  for (auto& value : builtSignatureCensus) { sourceSignatureCensus.add(zc::mv(value.entry)); }

  struct BuiltImplCensusEntry final {
    ImplAuthorityCensusEntry entry;
    zc::Array<uint8_t> key;
  };
  zc::Vector<BuiltImplCensusEntry> builtImplCensus;
  for (const auto& authority : input.boundModule.definitions().implAuthorities()) {
    zc::Maybe<ImplAuthorityKind> expectedKind;
    for (const auto& occurrence : input.boundModule.definitions().impls()) {
      if (occurrence.authority != authority.implementation) continue;
      if (!tree.contains(occurrence.node)) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, occurrence.node.value));
      }
      const auto syntaxKind = tree.node(occurrence.node).kind;
      zc::Maybe<ImplAuthorityKind> occurrenceKind;
      if (syntaxKind == ast::SyntaxKind::StandaloneImplDecl) {
        occurrenceKind = ImplAuthorityKind::Behavior;
      } else if (syntaxKind == ast::SyntaxKind::MarkerImpl) {
        occurrenceKind = ImplAuthorityKind::Marker;
      } else {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, occurrence.node.value));
      }
      ZC_IF_SOME(value, occurrenceKind) {
        if (expectedKind != zc::none) {
          ZC_IF_SOME(expected, expectedKind) {
            if (expected != value) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  occurrence.node.value));
            }
          }
        } else {
          expectedKind = value;
        }
      }
    }
    if (expectedKind == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, 0));
    }
    ZC_IF_SOME(kind, expectedKind) {
      builtImplCensus.add(BuiltImplCensusEntry{
          ImplAuthorityCensusEntry{authority.implementation, kind}, authority.key.encode()});
    }
  }
  for (size_t index = 1; index < builtImplCensus.size(); ++index) {
    auto current = zc::mv(builtImplCensus[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.key.asPtr(), builtImplCensus[insertion - 1].key.asPtr())) {
      builtImplCensus[insertion] = zc::mv(builtImplCensus[insertion - 1]);
      --insertion;
    }
    builtImplCensus[insertion] = zc::mv(current);
  }
  zc::Vector<ImplAuthorityCensusEntry> sourceImplCensus(builtImplCensus.size());
  for (auto& value : builtImplCensus) { sourceImplCensus.add(zc::mv(value.entry)); }

  for (const auto& definition : input.boundModule.definitions().definitions()) {
    const auto definitionKind = definition.record.kind();
    if (!isSignatureBearingDefinition(definitionKind)) { continue; }
    auto fact = findDefinitionFact(input.boundModule.bindings(), definition.definition);
    if (fact == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                          definition.node.value));
    }
    ZC_IF_SOME(bound, fact) {
      auto scope = signatureScope(input.boundModule, input.boundModule.bindings(), bound, module);
      if (scope == zc::none) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                            definition.node.value));
      }
      if (definitionKind == identity::DefinitionKind::Constant ||
          definitionKind == identity::DefinitionKind::Static) {
        const auto& site = definition.site.value();
        if (!site.is<binder::PatternBindingSite>()) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        const auto& patternSite = site.get<binder::PatternBindingSite>();
        if (patternSite.patternPath.size() != 0 || !tree.contains(patternSite.introducer) ||
            tree.node(patternSite.introducer).kind != ast::SyntaxKind::VariableDeclarator ||
            !tree.contains(definition.node) ||
            tree.node(definition.node).kind != ast::SyntaxKind::IdentifierPattern) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        auto declarationKind = declarationKindForDeclarator(tree, patternSite.introducer);
        if (declarationKind == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              patternSite.introducer.value));
        }
        ast::BindingDeclarationKind bindingKind = ast::BindingDeclarationKind::Let;
        ZC_IF_SOME(value, declarationKind) { bindingKind = value; }
        if ((definitionKind == identity::DefinitionKind::Constant) !=
            (bindingKind == ast::BindingDeclarationKind::Const)) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              patternSite.introducer.value));
        }

        const auto& declarator = tree.node(patternSite.introducer);
        const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
        const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
        zc::Maybe<identity::SemanticTypeId> valueType;
        zc::Maybe<CanonicalConstValue> constantValue;
        if (tree.contains(annotation)) {
          zc::Vector<identity::GenericParameterId> noGenerics;
          SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                        noGenerics.asPtr());
          auto builtType = typeBuilder.build(annotation);
          if (builtType == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                annotation.value));
          }
          ZC_IF_SOME(value, builtType) { valueType = value.type; }
        }
        if (tree.contains(initializer)) {
          auto key = checkedNodeKey(input.boundModule, initializer);
          if (key == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module,
                                                initializer.value));
          }
          ZC_IF_SOME(checkedNode, key) {
            auto emitted = scalar_literal::FactEmitter::emit(scalar_literal::FactEmissionInput{
                context, module, tree, initializer, checkedNode,
                input.boundModule.parsedModule().source(), input.registries, input.semanticTypes});
            if (emitted.is<checked::CheckedFactsInvariantRejected>()) {
              auto rejected = zc::mv(emitted).get<checked::CheckedFactsInvariantRejected>();
              return SignatureFactsInvariantRejected{zc::mv(rejected.failures)};
            }
            if (emitted.is<checked::CheckedFactsSourceRejected>()) {
              const auto& rejected = emitted.get<checked::CheckedFactsSourceRejected>();
              if (rejected.failures.size() != 1 ||
                  !(rejected.failures[0].diagnostic ==
                    checked::CheckerErrorId::BodyLiteralOutOfRange()) ||
                  rejected.failures[0].arguments.size() != 2 ||
                  !rejected.failures[0].arguments[0].variant().is<checked::LiteralDisplayArg>() ||
                  !rejected.failures[0]
                       .arguments[1]
                       .variant()
                       .is<checked::PrimitiveTypeDisplayArg>()) {
                return buildReject(
                    checkerInvariant(CheckerInvariantKind::InvalidFact, module, initializer.value));
              }
              zc::Vector<SignatureSourceArgument> arguments;
              arguments.add(SignatureSourceArgument(
                  SignatureLiteralDisplayArg{rejected.failures[0]
                                                 .arguments[0]
                                                 .variant()
                                                 .get<checked::LiteralDisplayArg>()
                                                 .literal.clone()}));
              arguments.add(SignatureSourceArgument(
                  SignaturePrimitiveTypeDisplayArg{rejected.failures[0]
                                                       .arguments[1]
                                                       .variant()
                                                       .get<checked::PrimitiveTypeDisplayArg>()
                                                       .kind}));
              sourceFailures.add(SignatureSourceFailureRef{
                  SignatureSourceDiagnostic::BodyLiteralOutOfRange,
                  rejected.failures[0].primaryNode, rejected.failures[0].primarySpan.clone(),
                  zc::mv(arguments),
                  SignatureEmitterOrdinal{rejected.failures[0].emitterOrdinal.ownerSchemaPreorder,
                                          rejected.failures[0].emitterOrdinal.siteSchemaPreorder,
                                          rejected.failures[0].emitterOrdinal.itemOrdinal}});
              continue;
            } else {
              auto literal = zc::mv(emitted).get<scalar_literal::EmittedFacts>();
              if (valueType == zc::none) valueType = literal.nodeType.value;
              if (definitionKind == identity::DefinitionKind::Constant) {
                constantValue = literal.literal.value.literal.clone();
              }
            }
          }
        }
        if (valueType == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        const auto mutability = bindingKind == ast::BindingDeclarationKind::Mut
                                    ? Mutability::Mutable
                                    : Mutability::Const;
        zc::Maybe<ExternAbi> noAbi;
        ZC_IF_SOME(typeValue, valueType) {
          ZC_IF_SOME(signatureScopeValue, scope) {
            built.add(BuiltSignature{
                SemanticSignature{definition.definition, definitionKind,
                                  zc::mv(signatureScopeValue), zc::Vector<SignatureModifier>(),
                                  zc::Vector<NormalizedAttributeFact>(),
                                  SemanticSignaturePayload(ValueSignature{
                                      typeValue, mutability, tree.contains(initializer),
                                      zc::mv(constantValue), zc::mv(noAbi)}),
                                  bound.source.clone()},
                SignatureDefinitionRequirement{definition.definition, definitionKind,
                                               zc::Array<uint8_t>()},
                definition.key.encode()});
          }
        }
        continue;
      }
      if (isNominalDefinition(definitionKind)) {
        if (!tree.contains(definition.node)) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        const auto& syntax = tree.node(definition.node);
        ast::NodeId baseNode;
        ast::NodeId contentsNode;
        ast::SyntaxKind contentsKind = ast::SyntaxKind::ClassMemberList;
        switch (definitionKind) {
          case identity::DefinitionKind::Class:
            if (syntax.kind != ast::SyntaxKind::ClassDecl) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  definition.node.value));
            }
            baseNode = ast::NodeId(syntax.payload.words[ast::kClassDeclBaseTyWord]);
            contentsNode = ast::NodeId(syntax.payload.words[ast::kClassDeclMembersIdWord]);
            break;
          case identity::DefinitionKind::Struct:
            if (syntax.kind != ast::SyntaxKind::StructDecl) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  definition.node.value));
            }
            contentsNode = ast::NodeId(syntax.payload.words[ast::kStructDeclMembersIdWord]);
            break;
          case identity::DefinitionKind::Enum:
            if (syntax.kind != ast::SyntaxKind::EnumDeclaration) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  definition.node.value));
            }
            contentsNode = ast::NodeId(syntax.payload.words[ast::kEnumDeclarationVariantsIdWord]);
            contentsKind = ast::SyntaxKind::EnumVariantList;
            break;
          case identity::DefinitionKind::Error:
            if (syntax.kind != ast::SyntaxKind::ErrorDecl) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  definition.node.value));
            }
            contentsNode = ast::NodeId(syntax.payload.words[ast::kErrorDeclMembersIdWord]);
            break;
          default:
            ZC_UNREACHABLE
        }

        auto builtGenerics = buildSourceGenericParameters(input, definition.definition);
        if (builtGenerics == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        zc::Vector<identity::GenericParameterId> genericParameterIds;
        zc::Vector<GenericParameterSignature> genericParameters;
        ZC_IF_SOME(value, builtGenerics) {
          genericParameterIds = zc::mv(value.identities);
          genericParameters = zc::mv(value.signatures);
        }
        SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                      genericParameterIds.asPtr());
        zc::Maybe<identity::SemanticTypeId> base;
        if (tree.contains(baseNode)) {
          auto builtBase = typeBuilder.build(baseNode);
          if (builtBase == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                baseNode.value));
          }
          ZC_IF_SOME(value, builtBase) { base = value.type; }
        }

        if (!tree.contains(contentsNode) || tree.node(contentsNode).kind != contentsKind) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, contentsNode.value));
        }
        const auto& contents = tree.node(contentsNode);
        ast::NodeList contentNodes;
        if (contentsKind == ast::SyntaxKind::EnumVariantList) {
          contentNodes =
              ast::NodeList{contents.payload.words[ast::kEnumVariantListVariantsFirstWord],
                            contents.payload.words[ast::kEnumVariantListVariantsSizeWord]};
        } else {
          contentNodes =
              ast::NodeList{contents.payload.words[ast::kClassMemberListMembersFirstWord],
                            contents.payload.words[ast::kClassMemberListMembersSizeWord]};
        }
        if (!tree.contains(contentNodes)) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, contentsNode.value));
        }
        zc::Vector<identity::DefId> fields;
        zc::Vector<identity::DefId> variants;
        zc::Vector<identity::DefId> members;
        for (const auto contentNode : tree.list(contentNodes)) {
          auto memberDefinition = input.boundModule.definitions().definitionAt(contentNode);
          if (memberDefinition == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                contentNode.value));
          }
          ZC_IF_SOME(value, memberDefinition) {
            auto memberRecord = input.registries.definitions().lookupRecord(value);
            if (memberRecord == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                  contentNode.value));
            }
            ZC_IF_SOME(record, memberRecord) {
              if (record.kind() == identity::DefinitionKind::Field) {
                fields.add(value);
              } else if (record.kind() == identity::DefinitionKind::EnumVariant) {
                variants.add(value);
              } else {
                members.add(value);
              }
            }
          }
        }
        if (!sortUniqueDefinitionIds(fields, input.registries) ||
            !sortUniqueDefinitionIds(variants, input.registries) ||
            !sortUniqueDefinitionIds(members, input.registries)) {
          return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module,
                                              definition.node.value));
        }
        ZC_IF_SOME(signatureScopeValue, scope) {
          built.add(BuiltSignature{
              SemanticSignature{
                  definition.definition, definitionKind, zc::mv(signatureScopeValue),
                  zc::Vector<SignatureModifier>(), zc::Vector<NormalizedAttributeFact>(),
                  SemanticSignaturePayload(NominalSignature{
                      zc::mv(genericParameters), zc::mv(base), zc::Vector<InterfaceInstantiation>(),
                      zc::mv(fields), zc::mv(variants), zc::mv(members)}),
                  bound.source.clone()},
              SignatureDefinitionRequirement{definition.definition, definitionKind,
                                             zc::Array<uint8_t>()},
              definition.key.encode()});
        }
        continue;
      }
      if (definitionKind == identity::DefinitionKind::Field) {
        zc::Maybe<identity::DefId> owner;
        ZC_IF_SOME(value, scope) {
          if (value.variant().is<MemberSignatureScope>()) {
            owner = value.variant().get<MemberSignatureScope>().owner;
          }
        }
        if (!tree.contains(definition.node) ||
            tree.node(definition.node).kind != ast::SyntaxKind::FieldDecl || owner == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        const auto& syntax = tree.node(definition.node);
        identity::DefId ownerDefinition;
        ZC_IF_SOME(value, owner) { ownerDefinition = value; }
        auto ownerGenerics = buildSourceGenericParameters(input, ownerDefinition);
        if (ownerGenerics == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        zc::Vector<identity::GenericParameterId> genericParameterIds;
        ZC_IF_SOME(value, ownerGenerics) { genericParameterIds = zc::mv(value.identities); }
        SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                      genericParameterIds.asPtr());
        const ast::NodeId typeNode(syntax.payload.words[ast::kFieldDeclTyWord]);
        auto builtType = typeBuilder.build(typeNode);
        if (builtType == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, typeNode.value));
        }
        identity::SemanticTypeId fieldType;
        ZC_IF_SOME(value, builtType) { fieldType = value.type; }
        zc::Vector<SignatureModifier> modifiers;
        if (syntax.payload.words[ast::kFieldDeclIsStaticWord] != 0) {
          modifiers.add(SignatureModifier::Static);
        }
        zc::Maybe<CanonicalConstValue> noConstant;
        zc::Maybe<ExternAbi> noAbi;
        ZC_IF_SOME(signatureScopeValue, scope) {
          built.add(BuiltSignature{
              SemanticSignature{
                  definition.definition, definitionKind, zc::mv(signatureScopeValue),
                  zc::mv(modifiers), zc::Vector<NormalizedAttributeFact>(),
                  SemanticSignaturePayload(ValueSignature{
                      fieldType,
                      syntax.payload.words[ast::kFieldDeclIsMutWord] != 0 ? Mutability::Mutable
                                                                          : Mutability::Const,
                      tree.contains(ast::NodeId(syntax.payload.words[ast::kFieldDeclInitWord])),
                      zc::mv(noConstant), zc::mv(noAbi)}),
                  bound.source.clone()},
              SignatureDefinitionRequirement{definition.definition, definitionKind,
                                             zc::Array<uint8_t>()},
              definition.key.encode()});
        }
        continue;
      }
      if (definitionKind == identity::DefinitionKind::EnumVariant) {
        zc::Maybe<identity::DefId> owner;
        ZC_IF_SOME(value, scope) {
          if (value.variant().is<EnclosedSignatureScope>()) {
            owner = value.variant().get<EnclosedSignatureScope>().owner;
          }
        }
        if (!tree.contains(definition.node) || owner == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        identity::DefId ownerDefinition;
        ZC_IF_SOME(value, owner) { ownerDefinition = value; }
        auto ownerGenerics = buildSourceGenericParameters(input, ownerDefinition);
        if (ownerGenerics == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        zc::Vector<identity::GenericParameterId> genericParameterIds;
        ZC_IF_SOME(value, ownerGenerics) { genericParameterIds = zc::mv(value.identities); }
        SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                      genericParameterIds.asPtr());
        const auto& syntax = tree.node(definition.node);
        zc::Vector<identity::SemanticTypeId> payload;
        ast::NodeId discriminantNode;
        if (syntax.kind == ast::SyntaxKind::UnitVariant) {
          discriminantNode = ast::NodeId(syntax.payload.words[ast::kUnitVariantDiscriminantWord]);
        } else if (syntax.kind == ast::SyntaxKind::TupleVariant) {
          const ast::NodeList typeNodes{syntax.payload.words[ast::kTupleVariantTysFirstWord],
                                        syntax.payload.words[ast::kTupleVariantTysSizeWord]};
          if (!tree.contains(typeNodes)) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
          }
          for (const auto typeNode : tree.list(typeNodes)) {
            auto builtType = typeBuilder.build(typeNode);
            if (builtType == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                  typeNode.value));
            }
            ZC_IF_SOME(value, builtType) { payload.add(value.type); }
          }
          discriminantNode = ast::NodeId(syntax.payload.words[ast::kTupleVariantDiscriminantWord]);
        } else {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        zc::Maybe<CanonicalInteger> discriminant;
        if (tree.contains(discriminantNode)) {
          auto checkedKey = checkedNodeKey(input.boundModule, discriminantNode);
          if (checkedKey == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::InputReceiptMismatch, module,
                                                discriminantNode.value));
          }
          ZC_IF_SOME(key, checkedKey) {
            auto emitted = scalar_literal::FactEmitter::emit(scalar_literal::FactEmissionInput{
                context, module, tree, discriminantNode, key,
                input.boundModule.parsedModule().source(), input.registries, input.semanticTypes});
            if (!emitted.is<scalar_literal::EmittedFacts>()) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  discriminantNode.value));
            }
            const auto& literal = emitted.get<scalar_literal::EmittedFacts>().literal.value.literal;
            auto integer = literal.integerValue();
            if (integer == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                                  discriminantNode.value));
            }
            ZC_IF_SOME(value, integer) {
              discriminant =
                  CanonicalInteger{value.sign, zc::heapArray<uint8_t>(value.magnitude.asPtr())};
            }
          }
        }
        ZC_IF_SOME(signatureScopeValue, scope) {
          built.add(BuiltSignature{
              SemanticSignature{definition.definition, definitionKind, zc::mv(signatureScopeValue),
                                zc::Vector<SignatureModifier>(),
                                zc::Vector<NormalizedAttributeFact>(),
                                SemanticSignaturePayload(
                                    EnumVariantSignature{zc::mv(payload), zc::mv(discriminant)}),
                                bound.source.clone()},
              SignatureDefinitionRequirement{definition.definition, definitionKind,
                                             zc::Array<uint8_t>()},
              definition.key.encode()});
        }
        continue;
      }
      if (definitionKind == identity::DefinitionKind::Interface) {
        auto shape = input.markerShapes.shape(definition.definition);
        if (shape == zc::none || !tree.contains(definition.node)) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        const auto& syntax = tree.node(definition.node);
        if (syntax.kind != ast::SyntaxKind::InterfaceDecl) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
        }
        const ast::NodeId generics(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
        InterfaceMarkerShape interfaceShape = InterfaceMarkerShape::Behavior;
        ZC_IF_SOME(value, shape) { interfaceShape = value; }
        if (interfaceShape == InterfaceMarkerShape::GenericMarkerShape) {
          if (!tree.contains(generics) ||
              tree.node(generics).kind != ast::SyntaxKind::GenericParams) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
          }
          const auto& genericSyntax = tree.node(generics);
          const ast::NodeList parameters{
              genericSyntax.payload.words[ast::kGenericParamsParamsFirstWord],
              genericSyntax.payload.words[ast::kGenericParamsParamsSizeWord]};
          if (!tree.contains(parameters) || parameters.empty()) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
          }
          auto failure =
              signatureSourceFailure(SignatureSourceDiagnostic::GenericMarkerInterfaceNotAllowed,
                                     input.boundModule, definition.node, tree.list(parameters)[0]);
          if (failure == zc::none) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, definition.node.value));
          }
          ZC_IF_SOME(value, failure) { sourceFailures.add(zc::mv(value)); }
          failedInterfaces.add(definition.definition);
          continue;
        }

        auto builtGenerics = buildSourceGenericParameters(input, definition.definition);
        if (builtGenerics == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              definition.node.value));
        }
        zc::Vector<identity::GenericParameterId> genericParameterIds;
        zc::Vector<GenericParameterSignature> genericParameters;
        ZC_IF_SOME(value, builtGenerics) {
          genericParameterIds = zc::mv(value.identities);
          genericParameters = zc::mv(value.signatures);
        }
        SourceTypeBuilder interfaceTypeBuilder(input.boundModule, input.registries,
                                               input.semanticTypes, genericParameterIds.asPtr());

        struct BuiltInterface final {
          InterfaceInstantiation interface;
          zc::Array<uint8_t> record;
        };
        zc::Vector<BuiltInterface> builtParents;
        const ast::NodeId parentsNode(syntax.payload.words[ast::kInterfaceDeclIfacesIdWord]);
        if (tree.contains(parentsNode)) {
          const auto& parentSyntax = tree.node(parentsNode);
          if (parentSyntax.kind != ast::SyntaxKind::ImplIfaceList) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, parentsNode.value));
          }
          const ast::NodeList parents{
              parentSyntax.payload.words[ast::kImplIfaceListIfacesFirstWord],
              parentSyntax.payload.words[ast::kImplIfaceListIfacesSizeWord]};
          if (!tree.contains(parents)) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, parentsNode.value));
          }
          for (const auto parent : tree.list(parents)) {
            auto parentInterface = interfaceTypeBuilder.buildInterface(parent);
            if (parentInterface == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                  parent.value));
            }
            ZC_IF_SOME(value, parentInterface) {
              auto record = SignatureFactsCanonicalCodec::encodeInterfaceInstantiation(
                  value, input.registries, input.semanticTypes);
              if (record == zc::none) {
                return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                                    module, parent.value));
              }
              ZC_IF_SOME(bytes, record) {
                builtParents.add(BuiltInterface{zc::mv(value), zc::mv(bytes)});
              }
            }
          }
        }
        for (size_t index = 1; index < builtParents.size(); ++index) {
          auto current = zc::mv(builtParents[index]);
          size_t insertion = index;
          while (insertion > 0 &&
                 lessBytes(current.record.asPtr(), builtParents[insertion - 1].record.asPtr())) {
            builtParents[insertion] = zc::mv(builtParents[insertion - 1]);
            --insertion;
          }
          builtParents[insertion] = zc::mv(current);
        }
        for (size_t index = 1; index < builtParents.size(); ++index) {
          if (sameBytes(builtParents[index - 1].record.asPtr(),
                        builtParents[index].record.asPtr())) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, parentsNode.value));
          }
        }
        zc::Vector<InterfaceInstantiation> parents(builtParents.size());
        for (auto& parent : builtParents) { parents.add(zc::mv(parent.interface)); }

        struct BuiltDefinitionRef final {
          identity::DefId definition;
          zc::Array<uint8_t> key;
        };
        zc::Vector<BuiltDefinitionRef> memberRefs;
        zc::Vector<BuiltDefinitionRef> associatedRefs;
        const ast::NodeId membersNode(syntax.payload.words[ast::kInterfaceDeclMembersIdWord]);
        if (tree.contains(membersNode)) {
          const auto& memberSyntax = tree.node(membersNode);
          if (memberSyntax.kind != ast::SyntaxKind::ClassMemberList) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, membersNode.value));
          }
          const ast::NodeList members{
              memberSyntax.payload.words[ast::kClassMemberListMembersFirstWord],
              memberSyntax.payload.words[ast::kClassMemberListMembersSizeWord]};
          if (!tree.contains(members)) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, membersNode.value));
          }
          for (const auto member : tree.list(members)) {
            auto memberDefinition = input.boundModule.definitions().definitionAt(member);
            if (memberDefinition == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                  member.value));
            }
            ZC_IF_SOME(value, memberDefinition) {
              auto key = input.registries.definitions().lookup(value);
              if (key == zc::none) {
                return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact,
                                                    module, member.value));
              }
              ZC_IF_SOME(definitionKey, key) {
                identity::CanonicalEncoder encoder;
                definitionKey.encode(encoder);
                BuiltDefinitionRef reference{value, encoder.finish()};
                if (tree.contains(member) &&
                    tree.node(member).kind == ast::SyntaxKind::AssociatedTypeDecl) {
                  associatedRefs.add(zc::mv(reference));
                } else {
                  memberRefs.add(zc::mv(reference));
                }
              }
            }
          }
        }
        const auto sortDefinitionRefs = [](zc::Vector<BuiltDefinitionRef>& values) {
          for (size_t index = 1; index < values.size(); ++index) {
            auto current = zc::mv(values[index]);
            size_t insertion = index;
            while (insertion > 0 &&
                   lessBytes(current.key.asPtr(), values[insertion - 1].key.asPtr())) {
              values[insertion] = zc::mv(values[insertion - 1]);
              --insertion;
            }
            values[insertion] = zc::mv(current);
          }
        };
        sortDefinitionRefs(memberRefs);
        sortDefinitionRefs(associatedRefs);
        zc::Vector<identity::DefId> members(memberRefs.size());
        zc::Vector<identity::DefId> associatedTypes(associatedRefs.size());
        for (const auto& member : memberRefs) { members.add(member.definition); }
        for (const auto& associated : associatedRefs) {
          associatedTypes.add(associated.definition);
        }
        ZC_IF_SOME(signatureScopeValue, scope) {
          built.add(BuiltSignature{
              SemanticSignature{
                  definition.definition, definitionKind, zc::mv(signatureScopeValue),
                  zc::Vector<SignatureModifier>(), zc::Vector<NormalizedAttributeFact>(),
                  SemanticSignaturePayload(InterfaceSignature{
                      zc::mv(genericParameters), zc::mv(parents), zc::mv(members),
                      zc::mv(associatedTypes), interfaceShape == InterfaceMarkerShape::ClosedMarker,
                      zc::Vector<ObjectSafetyCause>()}),
                  bound.source.clone()},
              SignatureDefinitionRequirement{definition.definition, definitionKind,
                                             zc::Array<uint8_t>()},
              definition.key.encode()});
        }
        continue;
      }
      if ((definitionKind != identity::DefinitionKind::Function &&
           definitionKind != identity::DefinitionKind::Method) ||
          !isSimpleCallableDeclaration(tree, definition.node, definitionKind)) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                            definition.node.value));
      }
      if (unitType == zc::none) {
        auto admitted = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
            type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Unit}));
        if (admitted.is<identity::IdentityInvariant>()) {
          return buildReject(zc::mv(admitted.get<identity::IdentityInvariant>()));
        }
        auto interned =
            input.semanticTypes.intern(zc::mv(admitted.get<type::semantic::CanonicalTypeData>()));
        if (interned.is<identity::IdentityInvariant>()) {
          return buildReject(zc::mv(interned.get<identity::IdentityInvariant>()));
        }
        unitType = interned.get<type::SemanticTypeInterned>().id;
      }
      ZC_IF_SOME(success, unitType) {
        ZC_IF_SOME(signatureScopeValue, scope) {
          zc::Maybe<ReceiverSignature> noReceiver;
          zc::Maybe<identity::SemanticTypeId> noRaises;
          zc::Maybe<ExternAbi> noAbi;
          built.add(BuiltSignature{
              SemanticSignature{
                  definition.definition, definitionKind, zc::mv(signatureScopeValue),
                  zc::Vector<SignatureModifier>(), zc::Vector<NormalizedAttributeFact>(),
                  SemanticSignaturePayload(CallableSignature{
                      zc::Vector<GenericParameterSignature>(), zc::mv(noReceiver),
                      zc::Vector<ParameterSignature>(), success, zc::mv(noRaises), zc::mv(noAbi)}),
                  bound.source.clone()},
              SignatureDefinitionRequirement{definition.definition, definitionKind,
                                             zc::Array<uint8_t>()},
              definition.key.encode()});
        }
      }
    }
  }

  for (size_t index = 1; index < built.size(); ++index) {
    auto current = zc::mv(built[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.definitionKey.asPtr(), built[insertion - 1].definitionKey.asPtr())) {
      built[insertion] = zc::mv(built[insertion - 1]);
      --insertion;
    }
    built[insertion] = zc::mv(current);
  }

  zc::Vector<SemanticSignature> signatures(built.size());
  zc::Vector<SignatureDefinitionRequirement> requirements(built.size());
  for (auto& value : built) {
    auto record = SignatureFactsCanonicalCodec::encodeSignature(
        value.signature, module, input.registries, input.semanticTypes);
    if (record == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module, 0));
    }
    ZC_IF_SOME(bytes, record) { value.requirement.canonicalRecord = zc::mv(bytes); }
    signatures.add(zc::mv(value.signature));
    requirements.add(zc::mv(value.requirement));
  }

  struct BuiltImplHead final {
    ImplHead head;
    zc::Array<uint8_t> record;
  };
  struct BuiltMarkerFact final {
    MarkerFact fact;
    zc::Array<uint8_t> record;
  };
  zc::Vector<BuiltImplHead> builtImplHeads;
  zc::Vector<BuiltMarkerFact> builtMarkerFacts;
  for (const auto& implementation : input.boundModule.definitions().impls()) {
    if (!tree.contains(implementation.node)) {
      return buildReject(
          checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
    }
    const auto& syntax = tree.node(implementation.node);
    if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl) {
      zc::Vector<identity::GenericParameterId> genericParameterIds;
      const ast::NodeId genericNode(syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
      if (tree.contains(genericNode)) {
        const auto& generics = tree.node(genericNode);
        if (generics.kind != ast::SyntaxKind::GenericParams) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              implementation.node.value));
        }
        const ast::NodeList parameters{generics.payload.words[ast::kGenericParamsParamsFirstWord],
                                       generics.payload.words[ast::kGenericParamsParamsSizeWord]};
        if (!tree.contains(parameters) || parameters.size > UINT32_MAX) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              implementation.node.value));
        }
        for (const auto parameter : tree.list(parameters)) {
          auto identity = input.boundModule.definitions().genericParameterAt(parameter);
          if (identity == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                parameter.value));
          }
          ZC_IF_SOME(value, identity) { genericParameterIds.add(value); }
        }
      }
      SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                    genericParameterIds.asPtr());
      struct BuiltConstraint final {
        CanonicalConstraint constraint;
        zc::Array<uint8_t> record;
      };
      zc::Vector<BuiltConstraint> builtConstraints;
      const ast::NodeId whereClause(syntax.payload.words[ast::kStandaloneImplDeclWhereWord]);
      if (tree.contains(whereClause)) {
        const auto& whereSyntax = tree.node(whereClause);
        if (whereSyntax.kind != ast::SyntaxKind::WhereClause) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, whereClause.value));
        }
        const ast::NodeList predicates{whereSyntax.payload.words[ast::kWhereClausePredsFirstWord],
                                       whereSyntax.payload.words[ast::kWhereClausePredsSizeWord]};
        if (!tree.contains(predicates)) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, whereClause.value));
        }
        for (const auto predicate : tree.list(predicates)) {
          if (!tree.contains(predicate)) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, predicate.value));
          }
          const auto& predicateSyntax = tree.node(predicate);
          if (predicateSyntax.kind != ast::SyntaxKind::WherePred ||
              static_cast<ast::WhereBoundKind>(
                  predicateSyntax.payload.words[ast::kWherePredKindWord]) !=
                  ast::WhereBoundKind::Implements) {
            return buildReject(
                checkerInvariant(CheckerInvariantKind::InvalidFact, module, predicate.value));
          }
          auto subject =
              typeBuilder.build(ast::NodeId(predicateSyntax.payload.words[ast::kWherePredTyWord]));
          auto bound = typeBuilder.buildInterface(
              ast::NodeId(predicateSyntax.payload.words[ast::kWherePredBoundWord]));
          if (subject == zc::none || bound == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                predicate.value));
          }
          identity::SemanticTypeId subjectType;
          InterfaceInstantiation interface{identity::DefId(),
                                           zc::Vector<identity::SemanticTypeId>()};
          ZC_IF_SOME(value, subject) { subjectType = value.type; }
          ZC_IF_SOME(value, bound) { interface = zc::mv(value); }
          auto shape = input.markerShapes.shape(interface.interface);
          if (shape == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                                predicate.value));
          }
          zc::Maybe<CanonicalConstraint> constraint;
          ZC_IF_SOME(value, shape) {
            if (value == InterfaceMarkerShape::GenericMarkerShape) {
              return buildReject(
                  checkerInvariant(CheckerInvariantKind::InvalidFact, module, predicate.value));
            }
            if (value == InterfaceMarkerShape::ClosedMarker) {
              if (!interface.arguments.empty()) {
                return buildReject(
                    checkerInvariant(CheckerInvariantKind::InvalidFact, module, predicate.value));
              }
              constraint = CanonicalConstraint(
                  MarkerConstraint{subjectType, interface.interface, Polarity::Positive});
            } else {
              constraint =
                  CanonicalConstraint(ImplementsConstraint{subjectType, zc::mv(interface)});
            }
          }
          ZC_IF_SOME(value, constraint) {
            auto record = SignatureFactsCanonicalCodec::encodeConstraint(value, input.registries,
                                                                         input.semanticTypes);
            if (record == zc::none) {
              return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                                  module, predicate.value));
            }
            ZC_IF_SOME(bytes, record) {
              builtConstraints.add(BuiltConstraint{zc::mv(value), zc::mv(bytes)});
            }
          }
        }
      }
      for (size_t index = 1; index < builtConstraints.size(); ++index) {
        auto current = zc::mv(builtConstraints[index]);
        size_t insertion = index;
        while (insertion > 0 &&
               lessBytes(current.record.asPtr(), builtConstraints[insertion - 1].record.asPtr())) {
          builtConstraints[insertion] = zc::mv(builtConstraints[insertion - 1]);
          --insertion;
        }
        builtConstraints[insertion] = zc::mv(current);
      }
      for (size_t index = 1; index < builtConstraints.size(); ++index) {
        if (sameBytes(builtConstraints[index - 1].record.asPtr(),
                      builtConstraints[index].record.asPtr())) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, whereClause.value));
        }
      }
      zc::Vector<CanonicalConstraint> whereConstraints(builtConstraints.size());
      for (auto& constraint : builtConstraints) {
        whereConstraints.add(zc::mv(constraint.constraint));
      }
      const ast::NodeId interfaceNode(syntax.payload.words[ast::kStandaloneImplDeclInterfaceWord]);
      auto interfacePattern = typeBuilder.buildPatternInterface(interfaceNode);
      auto interfaceValue = typeBuilder.buildInterface(interfaceNode);
      auto self =
          typeBuilder.build(ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclForTyWord]));
      if (interfacePattern == zc::none || interfaceValue == zc::none || self == zc::none) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                            implementation.node.value));
      }
      identity::DefId interfaceDefinition;
      ZC_IF_SOME(value, interfaceValue) { interfaceDefinition = value.interface; }
      bool interfaceSuppressed = false;
      for (const auto failed : failedInterfaces) {
        if (failed == interfaceDefinition) {
          interfaceSuppressed = true;
          break;
        }
      }
      if (interfaceSuppressed) continue;
      auto markerShape = input.markerShapes.shape(interfaceDefinition);
      if (markerShape == zc::none) {
        return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                            implementation.node.value));
      }
      bool markerOnlyTarget = false;
      ZC_IF_SOME(shape, markerShape) {
        if (shape == InterfaceMarkerShape::GenericMarkerShape) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              implementation.node.value));
        }
        markerOnlyTarget = shape == InterfaceMarkerShape::ClosedMarker;
      }
      if (markerOnlyTarget) {
        auto failure =
            signatureSourceFailure(SignatureSourceDiagnostic::MarkerInterfaceRequiresBodylessImpl,
                                   input.boundModule, implementation.node, interfaceNode);
        if (failure == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              implementation.node.value));
        }
        ZC_IF_SOME(value, failure) { sourceFailures.add(zc::mv(value)); }
        continue;
      }
      struct BuiltAssociatedBinding final {
        AssociatedTypeBindingData binding;
        zc::Array<uint8_t> key;
      };
      zc::Vector<BuiltAssociatedBinding> builtAssociatedBindings;
      const ast::NodeId membersNode(syntax.payload.words[ast::kStandaloneImplDeclMembersIdWord]);
      if (!tree.contains(membersNode) ||
          tree.node(membersNode).kind != ast::SyntaxKind::ClassMemberList) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
      }
      const auto& membersSyntax = tree.node(membersNode);
      const ast::NodeList members{
          membersSyntax.payload.words[ast::kClassMemberListMembersFirstWord],
          membersSyntax.payload.words[ast::kClassMemberListMembersSizeWord]};
      if (!tree.contains(members)) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, membersNode.value));
      }
      for (const auto member : tree.list(members)) {
        if (!tree.contains(member)) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::InvalidFact, module, member.value));
        }
        const auto& memberSyntax = tree.node(member);
        if (memberSyntax.kind != ast::SyntaxKind::AssociatedTypeDecl) continue;
        auto localDefinition = input.boundModule.definitions().definitionAt(member);
        if (localDefinition == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, member.value));
        }
        zc::Maybe<const binder::DefinitionFact&> localFact;
        ZC_IF_SOME(value, localDefinition) {
          localFact = findDefinitionFact(input.boundModule.bindings(), value);
        }
        if (localFact == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, member.value));
        }
        zc::Maybe<identity::DefId> associated;
        ZC_IF_SOME(value, localFact) {
          associated =
              directAssociatedType(input.boundModule.bindings(), interfaceDefinition, value.name);
        }
        const ast::NodeId target(memberSyntax.payload.words[ast::kAssociatedTypeDeclDefaultTyWord]);
        auto targetType = typeBuilder.build(target);
        if (associated == zc::none || targetType == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, member.value));
        }
        identity::DefId associatedDefinition;
        identity::SemanticTypeId type;
        ZC_IF_SOME(value, associated) { associatedDefinition = value; }
        ZC_IF_SOME(value, targetType) { type = value.type; }
        auto associatedKey = input.registries.definitions().lookup(associatedDefinition);
        if (associatedKey == zc::none) {
          return buildReject(
              checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module, member.value));
        }
        ZC_IF_SOME(value, associatedKey) {
          identity::CanonicalEncoder encoder;
          value.encode(encoder);
          builtAssociatedBindings.add(BuiltAssociatedBinding{
              AssociatedTypeBindingData{associatedDefinition, type}, encoder.finish()});
        }
      }
      for (size_t index = 1; index < builtAssociatedBindings.size(); ++index) {
        auto current = zc::mv(builtAssociatedBindings[index]);
        size_t insertion = index;
        while (insertion > 0 &&
               lessBytes(current.key.asPtr(), builtAssociatedBindings[insertion - 1].key.asPtr())) {
          builtAssociatedBindings[insertion] = zc::mv(builtAssociatedBindings[insertion - 1]);
          --insertion;
        }
        builtAssociatedBindings[insertion] = zc::mv(current);
      }
      for (size_t index = 1; index < builtAssociatedBindings.size(); ++index) {
        if (sameBytes(builtAssociatedBindings[index - 1].key.asPtr(),
                      builtAssociatedBindings[index].key.asPtr())) {
          return buildReject(checkerInvariant(CheckerInvariantKind::InvalidFact, module,
                                              implementation.node.value));
        }
      }
      zc::Vector<AssociatedTypeBindingData> associatedBindings(builtAssociatedBindings.size());
      for (auto& binding : builtAssociatedBindings) { associatedBindings.add(binding.binding); }
      PatternInterfaceInstantiation patternInterface{interfaceDefinition,
                                                     zc::Vector<TypeKeyPattern>()};
      ZC_IF_SOME(value, interfacePattern) { patternInterface = zc::mv(value); }
      BuiltSourceType selfValue{identity::SemanticTypeId(),
                                TypeKeyPattern::primitive(PrimitiveKind::Unit)};
      ZC_IF_SOME(value, self) { selfValue = zc::mv(value); }
      ImplPattern completePattern{zc::mv(patternInterface), selfValue.pattern.clone()};
      auto patternKey =
          SignatureFactsCanonicalCodec::makeImplPatternKey(completePattern, input.registries);
      auto head = typeHead(selfValue.type, input.semanticTypes);
      if (patternKey == zc::none || head == zc::none) {
        return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module,
                                            implementation.node.value));
      }
      zc::Maybe<ImplPatternKey> retainedPattern;
      ZC_IF_SOME(value, patternKey) { retainedPattern = zc::mv(value); }
      zc::Maybe<CanonicalTypeHead> retainedHead;
      ZC_IF_SOME(value, head) { retainedHead = zc::mv(value); }
      zc::Vector<identity::GenericParameterKey> genericParameterKeys(genericParameterIds.size());
      for (const auto parameter : genericParameterIds) {
        auto key = input.registries.genericParameters().lookup(parameter);
        if (key == zc::none) {
          return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                              implementation.node.value));
        }
        ZC_IF_SOME(value, key) { genericParameterKeys.add(value.clone()); }
      }
      ZC_IF_SOME(patternValue, retainedPattern) {
        ZC_IF_SOME(headValue, retainedHead) {
          ImplHead implHead{implementation.authority,
                            zc::mv(patternValue),
                            selfValue.type,
                            zc::mv(headValue),
                            zc::mv(genericParameterKeys),
                            zc::mv(whereConstraints),
                            syntax.payload.words[ast::kStandaloneImplDeclIsUnsafeWord] != 0
                                ? ImplSafety::UnsafeAssertion
                                : ImplSafety::Safe,
                            zc::mv(associatedBindings),
                            implementation.source.clone()};
          auto record = SignatureFactsCanonicalCodec::encodeImplHead(implHead, input.registries,
                                                                     input.semanticTypes);
          if (record == zc::none) {
            return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch,
                                                module, implementation.node.value));
          }
          ZC_IF_SOME(value, record) {
            builtImplHeads.add(BuiltImplHead{zc::mv(implHead), zc::mv(value)});
          }
        }
      }
      continue;
    }
    if (syntax.kind != ast::SyntaxKind::MarkerImpl) {
      return buildReject(
          checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
    }
    SourceTypeBuilder typeBuilder(input.boundModule, input.registries, input.semanticTypes,
                                  zc::ArrayPtr<const identity::GenericParameterId>());
    const ast::NodeId markerPath(syntax.payload.words[ast::kMarkerImplMarkerPathWord]);
    auto markerDefinition = typeBuilder.definitionAtPath(markerPath);
    auto self = typeBuilder.build(ast::NodeId(syntax.payload.words[ast::kMarkerImplForTyWord]));
    if (markerDefinition == zc::none || self == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                          implementation.node.value));
    }
    identity::DefId marker;
    ZC_IF_SOME(value, markerDefinition) { marker = value; }
    bool interfaceSuppressed = false;
    for (const auto failed : failedInterfaces) {
      if (failed == marker) {
        interfaceSuppressed = true;
        break;
      }
    }
    if (interfaceSuppressed) continue;
    auto markerShape = input.markerShapes.shape(marker);
    if (markerShape == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::MissingRequiredFact, module,
                                          implementation.node.value));
    }
    bool behaviorTarget = false;
    ZC_IF_SOME(shape, markerShape) {
      if (shape == InterfaceMarkerShape::GenericMarkerShape) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
      }
      behaviorTarget = shape == InterfaceMarkerShape::Behavior;
    }
    if (behaviorTarget) {
      auto failure =
          signatureSourceFailure(SignatureSourceDiagnostic::BehaviorInterfaceRequiresImplBody,
                                 input.boundModule, implementation.node, markerPath);
      if (failure == zc::none) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
      }
      ZC_IF_SOME(value, failure) { sourceFailures.add(zc::mv(value)); }
      continue;
    }
    const bool negated = syntax.payload.words[ast::kMarkerImplIsNegatedWord] != 0;
    const bool unsafe = syntax.payload.words[ast::kMarkerImplIsUnsafeWord] != 0;
    if (!negated && !unsafe) {
      auto failure =
          signatureSourceFailure(SignatureSourceDiagnostic::PositiveMarkerImplRequiresUnsafe,
                                 input.boundModule, implementation.node, implementation.node);
      if (failure == zc::none) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
      }
      ZC_IF_SOME(value, failure) { sourceFailures.add(zc::mv(value)); }
      continue;
    }
    if (negated && unsafe) {
      return buildReject(
          checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
    }
    identity::SemanticTypeId subject;
    ZC_IF_SOME(value, self) { subject = value.type; }
    bool builtinConflict = false;
    auto subjectLookup = input.semanticTypes.get(subject);
    if (subjectLookup.is<identity::IdentityInvariant>()) {
      return buildReject(zc::mv(subjectLookup.get<identity::IdentityInvariant>()));
    }
    ZC_IF_SOME(policy, input.markerPolicies.policy(marker)) {
      const auto& subjectData = subjectLookup.get<type::SemanticTypeLookup>().data();
      if (subjectData.is<type::semantic::PrimitiveTypeData>()) {
        const auto primitive = subjectData.get<type::semantic::PrimitiveTypeData>().kind;
        for (const auto authorized : policy.builtinPrimitives) {
          if (authorized == primitive) {
            builtinConflict = true;
            break;
          }
        }
      }
    }
    if (builtinConflict) {
      auto failure =
          signatureSourceFailure(SignatureSourceDiagnostic::ExplicitImplConflictsWithBuiltinMarker,
                                 input.boundModule, implementation.node, implementation.node);
      if (failure == zc::none) {
        return buildReject(
            checkerInvariant(CheckerInvariantKind::InvalidFact, module, implementation.node.value));
      }
      ZC_IF_SOME(value, failure) { sourceFailures.add(zc::mv(value)); }
      continue;
    }
    zc::Maybe<identity::SourceSpan> declarationSpan = implementation.source.clone();
    MarkerFact markerFact{
        MarkerFactKey{marker, subject}, negated ? Polarity::Negative : Polarity::Positive,
        MarkerEvidence(ExplicitMarkerEvidence{implementation.authority}), zc::mv(declarationSpan)};
    auto record = SignatureFactsCanonicalCodec::encodeMarkerFact(markerFact, input.registries,
                                                                 input.semanticTypes);
    if (record == zc::none) {
      return buildReject(checkerInvariant(CheckerInvariantKind::CanonicalCodecMismatch, module,
                                          implementation.node.value));
    }
    ZC_IF_SOME(value, record) {
      builtMarkerFacts.add(BuiltMarkerFact{zc::mv(markerFact), zc::mv(value)});
    }
  }

  for (size_t index = 1; index < sourceFailures.size(); ++index) {
    auto current = zc::mv(sourceFailures[index]);
    size_t insertion = index;
    const auto lessFailure = [](const SignatureSourceFailureRef& left,
                                const SignatureSourceFailureRef& right) {
      if (left.emitterOrdinal.ownerSchemaPreorder != right.emitterOrdinal.ownerSchemaPreorder) {
        return left.emitterOrdinal.ownerSchemaPreorder < right.emitterOrdinal.ownerSchemaPreorder;
      }
      if (left.emitterOrdinal.siteSchemaPreorder != right.emitterOrdinal.siteSchemaPreorder) {
        return left.emitterOrdinal.siteSchemaPreorder < right.emitterOrdinal.siteSchemaPreorder;
      }
      if (left.emitterOrdinal.itemOrdinal != right.emitterOrdinal.itemOrdinal) {
        return left.emitterOrdinal.itemOrdinal < right.emitterOrdinal.itemOrdinal;
      }
      return static_cast<uint16_t>(left.diagnostic) < static_cast<uint16_t>(right.diagnostic);
    };
    while (insertion > 0 && lessFailure(current, sourceFailures[insertion - 1])) {
      sourceFailures[insertion] = zc::mv(sourceFailures[insertion - 1]);
      --insertion;
    }
    sourceFailures[insertion] = zc::mv(current);
  }
  if (!sourceFailures.empty()) {
    return SignatureFactsSourceRejected{zc::mv(sourceFailures), zc::mv(advisories)};
  }

  const auto sortImpls = [&]() {
    for (size_t index = 1; index < builtImplHeads.size(); ++index) {
      auto current = zc::mv(builtImplHeads[index]);
      size_t insertion = index;
      while (insertion > 0 &&
             lessBytes(current.record.asPtr(), builtImplHeads[insertion - 1].record.asPtr())) {
        builtImplHeads[insertion] = zc::mv(builtImplHeads[insertion - 1]);
        --insertion;
      }
      builtImplHeads[insertion] = zc::mv(current);
    }
  };
  const auto sortMarkers = [&]() {
    for (size_t index = 1; index < builtMarkerFacts.size(); ++index) {
      auto current = zc::mv(builtMarkerFacts[index]);
      size_t insertion = index;
      while (insertion > 0 &&
             lessBytes(current.record.asPtr(), builtMarkerFacts[insertion - 1].record.asPtr())) {
        builtMarkerFacts[insertion] = zc::mv(builtMarkerFacts[insertion - 1]);
        --insertion;
      }
      builtMarkerFacts[insertion] = zc::mv(current);
    }
  };
  sortImpls();
  sortMarkers();
  zc::Vector<ImplHead> implHeads(builtImplHeads.size());
  zc::Vector<ImplHeadRequirement> implRequirements(builtImplHeads.size());
  for (auto& value : builtImplHeads) {
    implRequirements.add(ImplHeadRequirement{value.head.impl, zc::heapArray(value.record.asPtr())});
    implHeads.add(zc::mv(value.head));
  }
  zc::Vector<MarkerFact> markerFacts(builtMarkerFacts.size());
  zc::Vector<MarkerFactRequirement> markerRequirements(builtMarkerFacts.size());
  for (auto& value : builtMarkerFacts) {
    markerRequirements.add(
        MarkerFactRequirement{value.fact.key, zc::heapArray(value.record.asPtr())});
    markerFacts.add(zc::mv(value.fact));
  }
  auto candidate = SignatureFactsCandidate{context,
                                           input.boundModule.semanticFingerprint().clone(),
                                           module,
                                           input.boundModule.parsedModule().contentDigest(),
                                           input.boundModule.parsedModule().receipt(),
                                           input.boundModule.bindingSurface().revision(),
                                           input.markerPolicies.revision(),
                                           zc::mv(signatures),
                                           zc::mv(implHeads),
                                           zc::mv(markerFacts)};
  return SignatureFactsVerifier::verify(
      zc::mv(candidate),
      SignatureFactsVerificationInput{
          context, input.boundModule.semanticFingerprint(), module,
          input.boundModule.parsedModule().source(),
          input.boundModule.parsedModule().contentDigest(),
          input.boundModule.parsedModule().receipt(), input.boundModule.bindingSurface().revision(),
          input.markerPolicies.revision(), sourceSignatureCensus.asPtr(), sourceImplCensus.asPtr(),
          requirements.asPtr(), implRequirements.asPtr(), markerRequirements.asPtr(),
          input.registries, input.semanticTypes});
}

}  // namespace zomlang::compiler::checker::signature
