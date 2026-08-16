// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/module-binding-allocation-plan.h"
#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/query/query-types.h"

namespace zomlang::compiler::binder {

/// \brief Complete current fact projection retained by one materialized bound module.
struct MaterializedBindingFacts final {
  zc::ArrayPtr<const NodeScopeFact> nodeScopes;
  zc::ArrayPtr<const BindingResolution> nodeBindings;
  zc::ArrayPtr<const BoundSelfType> selfTypes;
  zc::ArrayPtr<const BoundThis> thisBindings;
  zc::ArrayPtr<const DefinitionFact> definitions;
  zc::ArrayPtr<const ImplBindingFact> implementations;
  zc::ArrayPtr<const ScopeRecord> scopes;
  zc::ArrayPtr<const ModuleAliasBindingFact> moduleAliases;
  zc::ArrayPtr<const ImportBindingFact> imports;
  zc::ArrayPtr<const LocalExportFact> localExports;
  zc::ArrayPtr<const DeferredMemberFact> deferredMembers;
  zc::ArrayPtr<const LabelFact> labels;
  zc::ArrayPtr<const ControlTransferFact> controlTransfers;
  zc::ArrayPtr<const ShadowTargetFact> shadowTargets;
  zc::ArrayPtr<const ClosureFreeVariableFact> closureFreeVariables;
  zc::ArrayPtr<const ExplicitClosureCaptureFact> explicitClosureCaptures;
  zc::ArrayPtr<const GenericParameterFact> genericParameters;
  zc::ArrayPtr<const CallableParameterFact> callableParameters;
  zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings;
  zc::ArrayPtr<const MaterializedFailedLookupFact> failedLookups;
};

/// \brief Immutable aggregate of one module skeleton and every exact owner-body fact set.
class ImmutableBindingMetadata final {
public:
  ~ImmutableBindingMetadata() noexcept(false);
  ImmutableBindingMetadata(ImmutableBindingMetadata&&) noexcept;
  ImmutableBindingMetadata& operator=(ImmutableBindingMetadata&&) noexcept;
  ZC_DISALLOW_COPY(ImmutableBindingMetadata);

  ZC_NODISCARD static zc::Maybe<ImmutableBindingMetadata> from(
      identity::SemanticContextBrand context, query::DatabaseRevision revision,
      const identity::ContextFingerprint& fingerprint, BoundModuleSkeleton&& skeleton,
      zc::Vector<BoundOwnerBody>&& ownerBodies, const MaterializedBindingFacts& facts);
  ZC_NODISCARD ImmutableBindingMetadata clone() const;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const BoundModuleSkeleton& skeleton() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BoundOwnerBody> ownerBodies() const noexcept;
  ZC_NODISCARD const ModuleBindingAllocationPlan& allocationPlan() const noexcept;
  ZC_NODISCARD zc::Maybe<const BoundOwnerBody&> ownerBody(
      const StableOwnerBodyQueryKey& owner) const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const NodeScopeFact> nodeScopes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BindingResolution> nodeBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BoundSelfType> selfTypes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BoundThis> thisBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionFact> definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImplBindingFact> implementations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ScopeRecord> scopes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleAliasBindingFact> moduleAliases() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImportBindingFact> imports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const LocalExportFact> localExports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DeferredMemberFact> deferredMembers() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const LabelFact> labels() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ControlTransferFact> controlTransfers() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ShadowTargetFact> shadowTargets() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ClosureFreeVariableFact> closureFreeVariables() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ExplicitClosureCaptureFact> explicitClosureCaptures()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const GenericParameterFact> genericParameters() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CallableParameterFact> callableParameters() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedFailedLookupFact> failedLookups() const noexcept;
  ZC_NODISCARD bool matches(identity::SemanticContextBrand context,
                            query::DatabaseRevision revision,
                            const identity::ContextFingerprint& fingerprint,
                            const BoundModuleSkeleton& skeleton,
                            zc::ArrayPtr<const BoundOwnerBody> ownerBodies,
                            const MaterializedBindingFacts& facts) const;

private:
  struct Impl;
  explicit ImmutableBindingMetadata(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::binder
