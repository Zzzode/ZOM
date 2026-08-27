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

#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/canonical/header-type-data.h"
#include "compiler/identity/canonical/header-type.h"

namespace zomlang::compiler::identity {
namespace detail = canonical_header_type_detail;
namespace {

template <typename Value>
void encodeSequence(CanonicalEncoder& encoder, zc::ArrayPtr<const Value> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { value.encode(encoder); }
}

}  // namespace

void CanonicalHeaderTypeSyntax::encode(CanonicalEncoder& encoder) const {
  const auto typeKind = kind();
  encoder.encodeUint8(static_cast<uint8_t>(typeKind));
  switch (typeKind) {
    case CanonicalHeaderTypeSyntaxKind::Named:
      impl->value.get<detail::NamedTypeData>().type.encode(encoder);
      return;
    case CanonicalHeaderTypeSyntaxKind::Predefined:
      encoder.encodeUint8(static_cast<uint8_t>(impl->value.get<detail::PredefinedTypeData>().kind));
      return;
    case CanonicalHeaderTypeSyntaxKind::Function: {
      const auto& value = impl->value.get<detail::FunctionTypeData>();
      encodeSequence(encoder, value.parameters.asPtr());
      value.result.encode(encoder);
      ZC_IF_SOME(raises, value.raises) {
        encoder.encodeSome();
        encodeSequence(encoder, raises.asPtr());
      } else {
        encoder.encodeNone();
      }
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::Union:
      encodeSequence(encoder, impl->value.get<detail::UnionTypeData>().members.asPtr());
      return;
    case CanonicalHeaderTypeSyntaxKind::Intersection:
      encodeSequence(encoder, impl->value.get<detail::IntersectionTypeData>().members.asPtr());
      return;
    case CanonicalHeaderTypeSyntaxKind::FixedArray: {
      const auto& value = impl->value.get<detail::FixedArrayTypeData>();
      value.element.encode(encoder);
      encoder.encodeUint64(value.length);
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::DynamicArray:
      impl->value.get<detail::DynamicArrayTypeData>().element.encode(encoder);
      return;
    case CanonicalHeaderTypeSyntaxKind::Slice:
      impl->value.get<detail::SliceTypeData>().element.encode(encoder);
      return;
    case CanonicalHeaderTypeSyntaxKind::Optional: {
      const auto& value = impl->value.get<detail::OptionalTypeData>();
      value.element.encode(encoder);
      encoder.encodeUint8(value.depth);
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::Reference: {
      const auto& value = impl->value.get<detail::ReferenceTypeData>();
      encoder.encodeUint8(static_cast<uint8_t>(value.mutability));
      value.element.encode(encoder);
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::RawPointer: {
      const auto& value = impl->value.get<detail::RawPointerTypeData>();
      encoder.encodeUint8(static_cast<uint8_t>(value.mutability));
      value.element.encode(encoder);
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::TypeQuery:
      impl->value.get<detail::TypeQueryTypeData>().name.encode(encoder);
      return;
    case CanonicalHeaderTypeSyntaxKind::Object:
      encodeSequence(encoder, impl->value.get<detail::ObjectTypeData>().members.asPtr());
      return;
    case CanonicalHeaderTypeSyntaxKind::Tuple:
      encodeSequence(encoder, impl->value.get<detail::TupleTypeData>().elements.asPtr());
      return;
    case CanonicalHeaderTypeSyntaxKind::AssociatedProjection: {
      const auto& value = impl->value.get<detail::AssociatedProjectionTypeData>();
      value.base.encode(encoder);
      ZC_IF_SOME(interfaceType, value.interfaceType) {
        encoder.encodeSome();
        interfaceType.encode(encoder);
      } else {
        encoder.encodeNone();
      }
      value.member.encode(encoder);
      return;
    }
    case CanonicalHeaderTypeSyntaxKind::Dynamic: {
      const auto& value = impl->value.get<detail::DynamicTypeData>();
      value.principal.encode(encoder);
      encodeSequence(encoder, value.markers.asPtr());
      encodeSequence(encoder, value.associatedBindings.asPtr());
      return;
    }
  }
  ZC_UNREACHABLE
}

zc::Array<uint8_t> CanonicalHeaderTypeSyntax::encode() const {
  CanonicalEncoder encoder;
  encode(encoder);
  return encoder.finish();
}

}  // namespace zomlang::compiler::identity
