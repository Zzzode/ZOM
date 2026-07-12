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

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::identity {

class SemanticContextFactory;
class RegistryBrandIssuer;
class SemanticIdentityRegistrySet;
class SemanticTypeStoreConstructionToken;

}  // namespace zomlang::compiler::identity

namespace zomlang::compiler::type {

class SemanticTypeStore;

}  // namespace zomlang::compiler::type

namespace zomlang::compiler::identity {

/// \brief Optional process-root quota used to exercise and constrain brand issuance.
struct SemanticContextIssueBudget final {
  uint64_t contextBrands;
  uint64_t registryBrands;
};

/// \brief Process-local issuer identity for one semantic context.
class SemanticContextBrand final {
public:
  constexpr SemanticContextBrand() noexcept = default;

  /// \brief Returns true when this brand was issued by a semantic context factory.
  ZC_NODISCARD constexpr bool isValid() const noexcept { return token != 0; }

  constexpr bool operator==(SemanticContextBrand other) const noexcept {
    return token == other.token;
  }
  constexpr bool operator!=(SemanticContextBrand other) const noexcept { return !(*this == other); }

private:
  explicit constexpr SemanticContextBrand(uint64_t value) noexcept : token(value) {}

  uint64_t token = 0;

  friend class SemanticContextFactory;
};

/// \brief Context-local issuer identity for one store registry.
class RegistryBrand final {
public:
  constexpr RegistryBrand() noexcept = default;

  /// \brief Returns true when this brand was issued for a valid semantic context.
  ZC_NODISCARD constexpr bool isValid() const noexcept { return context.isValid() && token != 0; }

  /// \brief Returns true when this registry belongs to the supplied context.
  ZC_NODISCARD constexpr bool belongsTo(SemanticContextBrand expected) const noexcept {
    return isValid() && context == expected;
  }

  constexpr bool operator==(RegistryBrand other) const noexcept {
    return context == other.context && token == other.token;
  }
  constexpr bool operator!=(RegistryBrand other) const noexcept { return !(*this == other); }

private:
  constexpr RegistryBrand(SemanticContextBrand owner, uint64_t value) noexcept
      : context(owner), token(value) {}

  SemanticContextBrand context;
  uint64_t token = 0;

  friend class RegistryBrandIssuer;
};

/// \brief Move-only authority to construct the sole semantic type store for one context.
class SemanticTypeStoreConstructionToken final {
public:
  SemanticTypeStoreConstructionToken(SemanticTypeStoreConstructionToken&& other) noexcept;
  SemanticTypeStoreConstructionToken& operator=(
      SemanticTypeStoreConstructionToken&& other) noexcept;
  ZC_DISALLOW_COPY(SemanticTypeStoreConstructionToken);

  /// \brief Returns true while this token has not been consumed.
  ZC_NODISCARD bool isValid() const noexcept;

private:
  explicit SemanticTypeStoreConstructionToken(SemanticContextBrand owner) noexcept;
  ZC_NODISCARD SemanticContextBrand consume() noexcept;

  SemanticContextBrand context;

  friend class SemanticContextFactory;
  friend class type::SemanticTypeStore;
};

/// \brief Thread-safe context-local issuer of registry brands.
class RegistryBrandIssuer final {
public:
  ~RegistryBrandIssuer() noexcept(false);
  RegistryBrandIssuer(RegistryBrandIssuer&&) noexcept;
  RegistryBrandIssuer& operator=(RegistryBrandIssuer&&) noexcept;
  ZC_DISALLOW_COPY(RegistryBrandIssuer);

  /// \brief Issues a fresh registry brand owned by this issuer's context.
  /// \return A brand, or none for an invalid context or exhausted token space.
  ZC_NODISCARD zc::Maybe<RegistryBrand> issue() const;

private:
  struct Impl;
  explicit RegistryBrandIssuer(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;

  friend class SemanticContextFactory;
};

/// \brief Thread-safe process-root issuer of semantic context and registry brands.
class SemanticContextFactory final {
public:
  SemanticContextFactory() noexcept;
  explicit SemanticContextFactory(SemanticContextIssueBudget budget) noexcept;
  ~SemanticContextFactory() noexcept(false);
  SemanticContextFactory(SemanticContextFactory&&) noexcept;
  SemanticContextFactory& operator=(SemanticContextFactory&&) noexcept;
  ZC_DISALLOW_COPY(SemanticContextFactory);

  /// \brief Issues a fresh process-local semantic context brand.
  /// \return A brand, or none when the non-reusable token space is exhausted.
  ZC_NODISCARD zc::Maybe<SemanticContextBrand> issue() const;

  /// \brief Creates the one registry-brand issuer owned by a context from this factory.
  /// \return An issuer, or none for an invalid or already-claimed context.
  ZC_NODISCARD zc::Maybe<RegistryBrandIssuer> issueRegistryBrandIssuer(
      SemanticContextBrand context) const;

  /// \brief Claims the one semantic type store construction authority for a context.
  /// \return A move-only token, or none for an invalid or already-claimed context.
  ZC_NODISCARD zc::Maybe<SemanticTypeStoreConstructionToken>
  issueSemanticTypeStoreConstructionToken(SemanticContextBrand context) const;

private:
  ZC_NODISCARD bool claimIdentityRegistrySet(SemanticContextBrand context) const;

  struct Impl;
  zc::Own<Impl> impl;

  friend class SemanticIdentityRegistrySet;
};

}  // namespace zomlang::compiler::identity
