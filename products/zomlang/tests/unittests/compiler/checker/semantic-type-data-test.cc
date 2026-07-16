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

#include "zomlang/compiler/type/semantic-type-data.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler {

namespace ast {
class NodeId;
}

namespace type {
class Type;
class TypeVar;
}  // namespace type

namespace type::semantic {
namespace {

identity::SemanticIdentifier identifier(zc::StringPtr text) {
  auto admitted = identity::SemanticIdentifier::fromCanonical(text);
  ZC_IREQUIRE(admitted != zc::none, "test identifier must be canonical");
  ZC_IF_SOME(value, admitted) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

template <typename Branch>
void expectBranch(Branch&& branch, TypeDataTag expectedTag) {
  TypeData data(zc::fwd<Branch>(branch));
  ZC_EXPECT(data.tag() == expectedTag);
  ZC_EXPECT(data.is<zc::Decay<Branch>>());

  TypeData moved(zc::mv(data));
  ZC_EXPECT(moved.tag() == expectedTag);
  ZC_EXPECT(moved.is<zc::Decay<Branch>>());
}

}  // namespace

static_assert(static_cast<uint8_t>(PrimitiveKind::I8) == 0x01);
static_assert(static_cast<uint8_t>(PrimitiveKind::I16) == 0x02);
static_assert(static_cast<uint8_t>(PrimitiveKind::I32) == 0x03);
static_assert(static_cast<uint8_t>(PrimitiveKind::I64) == 0x04);
static_assert(static_cast<uint8_t>(PrimitiveKind::U8) == 0x05);
static_assert(static_cast<uint8_t>(PrimitiveKind::U16) == 0x06);
static_assert(static_cast<uint8_t>(PrimitiveKind::U32) == 0x07);
static_assert(static_cast<uint8_t>(PrimitiveKind::U64) == 0x08);
static_assert(static_cast<uint8_t>(PrimitiveKind::Isize) == 0x09);
static_assert(static_cast<uint8_t>(PrimitiveKind::Usize) == 0x0a);
static_assert(static_cast<uint8_t>(PrimitiveKind::F32) == 0x0b);
static_assert(static_cast<uint8_t>(PrimitiveKind::F64) == 0x0c);
static_assert(static_cast<uint8_t>(PrimitiveKind::Bool) == 0x0d);
static_assert(static_cast<uint8_t>(PrimitiveKind::Char) == 0x0e);
static_assert(static_cast<uint8_t>(PrimitiveKind::Str) == 0x0f);
static_assert(static_cast<uint8_t>(PrimitiveKind::Unit) == 0x10);
static_assert(static_cast<uint8_t>(PrimitiveKind::Never) == 0x11);
static_assert(static_cast<uint8_t>(PrimitiveKind::Any) == 0x12);
static_assert(static_cast<uint8_t>(PrimitiveKind::Null) == 0x13);

static_assert(static_cast<uint8_t>(Mutability::Const) == 0x01);
static_assert(static_cast<uint8_t>(Mutability::Mutable) == 0x02);
static_assert(static_cast<uint8_t>(FieldPresence::Required) == 0x01);
static_assert(static_cast<uint8_t>(FieldPresence::Optional) == 0x02);

static_assert(static_cast<uint8_t>(TypeDataTag::Primitive) == 0x01);
static_assert(static_cast<uint8_t>(TypeDataTag::Tuple) == 0x02);
static_assert(static_cast<uint8_t>(TypeDataTag::Object) == 0x03);
static_assert(static_cast<uint8_t>(TypeDataTag::DynamicArray) == 0x04);
static_assert(static_cast<uint8_t>(TypeDataTag::Slice) == 0x05);
static_assert(static_cast<uint8_t>(TypeDataTag::FixedArray) == 0x06);
static_assert(static_cast<uint8_t>(TypeDataTag::Function) == 0x07);
static_assert(static_cast<uint8_t>(TypeDataTag::Nominal) == 0x08);
static_assert(static_cast<uint8_t>(TypeDataTag::TypeParameter) == 0x09);
static_assert(static_cast<uint8_t>(TypeDataTag::Union) == 0x0a);
static_assert(static_cast<uint8_t>(TypeDataTag::Intersection) == 0x0b);
static_assert(static_cast<uint8_t>(TypeDataTag::Reference) == 0x0c);
static_assert(static_cast<uint8_t>(TypeDataTag::RawPointer) == 0x0d);
static_assert(static_cast<uint8_t>(TypeDataTag::Existential) == 0x0e);
static_assert(static_cast<uint8_t>(TypeDataTag::InterfaceBound) == 0x0f);
static_assert(static_cast<uint8_t>(TypeDataTag::InterfaceSelf) == 0x10);

static_assert(__is_constructible(TypeData, TypeData&&));
static_assert(__is_assignable(TypeData&, TypeData&&));
static_assert(!__is_constructible(TypeData, const TypeData&));
static_assert(!__is_assignable(TypeData&, const TypeData&));
static_assert(!__is_constructible(TypeData, type::Type&));
static_assert(!__is_constructible(TypeData, type::TypeVar&));
static_assert(!__is_constructible(TypeData, ast::NodeId&));

ZC_TEST("SemanticTypeData.CoversEveryClosedBranch") {
  identity::SemanticTypeId typeId;
  identity::DefId definition;

  expectBranch(PrimitiveTypeData{PrimitiveKind::I32}, TypeDataTag::Primitive);

  zc::Vector<identity::SemanticTypeId> tupleElements;
  tupleElements.add(typeId);
  expectBranch(TupleTypeData{zc::mv(tupleElements)}, TypeDataTag::Tuple);

  zc::Vector<ObjectFieldData> fields;
  fields.add(ObjectFieldData{identifier("field"_zc), typeId, Mutability::Mutable,
                             FieldPresence::Optional});
  expectBranch(ObjectTypeData{zc::mv(fields)}, TypeDataTag::Object);

  expectBranch(DynamicArrayTypeData{typeId}, TypeDataTag::DynamicArray);
  expectBranch(SliceTypeData{typeId}, TypeDataTag::Slice);
  expectBranch(FixedArrayTypeData{typeId, 42}, TypeDataTag::FixedArray);

  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(typeId);
  expectBranch(FunctionTypeData{zc::mv(parameters), typeId, typeId}, TypeDataTag::Function);

  zc::Vector<identity::SemanticTypeId> nominalArguments;
  nominalArguments.add(typeId);
  expectBranch(NominalTypeData{definition, zc::mv(nominalArguments)}, TypeDataTag::Nominal);
  expectBranch(TypeParameterTypeData{definition}, TypeDataTag::TypeParameter);

  zc::Vector<identity::SemanticTypeId> alternatives;
  alternatives.add(typeId);
  expectBranch(UnionTypeData{zc::mv(alternatives)}, TypeDataTag::Union);

  zc::Vector<identity::SemanticTypeId> conjuncts;
  conjuncts.add(typeId);
  expectBranch(IntersectionTypeData{zc::mv(conjuncts)}, TypeDataTag::Intersection);

  expectBranch(ReferenceTypeData{Mutability::Const, typeId}, TypeDataTag::Reference);
  expectBranch(RawPointerTypeData{Mutability::Mutable, typeId}, TypeDataTag::RawPointer);

  zc::Vector<identity::SemanticTypeId> principalArguments;
  principalArguments.add(typeId);
  ExistentialInterfaceData principal{definition, zc::mv(principalArguments)};
  zc::Vector<ExistentialInterfaceData> additionalInterfaces;
  zc::Vector<identity::DefId> markers;
  markers.add(definition);
  zc::Vector<AssociatedTypeBindingData> associatedBindings;
  associatedBindings.add(AssociatedTypeBindingData{definition, typeId});
  expectBranch(ExistentialTypeData{zc::mv(principal), zc::mv(additionalInterfaces), zc::mv(markers),
                                   zc::mv(associatedBindings)},
               TypeDataTag::Existential);

  zc::Vector<identity::SemanticTypeId> interfaceArguments;
  interfaceArguments.add(typeId);
  expectBranch(
      InterfaceBoundTypeData{InterfaceInstantiation{definition, zc::mv(interfaceArguments)}},
      TypeDataTag::InterfaceBound);
  expectBranch(InterfaceSelfTypeData{definition}, TypeDataTag::InterfaceSelf);
}

ZC_TEST("SemanticTypeData.PreservesCompleteRecordPayloads") {
  identity::SemanticTypeId typeId;
  identity::DefId definition;

  zc::Vector<ObjectFieldData> fields;
  fields.add(
      ObjectFieldData{identifier("value"_zc), typeId, Mutability::Const, FieldPresence::Required});
  TypeData object(ObjectTypeData{zc::mv(fields)});
  const auto& objectData = object.get<ObjectTypeData>();
  ZC_REQUIRE(objectData.fields.size() == 1);
  ZC_EXPECT(objectData.fields[0].name.text() == "value"_zc);
  ZC_EXPECT(objectData.fields[0].type == typeId);
  ZC_EXPECT(objectData.fields[0].mutability == Mutability::Const);
  ZC_EXPECT(objectData.fields[0].presence == FieldPresence::Required);

  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(typeId);
  TypeData function(FunctionTypeData{zc::mv(parameters), typeId, zc::none});
  const auto& functionData = function.get<FunctionTypeData>();
  ZC_EXPECT(functionData.parameters.size() == 1);
  ZC_EXPECT(functionData.success == typeId);
  ZC_EXPECT(functionData.raises == zc::none);

  zc::Vector<identity::SemanticTypeId> arguments;
  arguments.add(typeId);
  TypeData bound(InterfaceBoundTypeData{InterfaceInstantiation{definition, zc::mv(arguments)}});
  const auto& boundData = bound.get<InterfaceBoundTypeData>();
  ZC_EXPECT(boundData.interface.interface == definition);
  ZC_EXPECT(boundData.interface.arguments.size() == 1);

  TypeData interfaceSelf(InterfaceSelfTypeData{definition});
  ZC_EXPECT(interfaceSelf.get<InterfaceSelfTypeData>().interface == definition);
}

}  // namespace type::semantic
}  // namespace zomlang::compiler
