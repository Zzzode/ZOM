// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/internal/binding-verifier.h"

namespace zomlang::compiler::binder {

/// \brief Independently reconstructs contextual Self and receiver semantics.
/// \return No value when valid, otherwise the exact closed Binder invariant kind.
ZC_NODISCARD zc::Maybe<BinderInvariantKind> verifyBindingContextSemantics(
    const VerifiedBindingInput& input, const BindingMetadataCandidate& candidate);

}  // namespace zomlang::compiler::binder
