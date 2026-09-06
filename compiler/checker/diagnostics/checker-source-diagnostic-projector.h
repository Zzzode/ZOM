// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/checker-identity-authority.h"
#include "compiler/checker/facts/coherence-facts.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"
#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/array.h"
#include "zc/core/string.h"

namespace zomlang::compiler::checker {

/// \brief Project signature-stage failures and advisories into immutable facts.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectSignatureSourceDiagnostics(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    const signature::SignatureFactsSourceRejected& rejected);

/// \brief Project successful signature/coherence advisories for one module.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectSignatureAdvisories(
    identity::ModuleId module, const CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const signature::SignatureAdvisoryRef> advisories);

/// \brief Project closed Checker source failures into immutable diagnostic facts.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectCheckedFactsSourceFailures(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    zc::ArrayPtr<const checked::CheckerFailureRef> failures);

/// \brief Project one verifier-admitted global coherence source failure.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectCoherenceSourceFailure(
    const CheckerIdentityAuthority& identities, const type::SemanticTypeStore& semanticTypes,
    const coherence::CoherenceFailureRef& failure);

/// \brief Deterministically render one verifier-admitted structured diagnostic argument.
ZC_NODISCARD zc::String renderCheckerDisplayArgument(
    const checked::CheckerDisplayArgument& argument, const CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes);

}  // namespace zomlang::compiler::checker
