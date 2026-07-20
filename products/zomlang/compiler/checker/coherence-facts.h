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
#include "zomlang/compiler/checker/checked-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

namespace zomlang::compiler::driver {
class VerifiedModuleInterface;
}

namespace zomlang::compiler::checker::coherence {

/// \brief Exact module and verified-interface revision association in one coherence view.
struct ModuleInterfaceRevisionEntry final {
  identity::ModuleId module;
  module_interface::ModuleInterfaceRevision revision;
};

/// \brief Immutable context-complete RFC 0005 coherence authority.
class FrozenCoherenceView final {
public:
  ~FrozenCoherenceView() noexcept(false);
  FrozenCoherenceView(FrozenCoherenceView&&) noexcept;
  FrozenCoherenceView& operator=(FrozenCoherenceView&&) noexcept;
  ZC_DISALLOW_COPY(FrozenCoherenceView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD const signature::MarkerPolicyRegistryRevision& markerPolicyRegistryRevision()
      const noexcept;
  ZC_NODISCARD const cross_module::CoherenceViewRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleInterfaceRevisionEntry> moduleInterfaceRevisions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::ImplHead> implHeads() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::MarkerFact> markerFacts() const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::ImplHead&> implementation(
      identity::ImplId implementation) const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::MarkerFact&> marker(
      const signature::MarkerFactKey& key) const noexcept;

private:
  struct Impl;
  explicit FrozenCoherenceView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class CoherenceVerifier;
};

enum class CoherenceFailureProducer : uint8_t { Coherence = 0x01, Orphan = 0x02 };

/// \brief Deterministic coherence-stage source rejection with optional earlier conflict note.
struct CoherenceFailureRef final {
  checked::CheckerErrorId diagnostic;
  identity::ImplId primaryImpl;
  zc::Maybe<identity::ImplId> relatedImpl;
  identity::SourceSpan primarySpan;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  zc::Vector<checked::CheckerNoteRef> notes;
  CoherenceFailureProducer producer;
};

struct CoherenceFrozen final {
  FrozenCoherenceView view;
  zc::Vector<signature::SignatureAdvisoryRef> advisories;
};

struct CoherenceSourceRejected final {
  zc::Vector<CoherenceFailureRef> failures;
  zc::Vector<signature::SignatureAdvisoryRef> advisories;
};

struct CoherenceInvariantRejected final {
  zc::Vector<signature::CheckerVerificationFailure> failures;
};

using CoherenceBuildResult =
    zc::OneOf<CoherenceFrozen, CoherenceSourceRejected, CoherenceInvariantRejected>;

/// \brief Unforgeable exact coherence projection of one verified module interface.
class CoherenceModuleInput final {
public:
  ~CoherenceModuleInput() noexcept(false);
  CoherenceModuleInput(CoherenceModuleInput&&) noexcept;
  CoherenceModuleInput& operator=(CoherenceModuleInput&&) noexcept;
  ZC_DISALLOW_COPY(CoherenceModuleInput);

  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const module_interface::ModuleInterfaceRevision& interfaceRevision() const noexcept;
  ZC_NODISCARD const signature::MarkerPolicyRegistryRevision& markerPolicyRegistryRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::ImplHead> implHeads() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::MarkerFact> markerFacts() const noexcept;

private:
  struct Impl;
  explicit CoherenceModuleInput(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static CoherenceModuleInput publish(
      identity::ModuleId module, module_interface::ModuleInterfaceRevision interfaceRevision,
      signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision,
      zc::Vector<signature::ImplHead>&& implHeads, zc::Vector<zc::Array<uint8_t>>&& implHeadRecords,
      zc::Vector<signature::MarkerFact>&& markerFacts,
      zc::Vector<zc::Array<uint8_t>>&& markerFactRecords);
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> implHeadRecords() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> markerFactRecords() const noexcept;
  zc::Own<Impl> impl;
  friend class ::zomlang::compiler::driver::VerifiedModuleInterface;
  friend class CoherenceVerifier;
};

/// \brief Complete checker-neutral candidate for global coherence publication.
struct CoherenceCandidate final {
  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision;
  zc::Vector<CoherenceModuleInput> modules;
};

/// \brief Validates orphan legality and first-order overlap before freezing coherence facts.
class CoherenceVerifier final {
public:
  ZC_NODISCARD static CoherenceBuildResult verify(
      CoherenceCandidate&& candidate, const signature::VerifiedMarkerPolicyRegistry& markerPolicies,
      const identity::SemanticIdentityRegistrySet& registries);
};

}  // namespace zomlang::compiler::checker::coherence
