// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/binder/graph/module-dependency-requests.h"
#include "compiler/binder/graph/module-graph-source-failure.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"

namespace zomlang::compiler::binder {

/// \brief Projects one verified module-graph source failure to a canonical fact batch.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectModuleGraphSourceFailure(
    const VerifiedParsedModule& parsed, const ModuleGraphSourceFailure& failure);

/// \brief Projects an explicit root declaration that differs from its selected module path.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult
projectModuleDeclarationNameMismatch(const VerifiedParsedModule& parsed,
                                     const identity::ModuleKey& selectedModule);

/// \brief Projects every retained site of one module-resolution failure.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult
projectModuleDependencyResolutionFailure(const VerifiedParsedModule& parsed,
                                         const ModuleDependencyRequest& request, bool ambiguous);

}  // namespace zomlang::compiler::binder
