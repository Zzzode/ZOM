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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/identity/identity-invariant.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::identity {
class CanonicalEncoder;
}

namespace zomlang::compiler::driver::module_graph_query {
class CheckerBoundModuleView;
}

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
}

namespace zomlang::compiler::checker {
class CheckerIdentityAuthority;
}

namespace zomlang::compiler::checker::signature {

/// \brief Resolves one closed source type under the bound module's verified identities.
ZC_NODISCARD zc::Maybe<identity::SemanticTypeId> resolveClosedSourceType(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId typeSyntax);

using InterfaceInstantiation = type::semantic::InterfaceInstantiation;
using AssociatedTypeBindingData = type::semantic::AssociatedTypeBindingData;
using FieldPresence = type::semantic::FieldPresence;
using Mutability = type::semantic::Mutability;
using PrimitiveKind = type::semantic::PrimitiveKind;

enum class MemberVisibility : uint8_t { Public = 0x01, Protected = 0x02, Private = 0x03 };
enum class ParameterMode : uint8_t {
  Value = 0x01,
  Move = 0x02,
  SharedReference = 0x03,
  MutableReference = 0x04
};
enum class ReceiverMode : uint8_t { Static = 0x01, Shared = 0x02, Mutable = 0x03, Move = 0x04 };
enum class SignatureModifier : uint8_t {
  Static = 0x01,
  Readonly = 0x02,
  Mutating = 0x03,
  Override = 0x04,
  Abstract = 0x05
};
enum class ExternAbi : uint8_t { Cdecl = 0x01, Stdcall = 0x02, ZomNative = 0x03 };
enum class NormalizedAttribute : uint8_t { MoveReceiver = 0x01 };

struct ModuleDefinitionSignatureScope final {};
struct MemberSignatureScope final {
  identity::DefId owner;
  MemberVisibility visibility;
};
struct EnclosedSignatureScope final {
  identity::DefId owner;
};

/// \brief Closed canonical signature-scope value.
class SignatureScope final {
public:
  explicit SignatureScope(ModuleDefinitionSignatureScope value) noexcept : value(value) {}
  explicit SignatureScope(MemberSignatureScope value) noexcept : value(value) {}
  explicit SignatureScope(EnclosedSignatureScope value) noexcept : value(value) {}
  SignatureScope(SignatureScope&&) noexcept = default;
  SignatureScope& operator=(SignatureScope&&) noexcept = default;
  ZC_DISALLOW_COPY(SignatureScope);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD SignatureScope clone() const;

private:
  zc::OneOf<ModuleDefinitionSignatureScope, MemberSignatureScope, EnclosedSignatureScope> value;
};

struct NormalizedAttributeFact final {
  identity::DefId target;
  NormalizedAttribute attribute;
  identity::SourceSpan sourceSpan;
};

struct GenericParameterSignature final {
  identity::GenericParameterKey parameter;
  uint32_t index;
  zc::Vector<InterfaceInstantiation> bounds;
  zc::Vector<identity::DefId> markerBounds;
  zc::Maybe<identity::SemanticTypeId> defaultType;
};

struct ReceiverSignature final {
  identity::CallableParameterKey parameter;
  ReceiverMode mode;
};

struct ParameterSignature final {
  identity::CallableParameterKey parameter;
  identity::SemanticIdentifier label;
  identity::SemanticTypeId type;
  ParameterMode mode;
  bool hasDefault;
};

enum class IntegerSign : uint8_t { NonNegative = 0x01, Negative = 0x02 };

struct CanonicalInteger final {
  IntegerSign sign;
  zc::Array<uint8_t> magnitude;
};

class CanonicalConstValue;

enum class CanonicalFloatWidth : uint8_t { Bits32 = 0x01, Bits64 = 0x02 };

struct CanonicalFloat final {
  CanonicalFloatWidth width;
  uint64_t bits;
};

struct CanonicalEnumerationView final {
  identity::DefId variant;
  zc::ArrayPtr<const CanonicalConstValue> payload;
};

struct ConstObjectField;

enum class CanonicalConstValueTag : uint8_t {
  Integer = 0x01,
  Float = 0x02,
  Bool = 0x03,
  Char = 0x04,
  String = 0x05,
  Null = 0x06,
  Unit = 0x07,
  Tuple = 0x08,
  Array = 0x09,
  Object = 0x0a,
  Enum = 0x0b
};

/// \brief Recursive canonical constant value without host-width numeric identity.
class CanonicalConstValue final {
public:
  static CanonicalConstValue integer(CanonicalInteger&& value);
  static CanonicalConstValue float32(uint32_t bits);
  static CanonicalConstValue float64(uint64_t bits);
  static CanonicalConstValue boolean(bool value);
  static CanonicalConstValue character(uint32_t scalar);
  static CanonicalConstValue string(zc::Array<uint8_t>&& bytes);
  static CanonicalConstValue null();
  static CanonicalConstValue unit();
  static CanonicalConstValue tuple(zc::Vector<CanonicalConstValue>&& values);
  static CanonicalConstValue array(zc::Vector<CanonicalConstValue>&& values);
  static CanonicalConstValue object(zc::Vector<ConstObjectField>&& fields);
  static CanonicalConstValue enumeration(identity::DefId variant,
                                         zc::Vector<CanonicalConstValue>&& payload);

  ~CanonicalConstValue() noexcept(false);
  CanonicalConstValue(CanonicalConstValue&&) noexcept;
  CanonicalConstValue& operator=(CanonicalConstValue&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalConstValue);

