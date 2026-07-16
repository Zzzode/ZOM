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

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/frozen-registry.h"
#include "zomlang/compiler/identity/semantic-type-id.h"

namespace zomlang::compiler::type::semantic {

/// \brief Closed primitive type discriminator with canonical RFC tag values.
enum class PrimitiveKind : uint8_t {
  I8 = 0x01,
  I16 = 0x02,
  I32 = 0x03,
  I64 = 0x04,
  U8 = 0x05,
  U16 = 0x06,
  U32 = 0x07,
  U64 = 0x08,
  Isize = 0x09,
  Usize = 0x0a,
  F32 = 0x0b,
  F64 = 0x0c,
  Bool = 0x0d,
  Char = 0x0e,
  Str = 0x0f,
  Unit = 0x10,
  Never = 0x11,
  Any = 0x12,
  Null = 0x13
};

/// \brief Canonical access mutability discriminator.
enum class Mutability : uint8_t { Const = 0x01, Mutable = 0x02 };

/// \brief Canonical structural object field-presence discriminator.
enum class FieldPresence : uint8_t { Required = 0x01, Optional = 0x02 };

/// \brief Closed semantic type branch discriminator with canonical RFC tag values.
enum class TypeDataTag : uint8_t {
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
  InterfaceSelf = 0x10
};

/// \brief One canonical structural object field.
struct ObjectFieldData final {
  identity::SemanticIdentifier name;
  identity::SemanticTypeId type;
  Mutability mutability;
  FieldPresence presence;
};

/// \brief One canonical interface instantiation.
struct InterfaceInstantiation final {
  identity::DefId interface;
  zc::Vector<identity::SemanticTypeId> arguments;
};

/// \brief Canonical function type payload.
struct FunctionTypeData final {
  zc::Vector<identity::SemanticTypeId> parameters;
  identity::SemanticTypeId success;
  zc::Maybe<identity::SemanticTypeId> raises;
};

/// \brief One interface component of a canonical existential type.
struct ExistentialInterfaceData final {
  identity::DefId definition;
  zc::Vector<identity::SemanticTypeId> arguments;
};

/// \brief One canonical associated-type binding.
struct AssociatedTypeBindingData final {
  identity::DefId associated;
  identity::SemanticTypeId type;
};

/// \brief Canonical existential type payload.
struct ExistentialTypeData final {
  ExistentialInterfaceData principal;
  zc::Vector<ExistentialInterfaceData> additionalInterfaces;
  zc::Vector<identity::DefId> markers;
  zc::Vector<AssociatedTypeBindingData> associatedBindings;
};

/// \brief Canonical primitive type branch payload.
struct PrimitiveTypeData final {
  PrimitiveKind kind;
};

/// \brief Canonical tuple type branch payload.
struct TupleTypeData final {
  zc::Vector<identity::SemanticTypeId> elements;
};

/// \brief Canonical structural object type branch payload.
struct ObjectTypeData final {
  zc::Vector<ObjectFieldData> fields;
};

/// \brief Canonical owned dynamic array type branch payload.
struct DynamicArrayTypeData final {
  identity::SemanticTypeId element;
};

/// \brief Canonical slice type branch payload.
struct SliceTypeData final {
  identity::SemanticTypeId element;
};

/// \brief Canonical fixed-length array type branch payload.
struct FixedArrayTypeData final {
  identity::SemanticTypeId element;
  uint64_t length;
};

/// \brief Canonical nominal type branch payload.
struct NominalTypeData final {
  identity::DefId definition;
  zc::Vector<identity::SemanticTypeId> arguments;
};

/// \brief Canonical generic type-parameter branch payload.
struct TypeParameterTypeData final {
  identity::DefId parameter;
};

/// \brief Canonical union type branch payload.
struct UnionTypeData final {
  zc::Vector<identity::SemanticTypeId> alternatives;
};

/// \brief Canonical intersection type branch payload.
struct IntersectionTypeData final {
  zc::Vector<identity::SemanticTypeId> conjuncts;
};

/// \brief Canonical reference type branch payload.
struct ReferenceTypeData final {
  Mutability mutability;
  identity::SemanticTypeId referent;
};

/// \brief Canonical raw-pointer type branch payload.
struct RawPointerTypeData final {
  Mutability mutability;
  identity::SemanticTypeId pointee;
};

/// \brief Canonical interface-bound type branch payload.
struct InterfaceBoundTypeData final {
  InterfaceInstantiation interface;
};

/// \brief Canonical contextual interface Self type branch payload.
struct InterfaceSelfTypeData final {
  identity::DefId interface;
};

/// \brief Move-only closed semantic type payload independent of AST and inference storage.
class TypeData final {
public:
  explicit TypeData(PrimitiveTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(TupleTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(ObjectTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(DynamicArrayTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(SliceTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(FixedArrayTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(FunctionTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(NominalTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(TypeParameterTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(UnionTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(IntersectionTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(ReferenceTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(RawPointerTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(ExistentialTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(InterfaceBoundTypeData&& data) : value(zc::mv(data)) {}
  explicit TypeData(InterfaceSelfTypeData&& data) : value(zc::mv(data)) {}

  TypeData(TypeData&&) noexcept = default;
  TypeData& operator=(TypeData&&) noexcept = default;
  ZC_DISALLOW_COPY(TypeData);

  /// \brief Returns the canonical branch tag for this payload.
  ZC_NODISCARD TypeDataTag tag() const noexcept {
    if (value.is<PrimitiveTypeData>()) return TypeDataTag::Primitive;
    if (value.is<TupleTypeData>()) return TypeDataTag::Tuple;
    if (value.is<ObjectTypeData>()) return TypeDataTag::Object;
    if (value.is<DynamicArrayTypeData>()) return TypeDataTag::DynamicArray;
    if (value.is<SliceTypeData>()) return TypeDataTag::Slice;
    if (value.is<FixedArrayTypeData>()) return TypeDataTag::FixedArray;
    if (value.is<FunctionTypeData>()) return TypeDataTag::Function;
    if (value.is<NominalTypeData>()) return TypeDataTag::Nominal;
    if (value.is<TypeParameterTypeData>()) return TypeDataTag::TypeParameter;
    if (value.is<UnionTypeData>()) return TypeDataTag::Union;
    if (value.is<IntersectionTypeData>()) return TypeDataTag::Intersection;
    if (value.is<ReferenceTypeData>()) return TypeDataTag::Reference;
    if (value.is<RawPointerTypeData>()) return TypeDataTag::RawPointer;
    if (value.is<ExistentialTypeData>()) return TypeDataTag::Existential;
    if (value.is<InterfaceBoundTypeData>()) return TypeDataTag::InterfaceBound;
    ZC_IREQUIRE(value.is<InterfaceSelfTypeData>(), "TypeData contains no semantic branch");
    return TypeDataTag::InterfaceSelf;
  }

  template <typename Branch>
  ZC_NODISCARD bool is() const noexcept {
    return value.is<Branch>();
  }

  template <typename Branch>
  ZC_NODISCARD const Branch& get() const& {
    return value.get<Branch>();
  }

private:
  zc::OneOf<PrimitiveTypeData, TupleTypeData, ObjectTypeData, DynamicArrayTypeData, SliceTypeData,
            FixedArrayTypeData, FunctionTypeData, NominalTypeData, TypeParameterTypeData,
            UnionTypeData, IntersectionTypeData, ReferenceTypeData, RawPointerTypeData,
            ExistentialTypeData, InterfaceBoundTypeData, InterfaceSelfTypeData>
      value;
};

}  // namespace zomlang::compiler::type::semantic
