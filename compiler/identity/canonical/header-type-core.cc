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

#include "compiler/identity/canonical/header-type-data.h"
#include "compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::identity {
namespace detail = canonical_header_type_detail;
namespace {

struct EncodedType final {
  zc::Array<uint8_t> bytes;
  CanonicalHeaderTypeSyntax value;
};

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

zc::Vector<CanonicalHeaderTypeSyntax> cloneTypes(
    zc::ArrayPtr<const CanonicalHeaderTypeSyntax> values) {
  zc::Vector<CanonicalHeaderTypeSyntax> result(values.size());
  for (const auto& value : values) { result.add(value.clone()); }
  return result;
}

zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> cloneTypes(
    const zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>& values) {
  ZC_IF_SOME(value, values) { return cloneTypes(value.asPtr()); }
  return zc::none;
}

zc::Vector<CanonicalHeaderTypeSyntax> sortUnique(zc::Vector<CanonicalHeaderTypeSyntax>&& values) {
  zc::Vector<EncodedType> encoded(values.size());
  for (auto& value : values) { encoded.add(EncodedType{value.encode(), zc::mv(value)}); }
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
  zc::Vector<CanonicalHeaderTypeSyntax> result(encoded.size());
  for (size_t index = 0; index < encoded.size(); ++index) {
    if (index == 0 || encoded[index - 1].bytes.asPtr() != encoded[index].bytes.asPtr()) {
      result.add(zc::mv(encoded[index].value));
    }
  }
  return result;
}

void appendFlattened(CanonicalHeaderTypeSyntaxKind kind, CanonicalHeaderTypeSyntax&& value,
                     zc::Vector<CanonicalHeaderTypeSyntax>& output) {
  if (value.kind() != kind) {
    output.add(zc::mv(value));
    return;
  }
  ZC_IF_SOME(members, value.members()) {
    for (const auto& member : members) { appendFlattened(kind, member.clone(), output); }
    return;
  }
  ZC_UNREACHABLE
}

}  // namespace

bool isCanonicalHeaderTypeSyntaxKind(CanonicalHeaderTypeSyntaxKind value) noexcept {
  return value >= CanonicalHeaderTypeSyntaxKind::Named &&
         value <= CanonicalHeaderTypeSyntaxKind::Dynamic;
}

