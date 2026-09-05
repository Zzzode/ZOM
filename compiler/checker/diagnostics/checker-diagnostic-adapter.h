// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/binder/graph/parsed-module.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/checker/facts/coherence-facts.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/array.h"
#include "zc/core/string.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::checker {

/// \brief Emit closed source failures with their verifier-checked diagnostic arguments.
void emitCheckedFactsSourceFailures(diagnostics::DiagnosticEngine& diagnostics,
                                    const binder::VerifiedParsedModule& parsedModule,
                                    const CheckerIdentityAuthority& identities,
                                    const type::SemanticTypeStore& semanticTypes,
                                    zc::ArrayPtr<const checked::CheckerFailureRef> failures);

/// \brief Emit one verifier-admitted global coherence source failure.
void emitCoherenceSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                const binder::VerifiedParsedModule& parsedModule,
                                const CheckerIdentityAuthority& identities,
                                const type::SemanticTypeStore& semanticTypes,
                                const coherence::CoherenceFailureRef& failure);

/// \brief Deterministically render one verifier-admitted structured diagnostic argument.
ZC_NODISCARD zc::String renderCheckerDisplayArgument(
    const checked::CheckerDisplayArgument& argument, const CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes);

}  // namespace zomlang::compiler::checker
