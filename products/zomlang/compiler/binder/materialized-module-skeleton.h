// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/identity/canonical/identity-interner-set.h"
#include "zomlang/compiler/identity/materialized-identity-entry.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/query/query-types.h"

namespace zomlang::compiler::binder {

using MaterializedDefinitionIdentityEntry =
    identity::MaterializedIdentityEntry<identity::DefinitionKey, identity::DefinitionIdentityRecord,
                                        identity::DefId>;
using MaterializedImplementationIdentityEntry =
    identity::MaterializedIdentityEntry<identity::ImplKey, identity::ImplIdentityRecord,
                                        identity::ImplId>;
using MaterializedGenericParameterIdentityEntry =
    identity::MaterializedIdentityEntry<identity::GenericParameterKey,
                                        identity::GenericParameterIdentityRecord,
                                        identity::GenericParameterId>;
using MaterializedCallableParameterIdentityEntry =
    identity::MaterializedIdentityEntry<identity::CallableParameterKey,
                                        identity::CallableParameterIdentityRecord,
                                        identity::CallableParameterId>;

/// \brief Four-domain identity materialization derived from one complete module skeleton.
class MaterializedModuleSkeletonIdentities final {
public:
  ~MaterializedModuleSkeletonIdentities() noexcept(false);
  MaterializedModuleSkeletonIdentities(MaterializedModuleSkeletonIdentities&&) noexcept;
  MaterializedModuleSkeletonIdentities& operator=(MaterializedModuleSkeletonIdentities&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedModuleSkeletonIdentities);

  /// \brief Interns and reverse-validates every skeleton identity authority in one context.
  ZC_NODISCARD static zc::Maybe<MaterializedModuleSkeletonIdentities> from(
      identity::SemanticContextBrand context, query::DatabaseRevision revision,
      const identity::ContextFingerprint& fingerprint, const BoundModuleSkeleton& skeleton,
      identity::IdentityInternerSet& interners);
  ZC_NODISCARD MaterializedModuleSkeletonIdentities clone() const;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const BoundModuleSkeleton& stableWitness() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedDefinitionIdentityEntry> definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedImplementationIdentityEntry> implementations()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedGenericParameterIdentityEntry> genericParameters()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedCallableParameterIdentityEntry> callableParameters()
      const noexcept;

private:
  struct Impl;
  explicit MaterializedModuleSkeletonIdentities(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
