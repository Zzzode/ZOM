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

#include "zc/core/debug.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-header-type.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint64_t kMaximumCanonicalHeaderBytes = 4 * 1024 * 1024;
constexpr uint32_t kMaximumCanonicalHeaderTypeDepth = 100;
constexpr uint64_t kMinimumEncodedTypeBytes = 1;
constexpr uint64_t kMinimumEncodedNameBytes = sizeof(uint64_t) + 1;
constexpr uint64_t kMinimumEncodedObjectMemberBytes =
    kMinimumEncodedNameBytes + kMinimumEncodedTypeBytes + 2;
constexpr uint64_t kMinimumEncodedBindingBytes =
    kMinimumEncodedNameBytes + kMinimumEncodedTypeBytes;

zc::Maybe<CanonicalHeaderTypeSyntax> decodeType(CanonicalDecoder& decoder, uint32_t remainingDepth);

zc::Maybe<CanonicalHeaderTypeSyntax> decodeNamed(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeFunction(CanonicalDecoder& decoder,
                                                    uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeUnion(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeIntersection(CanonicalDecoder& decoder,
                                                        uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeObject(CanonicalDecoder& decoder,
                                                  uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeTuple(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeAssociatedProjection(CanonicalDecoder& decoder,
                                                                uint32_t remainingDepth);
zc::Maybe<CanonicalHeaderTypeSyntax> decodeDynamic(CanonicalDecoder& decoder,
                                                   uint32_t remainingDepth);

zc::Maybe<uint64_t> decodeCount(CanonicalDecoder& decoder, uint64_t minimumElementBytes) {
  auto count = decoder.decodeSequenceSize(kMaximumCanonicalHeaderBytes / minimumElementBytes);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) > decoder.remaining() / minimumElementBytes) {
    return zc::none;
  }
  return count;
}

zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> decodeTypes(CanonicalDecoder& decoder,
                                                             uint32_t remainingDepth) {
  auto count = decodeCount(decoder, kMinimumEncodedTypeBytes);
  if (count == zc::none) { return zc::none; }
  zc::Vector<CanonicalHeaderTypeSyntax> values(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto value = decodeType(decoder, remainingDepth);
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  return values;
}

zc::Maybe<CanonicalNamedHeaderType> decodeNamedHeader(CanonicalDecoder& decoder,
                                                      uint32_t remainingDepth) {
  auto name = CanonicalNameReference::decodeCanonical(decoder);
  auto arguments = decodeTypes(decoder, remainingDepth);
  if (name == zc::none || arguments == zc::none) { return zc::none; }
  return CanonicalNamedHeaderType::from(zc::mv(ZC_ASSERT_NONNULL(name)),
                                        zc::mv(ZC_ASSERT_NONNULL(arguments)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeNamed(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth) {
  auto type = decodeNamedHeader(decoder, remainingDepth);
  if (type == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::named(zc::mv(ZC_ASSERT_NONNULL(type)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeFunction(CanonicalDecoder& decoder,
                                                    uint32_t remainingDepth) {
  auto parameters = decodeTypes(decoder, remainingDepth);
  auto result = decodeType(decoder, remainingDepth);
  auto raisesPresence = decoder.decodeUint8();
  if (parameters == zc::none || result == zc::none || raisesPresence == zc::none) {
    return zc::none;
  }
  zc::Maybe<zc::Vector<CanonicalHeaderTypeSyntax>> raises;
  switch (ZC_ASSERT_NONNULL(raisesPresence)) {
    case 0x00:
      break;
    case 0x01: {
      auto values = decodeTypes(decoder, remainingDepth);
      if (values == zc::none) { return zc::none; }
      raises = zc::mv(ZC_ASSERT_NONNULL(values));
      break;
    }
    default:
      return zc::none;
  }
  return CanonicalHeaderTypeSyntax::function(zc::mv(ZC_ASSERT_NONNULL(parameters)),
                                             zc::mv(ZC_ASSERT_NONNULL(result)), zc::mv(raises));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeUnion(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth) {
  auto members = decodeTypes(decoder, remainingDepth);
  if (members == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::unionOf(zc::mv(ZC_ASSERT_NONNULL(members)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeIntersection(CanonicalDecoder& decoder,
                                                        uint32_t remainingDepth) {
  auto members = decodeTypes(decoder, remainingDepth);
  if (members == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::intersectionOf(zc::mv(ZC_ASSERT_NONNULL(members)));
}

zc::Maybe<CanonicalObjectTypeMember> decodeObjectMember(CanonicalDecoder& decoder,
                                                        uint32_t remainingDepth) {
  auto name = SemanticIdentifier::decodeCanonical(decoder);
  auto type = decodeType(decoder, remainingDepth);
  auto isMutable = decoder.decodeBool();
  auto isOptional = decoder.decodeBool();
  if (name == zc::none || type == zc::none || isMutable == zc::none || isOptional == zc::none) {
    return zc::none;
  }
  return CanonicalObjectTypeMember::from(
      zc::mv(ZC_ASSERT_NONNULL(name)), zc::mv(ZC_ASSERT_NONNULL(type)),
      ZC_ASSERT_NONNULL(isMutable), ZC_ASSERT_NONNULL(isOptional));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeObject(CanonicalDecoder& decoder,
                                                  uint32_t remainingDepth) {
  auto count = decodeCount(decoder, kMinimumEncodedObjectMemberBytes);
  if (count == zc::none) { return zc::none; }
  zc::Vector<CanonicalObjectTypeMember> members(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto member = decodeObjectMember(decoder, remainingDepth);
    if (member == zc::none) { return zc::none; }
    members.add(zc::mv(ZC_ASSERT_NONNULL(member)));
  }
  return CanonicalHeaderTypeSyntax::object(zc::mv(members));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeTuple(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth) {
  auto elements = decodeTypes(decoder, remainingDepth);
  if (elements == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::tuple(zc::mv(ZC_ASSERT_NONNULL(elements)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeAssociatedProjection(CanonicalDecoder& decoder,
                                                                uint32_t remainingDepth) {
  auto base = decodeType(decoder, remainingDepth);
  auto interfacePresence = decoder.decodeUint8();
  if (base == zc::none || interfacePresence == zc::none) { return zc::none; }
  zc::Maybe<CanonicalHeaderTypeSyntax> interfaceType;
  switch (ZC_ASSERT_NONNULL(interfacePresence)) {
    case 0x00:
      break;
    case 0x01: {
      auto value = decodeType(decoder, remainingDepth);
      if (value == zc::none) { return zc::none; }
      interfaceType = zc::mv(ZC_ASSERT_NONNULL(value));
      break;
    }
    default:
      return zc::none;
  }
  auto member = SemanticIdentifier::decodeCanonical(decoder);
  if (member == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::associatedProjection(
      zc::mv(ZC_ASSERT_NONNULL(base)), zc::mv(interfaceType), zc::mv(ZC_ASSERT_NONNULL(member)));
}

zc::Maybe<CanonicalAssociatedBinding> decodeBinding(CanonicalDecoder& decoder,
                                                    uint32_t remainingDepth) {
  auto name = SemanticIdentifier::decodeCanonical(decoder);
  auto type = decodeType(decoder, remainingDepth);
  if (name == zc::none || type == zc::none) { return zc::none; }
  return CanonicalAssociatedBinding::from(zc::mv(ZC_ASSERT_NONNULL(name)),
                                          zc::mv(ZC_ASSERT_NONNULL(type)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeDynamic(CanonicalDecoder& decoder,
                                                   uint32_t remainingDepth) {
  auto principal = decodeNamedHeader(decoder, remainingDepth);
  auto markerCount = decodeCount(decoder, kMinimumEncodedNameBytes);
  if (principal == zc::none || markerCount == zc::none) { return zc::none; }
  zc::Vector<CanonicalNameReference> markers(static_cast<size_t>(ZC_ASSERT_NONNULL(markerCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(markerCount); ++index) {
    auto marker = CanonicalNameReference::decodeCanonical(decoder);
    if (marker == zc::none) { return zc::none; }
    markers.add(zc::mv(ZC_ASSERT_NONNULL(marker)));
  }
  auto bindingCount = decodeCount(decoder, kMinimumEncodedBindingBytes);
  if (bindingCount == zc::none) { return zc::none; }
  zc::Vector<CanonicalAssociatedBinding> bindings(
      static_cast<size_t>(ZC_ASSERT_NONNULL(bindingCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(bindingCount); ++index) {
    auto binding = decodeBinding(decoder, remainingDepth);
    if (binding == zc::none) { return zc::none; }
    bindings.add(zc::mv(ZC_ASSERT_NONNULL(binding)));
  }
  return CanonicalHeaderTypeSyntax::dynamic(zc::mv(ZC_ASSERT_NONNULL(principal)), zc::mv(markers),
                                            zc::mv(bindings));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeDynamicArray(CanonicalDecoder& decoder,
                                                        uint32_t remainingDepth) {
  auto element = decodeType(decoder, remainingDepth);
  if (element == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::dynamicArray(zc::mv(ZC_ASSERT_NONNULL(element)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeSlice(CanonicalDecoder& decoder,
                                                 uint32_t remainingDepth) {
  auto element = decodeType(decoder, remainingDepth);
  if (element == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::slice(zc::mv(ZC_ASSERT_NONNULL(element)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeFixedArray(CanonicalDecoder& decoder,
                                                      uint32_t remainingDepth) {
  auto element = decodeType(decoder, remainingDepth);
  auto length = decoder.decodeUint64();
  if (element == zc::none || length == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::fixedArray(zc::mv(ZC_ASSERT_NONNULL(element)),
                                               ZC_ASSERT_NONNULL(length));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeOptional(CanonicalDecoder& decoder,
                                                    uint32_t remainingDepth) {
  auto element = decodeType(decoder, remainingDepth);
  auto depth = decoder.decodeUint8();
  if (element == zc::none || depth == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::optional(zc::mv(ZC_ASSERT_NONNULL(element)),
                                             ZC_ASSERT_NONNULL(depth));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeReference(CanonicalDecoder& decoder,
                                                     uint32_t remainingDepth) {
  auto mutability = decoder.decodeUint8();
  auto element = decodeType(decoder, remainingDepth);
  if (mutability == zc::none || element == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::reference(
      static_cast<ReferenceMutability>(ZC_ASSERT_NONNULL(mutability)),
      zc::mv(ZC_ASSERT_NONNULL(element)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeRawPointer(CanonicalDecoder& decoder,
                                                      uint32_t remainingDepth) {
  auto mutability = decoder.decodeUint8();
  auto element = decodeType(decoder, remainingDepth);
  if (mutability == zc::none || element == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::rawPointer(
      static_cast<RawPointerMutability>(ZC_ASSERT_NONNULL(mutability)),
      zc::mv(ZC_ASSERT_NONNULL(element)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeTypeQuery(CanonicalDecoder& decoder) {
  auto name = CanonicalNameReference::decodeCanonical(decoder);
  if (name == zc::none) { return zc::none; }
  return CanonicalHeaderTypeSyntax::typeQuery(zc::mv(ZC_ASSERT_NONNULL(name)));
}

zc::Maybe<CanonicalHeaderTypeSyntax> decodeType(CanonicalDecoder& decoder,
                                                uint32_t remainingDepth) {
  if (remainingDepth == 0) { return zc::none; }
  auto tag = decoder.decodeUint8();
  if (tag == zc::none) { return zc::none; }
  const uint32_t childDepth = remainingDepth - 1;
  switch (static_cast<CanonicalHeaderTypeSyntaxKind>(ZC_ASSERT_NONNULL(tag))) {
    case CanonicalHeaderTypeSyntaxKind::Named:
      return decodeNamed(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Predefined: {
      auto kind = decoder.decodeUint8();
      if (kind == zc::none) { return zc::none; }
      return CanonicalHeaderTypeSyntax::predefined(
          static_cast<PredefinedTypeKind>(ZC_ASSERT_NONNULL(kind)));
    }
    case CanonicalHeaderTypeSyntaxKind::Function:
      return decodeFunction(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Union:
      return decodeUnion(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Intersection:
      return decodeIntersection(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::FixedArray:
      return decodeFixedArray(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::DynamicArray:
      return decodeDynamicArray(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Slice:
      return decodeSlice(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Optional:
      return decodeOptional(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Reference:
      return decodeReference(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::RawPointer:
      return decodeRawPointer(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::TypeQuery:
      return decodeTypeQuery(decoder);
    case CanonicalHeaderTypeSyntaxKind::Object:
      return decodeObject(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Tuple:
      return decodeTuple(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::AssociatedProjection:
      return decodeAssociatedProjection(decoder, childDepth);
    case CanonicalHeaderTypeSyntaxKind::Dynamic:
      return decodeDynamic(decoder, childDepth);
  }
  return zc::none;
}

}  // namespace

zc::Maybe<CanonicalHeaderTypeSyntax> CanonicalHeaderTypeSyntax::decodeCanonical(
    CanonicalDecoder& decoder) {
  return decodeType(decoder, kMaximumCanonicalHeaderTypeDepth);
}

}  // namespace zomlang::compiler::identity
