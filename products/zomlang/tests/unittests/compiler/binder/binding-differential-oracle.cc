// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "binding-oracle-components.h"
#include "zc/core/debug.h"
#include "zomlang/compiler/binder/internal/binding-candidate-codec.h"
#include "zomlang/compiler/binder/internal/binding-candidate-validator.h"

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

zc::Maybe<BinderInvariantKind> verifySemanticOracles(const VerifiedBindingInput& input,
                                                     const BindingMetadataCandidate& candidate) {
  auto failureKind = verifyLabelOracle(input, candidate);
  if (failureKind != zc::none) { return failureKind; }
  failureKind = verifyControlTransferOracle(input, candidate);
  if (failureKind != zc::none) { return failureKind; }
  failureKind = verifyDeferredMemberOracle(input, candidate);
  if (failureKind != zc::none) { return failureKind; }
  failureKind = verifyContextualSelfOracle(input, candidate);
  if (failureKind != zc::none) { return failureKind; }
  failureKind = verifyExplicitCaptureOracle(input, candidate);
  if (failureKind != zc::none) { return failureKind; }
  return verifyClosureFreeVariableOracle(input, candidate);
}

bool candidateFactCountsMatch(const BindingMetadataCandidate& left,
                              const BindingMetadataCandidate& right) {
#define ZOM_BINDING_FACT(id, type, member, accessor, publication, tag, domain, mutations, test) \
  if (left.member.size() != right.member.size()) { return false; }
#include "zomlang/compiler/binder/binding-fact-schema.def"
#undef ZOM_BINDING_FACT
  return true;
}

}  // namespace

BindingVerificationResult BindingDifferentialOracle::verify(const VerifiedBindingInput& input,
                                                            BindingMetadataCandidate&& candidate) {
  auto expectedResult = BindingBuilder::buildCandidate(input, zc::none);
  if (!expectedResult.is<BindingMetadataCandidate>()) {
    return rejectBinderInvariant(zc::mv(expectedResult.get<BinderInvariantFact>()));
  }
  if (bindingCandidateHasForeignContext(input, candidate)) { return rejectForeignContext(input); }
  if (bindingCandidateHasInvalidSourceRange(input, candidate)) {
    return rejectInvalidSourceRange(input);
  }
  const auto& expected = expectedResult.get<BindingMetadataCandidate>();
  auto expectedStructure = verifyBindingCandidateStructure(input, expected);
  ZC_IF_SOME(kind, expectedStructure) {
    return rejectBinderInvariant(verifierFailure(input, kind));
  }
  auto expectedSemantic = verifySemanticOracles(input, expected);
  ZC_IF_SOME(kind, expectedSemantic) { return rejectBinderInvariant(verifierFailure(input, kind)); }
  auto candidateSemantic = verifySemanticOracles(input, candidate);
  ZC_IF_SOME(kind, candidateSemantic) {
    return rejectBinderInvariant(verifierFailure(input, kind));
  }
  if (!candidateFactCountsMatch(candidate, expected)) {
    return rejectBinderInvariant(
        verifierFailure(input, BinderInvariantKind::MissingRequiredResolution));
  }

  auto candidateAllocation =
      encodeBindingAllocationDump(input, candidate.scopes.asPtr(), candidate.labels.asPtr());
  auto expectedAllocation =
      encodeBindingAllocationDump(input, expected.scopes.asPtr(), expected.labels.asPtr());
  if (candidateAllocation == zc::none || expectedAllocation == zc::none) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::MalformedScopeGraph));
  }
  ZC_IF_SOME(candidateDump, candidateAllocation) {
    ZC_IF_SOME(expectedDump, expectedAllocation) {
      if (candidateDump.asPtr() != expectedDump.asPtr()) {
        return rejectBinderInvariant(
            verifierFailure(input, BinderInvariantKind::MalformedScopeGraph));
      }
    }
  }
  auto candidateBytes = encodeBindingCandidate(input, candidate);
  auto expectedBytes = encodeBindingCandidate(input, expected);
  if (candidateBytes == zc::none || expectedBytes == zc::none) {
    return rejectBinderInvariant(verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
  }
  ZC_IF_SOME(candidateValue, candidateBytes) {
    ZC_IF_SOME(expectedValue, expectedBytes) {
      if (candidateValue.asPtr() != expectedValue.asPtr()) {
        return rejectBinderInvariant(
            verifierFailure(input, BinderInvariantKind::InvalidBindingFact));
      }
    }
  }
  return BindingVerifier::verify(input, zc::mv(candidate));
}

}  // namespace zomlang::compiler::binder