  ZC_NODISCARD CanonicalConstValueTag tag() const noexcept;
  ZC_NODISCARD CanonicalConstValue clone() const;
  ZC_NODISCARD zc::Maybe<const CanonicalInteger&> integerValue() const noexcept;
  ZC_NODISCARD zc::Maybe<CanonicalFloat> floatValue() const noexcept;
  ZC_NODISCARD zc::Maybe<bool> booleanValue() const noexcept;
  ZC_NODISCARD zc::Maybe<uint32_t> characterValue() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> stringValue() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const CanonicalConstValue>> elements() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const ConstObjectField>> objectFields() const noexcept;
  ZC_NODISCARD zc::Maybe<CanonicalEnumerationView> enumerationValue() const noexcept;
  /// \brief Append every definition identity referenced by this recursive value.
  void appendReferencedDefinitions(zc::Vector<identity::DefId>& output) const;

private:
  struct Impl;
  explicit CanonicalConstValue(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class SignatureFactsCanonicalEncoder;
  friend class SignatureFactsCanonicalCodec;
};

struct ConstObjectField final {
  identity::SemanticIdentifier name;
  CanonicalConstValue value;
};

struct CallableSignature final {
  zc::Vector<GenericParameterSignature> genericParameters;
  zc::Maybe<ReceiverSignature> receiver;
  zc::Vector<ParameterSignature> parameters;
  identity::SemanticTypeId success;
  zc::Maybe<identity::SemanticTypeId> raises;
  zc::Maybe<ExternAbi> abi;
};

struct NominalSignature final {
  zc::Vector<GenericParameterSignature> genericParameters;
  zc::Maybe<identity::SemanticTypeId> base;
  zc::Vector<InterfaceInstantiation> interfaces;
  zc::Vector<identity::DefId> fields;
  zc::Vector<identity::DefId> variants;
  zc::Vector<identity::DefId> members;
};

struct UnsafeSuperinterfaceCause final {
  identity::DefId interface;
};
struct GenericMethodCause final {
  identity::DefId method;
};
struct ReturnsSelfCause final {
  identity::DefId method;
};
struct MovesSelfCause final {
  identity::DefId method;
};
struct StaticMethodCause final {
  identity::DefId method;
};
struct GenericAssociatedTypeCause final {
  identity::DefId associated;
};
struct UnsizedParameterCause final {
  identity::DefId method;
  identity::CallableParameterKey parameter;
  identity::SemanticTypeId type;
};

/// \brief Closed intrinsic object-safety cause with RFC tag order.
class ObjectSafetyCause final {
public:
  explicit ObjectSafetyCause(UnsafeSuperinterfaceCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(GenericMethodCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(ReturnsSelfCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(MovesSelfCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(StaticMethodCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(GenericAssociatedTypeCause value) noexcept : value(value) {}
  explicit ObjectSafetyCause(UnsizedParameterCause&& value) noexcept : value(zc::mv(value)) {}
  ObjectSafetyCause(ObjectSafetyCause&&) noexcept = default;
  ObjectSafetyCause& operator=(ObjectSafetyCause&&) noexcept = default;
  ZC_DISALLOW_COPY(ObjectSafetyCause);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD ObjectSafetyCause clone() const;

private:
  zc::OneOf<UnsafeSuperinterfaceCause, GenericMethodCause, ReturnsSelfCause, MovesSelfCause,
            StaticMethodCause, GenericAssociatedTypeCause, UnsizedParameterCause>
      value;
};

struct InterfaceSignature final {
  zc::Vector<GenericParameterSignature> genericParameters;
  zc::Vector<InterfaceInstantiation> parents;
  zc::Vector<identity::DefId> members;
  zc::Vector<identity::DefId> associatedTypes;
  bool markerOnly;
  zc::Vector<ObjectSafetyCause> objectSafetyCauses;
};

struct TypeAliasSignature final {
  zc::Vector<GenericParameterSignature> genericParameters;
  identity::SemanticTypeId target;
};

struct AssociatedTypeSignature final {
  zc::Vector<GenericParameterSignature> genericParameters;
  zc::Vector<InterfaceInstantiation> bounds;
  zc::Vector<identity::DefId> markerBounds;
  zc::Maybe<identity::SemanticTypeId> defaultType;
};

struct ValueSignature final {
  identity::SemanticTypeId type;
  Mutability mutability;
  bool hasInitializer;
  zc::Maybe<CanonicalConstValue> constantValue;
  zc::Maybe<ExternAbi> abi;
};

struct EnumVariantSignature final {
  zc::Vector<identity::SemanticTypeId> payload;
  zc::Maybe<CanonicalInteger> discriminant;
};

/// \brief Closed canonical semantic-signature payload.
class SemanticSignaturePayload final {
public:
  explicit SemanticSignaturePayload(CallableSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(NominalSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(InterfaceSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(TypeAliasSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(AssociatedTypeSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(ValueSignature&& value) : value(zc::mv(value)) {}
  explicit SemanticSignaturePayload(EnumVariantSignature&& value) : value(zc::mv(value)) {}

  SemanticSignaturePayload(SemanticSignaturePayload&&) noexcept = default;
  SemanticSignaturePayload& operator=(SemanticSignaturePayload&&) noexcept = default;
  ZC_DISALLOW_COPY(SemanticSignaturePayload);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD SemanticSignaturePayload clone() const;

private:
  zc::OneOf<CallableSignature, NominalSignature, InterfaceSignature, TypeAliasSignature,
            AssociatedTypeSignature, ValueSignature, EnumVariantSignature>
      value;
};

struct SemanticSignature final {
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  SignatureScope scope;
  zc::Vector<SignatureModifier> modifiers;
  zc::Vector<NormalizedAttributeFact> attributes;
  SemanticSignaturePayload payload;
  identity::SourceSpan declarationSpan;

  ZC_NODISCARD SemanticSignature clone() const;
};

enum class Polarity : uint8_t { Positive = 0x01, Negative = 0x02 };
enum class ImplSafety : uint8_t { Safe = 0x01, UnsafeAssertion = 0x02 };

struct PatternObjectField;
struct PatternFunctionType;
struct PatternInterfaceInstantiation;
struct PatternExistentialInterface;
struct PatternAssociatedTypeBinding;
struct PatternExistentialType;

enum class TypeKeyPatternTag : uint8_t {
  Primitive = 0x01,
  Tuple = 0x02,
  Object = 0x03,
  DynamicArray = 0x04,
  Slice = 0x05,
  FixedArray = 0x06,
  Function = 0x07,
  Nominal = 0x08,
  TypeParameter = 0x09,
  Union = 0x0a,
  Intersection = 0x0b,
  Reference = 0x0c,
  RawPointer = 0x0d,
  Existential = 0x0e,
  InterfaceBound = 0x0f,
  InterfaceSelf = 0x10,
  Parameter = 0x11
};

/// \brief Closed recursive RFC 0015 type-key pattern algebra.
class TypeKeyPattern final {
public:
  ZC_NODISCARD static TypeKeyPattern primitive(PrimitiveKind kind);
  ZC_NODISCARD static TypeKeyPattern tuple(zc::Vector<TypeKeyPattern>&& elements);
  ZC_NODISCARD static TypeKeyPattern object(zc::Vector<PatternObjectField>&& fields);
  ZC_NODISCARD static TypeKeyPattern dynamicArray(TypeKeyPattern&& element);
  ZC_NODISCARD static TypeKeyPattern slice(TypeKeyPattern&& element);
  ZC_NODISCARD static TypeKeyPattern fixedArray(TypeKeyPattern&& element, uint64_t length);
  ZC_NODISCARD static TypeKeyPattern function(PatternFunctionType&& function);
  ZC_NODISCARD static TypeKeyPattern nominal(identity::DefId definition,
                                             zc::Vector<TypeKeyPattern>&& arguments);
  ZC_NODISCARD static TypeKeyPattern typeParameter(identity::GenericParameterKey&& parameter);
  ZC_NODISCARD static TypeKeyPattern unionOf(zc::Vector<TypeKeyPattern>&& alternatives);
  ZC_NODISCARD static TypeKeyPattern intersection(zc::Vector<TypeKeyPattern>&& conjuncts);
  ZC_NODISCARD static TypeKeyPattern reference(Mutability mutability, TypeKeyPattern&& referent);
  ZC_NODISCARD static TypeKeyPattern rawPointer(Mutability mutability, TypeKeyPattern&& pointee);
  ZC_NODISCARD static TypeKeyPattern existential(PatternExistentialType&& existential);
  ZC_NODISCARD static TypeKeyPattern interfaceBound(PatternInterfaceInstantiation&& interface);
  ZC_NODISCARD static TypeKeyPattern interfaceSelf(identity::DefId interface);
  ZC_NODISCARD static TypeKeyPattern parameter(uint32_t index);

  ~TypeKeyPattern() noexcept(false);
  TypeKeyPattern(TypeKeyPattern&&) noexcept;
  TypeKeyPattern& operator=(TypeKeyPattern&&) noexcept;
  ZC_DISALLOW_COPY(TypeKeyPattern);

  ZC_NODISCARD TypeKeyPatternTag tag() const noexcept;
  ZC_NODISCARD TypeKeyPattern clone() const;

private:
  struct Impl;
  explicit TypeKeyPattern(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class SignatureFactsCanonicalCodec;
};

struct PatternObjectField final {
  identity::SemanticIdentifier name;
  TypeKeyPattern type;
  Mutability mutability;
  FieldPresence presence;
};

struct PatternFunctionType final {
  zc::Vector<TypeKeyPattern> parameters;
  TypeKeyPattern success;
  zc::Maybe<TypeKeyPattern> raises;
};

struct PatternInterfaceInstantiation final {
  identity::DefId interface;
  zc::Vector<TypeKeyPattern> arguments;
};

struct PatternExistentialInterface final {
  identity::DefId definition;
  zc::Vector<TypeKeyPattern> arguments;
};

struct PatternAssociatedTypeBinding final {
  identity::DefId associated;
  TypeKeyPattern type;
};

struct PatternExistentialType final {
  PatternExistentialInterface principal;
  zc::Vector<PatternExistentialInterface> additionalInterfaces;
  zc::Vector<identity::DefId> markers;
  zc::Vector<PatternAssociatedTypeBinding> associatedBindings;
};

struct ImplPattern final {
  PatternInterfaceInstantiation interface;
  TypeKeyPattern self;

  ZC_NODISCARD ImplPattern clone() const;
};

/// \brief Canonical framed `zom.type-key-pattern` key.
class TypeKeyPatternKey final {
public:
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;
  ZC_NODISCARD TypeKeyPatternKey clone() const;

private:
  TypeKeyPatternKey(zc::Array<uint8_t>&& bytes, TypeKeyPattern&& decoded) noexcept;
  zc::Array<uint8_t> value;
  zc::Own<TypeKeyPattern> decoded;
  friend class SignatureFactsCanonicalCodec;
};

/// \brief Canonical framed `zom.impl-pattern` key.
class ImplPatternKey final {
public:
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept;
  ZC_NODISCARD ImplPatternKey clone() const;

private:
  ImplPatternKey(zc::Array<uint8_t>&& bytes, ImplPattern&& decoded) noexcept;
  zc::Array<uint8_t> value;
  zc::Own<ImplPattern> decoded;
  friend class SignatureFactsCanonicalCodec;
};

struct ProjectionKey final {
  identity::SemanticTypeId subject;
  InterfaceInstantiation interface;
  identity::DefId associated;
};

struct ImplementsConstraint final {
  identity::SemanticTypeId subject;
  InterfaceInstantiation interface;
};
struct MarkerConstraint final {
  identity::SemanticTypeId subject;
  identity::DefId marker;
  Polarity polarity;
};
struct ProjectionEqualsConstraint final {
  ProjectionKey projection;
  identity::SemanticTypeId type;
};

class CanonicalConstraint final {
public:
  explicit CanonicalConstraint(ImplementsConstraint&& value) : value(zc::mv(value)) {}
  explicit CanonicalConstraint(MarkerConstraint value) : value(value) {}
  explicit CanonicalConstraint(ProjectionEqualsConstraint&& value) : value(zc::mv(value)) {}
  CanonicalConstraint(CanonicalConstraint&&) noexcept = default;
  CanonicalConstraint& operator=(CanonicalConstraint&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalConstraint);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD CanonicalConstraint clone() const;

private:
  zc::OneOf<ImplementsConstraint, MarkerConstraint, ProjectionEqualsConstraint> value;
};

struct BlanketTypeHead final {};
struct PrimitiveTypeHead final {
  PrimitiveKind primitive;
};
struct TupleTypeHead final {
  uint32_t arity;
};
struct ObjectTypeHead final {};
struct DynamicArrayTypeHead final {};
struct SliceTypeHead final {};
struct FixedArrayTypeHead final {};
struct FunctionTypeHead final {
  uint32_t arity;
  bool hasRaises;
};
struct NominalTypeHead final {
  identity::DefId definition;
};
struct UnionTypeHead final {
  uint32_t arity;
};
struct IntersectionTypeHead final {
  uint32_t arity;
};
struct ReferenceTypeHead final {
  Mutability mutability;
};
struct RawPointerTypeHead final {
  Mutability mutability;
};
struct ExistentialTypeHead final {
  identity::DefId interface;
};

class CanonicalTypeHead final {
public:
  explicit CanonicalTypeHead(BlanketTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(PrimitiveTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(TupleTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(ObjectTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(DynamicArrayTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(SliceTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(FixedArrayTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(FunctionTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(NominalTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(UnionTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(IntersectionTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(ReferenceTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(RawPointerTypeHead value) noexcept : value(value) {}
  explicit CanonicalTypeHead(ExistentialTypeHead value) noexcept : value(value) {}
  CanonicalTypeHead(CanonicalTypeHead&&) noexcept = default;
  CanonicalTypeHead& operator=(CanonicalTypeHead&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalTypeHead);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD CanonicalTypeHead clone() const;

private:
  zc::OneOf<BlanketTypeHead, PrimitiveTypeHead, TupleTypeHead, ObjectTypeHead, DynamicArrayTypeHead,
            SliceTypeHead, FixedArrayTypeHead, FunctionTypeHead, NominalTypeHead, UnionTypeHead,
            IntersectionTypeHead, ReferenceTypeHead, RawPointerTypeHead, ExistentialTypeHead>
      value;
};

struct ImplHead final {
  identity::ImplId impl;
  ImplPatternKey pattern;
  identity::SemanticTypeId selfType;
  CanonicalTypeHead head;
  zc::Vector<identity::GenericParameterKey> genericParameters;
  zc::Vector<CanonicalConstraint> whereConstraints;
  ImplSafety safety;
  zc::Vector<AssociatedTypeBindingData> associatedBindings;
  identity::SourceSpan declarationSpan;

  ZC_NODISCARD ImplHead clone() const;
};

struct TupleElementStep final {
  uint32_t index;
};
struct ObjectFieldStep final {
  identity::SemanticIdentifier name;
};
struct ArrayElementStep final {};
struct NominalFieldStep final {
  identity::DefId field;
};
struct ReferenceReferentStep final {};
struct EnumVariantPayloadStep final {
  identity::DefId variant;
  uint32_t index;
};

class MarkerComponentStep final {
public:
  explicit MarkerComponentStep(TupleElementStep value) noexcept : value(value) {}
  explicit MarkerComponentStep(ObjectFieldStep&& value) : value(zc::mv(value)) {}
  explicit MarkerComponentStep(ArrayElementStep value) noexcept : value(value) {}
  explicit MarkerComponentStep(NominalFieldStep value) noexcept : value(value) {}
  explicit MarkerComponentStep(ReferenceReferentStep value) noexcept : value(value) {}
  explicit MarkerComponentStep(EnumVariantPayloadStep value) noexcept : value(value) {}
  MarkerComponentStep(MarkerComponentStep&&) noexcept = default;
  MarkerComponentStep& operator=(MarkerComponentStep&&) noexcept = default;
  ZC_DISALLOW_COPY(MarkerComponentStep);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD MarkerComponentStep clone() const;

private:
  zc::OneOf<TupleElementStep, ObjectFieldStep, ArrayElementStep, NominalFieldStep,
            ReferenceReferentStep, EnumVariantPayloadStep>
      value;
};

struct MarkerFactKey final {
  identity::DefId marker;
  identity::SemanticTypeId subject;
};

struct MarkerComponentEvidence final {
  zc::Vector<MarkerComponentStep> path;
  identity::SemanticTypeId componentType;
  MarkerFactKey supportingFact;
};

struct ExplicitMarkerEvidence final {
  identity::ImplId impl;
};
struct StructuralMarkerEvidence final {
  zc::Vector<MarkerComponentEvidence> components;
};
struct BuiltinMarkerEvidence final {
  PrimitiveKind primitive;
};

class MarkerEvidence final {
public:
  explicit MarkerEvidence(ExplicitMarkerEvidence value) noexcept : value(value) {}
  explicit MarkerEvidence(StructuralMarkerEvidence&& value) : value(zc::mv(value)) {}
  explicit MarkerEvidence(BuiltinMarkerEvidence value) noexcept : value(value) {}
  MarkerEvidence(MarkerEvidence&&) noexcept = default;
  MarkerEvidence& operator=(MarkerEvidence&&) noexcept = default;
  ZC_DISALLOW_COPY(MarkerEvidence);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD MarkerEvidence clone() const;

private:
  zc::OneOf<ExplicitMarkerEvidence, StructuralMarkerEvidence, BuiltinMarkerEvidence> value;
};

struct MarkerFact final {
  MarkerFactKey key;
  Polarity polarity;
  MarkerEvidence evidence;
  zc::Maybe<identity::SourceSpan> declarationSpan;

  ZC_NODISCARD MarkerFact clone() const;
};

enum class InterfaceMarkerShape : uint8_t {
  Behavior = 0x01,
  GenericMarkerShape = 0x02,
  ClosedMarker = 0x03
};

struct InterfaceMarkerShapeFact final {
  identity::DefId interface;
  InterfaceMarkerShape shape;
};

/// \brief Domain-separated revision of one context-complete marker-shape inventory.
class MarkerShapeInventoryRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<MarkerShapeInventoryRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> shapeRecords);

private:
  explicit MarkerShapeInventoryRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
  friend class MarkerShapeInventoryBuilder;
};

/// \brief Verified context-complete marker-shape classification.
class VerifiedMarkerShapeInventory final {
public:
  ~VerifiedMarkerShapeInventory() noexcept(false);
  VerifiedMarkerShapeInventory(VerifiedMarkerShapeInventory&&) noexcept;
  VerifiedMarkerShapeInventory& operator=(VerifiedMarkerShapeInventory&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedMarkerShapeInventory);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD const MarkerShapeInventoryRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const InterfaceMarkerShapeFact> shapes() const noexcept;
  ZC_NODISCARD zc::Maybe<InterfaceMarkerShape> shape(identity::DefId interface) const noexcept;

private:
  struct Impl;
  explicit VerifiedMarkerShapeInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class MarkerShapeInventoryBuilder;
};

enum class MarkerStructuralSubject : uint8_t {
  Tuple = 0x01,
  Object = 0x02,
  FixedArray = 0x03,
  NominalStruct = 0x04,
  NominalEnum = 0x05
};

struct MarkerPolicyReferenceConfiguration final {
  Mutability mutability;
  zc::Maybe<identity::DefinitionKey> requiredMarker;

  ZC_NODISCARD MarkerPolicyReferenceConfiguration clone() const;
};

struct MarkerPolicyConfigurationEntry final {
  identity::DefinitionKey marker;
  zc::Vector<MarkerStructuralSubject> structuralSubjects;
  zc::Vector<PrimitiveKind> builtinPrimitives;
  zc::Vector<MarkerPolicyReferenceConfiguration> referenceRequirements;
  zc::Vector<Mutability> rawPointerMutabilities;

  ZC_NODISCARD MarkerPolicyConfigurationEntry clone() const;
};

struct MarkerPolicyReferenceRequirement final {
  Mutability mutability;
  zc::Maybe<identity::DefId> requiredMarker;
};

struct MarkerPolicy final {
  zc::Vector<MarkerStructuralSubject> structuralSubjects;
  zc::Vector<PrimitiveKind> builtinPrimitives;
  zc::Vector<MarkerPolicyReferenceRequirement> referenceRequirements;
  zc::Vector<Mutability> rawPointerMutabilities;

  ZC_NODISCARD MarkerPolicy clone() const;
};

struct MarkerPolicyEntry final {
  identity::DefId marker;
  MarkerPolicy policy;
};

/// \brief Canonical compiler-distribution marker policy configuration.
class MarkerPolicyConfiguration final {
public:
  ~MarkerPolicyConfiguration() noexcept(false);
  MarkerPolicyConfiguration(MarkerPolicyConfiguration&&) noexcept;
  MarkerPolicyConfiguration& operator=(MarkerPolicyConfiguration&&) noexcept;
  ZC_DISALLOW_COPY(MarkerPolicyConfiguration);

  ZC_NODISCARD static zc::Maybe<MarkerPolicyConfiguration> from(
      zc::Vector<MarkerPolicyConfigurationEntry>&& entries);
  ZC_NODISCARD static MarkerPolicyConfiguration explicitOnly();
  ZC_NODISCARD const identity::Sha256Digest& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MarkerPolicyConfigurationEntry> entries() const noexcept;

private:
  struct Impl;
  explicit MarkerPolicyConfiguration(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Domain-separated revision of one verified context-bound marker policy registry.
class MarkerPolicyRegistryRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<MarkerPolicyRegistryRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      const identity::Sha256Digest& configurationRevision,
      const MarkerShapeInventoryRevision& shapeInventoryRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> entryRecords);

private:
  explicit MarkerPolicyRegistryRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
  friend class MarkerPolicyRegistryBuilder;
};

/// \brief Verified explicit-only marker policy lineage for one semantic context.
class VerifiedMarkerPolicyRegistry final {
public:
  ~VerifiedMarkerPolicyRegistry() noexcept(false);
  VerifiedMarkerPolicyRegistry(VerifiedMarkerPolicyRegistry&&) noexcept;
  VerifiedMarkerPolicyRegistry& operator=(VerifiedMarkerPolicyRegistry&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedMarkerPolicyRegistry);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& configurationRevision() const noexcept;
  ZC_NODISCARD const MarkerShapeInventoryRevision& shapeInventoryRevision() const noexcept;
  ZC_NODISCARD const MarkerPolicyRegistryRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MarkerPolicyEntry> entries() const noexcept;
  ZC_NODISCARD zc::Maybe<const MarkerPolicy&> policy(identity::DefId marker) const noexcept;

private:
  struct Impl;
  explicit VerifiedMarkerPolicyRegistry(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class MarkerPolicyRegistryBuilder;
};

struct SignatureFactsCandidate final {
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

/// \brief Domain-separated digest of verified local canonical signature facts.
class SignatureFactsRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  /// \brief Computes the normative frame from already-canonical record bytes.
  ZC_NODISCARD static zc::Maybe<SignatureFactsRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedOwningModule,
      const identity::Sha256Digest& sourceContentDigest,
      const identity::Sha256Digest& bindingSurfaceRevision,
      const identity::Sha256Digest& markerPolicyRegistryRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords);

private:
  explicit SignatureFactsRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;
  friend class SignatureFactsVerifier;
};

enum class CheckerInvariantKind : uint8_t {
  InputReceiptMismatch = 0x01,
  MissingRequiredFact = 0x02,
  AdditionalFact = 0x03,
  InvalidFact = 0x04,
  StaleRevision = 0x05,
  ViewMismatch = 0x06,
  InferenceLifecycle = 0x07,
  SolverStateInvalid = 0x08,
  InvalidEmitterOrdinal = 0x09,
  CanonicalCodecMismatch = 0x0a
};

enum class CheckerInvariantStage : uint8_t {
  Signature = 0x01,
  Coherence = 0x02,
  Body = 0x03,
  Verification = 0x04
};

struct CheckerInvariantFact final {
  CheckerInvariantKind kind;
  CheckerInvariantStage stage;
  identity::ModuleId module;
  zc::Maybe<identity::DefId> owner;
  zc::Maybe<ast::NodeId> node;
  zc::Maybe<identity::SourceSpan> sourceSpan;
  zc::Vector<uint32_t> structuralFieldPath;
  zc::Maybe<identity::Sha256Digest> expectedRevision;
  zc::Maybe<identity::Sha256Digest> actualRevision;
  uint32_t traversalOrdinal;
};

/// \brief Closed identity-or-checker invariant failure.
class CheckerVerificationFailure final {
public:
  explicit CheckerVerificationFailure(identity::IdentityInvariant&& value) : value(zc::mv(value)) {}
  explicit CheckerVerificationFailure(CheckerInvariantFact&& value) : value(zc::mv(value)) {}
  CheckerVerificationFailure(CheckerVerificationFailure&&) noexcept = default;
  CheckerVerificationFailure& operator=(CheckerVerificationFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(CheckerVerificationFailure);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<identity::IdentityInvariant, CheckerInvariantFact> value;
};

/// \brief Independent expected signature-bearing definition census row.
struct SignatureDefinitionRequirement final {
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  zc::Array<uint8_t> canonicalRecord;
};

struct ImplHeadRequirement final {
  identity::ImplId implementation;
  zc::Array<uint8_t> canonicalRecord;
};

struct MarkerFactRequirement final {
  MarkerFactKey key;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief One independently enumerated signature-bearing bound definition.
struct SignatureDefinitionCensusEntry final {
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
};

enum class ImplAuthorityKind : uint8_t { Behavior = 0x01, Marker = 0x02 };

/// \brief One independently enumerated implementation authority and source shape.
struct ImplAuthorityCensusEntry final {
  identity::ImplId implementation;
  ImplAuthorityKind kind;
};

/// \brief Immutable verifier inputs supplied independently of the candidate.
struct SignatureFactsVerificationInput final {
  identity::SemanticContextBrand semanticContext;
  const identity::SemanticContextFingerprint& contextFingerprint;
  identity::ModuleId module;
  const identity::SourceFileKey& source;
  const identity::Sha256Digest& sourceContentDigest;
  const binder::ParsedModuleReceipt& parsedModuleReceipt;
  const binder::ExportSurfaceRevision& bindingSurfaceRevision;
  const MarkerPolicyRegistryRevision& markerPolicyRegistryRevision;
  zc::ArrayPtr<const SignatureDefinitionCensusEntry> sourceSignatureCensus;
  zc::ArrayPtr<const ImplAuthorityCensusEntry> sourceImplCensus;
  zc::ArrayPtr<const SignatureDefinitionRequirement> requiredSignatures;
  zc::ArrayPtr<const ImplHeadRequirement> requiredImplHeads;
  zc::ArrayPtr<const MarkerFactRequirement> requiredMarkerFacts;
  const type::SemanticTypeStore& semanticTypes;
  const CheckerIdentityAuthority& identities;
};

/// \brief Immutable verified signatures, impl heads, and explicit marker facts.
class VerifiedSignatureFacts final {
public:
  ~VerifiedSignatureFacts() noexcept(false);
  VerifiedSignatureFacts(VerifiedSignatureFacts&&) noexcept;
  VerifiedSignatureFacts& operator=(VerifiedSignatureFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedSignatureFacts);

  ZC_NODISCARD const SignatureFactsRevision& revision() const noexcept;
  ZC_NODISCARD const MarkerPolicyRegistryRevision& markerPolicyRegistryRevision() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const binder::ParsedModuleReceipt& parsedModuleReceipt() const noexcept;
  ZC_NODISCARD const binder::ExportSurfaceRevision& bindingSurfaceRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const SemanticSignature> signatures() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImplHead> implHeads() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MarkerFact> markerFacts() const noexcept;

private:
  struct Impl;
  explicit VerifiedSignatureFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class SignatureFactsVerifier;
};

struct SignatureFactsInvariantRejected final {
  zc::Vector<CheckerVerificationFailure> failures;
};

enum class SignatureSourceDiagnostic : uint16_t {
  ConflictingImpl = 4017,
  OrphanImpl = 4054,
  BodyLiteralOutOfRange = 4077,
  MarkerInterfaceRequiresBodylessImpl = 4088,
  BehaviorInterfaceRequiresImplBody = 4089,
  GenericMarkerInterfaceNotAllowed = 4090,
  PositiveMarkerImplRequiresUnsafe = 4091,
  ExplicitImplConflictsWithBuiltinMarker = 4092
};

struct SignatureLiteralDisplayArg final {
  CanonicalConstValue literal;
};

struct SignaturePrimitiveTypeDisplayArg final {
  PrimitiveKind kind;
};

/// \brief Closed typed argument algebra for signature-stage source diagnostics.
class SignatureSourceArgument final {
public:
  explicit SignatureSourceArgument(SignatureLiteralDisplayArg&& value) : value(zc::mv(value)) {}
  explicit SignatureSourceArgument(SignaturePrimitiveTypeDisplayArg value) noexcept
      : value(value) {}
  SignatureSourceArgument(SignatureSourceArgument&&) noexcept = default;
  SignatureSourceArgument& operator=(SignatureSourceArgument&&) noexcept = default;
  ZC_DISALLOW_COPY(SignatureSourceArgument);
  ZC_NODISCARD const auto& variant() const noexcept { return value; }

private:
  zc::OneOf<SignatureLiteralDisplayArg, SignaturePrimitiveTypeDisplayArg> value;
};

struct SignatureEmitterOrdinal final {
  uint32_t ownerSchemaPreorder;
  uint32_t siteSchemaPreorder;
  uint32_t itemOrdinal;
};

/// \brief One structured signature-stage source failure with no recovery handle.
struct SignatureSourceFailureRef final {
  SignatureSourceDiagnostic diagnostic;
  ast::NodeId primaryNode;
  identity::SourceSpan primarySpan;
  zc::Vector<SignatureSourceArgument> arguments;
  SignatureEmitterOrdinal emitterOrdinal;
};

/// \brief Structured advisory retained by a successful or source-rejected signature result.
struct SignatureAdvisoryRef final {
  diagnostics::DiagID diagnostic;
  ast::NodeId primaryNode;
  identity::SourceSpan primarySpan;
  SignatureEmitterOrdinal emitterOrdinal;
};

struct SignatureFactsSourceRejected final {
  zc::Vector<SignatureSourceFailureRef> failures;
  zc::Vector<SignatureAdvisoryRef> advisories;
};

using SignatureFactsVerificationResult =
    zc::OneOf<VerifiedSignatureFacts, SignatureFactsInvariantRejected>;

/// \brief Independently verifies the canonical RFC 0015 fact candidate.
class SignatureFactsVerifier final {
public:
  ZC_NODISCARD static SignatureFactsVerificationResult verify(
      SignatureFactsCandidate&& candidate, const SignatureFactsVerificationInput& input);
};

/// \brief Public canonical RFC 0015 encoders for immutable verified fact projection.
class SignatureFactsCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<TypeKeyPatternKey> makeTypeKeyPatternKey(
      const TypeKeyPattern& pattern, const CheckerIdentityAuthority& identities);
  /// \brief Decode and independently re-verify one canonical type-key pattern key.
  ZC_NODISCARD static zc::Maybe<TypeKeyPatternKey> decodeTypeKeyPatternKey(
      zc::ArrayPtr<const uint8_t> bytes, const CheckerIdentityAuthority& identities);
  /// \brief Recompute and compare the canonical type-key pattern against its decoded value.
  ZC_NODISCARD static bool typeKeyPatternKeyIsCanonical(const TypeKeyPatternKey& pattern,
                                                        const CheckerIdentityAuthority& identities);
  ZC_NODISCARD static zc::Maybe<ImplPatternKey> makeImplPatternKey(
      const ImplPattern& pattern, const CheckerIdentityAuthority& identities);
  /// \brief Decode and independently re-verify one canonical complete impl-pattern key.
  ZC_NODISCARD static zc::Maybe<ImplPatternKey> decodeImplPatternKey(
      zc::ArrayPtr<const uint8_t> bytes, const CheckerIdentityAuthority& identities);
  /// \brief Recompute and compare the complete canonical key against its decoded value.
  ZC_NODISCARD static bool implPatternKeyIsCanonical(const ImplPatternKey& pattern,
                                                     const CheckerIdentityAuthority& identities);
  /// \brief Perform RFC 0005 first-order unification with disjoint parameter spaces.
  ZC_NODISCARD static zc::Maybe<bool> implPatternsOverlap(
      const ImplPatternKey& left, const ImplPatternKey& right,
      const CheckerIdentityAuthority& identities);
  /// \brief Enforce the complete RFC 0015 publication restrictions for one impl pattern.
  ZC_NODISCARD static bool implPatternIsPublishable(const ImplPatternKey& pattern,
                                                    size_t genericParameterCount) noexcept;
  /// \brief Read the verified interface identity retained by the canonical pattern key.
  ZC_NODISCARD static identity::DefId implPatternInterface(const ImplPatternKey& pattern) noexcept;
  /// \brief Derive the canonical outer head directly from the verified self pattern.
  ZC_NODISCARD static zc::Maybe<CanonicalTypeHead> implPatternHead(
      const ImplPatternKey& pattern) noexcept;
  /// \brief Derive the canonical outer head used by coherence and impl lookup.
  ZC_NODISCARD static zc::Maybe<CanonicalTypeHead> canonicalTypeHead(
      identity::SemanticTypeId type, const type::SemanticTypeStore& semanticTypes);
  /// \brief Encodes a signature through the retained Checker identity authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeSignature(
      const SemanticSignature& signature, identity::ModuleId owningModule,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes);
  /// \brief Encodes one closed type-free marker interface without accessing a semantic type store.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeTypeFreeInterfaceSignature(
      const SemanticSignature& signature, identity::ModuleId owningModule,
      const CheckerIdentityAuthority& identities);
  /// \brief Encodes an implementation head through the retained Checker identity authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeImplHead(
      const ImplHead& head, const CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
  /// \brief Encodes a marker fact through the retained Checker identity authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeMarkerFact(
      const MarkerFact& fact, const CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
  /// \brief Encodes one canonical constraint through the retained owner module authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeConstraint(
      const CanonicalConstraint& constraint, identity::ModuleId owningModule,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes);
  /// \brief Encodes one interface instantiation through the retained owner module authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeInterfaceInstantiation(
      const InterfaceInstantiation& interface, identity::ModuleId owningModule,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes);
  /// \brief Encode one shared canonical constant value through retained Checker authority.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeCanonicalConstValueFromAuthority(
      const CanonicalConstValue& value, identity::ModuleId owningModule,
      const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes);

private:
  static bool encodePattern(identity::CanonicalEncoder& encoder, const TypeKeyPattern& pattern,
                            const CheckerIdentityAuthority& identities);
  static bool encodePatternInterface(identity::CanonicalEncoder& encoder,
                                     const PatternInterfaceInstantiation& interface,
                                     const CheckerIdentityAuthority& identities);
};

struct MarkerShapeModuleInput final {
  const ownership::OwnershipAdmittedBoundModule& boundModule;
};

using MarkerShapeInventoryBuildResult =
    zc::OneOf<VerifiedMarkerShapeInventory, SignatureFactsInvariantRejected>;

/// \brief Builds the single context-complete marker-shape inventory from verified modules.
class MarkerShapeInventoryBuilder final {
public:
  ZC_NODISCARD static MarkerShapeInventoryBuildResult build(
      identity::SemanticContextBrand semanticContext,
      const identity::SemanticContextFingerprint& contextFingerprint,
      identity::ModuleId diagnosticModule, zc::ArrayPtr<const MarkerShapeModuleInput> modules,
      const CheckerIdentityAuthority& identities);
};

using MarkerPolicyRegistryBuildResult =
    zc::OneOf<VerifiedMarkerPolicyRegistry, SignatureFactsInvariantRejected>;

/// \brief Binds compiler-distribution marker policy to verified shape and context lineage.
class MarkerPolicyRegistryBuilder final {
public:
  ZC_NODISCARD static MarkerPolicyRegistryBuildResult build(
      identity::ModuleId diagnosticModule, const MarkerPolicyConfiguration& configuration,
      const VerifiedMarkerShapeInventory& shapeInventory,
      zc::ArrayPtr<const identity::ModuleId> authorizedPreludeModules,
      const CheckerIdentityAuthority& identities);
};

/// \brief Immutable verified-only inputs accepted by production signature construction.
struct SignatureFactsBuildInput final {
  const ownership::OwnershipAdmittedBoundModule& boundModule;
  type::SemanticTypeStore& semanticTypes;
  const VerifiedMarkerShapeInventory& markerShapes;
  const VerifiedMarkerPolicyRegistry& markerPolicies;
  const CheckerIdentityAuthority& identities;
};

using SignatureFactsBuildResult = zc::OneOf<VerifiedSignatureFacts, SignatureFactsSourceRejected,
                                            SignatureFactsInvariantRejected>;

/// \brief Constructs signature facts only from verified syntax, binding, identity, and type input.
class SignatureFactsBuilder final {
public:
  /// \brief Validate verified input lineage and construct canonical local signature facts.
  /// \param input Complete verified binder handoff and context-owned semantic stores.
  /// \return Verified facts or a structured fail-closed rejection without partial publication.
  ZC_NODISCARD static SignatureFactsBuildResult build(const SignatureFactsBuildInput& input);
};

}  // namespace zomlang::compiler::checker::signature
