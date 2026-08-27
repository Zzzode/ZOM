// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/binder/metadata/binding-metadata.h"
#include "compiler/binder/graph/materialized-module-skeleton.h"
#include "compiler/binder/stable/stable-binding-facts.h"

namespace zomlang::compiler::binder {

/// \brief Exact current projection for one named definition.
struct MaterializedDefinitionInventoryEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::DefId definition;
  identity::DefinitionKey key;
  identity::DefinitionIdentityRecord record;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
};

/// \brief Exact current projection for one generic parameter.
struct MaterializedGenericParameterInventoryEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::GenericParameterId parameter;
  identity::GenericParameterKey key;
  identity::GenericParameterIdentityRecord record;
  identity::DeclaredDefinitionName bindingName;
  identity::SourceSpan source;
};

/// \brief Exact current projection for one callable parameter.
struct MaterializedCallableParameterInventoryEntry final {
  ast::NodeId node;
  DefinitionSite site;
  identity::CallableParameterId parameter;
  identity::CallableParameterKey key;
  identity::CallableParameterIdentityRecord record;
  zc::Maybe<identity::DeclaredDefinitionName> bindingName;
  identity::SourceSpan source;
};

/// \brief Exact current projection for one owner-local binding.
struct MaterializedOwnerLocalBindingInventoryEntry final {
  ast::NodeId node;
  DefinitionSite site;
  OwnerLocalBindingId binding;
  OwnerLocalBindingKey key;
  identity::SourceSpan source;
};

/// \brief Exact current projection for one anonymous owner-local entity.
struct MaterializedAnonymousEntityEntry final {
  ast::NodeId node;
  DefinitionSite site;
  AnonymousOwnerLocalId entity;
  AnonymousOwnerLocalKey key;
  identity::SourceSpan source;
};

/// \brief Materialized authority for one implementation identity.
struct MaterializedImplAuthorityInventoryEntry final {
  identity::ImplId implementation;
  identity::ImplKey key;
  identity::ImplIdentityRecord record;
};

/// \brief Exact current projection for one implementation source occurrence.
struct MaterializedImplOccurrenceInventoryEntry final {
  ImplOccurrenceId occurrence;
  ImplSourceOccurrenceKey key;
  identity::ImplId authority;
  ast::NodeId node;
  identity::SourceSpan source;
};

/// \brief Immutable owned index derived from one materialized skeleton and its bodies.
class ImmutableDefinitionInventory final {
public:
  ~ImmutableDefinitionInventory() noexcept(false);
  ImmutableDefinitionInventory(ImmutableDefinitionInventory&&) noexcept;
  ImmutableDefinitionInventory& operator=(ImmutableDefinitionInventory&&) noexcept;
  ZC_DISALLOW_COPY(ImmutableDefinitionInventory);

  ZC_NODISCARD static zc::Maybe<ImmutableDefinitionInventory> from(
      MaterializedModuleSkeletonIdentities&& identities, zc::Vector<BoundOwnerBody>&& ownerBodies,
      zc::ArrayPtr<const DefinitionFact> definitions,
      zc::ArrayPtr<const GenericParameterFact> genericParameters,
      zc::ArrayPtr<const CallableParameterFact> callableParameters,
      zc::ArrayPtr<const ImplBindingFact> implementations,
      zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
      zc::ArrayPtr<const AnonymousEntityFact> anonymousEntities);
  ZC_NODISCARD ImmutableDefinitionInventory clone() const;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const MaterializedModuleSkeletonIdentities& identities() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BoundOwnerBody> ownerBodies() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedDefinitionInventoryEntry> definitions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedGenericParameterInventoryEntry> genericParameters()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedCallableParameterInventoryEntry> callableParameters()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedOwnerLocalBindingInventoryEntry> ownerLocalBindings()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedImplAuthorityInventoryEntry> implAuthorities()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedImplOccurrenceInventoryEntry> impls() const noexcept;
  ZC_NODISCARD zc::Maybe<const MaterializedDefinitionIdentityEntry&> definition(
      identity::DefId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const MaterializedImplementationIdentityEntry&> implementation(
      identity::ImplId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const MaterializedGenericParameterIdentityEntry&> genericParameter(
      identity::GenericParameterId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<const MaterializedCallableParameterIdentityEntry&> callableParameter(
      identity::CallableParameterId handle) const noexcept;
  ZC_NODISCARD zc::Maybe<identity::DefId> definitionAt(ast::NodeId node) const noexcept;
  ZC_NODISCARD zc::Maybe<identity::GenericParameterId> genericParameterAt(
      ast::NodeId node) const noexcept;
  ZC_NODISCARD zc::Maybe<identity::CallableParameterId> callableParameterAt(
      ast::NodeId node) const noexcept;
  ZC_NODISCARD zc::Maybe<OwnerLocalBindingId> ownerLocalBindingAt(ast::NodeId node) const noexcept;
  ZC_NODISCARD zc::Maybe<const MaterializedAnonymousEntityEntry&> anonymousEntityAt(
      ast::NodeId node, AnonymousOwnerLocalRole role) const noexcept;
  ZC_NODISCARD zc::Maybe<ImplOccurrenceId> implementationAt(ast::NodeId node) const noexcept;
  ZC_NODISCARD zc::Maybe<identity::ImplId> implementationAuthority(
      ImplOccurrenceId occurrence) const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedAnonymousEntityEntry> anonymousEntities()
      const noexcept;
  ZC_NODISCARD bool matches(const MaterializedModuleSkeletonIdentities& identities,
                            zc::ArrayPtr<const BoundOwnerBody> ownerBodies,
                            zc::ArrayPtr<const DefinitionFact> definitions,
                            zc::ArrayPtr<const GenericParameterFact> genericParameters,
                            zc::ArrayPtr<const CallableParameterFact> callableParameters,
                            zc::ArrayPtr<const ImplBindingFact> implementations,
                            zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
                            zc::ArrayPtr<const AnonymousEntityFact> anonymousEntities) const;

private:
  struct Impl;
  explicit ImmutableDefinitionInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
