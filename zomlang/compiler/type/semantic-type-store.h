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
#include "zc/core/one-of.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/canonical/identity-interner-set.h"
#include "zomlang/compiler/identity/identity-invariant.h"
#include "zomlang/compiler/identity/semantic/type-id.h"
#include "zomlang/compiler/type/semantic-type-key.h"

namespace zomlang::compiler::type {

using SemanticTypeId = identity::SemanticTypeId;

/// \brief Successful canonical semantic type interning result.
struct SemanticTypeInterned final {
  identity::SemanticTypeId id;
};

using SemanticTypeInternResult = zc::OneOf<SemanticTypeInterned, identity::IdentityInvariant>;

using SemanticTypeAdmissionResult =
    zc::OneOf<semantic::CanonicalTypeData, identity::IdentityInvariant>;

/// \brief Immutable lookup view backed by address-stable store payload.
class SemanticTypeLookup final {
public:
  /// \brief Returns the closed semantic type payload.
  ZC_NODISCARD const semantic::TypeData& data() const;

  /// \brief Returns the canonical semantic type key.
  ZC_NODISCARD const semantic::SemanticTypeKey& key() const;

private:
  SemanticTypeLookup(const semantic::TypeData& data, const semantic::SemanticTypeKey& key) noexcept;

  zc::Maybe<const semantic::TypeData&> dataValue;
  zc::Maybe<const semantic::SemanticTypeKey&> keyValue;

  friend class SemanticTypeStore;
};

using SemanticTypeLookupResult = zc::OneOf<SemanticTypeLookup, identity::IdentityInvariant>;

/// \brief Context-global append-only canonical semantic type store.
class SemanticTypeStore final {
public:
  SemanticTypeStore(identity::SemanticTypeStoreConstructionToken&& token,
                    const identity::IdentityInternerSet& identities);
  ~SemanticTypeStore() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(SemanticTypeStore);

  /// \brief Validates and canonicalizes one closed payload against this store's identity authority.
  ZC_NODISCARD SemanticTypeAdmissionResult canonicalizeClosed(semantic::TypeData&& data) const;

  /// \brief Interns one validated canonical semantic type payload.
  /// \param canonical Payload admitted by the type canonicalizer.
  /// \return An existing or newly-issued identity, or a structured identity invariant.
  ZC_NODISCARD SemanticTypeInternResult intern(semantic::CanonicalTypeData&& canonical);

  /// \brief Looks up immutable data and key after context and slot validation.
  ZC_NODISCARD SemanticTypeLookupResult get(identity::SemanticTypeId id) const;

  /// \brief Returns the number of unique canonical semantic types.
  ZC_NODISCARD size_t size() const;

  /// \brief Returns the semantic context that owns this store.
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;

private:
  ZC_NODISCARD zc::Maybe<identity::IdentityInvariantKind> validateTypeForAdmission(
      identity::SemanticTypeId id) const;
  ZC_NODISCARD zc::Maybe<const semantic::TypeData&> typeDataForAdmission(
      identity::SemanticTypeId id) const;
  ZC_NODISCARD zc::Maybe<identity::IdentityInvariantKind> validateDefinitionForAdmission(
      identity::DefId id) const;
  ZC_NODISCARD zc::Maybe<identity::DefinitionKey> definitionKeyForAdmission(
      identity::DefId id) const;
  ZC_NODISCARD zc::Maybe<identity::DefinitionIdentityRecord> definitionRecordForAdmission(
      identity::DefId id) const;
  ZC_NODISCARD zc::Maybe<identity::IdentityInvariantKind> validateGenericParameterForAdmission(
      const identity::GenericParameterKey& key) const;

  struct Impl;
  zc::Own<Impl> impl;

  friend class semantic::StoreBoundTypeEncoder;
};

}  // namespace zomlang::compiler::type
