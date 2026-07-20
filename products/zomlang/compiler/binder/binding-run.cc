// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-run.h"

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

BindingVerificationResult runBinding(const VerifiedBindingInput& input,
                                     diagnostics::DiagnosticEngine& diagnostics) {
  auto candidate = BindingBuilder::build(input, diagnostics);
  if (candidate.is<BinderInvariantFact>()) {
    return InvariantRejected::single(BindingVerificationFailure(
        BindingVerificationFailureValue(zc::mv(candidate.get<BinderInvariantFact>()))));
  }
  return BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
}

}  // namespace zomlang::compiler::binder
