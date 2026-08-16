// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/facts/checked-facts-repository.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/facts/dispatch-facts.h"
#include "zomlang/compiler/checker/facts/signature-facts.h"
#include "zomlang/compiler/driver/borrow-evidence.h"
#include "zomlang/compiler/driver/interface-source.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/module-interface.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
}

namespace zomlang::compiler::hir {

class HirBuilder;
class HirVerifier;

/// \brief Exact immutable module-interface revision retained by the frontend handoff.
struct ModuleInterfaceLineage final {
  identity::ModuleId module;
  module_interface::ImportedInterfaceRevision revision;
};

/// \brief Complete verified-only inputs for one checked-module assembly.
struct CheckedModuleBuildInput final {
  const ownership::OwnershipAdmittedBoundModule& boundModule;
  const checker::signature::VerifiedSignatureFacts& localSignatureFacts;
  const driver::VerifiedModuleInterface& moduleInterface;
  const checker::cross_module::ImportedSignatureView& importedSignatures;
  zc::ArrayPtr<const driver::VerifiedInterfaceSource> availableModuleInterfaces;
  const checker::checked::CheckedEvidenceLease& checkedLease;
  const checker::checked::CheckedFactsRepository& checkedRepository;
  const checker::dispatch::VerifiedDispatchFacts& dispatchFacts;
  driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository;
  const checker::CheckerIdentityAuthority& identities;
  const type::SemanticTypeStore& semanticTypes;
};

/// \brief Frozen frontend semantic handoff accepted by HIR construction.
class VerifiedCheckedModule final {
public:
  ~VerifiedCheckedModule() noexcept(false);
  VerifiedCheckedModule(VerifiedCheckedModule&&) noexcept;
  VerifiedCheckedModule& operator=(VerifiedCheckedModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCheckedModule);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const binder::ParsedModuleReceipt& parsedModuleReceipt() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD const ModuleInterfaceLineage& ownInterface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleInterfaceLineage> visibleImportedInterfaces()
      const noexcept;
  ZC_NODISCARD const checker::checked::CheckedEvidenceLease& checkedEvidenceLease() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::VerifiedBorrowEvidenceLease& borrowEvidenceLease()
      const noexcept;

private:
  struct Impl;
  explicit VerifiedCheckedModule(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;
  ZC_NODISCARD const checker::checked::CheckedFactsRepository& checkedRepository() const noexcept;
  ZC_NODISCARD const checker::checked::VerifiedCheckedFacts& checkedFacts() const noexcept;
  ZC_NODISCARD const checker::dispatch::VerifiedDispatchFacts& dispatchFacts() const noexcept;
  ZC_NODISCARD driver::borrow_evidence::BorrowEvidenceRepositoryCapability
  borrowEvidenceCapability() const noexcept;
  ZC_NODISCARD checker::CheckerIdentityAuthority retainIdentityAuthority() const;
  ZC_NODISCARD const driver::VerifiedModuleInterface& ownModuleInterface() const noexcept;
  ZC_NODISCARD const type::SemanticTypeStore& semanticTypes() const noexcept;

  zc::Own<Impl> impl;

  friend class CheckedModuleBuilder;
  friend class HirBuilder;
  friend class HirVerifier;
};

/// \brief Sole assembler for one RFC 0010 verified checked-module handoff.
class CheckedModuleBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedCheckedModule> build(
      CheckedModuleBuildInput&& input);
};

}  // namespace zomlang::compiler::hir
