// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/checked-facts.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/coherence-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/crate-key.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::checker::body {

/// \brief How one checked-fact family enters the generated body requirement inventory.
enum class BodyFactRequirementOrigin : uint8_t {
  Syntax = 0x01,
  Binding = 0x02,
  SemanticAnalysis = 0x03
};

/// \brief One closed coverage row for a checked-facts family.
struct BodyFactFamilyCoverage final {
  checked::CheckedFactGroup group;
  BodyFactRequirementOrigin origin;
};

/// \brief Generated and verified exact requirements for one bound module body.
class VerifiedBodyFactRequirementInventory final {
public:
  ~VerifiedBodyFactRequirementInventory() noexcept(false);
  VerifiedBodyFactRequirementInventory(VerifiedBodyFactRequirementInventory&&) noexcept;
  VerifiedBodyFactRequirementInventory& operator=(VerifiedBodyFactRequirementInventory&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBodyFactRequirementInventory);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const binder::ParsedModuleReceipt& parsedModuleReceipt() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BodyFactFamilyCoverage> familyCoverage() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checked::NodeFactRequirement> nodeRequirements() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checked::DefinitionFactRequirement> definitionRequirements()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checked::CaptureFactRequirement> captureRequirements()
      const noexcept;

private:
  struct Impl;
  explicit VerifiedBodyFactRequirementInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BodyFactRequirementInventoryBuilder;
  friend class BodyChecker;
};

using BodyFactRequirementInventoryBuildResult =
    zc::OneOf<VerifiedBodyFactRequirementInventory, checked::CheckedFactsInvariantRejected>;

/// \brief Walks the verified AST and binder publication to generate exact fact requirements.
class BodyFactRequirementInventoryBuilder final {
public:
  ZC_NODISCARD static BodyFactRequirementInventoryBuildResult build(
      const driver::module_graph_query::CheckerBoundModuleView& boundModule);
};

/// \brief Complete capability input for production body checking.
struct BodyCheckingInput final {
  const driver::module_graph_query::CheckerBoundModuleView& boundModule;
  const CheckerIdentityAuthority& identities;
  const signature::VerifiedSignatureFacts& signatureFacts;
  const cross_module::ImportedSignatureView& importedSignatures;
  const coherence::FrozenCoherenceView& coherence;
  type::SemanticTypeStore& semanticTypes;
  const VerifiedBodyFactRequirementInventory& requirements;
  const identity::SemanticCompilerOptionsKey& semanticOptions;
};

using BodyCheckingResult =
    zc::OneOf<checked::CheckedFactsCandidate, checked::CheckedFactsSourceRejected,
              checked::CheckedFactsInvariantRejected>;

/// \brief Fail-closed RFC 0005 body fact producer; never constructs verified checked facts.
class BodyChecker final {
public:
  BodyChecker();
  ~BodyChecker() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(BodyChecker);

  ZC_NODISCARD BodyCheckingResult check(const BodyCheckingInput& input,
                                        const identity::RegistryBrandIssuer& factStoreBrands);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::checker::body
