// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/irgen/error-union-layout.h"

#include "zc/core/string.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/union-type.h"

namespace zomlang {
namespace compiler {
namespace irgen {

namespace {

struct PayloadLayout {
  ErrorUnionPayloadLayoutState state = ErrorUnionPayloadLayoutState::Unknown;
  uint64_t size = 0;
  uint64_t align = 1;
};

struct ErrorCandidate {
  type::SemanticTypeId semanticTypeId;
  zc::String canonicalKey;
  PayloadLayout payload;
};

struct ObjectFieldLayout {
  zc::String name;
  PayloadLayout payload;
};

uint64_t alignUp(uint64_t value, uint64_t alignment) {
  if (alignment <= 1) { return value; }
  const uint64_t remainder = value % alignment;
  if (remainder == 0) { return value; }
  return value + (alignment - remainder);
}

ErrorUnionTagType selectTagType(size_t alternativeCount) {
  if (alternativeCount <= 256) { return ErrorUnionTagType::U8; }
  if (alternativeCount <= 65536) { return ErrorUnionTagType::U16; }
  if (alternativeCount <= 4294967296ULL) { return ErrorUnionTagType::U32; }
  return ErrorUnionTagType::U64;
}

ScalarLayout tagLayout(const TargetDataLayout& target, ErrorUnionTagType tagType) {
  switch (tagType) {
    case ErrorUnionTagType::U8:
      return target.getIntegerLayout(IntegerScalarWidth::W8);
    case ErrorUnionTagType::U16:
      return target.getIntegerLayout(IntegerScalarWidth::W16);
    case ErrorUnionTagType::U32:
      return target.getIntegerLayout(IntegerScalarWidth::W32);
    case ErrorUnionTagType::U64:
      return target.getIntegerLayout(IntegerScalarWidth::W64);
  }
  ZC_UNREACHABLE;
}

PayloadLayout payloadLayoutOf(const TargetDataLayout& target, const type::Type& valueType);

class AggregateLayoutBuilder final {
public:
  bool addField(PayloadLayout field) {
    if (field.state == ErrorUnionPayloadLayoutState::Unknown) {
      state = ErrorUnionPayloadLayoutState::Unknown;
      return false;
    }

    if (field.align > maxAlign) { maxAlign = field.align; }
    offset = alignUp(offset, field.align);
    offset += field.size;
    return true;
  }