CanonicalHeaderTypeSyntax::CanonicalHeaderTypeSyntax(
    zc::Own<detail::CanonicalHeaderTypeSyntaxData>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalHeaderTypeSyntax::~CanonicalHeaderTypeSyntax() noexcept(false) = default;
CanonicalHeaderTypeSyntax::CanonicalHeaderTypeSyntax(CanonicalHeaderTypeSyntax&&) noexcept =
    default;
CanonicalHeaderTypeSyntax& CanonicalHeaderTypeSyntax::operator=(
    CanonicalHeaderTypeSyntax&&) noexcept = default;

CanonicalHeaderTypeSyntaxKind CanonicalHeaderTypeSyntax::kind() const noexcept {
  const auto& value = impl->value;
  if (value.is<detail::NamedTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Named; }
  if (value.is<detail::PredefinedTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Predefined; }
  if (value.is<detail::FunctionTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Function; }
  if (value.is<detail::UnionTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Union; }
  if (value.is<detail::IntersectionTypeData>()) {
    return CanonicalHeaderTypeSyntaxKind::Intersection;
  }
  if (value.is<detail::FixedArrayTypeData>()) { return CanonicalHeaderTypeSyntaxKind::FixedArray; }
  if (value.is<detail::DynamicArrayTypeData>()) {
    return CanonicalHeaderTypeSyntaxKind::DynamicArray;
  }
  if (value.is<detail::SliceTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Slice; }
  if (value.is<detail::OptionalTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Optional; }
  if (value.is<detail::ReferenceTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Reference; }
  if (value.is<detail::RawPointerTypeData>()) { return CanonicalHeaderTypeSyntaxKind::RawPointer; }
  if (value.is<detail::TypeQueryTypeData>()) { return CanonicalHeaderTypeSyntaxKind::TypeQuery; }
  if (value.is<detail::ObjectTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Object; }
  if (value.is<detail::TupleTypeData>()) { return CanonicalHeaderTypeSyntaxKind::Tuple; }
  if (value.is<detail::AssociatedProjectionTypeData>()) {
    return CanonicalHeaderTypeSyntaxKind::AssociatedProjection;
  }
  return CanonicalHeaderTypeSyntaxKind::Dynamic;
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::clone() const {
  const auto& value = impl->value;
  if (value.is<detail::NamedTypeData>()) {
    return CanonicalHeaderTypeSyntax(
        makeData(detail::NamedTypeData{value.get<detail::NamedTypeData>().type.clone()}));
  }
  if (value.is<detail::PredefinedTypeData>()) {
    return CanonicalHeaderTypeSyntax(
        makeData(detail::PredefinedTypeData{value.get<detail::PredefinedTypeData>().kind}));
  }
  if (value.is<detail::FunctionTypeData>()) {
    const auto& input = value.get<detail::FunctionTypeData>();
    return CanonicalHeaderTypeSyntax(makeData(detail::FunctionTypeData{
        cloneTypes(input.parameters.asPtr()), input.result.clone(), cloneTypes(input.raises)}));
  }
  if (value.is<detail::UnionTypeData>()) {
    return CanonicalHeaderTypeSyntax(makeData(
        detail::UnionTypeData{cloneTypes(value.get<detail::UnionTypeData>().members.asPtr())}));
  }
  if (value.is<detail::IntersectionTypeData>()) {
    return CanonicalHeaderTypeSyntax(makeData(detail::IntersectionTypeData{
        cloneTypes(value.get<detail::IntersectionTypeData>().members.asPtr())}));
  }
  if (value.is<detail::FixedArrayTypeData>()) {
    const auto& input = value.get<detail::FixedArrayTypeData>();
    return CanonicalHeaderTypeSyntax(
        makeData(detail::FixedArrayTypeData{input.element.clone(), input.length}));
  }
  if (value.is<detail::DynamicArrayTypeData>()) {
    return CanonicalHeaderTypeSyntax(makeData(
        detail::DynamicArrayTypeData{value.get<detail::DynamicArrayTypeData>().element.clone()}));
  }
  if (value.is<detail::SliceTypeData>()) {
    return CanonicalHeaderTypeSyntax(
        makeData(detail::SliceTypeData{value.get<detail::SliceTypeData>().element.clone()}));
  }
  if (value.is<detail::OptionalTypeData>()) {
    const auto& input = value.get<detail::OptionalTypeData>();
    return CanonicalHeaderTypeSyntax(
        makeData(detail::OptionalTypeData{input.element.clone(), input.depth}));
  }
  if (value.is<detail::ReferenceTypeData>()) {
    const auto& input = value.get<detail::ReferenceTypeData>();
    return CanonicalHeaderTypeSyntax(
        makeData(detail::ReferenceTypeData{input.mutability, input.element.clone()}));
  }
  if (value.is<detail::RawPointerTypeData>()) {
    const auto& input = value.get<detail::RawPointerTypeData>();
    return CanonicalHeaderTypeSyntax(
        makeData(detail::RawPointerTypeData{input.mutability, input.element.clone()}));
  }
  if (value.is<detail::TypeQueryTypeData>()) {
    return CanonicalHeaderTypeSyntax(
        makeData(detail::TypeQueryTypeData{value.get<detail::TypeQueryTypeData>().name.clone()}));
  }
  if (value.is<detail::ObjectTypeData>()) {
    const auto& input = value.get<detail::ObjectTypeData>().members;
    zc::Vector<CanonicalObjectTypeMember> members(input.size());
    for (const auto& member : input) { members.add(member.clone()); }
    return CanonicalHeaderTypeSyntax(makeData(detail::ObjectTypeData{zc::mv(members)}));
  }
  if (value.is<detail::TupleTypeData>()) {
    return CanonicalHeaderTypeSyntax(makeData(
        detail::TupleTypeData{cloneTypes(value.get<detail::TupleTypeData>().elements.asPtr())}));
  }
  if (value.is<detail::AssociatedProjectionTypeData>()) {
    const auto& input = value.get<detail::AssociatedProjectionTypeData>();
    zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType;
    ZC_IF_SOME(type, input.interfaceType) { interfaceType = type.clone(); }
    return CanonicalHeaderTypeSyntax(makeData(detail::AssociatedProjectionTypeData{
        input.base.clone(), zc::mv(interfaceType), input.member.clone()}));
  }
  const auto& input = value.get<detail::DynamicTypeData>();
  zc::Vector<CanonicalNameReference> markers(input.markers.size());
  for (const auto& marker : input.markers) { markers.add(marker.clone()); }
  zc::Vector<CanonicalAssociatedBinding> bindings(input.associatedBindings.size());
  for (const auto& binding : input.associatedBindings) { bindings.add(binding.clone()); }
  return CanonicalHeaderTypeSyntax(makeData(
      detail::DynamicTypeData{input.principal.clone(), zc::mv(markers), zc::mv(bindings)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::named(CanonicalNamedHeaderType&& type) {
  return CanonicalHeaderTypeSyntax(makeData(detail::NamedTypeData{zc::mv(type)}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::predefined(
    PredefinedTypeKind kind) {
  if (!isCanonicalHeaderValue(kind)) { return zc::none; }
  return CanonicalHeaderTypeSyntax(makeData(detail::PredefinedTypeData{kind}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::function(
    zc::Vector<CanonicalHeaderTypeSyntax>&& parameters, CanonicalHeaderTypeSyntax&& result,
    zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>>&& raises) {
  ZC_IF_SOME(values, raises) {
    if (values.size() == 0) { return zc::none; }
    values = sortUnique(zc::mv(values));
  }
  return CanonicalHeaderTypeSyntax(
      makeData(detail::FunctionTypeData{zc::mv(parameters), zc::mv(result), zc::mv(raises)}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::unionOf(
    zc::Vector<CanonicalHeaderTypeSyntax>&& members) {
  if (members.size() == 0) { return zc::none; }
  zc::Vector<CanonicalHeaderTypeSyntax> flattened(members.size());
  for (auto& member : members) {
    appendFlattened(CanonicalHeaderTypeSyntaxKind::Union, zc::mv(member), flattened);
  }
  auto normalized = sortUnique(zc::mv(flattened));
  if (normalized.size() == 1) { return zc::mv(normalized[0]); }
  return CanonicalHeaderTypeSyntax(makeData(detail::UnionTypeData{zc::mv(normalized)}));
}

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::intersectionOf(
    zc::Vector<CanonicalHeaderTypeSyntax>&& members) {
  if (members.size() == 0) { return zc::none; }
  zc::Vector<CanonicalHeaderTypeSyntax> flattened(members.size());
  for (auto& member : members) {
    appendFlattened(CanonicalHeaderTypeSyntaxKind::Intersection, zc::mv(member), flattened);
  }
  auto normalized = sortUnique(zc::mv(flattened));
  if (normalized.size() == 1) { return zc::mv(normalized[0]); }
  return CanonicalHeaderTypeSyntax(makeData(detail::IntersectionTypeData{zc::mv(normalized)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::fixedArray(CanonicalHeaderTypeSyntax&& element,
                                                                uint64_t evaluatedLength) {
  return CanonicalHeaderTypeSyntax(
      makeData(detail::FixedArrayTypeData{zc::mv(element), evaluatedLength}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::dynamicArray(
    CanonicalHeaderTypeSyntax&& element) {
  return CanonicalHeaderTypeSyntax(makeData(detail::DynamicArrayTypeData{zc::mv(element)}));
}

CanonicalHeaderTypeSyntax CanonicalHeaderTypeSyntax::slice(CanonicalHeaderTypeSyntax&& element) {
  return CanonicalHeaderTypeSyntax(makeData(detail::SliceTypeData{zc::mv(element)}));
}

zc::Maybe<const CanonicalNamedHeaderType&> CanonicalHeaderTypeSyntax::namedType() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::NamedTypeData>()) { return value.type; }
  return zc::none;
}

zc::Maybe<PredefinedTypeKind> CanonicalHeaderTypeSyntax::predefinedKind() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::PredefinedTypeData>()) { return value.kind; }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>>
CanonicalHeaderTypeSyntax::functionParameters() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::FunctionTypeData>()) {
    return value.parameters.asPtr();
  }
  return zc::none;
}

zc::Maybe<const CanonicalHeaderTypeSyntax&> CanonicalHeaderTypeSyntax::functionResult()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::FunctionTypeData>()) { return value.result; }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> CanonicalHeaderTypeSyntax::functionRaises()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::FunctionTypeData>()) {
    ZC_IF_SOME(raises, value.raises) { return raises.asPtr(); }
  }
  return zc::none;
}

zc::Maybe<zc::ArrayPtr<const CanonicalHeaderTypeSyntax>> CanonicalHeaderTypeSyntax::members()
    const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::UnionTypeData>()) { return value.members.asPtr(); }
  ZC_IF_SOME(value, impl->value.tryGet<detail::IntersectionTypeData>()) {
    return value.members.asPtr();
  }
  return zc::none;
}

zc::Maybe<uint64_t> CanonicalHeaderTypeSyntax::fixedArrayLength() const noexcept {
  ZC_IF_SOME(value, impl->value.tryGet<detail::FixedArrayTypeData>()) { return value.length; }
  return zc::none;
}

}  // namespace zomlang::compiler::identity
