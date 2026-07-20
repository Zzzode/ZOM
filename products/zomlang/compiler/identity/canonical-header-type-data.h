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

#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/canonical-header-type.h"

namespace zomlang::compiler::identity::canonical_header_type_detail {

struct CanonicalNamedHeaderTypeData final {
  CanonicalNameReference name;
  zc::Vector<CanonicalHeaderTypeSyntax> arguments;
};

struct CanonicalObjectTypeMemberData final {
  SemanticIdentifier name;
  CanonicalHeaderTypeSyntax type;
  bool isMutable;
  bool isOptional;
};

struct CanonicalAssociatedBindingData final {
  SemanticIdentifier name;
  CanonicalHeaderTypeSyntax type;
};

struct NamedTypeData final {
  CanonicalNamedHeaderType type;
};
struct PredefinedTypeData final {
  PredefinedTypeKind kind;
};
struct FunctionTypeData final {
  zc::Vector<CanonicalHeaderTypeSyntax> parameters;
  CanonicalHeaderTypeSyntax result;
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
};
struct UnionTypeData final {
  zc::Vector<CanonicalHeaderTypeSyntax> members;
};
struct IntersectionTypeData final {
  zc::Vector<CanonicalHeaderTypeSyntax> members;
};
struct FixedArrayTypeData final {
  CanonicalHeaderTypeSyntax element;
  uint64_t length;
};
struct DynamicArrayTypeData final {
  CanonicalHeaderTypeSyntax element;
};
struct SliceTypeData final {
  CanonicalHeaderTypeSyntax element;
};
struct OptionalTypeData final {
  CanonicalHeaderTypeSyntax element;
  uint8_t depth;
};
struct ReferenceTypeData final {
  ReferenceMutability mutability;
  CanonicalHeaderTypeSyntax element;
};
struct RawPointerTypeData final {
  RawPointerMutability mutability;
  CanonicalHeaderTypeSyntax element;
};
struct TypeQueryTypeData final {
  CanonicalNameReference name;
};
struct ObjectTypeData final {
  zc::Vector<CanonicalObjectTypeMember> members;
};
struct TupleTypeData final {
  zc::Vector<CanonicalHeaderTypeSyntax> elements;
};
struct AssociatedProjectionTypeData final {
  CanonicalHeaderTypeSyntax base;
  zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType;
  SemanticIdentifier member;
};
struct DynamicTypeData final {
  CanonicalNamedHeaderType principal;
  zc::Vector<CanonicalNameReference> markers;
  zc::Vector<CanonicalAssociatedBinding> associatedBindings;
};

struct CanonicalHeaderTypeSyntaxData final {
  zc::OneOf<NamedTypeData, PredefinedTypeData, FunctionTypeData, UnionTypeData,
            IntersectionTypeData, FixedArrayTypeData, DynamicArrayTypeData, SliceTypeData,
            OptionalTypeData, ReferenceTypeData, RawPointerTypeData, TypeQueryTypeData,
            ObjectTypeData, TupleTypeData, AssociatedProjectionTypeData, DynamicTypeData>
      value;
};

}  // namespace zomlang::compiler::identity::canonical_header_type_detail