  PayloadLayout finish() const {
    if (state == ErrorUnionPayloadLayoutState::Unknown) { return PayloadLayout{state, 0, 1}; }
    return PayloadLayout{state, alignUp(offset, maxAlign), maxAlign};
  }

private:
  ErrorUnionPayloadLayoutState state = ErrorUnionPayloadLayoutState::Known;
  uint64_t offset = 0;
  uint64_t maxAlign = 1;
};

PayloadLayout primitivePayloadLayout(const TargetDataLayout& target,
                                     const type::PrimitiveType& primitive) {
  using type::PrimitiveKind;

  switch (primitive.getPrimitiveKind()) {
    case PrimitiveKind::I8:
    case PrimitiveKind::U8: {
      const auto layout = target.getIntegerLayout(IntegerScalarWidth::W8);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::I16:
    case PrimitiveKind::U16: {
      const auto layout = target.getIntegerLayout(IntegerScalarWidth::W16);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::I32:
    case PrimitiveKind::U32: {
      const auto layout = target.getIntegerLayout(IntegerScalarWidth::W32);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::I64:
    case PrimitiveKind::U64: {
      const auto layout = target.getIntegerLayout(IntegerScalarWidth::W64);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::F32: {
      const auto layout = target.getFloatLayout(FloatScalarWidth::W32);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::F64: {
      const auto layout = target.getFloatLayout(FloatScalarWidth::W64);
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::Bool: {
      const auto layout = target.getBoolLayout();
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::Char: {
      const auto layout = target.getCharLayout();
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, layout.size, layout.align};
    }
    case PrimitiveKind::Str:
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, target.getPointerSize() * 2,
                           target.getPointerAlignment()};
    case PrimitiveKind::Null:
    case PrimitiveKind::Unit:
    case PrimitiveKind::Never:
      return PayloadLayout{ErrorUnionPayloadLayoutState::Known, 0, 1};
    case PrimitiveKind::Any:
      return PayloadLayout{ErrorUnionPayloadLayoutState::Unknown, 0, 1};
  }
  ZC_UNREACHABLE;
}

void sortObjectFields(zc::Vector<ObjectFieldLayout>& fields) {
  for (size_t i = 1; i < fields.size(); ++i) {
    size_t j = i;
    while (j > 0 && fields[j].name < fields[j - 1].name) {
      auto temporary = zc::mv(fields[j - 1]);
      fields[j - 1] = zc::mv(fields[j]);
      fields[j] = zc::mv(temporary);
      --j;
    }
  }
}

PayloadLayout objectPayloadLayout(const TargetDataLayout& target, const type::ObjectType& object) {
  auto members = object.getMembers();
  zc::Vector<ObjectFieldLayout> fields;
  fields.reserve(members.size());

  for (size_t i = 0; i < members.size(); ++i) {
    ZC_IF_SOME(memberType, members[i].type) {
      fields.add(ObjectFieldLayout{zc::str(members[i].name), payloadLayoutOf(target, memberType)});
    }
    else { return PayloadLayout{ErrorUnionPayloadLayoutState::Unknown, 0, 1}; }
  }

  sortObjectFields(fields);
  AggregateLayoutBuilder builder;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (!builder.addField(fields[i].payload)) { return builder.finish(); }
  }
  return builder.finish();
}

PayloadLayout payloadLayoutOf(const TargetDataLayout& target, const type::Type& valueType) {
  if (type::isPrimitive(valueType)) {
    return primitivePayloadLayout(target, static_cast<const type::PrimitiveType&>(valueType));
  }

  if (type::isReference(valueType) || type::isRawPointer(valueType)) {
    return PayloadLayout{ErrorUnionPayloadLayoutState::Known, target.getPointerSize(),
                         target.getPointerAlignment()};
  }

  if (type::isTuple(valueType)) {
    const auto& tuple = static_cast<const type::TupleType&>(valueType);
    AggregateLayoutBuilder builder;
    for (size_t i = 0; i < tuple.getElementCount(); ++i) {
      if (!builder.addField(payloadLayoutOf(target, tuple.getElementType(i)))) {
        return builder.finish();
      }
    }
    return builder.finish();
  }

  if (type::isObject(valueType)) {
    return objectPayloadLayout(target, static_cast<const type::ObjectType&>(valueType));
  }

  return PayloadLayout{ErrorUnionPayloadLayoutState::Unknown, 0, 1};
}

bool containsCandidate(const zc::Vector<ErrorCandidate>& candidates, zc::StringPtr canonicalKey) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].canonicalKey == canonicalKey) { return true; }
  }
  return false;
}

void addErrorCandidate(type::SemanticTypeStore& semanticTypes, const TargetDataLayout& target,
                       const type::Type& errorType, zc::Vector<ErrorCandidate>& candidates) {
  const auto semanticTypeId = semanticTypes.intern(errorType);
  auto canonicalKey = semanticTypes.getCanonicalKey(semanticTypeId);
  if (containsCandidate(candidates, canonicalKey)) { return; }
  candidates.add(
      ErrorCandidate{semanticTypeId, zc::str(canonicalKey), payloadLayoutOf(target, errorType)});
}

void collectErrorCandidates(type::SemanticTypeStore& semanticTypes, const TargetDataLayout& target,
                            const type::Type& valueType, const type::Type& successType,
                            zc::Vector<ErrorCandidate>& candidates) {
  if (type::isUnion(valueType)) {
    const auto& unionType = static_cast<const type::UnionType&>(valueType);
    for (size_t i = 0; i < unionType.getAlternativeCount(); ++i) {
      collectErrorCandidates(semanticTypes, target, unionType.getAlternative(i), successType,
                             candidates);
    }
    return;
  }

  if (!valueType.equals(successType) && !type::isNever(valueType)) {
    addErrorCandidate(semanticTypes, target, valueType, candidates);
  }
}

void sortErrorCandidates(zc::Vector<ErrorCandidate>& candidates) {
  for (size_t i = 1; i < candidates.size(); ++i) {
    size_t j = i;
    while (j > 0 && candidates[j].canonicalKey < candidates[j - 1].canonicalKey) {
      auto temporary = zc::mv(candidates[j - 1]);
      candidates[j - 1] = zc::mv(candidates[j]);
      candidates[j] = zc::mv(temporary);
      --j;
    }
  }
}

ErrorUnionLayout computeLayout(type::SemanticTypeStore& semanticTypes,
                               const TargetDataLayout& target, type::SemanticTypeId layoutType,
                               const type::Type& alternativesType, const type::Type& successType) {
  ErrorUnionLayout layout;
  layout.semanticTypeId = layoutType;

  zc::Vector<ErrorCandidate> errors;
  collectErrorCandidates(semanticTypes, target, alternativesType, successType, errors);
  sortErrorCandidates(errors);

  const auto successPayload = payloadLayoutOf(target, successType);
  layout.payloadLayoutState = successPayload.state;
  layout.payloadSize = successPayload.size;
  layout.payloadAlign = successPayload.align;
  layout.alternatives.add(ErrorUnionAlternativeLayout{
      0, semanticTypes.intern(successType), ErrorUnionAlternativeKind::Success,
      successPayload.state, successPayload.size, successPayload.align});

  if (errors.empty()) {
    layout.kind = ErrorUnionLayoutKind::DirectSuccess;
    if (layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known) {
      layout.size = layout.payloadSize;
      layout.align = layout.payloadAlign;
    }
    return layout;
  }

  layout.kind = ErrorUnionLayoutKind::TaggedUnion;
  layout.tagType = selectTagType(errors.size() + 1);

  for (size_t i = 0; i < errors.size(); ++i) {
    const auto& candidate = errors[i];
    layout.alternatives.add(ErrorUnionAlternativeLayout{
        static_cast<uint64_t>(i + 1), candidate.semanticTypeId, ErrorUnionAlternativeKind::Error,
        candidate.payload.state, candidate.payload.size, candidate.payload.align});
    if (candidate.payload.state == ErrorUnionPayloadLayoutState::Unknown) {
      layout.payloadLayoutState = ErrorUnionPayloadLayoutState::Unknown;
    }
    if (candidate.payload.size > layout.payloadSize) {
      layout.payloadSize = candidate.payload.size;
    }
    if (candidate.payload.align > layout.payloadAlign) {
      layout.payloadAlign = candidate.payload.align;
    }
  }

  const auto tag = tagLayout(target, layout.tagType);
  layout.payloadOffset = alignUp(tag.size, layout.payloadAlign);
  if (layout.payloadLayoutState == ErrorUnionPayloadLayoutState::Known) {
    layout.align = layout.payloadAlign > tag.align ? layout.payloadAlign : tag.align;
    layout.size = alignUp(layout.payloadOffset + layout.payloadSize, layout.align);
  }

  return layout;
}

}  // namespace

ErrorUnionLayout computeErrorUnionLayout(type::SemanticTypeStore& semanticTypes,
                                         const TargetDataLayout& target,
                                         const type::Type& unionType,
                                         const type::Type& successType) {
  return computeLayout(semanticTypes, target, semanticTypes.intern(unionType), unionType,
                       successType);
}

ErrorUnionLayout computeFunctionErrorUnionLayout(type::SemanticTypeStore& semanticTypes,
                                                 const TargetDataLayout& target,
                                                 const type::Type& successType,
                                                 const type::Type& raisesType) {
  return computeLayout(semanticTypes, target, semanticTypes.internUnion(successType, raisesType),
                       raisesType, successType);
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
