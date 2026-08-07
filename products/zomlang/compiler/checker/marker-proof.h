// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/coherence-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::checker::body {

struct BodyCheckingInput;

}  // namespace zomlang::compiler::checker::body

namespace zomlang::compiler::checker::marker {

struct MarkerProofPositive final {
  signature::MarkerFact proof;
};

struct MarkerProofNegative final {
  signature::MarkerFact explicitFact;
};

struct MarkerProofUnsatisfied final {};

struct MarkerProofInvariantRejected final {
  zc::Vector<signature::CheckerVerificationFailure> failures;
};

using MarkerProofResult = zc::OneOf<MarkerProofPositive, MarkerProofNegative,
                                    MarkerProofUnsatisfied, MarkerProofInvariantRejected>;

/// \brief Call-duration authority to intern canonical component types in one semantic store.
class SemanticTypeInterningCapability final {
public:
  ~SemanticTypeInterningCapability() noexcept(false);
  SemanticTypeInterningCapability(SemanticTypeInterningCapability&&) noexcept;
  SemanticTypeInterningCapability& operator=(SemanticTypeInterningCapability&&) noexcept;
  ZC_DISALLOW_COPY(SemanticTypeInterningCapability);

  ZC_NODISCARD type::SemanticTypeInternResult intern(type::semantic::CanonicalTypeData&& canonical);

private:
  struct Impl;
  explicit SemanticTypeInterningCapability(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class MarkerProofInput;
};

/// \brief Verified non-owning capability set for one marker-proof authority.
class MarkerProofInput final {
public:
  ~MarkerProofInput() noexcept(false);
  MarkerProofInput(MarkerProofInput&&) noexcept;
  MarkerProofInput& operator=(MarkerProofInput&&) noexcept;
  ZC_DISALLOW_COPY(MarkerProofInput);

  /// \brief Validate one exact body-checking lineage before constructing a proof engine.
  ZC_NODISCARD static zc::Maybe<MarkerProofInput> from(
      const body::BodyCheckingInput& bodyInput,
      const signature::VerifiedMarkerPolicyRegistry& policy);

private:
  struct Impl;
  explicit MarkerProofInput(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class MarkerProofEngine;
};

/// \brief Demand-driven explicit, builtin, and structural marker proof authority.
class MarkerProofEngine final {
public:
  explicit MarkerProofEngine(MarkerProofInput&& input);
  ~MarkerProofEngine() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(MarkerProofEngine);

  /// \brief Resolve and independently reconstruct one exact marker-subject proof query.
  ZC_NODISCARD MarkerProofResult prove(identity::DefId marker, identity::SemanticTypeId subject);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::checker::marker
