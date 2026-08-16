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

#include "zomlang/compiler/identity/canonical/header-type-data.h"
#include "zomlang/compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::identity {
namespace detail = canonical_header_type_detail;
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) { return left[index] < right[index]; }
  }
  return left.size() < right.size();
}

template <typename Variant>
zc::Own<detail::CanonicalHeaderTypeSyntaxData> makeData(Variant&& value) {
  return zc::heap<detail::CanonicalHeaderTypeSyntaxData>(
      detail::CanonicalHeaderTypeSyntaxData{zc::fwd<Variant>(value)});
}

template <typename Value>
struct EncodedValue final {
  zc::Array<uint8_t> bytes;
  Value value;
};

template <typename Value>
zc::Vector<Value> sortUnique(zc::Vector<Value>&& values) {
  zc::Vector<EncodedValue<Value>> encoded(values.size());
  for (auto& value : values) { encoded.add(EncodedValue<Value>{value.encode(), zc::mv(value)}); }
  for (size_t index = 1; index < encoded.size(); ++index) {
    auto current = zc::mv(encoded[index]);
    size_t insertion = index;
    while (insertion > 0 &&
           lessBytes(current.bytes.asPtr(), encoded[insertion - 1].bytes.asPtr())) {
      encoded[insertion] = zc::mv(encoded[insertion - 1]);
      --insertion;
    }
    encoded[insertion] = zc::mv(current);
  }
  zc::Vector<Value> result(encoded.size());
  for (size_t index = 0; index < encoded.size(); ++index) {
    if (index == 0 || encoded[index - 1].bytes.asPtr() != encoded[index].bytes.asPtr()) {
      result.add(zc::mv(encoded[index].value));
    }
  }
  return result;
}

}  // namespace

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::optional(
    CanonicalHeaderTypeSyntax&& element, uint8_t depth) {
  if (depth != 0x01 && depth != 0x02) { return zc::none; }
  return CanonicalHeaderTypeSyntax(makeData(detail::OptionalTypeData{zc::mv(element), depth}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::reference(
    ReferenceMutability mutability, CanonicalHeaderTypeSyntax&& element) {
  if (!isCanonicalHeaderValue(mutability)) { return zc::none; }
  return CanonicalHeaderTypeSyntax(
      makeData(detail::ReferenceTypeData{mutability, zc::mv(element)}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::rawPointer(
    RawPointerMutability mutability, CanonicalHeaderTypeSyntax&& element) {
  if (!isCanonicalHeaderValue(mutability)) { return zc::none; }
  return CanonicalHeaderTypeSyntax(
      makeData(detail::RawPointerTypeData{mutability, zc::mv(element)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::typeQuery(CanonicalNameReference&& name) {
  return CanonicalHeaderTypeSyntax(makeData(detail::TypeQueryTypeData{zc::mv(name)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::object(
    zc::Vector<CanonicalObjectTypeMember>&& members) {
  return CanonicalHeaderTypeSyntax(makeData(detail::ObjectTypeData{sortUnique(zc::mv(members))}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::tuple(
    zc::Vector<CanonicalHeaderTypeSyntax>&& elements) {
  return CanonicalHeaderTypeSyntax(makeData(detail::TupleTypeData{zc::mv(elements)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::associatedProjection(
    CanonicalHeaderTypeSyntax&& base, zc::Maybe<CanonicalHeaderTypeSyntax>&& interfaceType,
    SemanticIdentifier&& member) {
  return CanonicalHeaderTypeSyntax(makeData(
      detail::AssociatedProjectionTypeData{zc::mv(base), zc::mv(interfaceType), zc::mv(member)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::dynamic(
    CanonicalNamedHeaderType&& principal, zc::Vector<CanonicalNameReference>&& markers,
    zc::Vector<CanonicalAssociatedBinding>&& associatedBindings) {
  return CanonicalHeaderTypeSyntax(makeData(detail::DynamicTypeData{
      zc::mv(principal), sortUnique(zc::mv(markers)), sortUnique(zc::mv(associatedBindings))}));
}

zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalHeaderTypeSyntax::element() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::FixedArrayTypeData>()) { return value.element; }
  ZC_IF_SOME(value, impl->value.tryGet<detail::DynamicArrayTypeData>()) { return value.element; }
  ZC_IF_SOME(value, impl->value.tryGet<detail::SliceTypeData>()) { return value.element; }
  ZC_IF_SOME(value, impl->value.tryGet<detail::OptionalTypeData>()) { return value.element; }
  ZC_IF_SOME(value, impl->value.tryGet<detail::ReferenceTypeData>()) { return value.element; }
  ZC_IF_SOME(value, impl->value.tryGet<detail::RawPointerTypeData>()) { return value.element; }
  return zc::none;
}

zc::Maybe<uint8_t> CanonicalHeaderTypeSyntax::optionalDepth() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::OptionalTypeData>()) { return value.depth; }
  return zc::none;
}

zc::Maybe<ReferenceMutability> CanonicalHeaderTypeSyntax::referenceMutability() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::ReferenceTypeData>()) { return value.mutability; }
  return zc::none;
}

zc::Maybe<RawPointerMutability> CanonicalHeaderTypeSyntax::rawPointerMutability() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::RawPointerTypeData>()) { return value.mutability; }
  return zc::none;
}

zc::Maybe<const CanonicalNameReference&> CanonicalHeaderTypeSyntax::typeQueryName() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::TypeQueryTypeData>()) { return value.name; }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalObjectTypeMember>> CanonicalHeaderTypeSyntax::objectMembers()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::ObjectTypeData>()) { return value.members.asPtr(); }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> CanonicalHeaderTypeSyntax::tupleElements()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::TupleTypeData>()) { return value.elements.asPtr(); }
  return zc::none;
}

zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalHeaderTypeSyntax::associatedBase()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::AssociatedProjectionTypeData>()) {
    return value.base;
  }
  return zc::none;
}

zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalHeaderTypeSyntax::associatedInterface()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::AssociatedProjectionTypeData>()) {
    ZC_IF_SOME(interfaceType, value.interfaceType) { return interfaceType; }
  }
  return zc::none;
}

zc::Maybe<zc::StringPtr> CanonicalHeaderTypeSyntax::associatedMember() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::AssociatedProjectionTypeData>()) {
    return value.member.text();
  }
  return zc::none;
}

zc::Maybe<const CanonicalNamedHeaderType&> CanonicalHeaderTypeSyntax::dynamicPrincipal()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::DynamicTypeData>()) { return value.principal; }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalNameReference>> CanonicalHeaderTypeSyntax::dynamicMarkers()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::DynamicTypeData>()) { return value.markers.asPtr(); }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalAssociatedBinding>>
CanonicalHeaderTypeSyntax::dynamicBindings() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::DynamicTypeData>()) {
    return value.associatedBindings.asPtr();
  }
  return zc::none;
}

}  // namespace zomlang::compiler::identity
