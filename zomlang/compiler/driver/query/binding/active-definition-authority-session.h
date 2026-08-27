// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/vector.h"
#include "zomlang/compiler/binder/stable/stable-binding-facts.h"
#include "zomlang/compiler/driver/query/binding/active-definition-authority-query.h"
#include "zomlang/compiler/driver/query/binding/active-identity-membership-query.h"
#include "zomlang/compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

using ContextualDefinitionAuthorityEntry =
    module_graph_query::CanonicalInputEntry<ContextualDefinitionKey,
                                            identity::DefinitionIdentityRecord>;
using ContextualImplementationAuthorityEntry =
    module_graph_query::CanonicalInputEntry<ContextualImplementationKey,
                                            ActiveImplementationMembershipRecord>;
using ContextualGenericParameterAuthorityEntry =
    module_graph_query::CanonicalInputEntry<ContextualGenericParameterKey,
                                            ActiveGenericParameterMembership>;
using ContextualCallableParameterAuthorityEntry =
    module_graph_query::CanonicalInputEntry<ContextualCallableParameterKey,
                                            ActiveCallableParameterMembershipRecord>;

/// \brief Complete canonical value installed by the contextual-authority transaction.
class ContextualIdentityAuthorityInputPayload final {
public:
  ~ContextualIdentityAuthorityInputPayload() noexcept(false);
  ContextualIdentityAuthorityInputPayload(ContextualIdentityAuthorityInputPayload&&) noexcept;
  ContextualIdentityAuthorityInputPayload& operator=(
      ContextualIdentityAuthorityInputPayload&&) noexcept;
  ZC_DISALLOW_COPY(ContextualIdentityAuthorityInputPayload);

  ZC_NODISCARD static zc::Maybe<ContextualIdentityAuthorityInputPayload> from(
      CompilationRootSetQueryKey&& contextRoots,
      zc::Vector<ContextualDefinitionAuthorityEntry>&& definitionAuthorities,
      zc::Vector<ContextualImplementationAuthorityEntry>&& implementationAuthorities,
      zc::Vector<ContextualGenericParameterAuthorityEntry>&& genericParameterAuthorities,
      zc::Vector<ContextualCallableParameterAuthorityEntry>&& callableParameterAuthorities,
      CompleteRootIdentityReadiness&& completeRootReadiness);
  ZC_NODISCARD static zc::Maybe<ContextualIdentityAuthorityInputPayload> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ContextualIdentityAuthorityInputPayload clone() const;
  ZC_NODISCARD const CompilationRootSetQueryKey& contextRoots() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ContextualDefinitionAuthorityEntry> definitionAuthorities()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ContextualImplementationAuthorityEntry>
  implementationAuthorities() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ContextualGenericParameterAuthorityEntry>
  genericParameterAuthorities() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ContextualCallableParameterAuthorityEntry>
  callableParameterAuthorities() const noexcept;
  ZC_NODISCARD const CompleteRootIdentityReadiness& completeRootReadiness() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ContextualIdentityAuthorityInputPayload& other) const;

private:
  struct Impl;
  explicit ContextualIdentityAuthorityInputPayload(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Independently verifies a complete contextual-authority payload.
class ContextualIdentityAuthorityInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const query::QuerySnapshot& authorityStagingSnapshot,
                                  const ContextualIdentityAuthorityInputPayload& candidate);
};

/// \brief Exact prior input keys replaced by the next contextual-authority transaction.
class ContextualIdentityAuthorityInputLedger final {
public:
  ContextualIdentityAuthorityInputLedger() noexcept = default;
  ContextualIdentityAuthorityInputLedger(ContextualIdentityAuthorityInputLedger&&) noexcept =
      default;
  ContextualIdentityAuthorityInputLedger& operator=(
      ContextualIdentityAuthorityInputLedger&&) noexcept = default;
  ZC_DISALLOW_COPY(ContextualIdentityAuthorityInputLedger);

  /// \brief Opens a base-input transaction that first removes published readiness.
  ZC_NODISCARD zc::Maybe<query::InputTransaction> beginBaseMutation(query::QueryDatabase& database);

  ZC_NODISCARD zc::ArrayPtr<const binder::StableDefinitionQueryKey> definitionKeys() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const ContextualImplementationKey> implementationKeys() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const ContextualGenericParameterKey> genericParameterKeys() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const ContextualCallableParameterKey> callableParameterKeys() const
      ZC_LIFETIMEBOUND;

private:
  zc::Vector<binder::StableDefinitionQueryKey> definitionKeyFields;
  zc::Vector<ContextualImplementationKey> implementationKeyFields;
  zc::Vector<ContextualGenericParameterKey> genericParameterKeyFields;
  zc::Vector<ContextualCallableParameterKey> callableParameterKeyFields;
  zc::Maybe<CompilationRootSetQueryKey> contextRootsField;

  friend class ContextualIdentityAuthorityInputTransaction;
};

/// \brief Complete atomic replacement of all contextual identity authorities.
class ContextualIdentityAuthorityInputTransaction final {
public:
  ContextualIdentityAuthorityInputTransaction(
      ContextualIdentityAuthorityInputTransaction&&) noexcept;
  ContextualIdentityAuthorityInputTransaction& operator=(
      ContextualIdentityAuthorityInputTransaction&&) noexcept;
  ~ContextualIdentityAuthorityInputTransaction() noexcept(false);
  ZC_DISALLOW_COPY(ContextualIdentityAuthorityInputTransaction);

  ZC_NODISCARD static zc::Maybe<ContextualIdentityAuthorityInputTransaction> prepare(
      const query::QuerySnapshot& authorityStagingSnapshot,
      query::DatabaseRevision expectedPreviousRevision,
      const CompilationRootSetQueryKey& contextRoots,
      const ContextualIdentityAuthorityInputLedger& priorLedger);

  ZC_NODISCARD ContextualIdentityAuthorityInputLedger takeNextLedger() &&;
  ZC_NODISCARD const ContextualIdentityAuthorityInputPayload& payload() const noexcept;
  ZC_NODISCARD query::InputCommitResult commit(query::QueryDatabase& database);

private:
  struct Impl;
  explicit ContextualIdentityAuthorityInputTransaction(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::driver::incremental_binding_query
