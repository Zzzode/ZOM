// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/binder/definition-site.h"
#include "zomlang/compiler/binder/identity-pre-admission.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/binder/stable-identity-candidate-producer.h"
#include "zomlang/compiler/identity/canonical-scalar.h"
#include "zomlang/compiler/identity/definition-key.h"

namespace zomlang::compiler::binder {

enum class FrozenInventoryInvariantKind : uint8_t {
  InputMismatch,
  IncompleteInventory,
  InvalidDefinitionSite,
  InvalidDefinitionIdentity,
  CanonicalHeaderMismatch,
  DuplicateBoundMismatch
};

struct FrozenInventoryInvariantFact final {
  FrozenInventoryInvariantKind kind;
  uint32_t occurrence;
};

/// \brief Node-to-key projection for one stable generic parameter candidate.
struct FrozenGenericParameterProjection final {
  ast::NodeId node;
  identity::GenericParameterKey key;
};

/// \brief Node-to-key projection for one stable callable parameter candidate.
struct FrozenCallableParameterProjection final {
  ast::NodeId node;
  identity::CallableParameterKey key;
};

/// \brief Node-to-materialized-key projection for one owner-local binding.
struct FrozenOwnerLocalBindingProjection final {
  ast::NodeId node;
  OwnerLocalBindingId binding;
  OwnerLocalBindingKey key;
};

/// \brief Node-to-key projection for one anonymous owner-local callable.
struct FrozenAnonymousEntityProjection final {
  ast::NodeId node;
  AnonymousOwnerLocalKey key;
};

/// \brief Node-to-occurrence projection for one implementation source candidate.
struct FrozenImplOccurrenceProjection final {
  ast::NodeId node;
  ImplOccurrenceId occurrence;
  ImplSourceOccurrenceKey key;
};

/// \brief One producer-retained duplicate bound paired with its stable identity syntax owner.
struct FrozenDuplicateBoundProjection final {
  ast::NodeId identity;
  DuplicateBoundOccurrence occurrence;
};

/// \brief Complete materialization handoff verified before frozen publication.
struct FrozenDefinitionInventoryInput final {
  zc::Vector<ProducedDefinitionIdentity> definitionCandidates;
  /// \brief Exact duplicate-bound inventory in canonical duplicate-site source order.
  zc::Vector<FrozenDuplicateBoundProjection> duplicateBounds;
  zc::Vector<FrozenGenericParameterProjection> genericParameters;
  zc::Vector<FrozenCallableParameterProjection> callableParameters;
  zc::Vector<FrozenOwnerLocalBindingProjection> ownerLocalBindings;
  zc::Vector<FrozenAnonymousEntityProjection> anonymousEntities;
  zc::Vector<FrozenImplOccurrenceProjection> implOccurrences;
};

/// \brief One admitted stable definition authority paired with current source provenance.
struct FrozenDefinitionEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::DefId definition;
  identity::DefinitionKey key;
  identity::DefinitionIdentityRecord record;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
};

/// \brief One admitted stable generic parameter paired with current source provenance.
struct FrozenGenericParameterEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::GenericParameterId parameter;
  identity::GenericParameterKey key;
  identity::GenericParameterIdentityRecord record;
  identity::DeclaredDefinitionName bindingName;
  identity::SourceSpan source;
};

/// \brief One admitted callable parameter paired with current source provenance.
struct FrozenCallableParameterEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::CallableParameterId parameter;
  identity::CallableParameterKey key;
  identity::CallableParameterIdentityRecord record;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
};

/// \brief One revision-local binding beneath a stable named owner.
struct FrozenOwnerLocalBindingEntry final {
  ast::NodeId node;
  DefinitionSite site;
  OwnerLocalBindingId binding;
  OwnerLocalBindingKey key;
  identity::SourceSpan source;
};

/// \brief One anonymous callable retained only inside its stable owner query.
struct FrozenAnonymousEntityEntry final {
  ast::NodeId node;
  DefinitionSite site;
  AnonymousOwnerLocalKey key;
  identity::SourceSpan source;
};

/// \brief One stable implementation authority projected from the frozen registry.
struct FrozenImplAuthorityEntry final {
  identity::ImplId implementation;
  identity::ImplKey key;
  identity::ImplIdentityRecord record;
};

