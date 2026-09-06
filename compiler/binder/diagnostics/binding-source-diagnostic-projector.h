// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/binder/graph/parsed-module.h"
#include "compiler/binder/identity/identity-pre-admission.h"
#include "compiler/binder/metadata/immutable-binding-metadata.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"

namespace zomlang::compiler::binder {

/// \brief Projects all stable Binder lookup failures with revision-local provenance.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectBindingSourceDiagnostics(
    const ImmutableBindingMetadata& bindings, const CanonicalParsedModule& parsed);

/// \brief Resolves stable identity-admission facts against one verified source revision.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult
projectIdentityAdmissionSourceDiagnostics(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
                                          const IdentitySyntaxSiteInventory& sites,
                                          const CanonicalParsedModule& parsed);

}  // namespace zomlang::compiler::binder
