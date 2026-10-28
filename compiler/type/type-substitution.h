// Copyright (c) 2026 Zode.Z. All rights reserved.
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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/one-of.h"
#include "compiler/identity/handle.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::type::semantic {

/// \brief One generic-parameter to closed-type mapping applied during substitution.
struct TypeSubstitution final {
  identity::GenericParameterKey parameter;
  identity::SemanticTypeId argument;
};

using SubstituteTypeResult =
    zc::OneOf<identity::SemanticTypeId, identity::IdentityInvariant>;

/// \brief Substitutes generic type parameters through one closed semantic type.
///
/// Walks every `TypeData` branch, replacing each `TypeParameter` leaf named by
/// a substitution with its argument (recursively, so an argument may itself
/// mention mapped parameters), rebuilding composite branches and re-interning
/// them canonically. Types without a mapped parameter intern to their original
/// id, so callers can compare ids before and after substitution.
///
/// \param store Session-owned canonical semantic type store.
/// \param type Closed or parametric semantic type to substitute.
/// \param substitutions Generic-parameter mappings applied left to right.
/// \return The substituted canonical type id, or an identity invariant.
ZC_NODISCARD SubstituteTypeResult substituteType(
    SemanticTypeStore& store, identity::SemanticTypeId type,
    zc::ArrayPtr<const TypeSubstitution> substitutions);

}  // namespace zomlang::compiler::type::semantic
