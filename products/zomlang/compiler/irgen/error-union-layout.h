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

#pragma once

#include <cstdint>

#include "zc/core/vector.h"
#include "zomlang/compiler/irgen/target-data-layout.h"
#include "zomlang/compiler/type/type-interner.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace irgen {

/// \brief Role of one alternative in a lowered error union.
enum class ErrorUnionAlternativeKind : uint8_t {
  Success,
  Error,
};

/// \brief Unsigned integer representation used for the error-union tag.
enum class ErrorUnionTagType : uint8_t {
  U8,
  U16,
  U32,
  U64,
};

/// \brief Physical representation selected for an error union.
enum class ErrorUnionLayoutKind : uint8_t {
  DirectSuccess,
  TaggedUnion,
};

/// \brief Whether all information required for a payload layout is available.
enum class ErrorUnionPayloadLayoutState : uint8_t {
  Known,
  Unknown,
};

/// \brief Target layout assigned to one canonical error-union alternative.
struct ErrorUnionAlternativeLayout {
  uint64_t tag = 0;
  type::TypeId typeId;
  ErrorUnionAlternativeKind kind = ErrorUnionAlternativeKind::Error;
  ErrorUnionPayloadLayoutState payloadLayoutState = ErrorUnionPayloadLayoutState::Unknown;
  uint64_t payloadSize = 0;
  uint64_t payloadAlign = 1;
};

/// \brief Target-aware physical layout of a success value and its error alternatives.
struct ErrorUnionLayout {
  type::TypeId typeId;
  ErrorUnionLayoutKind kind = ErrorUnionLayoutKind::TaggedUnion;
  ErrorUnionTagType tagType = ErrorUnionTagType::U8;
  ErrorUnionPayloadLayoutState payloadLayoutState = ErrorUnionPayloadLayoutState::Unknown;
  uint64_t tagOffset = 0;
  uint64_t payloadOffset = 0;
  uint64_t payloadSize = 0;
  uint64_t payloadAlign = 1;
  uint64_t size = 0;
  uint64_t align = 1;
  zc::Vector<ErrorUnionAlternativeLayout> alternatives;
};

/// \brief Compute the canonical target layout for an error union.
/// \param interner Canonical type interner used to identify and order alternatives.
/// \param target Target ABI data layout used for pointer-dependent payloads.
/// \param unionType Complete success-or-error type to lower.
/// \param successType Alternative represented by tag zero.
/// \return A direct-success layout or a tagged layout with canonically ordered errors.
ErrorUnionLayout computeErrorUnionLayout(type::TypeInterner& interner,
                                         const TargetDataLayout& target,
                                         const type::Type& unionType,
                                         const type::Type& successType);

/// \brief Compute a function error-union layout without constructing an owning union type.
/// \param interner Canonical type interner used to identify and order alternatives.
/// \param target Target ABI data layout used for pointer-dependent payloads.
/// \param successType Function return type represented by tag zero.
/// \param raisesType Function raises component, possibly itself a union.
/// \return A direct-success layout or a tagged layout with canonically ordered errors.
ErrorUnionLayout computeFunctionErrorUnionLayout(type::TypeInterner& interner,
                                                 const TargetDataLayout& target,
                                                 const type::Type& successType,
                                                 const type::Type& raisesType);

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
