// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

/// \brief Clone a type tree into a new owned value.
/// \param type The type tree to clone.
/// \return An owned structural copy of the type.
zc::Own<Type> cloneType(const Type& type);

/// \brief Find the first type variable with the requested name in a type tree.
/// \param type The type tree to search.
/// \param name The type variable name to find.
/// \return A reference to the first matching type variable, or none.
zc::Maybe<const Type&> findTypeVarByName(const Type& type, zc::StringPtr name);

struct GenericSubstitution {
  zc::String name;
  zc::Own<Type> type;
};

zc::Maybe<const Type&> lookupGenericSubstitution(
    zc::ArrayPtr<const GenericSubstitution> substitutions, zc::StringPtr name);

bool bindGenericSubstitution(zc::Vector<GenericSubstitution>& substitutions, zc::StringPtr name,
                             const Type& concrete);

bool isGenericParamName(zc::ArrayPtr<const zc::StringPtr> genericNames, zc::StringPtr name);

bool matchGenericTypePattern(zc::ArrayPtr<const zc::StringPtr> genericNames, const Type& pattern,
                             const Type& concrete, zc::Vector<GenericSubstitution>& substitutions);

zc::Own<Type> substituteGenericTypePattern(zc::ArrayPtr<const zc::StringPtr> genericNames,
                                           const Type& pattern,
                                           zc::ArrayPtr<const GenericSubstitution> substitutions);

bool isBareGenericTypePattern(zc::ArrayPtr<const zc::StringPtr> genericNames, const Type& type);

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
