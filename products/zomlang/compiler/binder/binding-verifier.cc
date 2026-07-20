// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-verifier.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/binder/internal/binding-candidate-validator.h"
#include "zomlang/compiler/binder/internal/binding-capture-validator.h"
#include "zomlang/compiler/binder/internal/binding-context-validator.h"
#include "zomlang/compiler/binder/internal/binding-control-validator.h"

namespace zomlang::compiler::binder {
namespace {

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            BinderEmitterSite site, uint32_t ordinal = 0) {
  return BinderInvariantFact{kind, input.module(), zc::none, site, ordinal};
}
BinderInvariantFact verifierFailure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                                    uint32_t ordinal = 0) {
  return failure(input, kind, BinderEmitterSite::BindingVerifier, ordinal);
}
BindingVerificationResult rejectBinderInvariant(BinderInvariantFact&& fact) {
  return InvariantRejected::single(
      BindingVerificationFailure(BindingVerificationFailureValue(zc::mv(fact))));
}

BindingVerificationResult rejectIdentityInvariant(const VerifiedBindingInput& input,
                                                  identity::IdentityInvariantKind kind,
                                                  identity::IdentityAllocationPhase phase,
                                                  uint32_t ordinal = 0) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto fact = identity::IdentityInvariant::from(kind, phase, zc::mv(noKey), zc::mv(noRange),
                                                identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, fact) {
    return InvariantRejected::single(
        BindingVerificationFailure(BindingVerificationFailureValue(zc::mv(value))));
  }
  return rejectBinderInvariant(
      verifierFailure(input, BinderInvariantKind::InvalidBindingFact, ordinal));
}

BindingVerificationResult rejectForeignContext(const VerifiedBindingInput& input,
                                               uint32_t ordinal = 0) {
  return rejectIdentityInvariant(input, identity::IdentityInvariantKind::ForeignContext,
                                 identity::IdentityAllocationPhase::Module, ordinal);
}

BindingVerificationResult rejectInvalidSourceRange(const VerifiedBindingInput& input,
                                                   uint32_t ordinal = 0) {
  return rejectIdentityInvariant(input, identity::IdentityInvariantKind::InvalidSourceRange,
                                 identity::IdentityAllocationPhase::Source, ordinal);
}

}  // namespace

BindingVerificationResult BindingVerifier::verify(const VerifiedBindingInput& input,
                                                  BindingMetadataCandidate&& candidate) {
  if (bindingCandidateHasForeignContext(input, candidate)) { return rejectForeignContext(input); }
  if (bindingCandidateHasInvalidSourceRange(input, candidate)) {
    return rejectInvalidSourceRange(input);
  }
  auto structureFailure = verifyBindingCandidateStructure(input, candidate);
  ZC_IF_SOME(kind, structureFailure) { return rejectBinderInvariant(verifierFailure(input, kind)); }
  auto captureFailure = verifyBindingCaptureSemantics(input, candidate);
  ZC_IF_SOME(kind, captureFailure) { return rejectBinderInvariant(verifierFailure(input, kind)); }
  auto contextFailure = verifyBindingContextSemantics(input, candidate);
  ZC_IF_SOME(kind, contextFailure) { return rejectBinderInvariant(verifierFailure(input, kind)); }
  auto controlFailure = verifyBindingControlSemantics(input, candidate);
  ZC_IF_SOME(kind, controlFailure) { return rejectBinderInvariant(verifierFailure(input, kind)); }
  if (!candidate.sourceFailures.empty()) {
    return SourceRejected(zc::mv(candidate.sourceFailures));
  }
  return publishCandidate(zc::mv(candidate));
}

}  // namespace zomlang::compiler::binder
