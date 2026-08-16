// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/interface/coherence-builder.h"

namespace zomlang::compiler::driver {

checker::coherence::CoherenceBuildResult CoherenceBuilder::build(const CoherenceBuildInput& input) {
  checker::coherence::CoherenceCandidate candidate{
      input.semanticContext, input.contextFingerprint.clone(), input.markerPolicies.revision(),
      zc::Vector<checker::coherence::CoherenceModuleInput>(input.interfaces.size())};
  for (const auto& interface : input.interfaces) {
    candidate.modules.add(interface.projectCoherenceInput());
  }
  return checker::coherence::CoherenceVerifier::verify(zc::mv(candidate), input.markerPolicies,
                                                       input.identities);
}

}  // namespace zomlang::compiler::driver