/// \brief One revision-local implementation source occurrence under a shared authority.
struct FrozenImplOccurrenceEntry final {
  ImplOccurrenceId occurrence;
  ImplSourceOccurrenceKey key;
  identity::ImplId authority;
  ast::NodeId node;
  identity::SourceSpan source;
};

/// \brief Immutable single-module projection of every Binder identity domain.
class FrozenDefinitionInventoryView final {
public:
  ~FrozenDefinitionInventoryView() noexcept(false);
  FrozenDefinitionInventoryView(FrozenDefinitionInventoryView&&) noexcept;
  FrozenDefinitionInventoryView& operator=(FrozenDefinitionInventoryView&&) noexcept;
  ZC_DISALLOW_COPY(FrozenDefinitionInventoryView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD ast::NodeId moduleNode() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenDefinitionEntry> definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenGenericParameterEntry> genericParameters() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenCallableParameterEntry> callableParameters() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenOwnerLocalBindingEntry> ownerLocalBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenAnonymousEntityEntry> anonymousEntities() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenImplAuthorityEntry> implAuthorities() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const FrozenImplOccurrenceEntry> impls() const noexcept;

  ZC_NODISCARD zc::Maybe<const identity::DefinitionKey&> definitionKey(
      identity::DefId definition) const;
  ZC_NODISCARD zc::Maybe<const identity::DefinitionIdentityRecord&> definitionRecord(
      identity::DefId definition) const;
  ZC_NODISCARD zc::Maybe<const identity::GenericParameterKey&> genericParameterKey(
      identity::GenericParameterId parameter) const;
  ZC_NODISCARD zc::Maybe<const identity::GenericParameterIdentityRecord&> genericParameterRecord(
      identity::GenericParameterId parameter) const;
  ZC_NODISCARD zc::Maybe<const identity::CallableParameterKey&> callableParameterKey(
      identity::CallableParameterId parameter) const;
  ZC_NODISCARD zc::Maybe<const identity::CallableParameterIdentityRecord&> callableParameterRecord(
      identity::CallableParameterId parameter) const;
  ZC_NODISCARD zc::Maybe<const identity::ImplKey&> implKey(identity::ImplId implementation) const;
  ZC_NODISCARD zc::Maybe<const identity::ImplIdentityRecord&> implRecord(
      identity::ImplId implementation) const;

  ZC_NODISCARD zc::Maybe<identity::DefId> definitionAt(ast::NodeId node) const;
  ZC_NODISCARD zc::Maybe<identity::GenericParameterId> genericParameterAt(ast::NodeId node) const;
  ZC_NODISCARD zc::Maybe<identity::CallableParameterId> callableParameterAt(ast::NodeId node) const;
  ZC_NODISCARD zc::Maybe<OwnerLocalBindingId> ownerLocalBindingAt(ast::NodeId node) const;
  ZC_NODISCARD zc::Maybe<const FrozenAnonymousEntityEntry&> anonymousEntityAt(
      ast::NodeId node) const;
  /// \brief Returns the revision-local occurrence, never the shared semantic ImplId.
  ZC_NODISCARD zc::Maybe<ImplOccurrenceId> implAt(ast::NodeId node) const;
  ZC_NODISCARD zc::Maybe<identity::ImplId> implAuthority(ImplOccurrenceId occurrence) const;

private:
  struct Impl;
  explicit FrozenDefinitionInventoryView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class FrozenDefinitionInventoryVerifier;
};

using FrozenDefinitionInventoryResult =
    zc::OneOf<FrozenDefinitionInventoryView, FrozenInventoryInvariantFact>;

/// \brief Verifies a materialization handoff against syntax census and admitted records.
class FrozenDefinitionInventoryVerifier final {
public:
  ZC_NODISCARD static FrozenDefinitionInventoryResult verifySingleModule(
      identity::SemanticContextBrand context, identity::ModuleId module,
      const VerifiedParsedModule& parsedModule,
      const identity::SemanticIdentityRegistrySet& registries,
      FrozenDefinitionInventoryInput&& input);
};

}  // namespace zomlang::compiler::binder
