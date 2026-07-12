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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/semantic-type-id.h"

namespace zomlang::compiler::type {

class Type;
using SemanticTypeId = identity::SemanticTypeId;

/// \brief Immutable semantic type lookup view backed by address-stable store payload.
struct SemanticTypeLookup final {
  const Type& data;
  zc::StringPtr canonicalKey;
};

/// \brief Context-global append-only canonical semantic type store.
class SemanticTypeStore final {
public:
  explicit SemanticTypeStore(identity::SemanticTypeStoreConstructionToken&& token);
  ~SemanticTypeStore() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(SemanticTypeStore);

  /// \brief Canonicalizes and interns one type tree.
  /// \param type Type tree whose structural identity is required.
  /// \return The context-branded semantic type identity.
  ZC_NODISCARD identity::SemanticTypeId intern(const Type& type);

  /// \brief Canonicalizes and interns a flattened union of two type trees.
  ZC_NODISCARD identity::SemanticTypeId internUnion(const Type& first, const Type& second);

  /// \brief Returns true when an identity belongs to this store and is in range.
  ZC_NODISCARD bool contains(identity::SemanticTypeId id) const;

  /// \brief Looks up immutable type data after context and slot validation.
  ZC_NODISCARD zc::Maybe<SemanticTypeLookup> get(identity::SemanticTypeId id) const;

  /// \brief Returns the canonical key for a valid semantic type identity.
  ZC_NODISCARD zc::StringPtr getCanonicalKey(identity::SemanticTypeId id) const;

  /// \brief Returns the number of unique canonical semantic types.
  ZC_NODISCARD size_t size() const;

  /// \brief Returns the semantic context that owns this store.
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::type
