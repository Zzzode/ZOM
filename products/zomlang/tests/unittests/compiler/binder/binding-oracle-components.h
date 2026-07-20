// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyDeferredMemberOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyContextualSelfOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyExplicitCaptureOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyClosureFreeVariableOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyLabelOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyControlTransferOracle(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);

}  // namespace zomlang::compiler::binder
