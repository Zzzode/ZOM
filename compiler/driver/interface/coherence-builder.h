// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/facts/coherence-facts.h"
#include "compiler/driver/interface/module-interface.h"

namespace zomlang::compiler::driver {

/// \brief Verified-interface-only input for global coherence construction.
struct CoherenceBuildInput final {
  identity::SemanticContextBrand semanticContext;
  const identity::ContextFingerprint& contextFingerprint;
  const checker::signature::VerifiedMarkerPolicyRegistry& markerPolicies;
  zc::ArrayPtr<const VerifiedModuleInterface> interfaces;
  const checker::CheckerIdentityAuthority& identities;
};

/// \brief Sole driver projection into the checker-neutral coherence verifier.
class CoherenceBuilder final {
public:
  ZC_NODISCARD static checker::coherence::CoherenceBuildResult build(
      const CoherenceBuildInput& input);
};

}  // namespace zomlang::compiler::driver
