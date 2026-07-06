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

#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief Directional coercion kinds accepted by RFC 0005.
enum class CoercionKind {
  Identity,
  NeverToAny,
  ToAny,
  MutRefToSharedRef,
  MutRawToConstRaw,
  UnionInjection,
  NullToNullableUnion,
  ExistentialErasure,
  DynUpcast
};

/// \brief Result of checking a directional coercion.
struct CoercionResult {
  bool success;
  CoercionKind kind;

  operator bool() const { return success; }
};

/// \brief Checks directional subtype/coercion relations outside unification.
class CoercionResolver final {
public:
  CoercionResolver() = default;
  ~CoercionResolver() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(CoercionResolver);

  /// \brief Return true if a value of source type can coerce to target type.
  bool canCoerce(const Type& source, const Type& target) const;

  /// \brief Return the matching coercion kind, or failure.
  CoercionResult check(const Type& source, const Type& target) const;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
