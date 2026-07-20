// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

/// \brief Detects identities branded by a semantic context other than the input context.
ZC_NODISCARD bool bindingCandidateHasForeignContext(const VerifiedBindingInput& input,
                                                    const BindingMetadataCandidate& candidate);

/// \brief Detects source spans that are outside the frozen input source snapshot.
ZC_NODISCARD bool bindingCandidateHasInvalidSourceRange(const VerifiedBindingInput& input,
                                                        const BindingMetadataCandidate& candidate);

/// \brief Checks local record shape and cross-record graph invariants independently of production.
/// \return No value when valid, otherwise the exact closed Binder invariant kind.
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyBindingCandidateStructure(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);

}  // namespace zomlang::compiler::binder
