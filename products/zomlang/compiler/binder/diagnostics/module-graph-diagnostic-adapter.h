// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zomlang/compiler/binder/graph/module-dependency-requests.h"
#include "zomlang/compiler/binder/graph/module-graph-source-failure.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

/// \brief Checks whether one graph failure has an exact typed diagnostic projection.
ZC_NODISCARD bool canEmitModuleGraphSourceFailure(const VerifiedParsedModule& parsedModule,
                                                  const ModuleGraphSourceFailure& failure);

/// \brief Emits one diagnostic for every retained site after all source anchors verify.
/// \param diagnostics Destination diagnostic engine for the parsed source.
/// \param parsedModule Verified parser result that must own the failure span.
/// \param failure Closed graph failure to project into a typed source diagnostic.
/// \return True when all retained source diagnostics were emitted; false on any mismatch.
ZC_NODISCARD bool emitModuleGraphSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                               const VerifiedParsedModule& parsedModule,
                                               const ModuleGraphSourceFailure& failure);

/// \brief Emits one stable query resolution failure from verified revision-local request sites.
ZC_NODISCARD bool emitModuleDependencyResolutionFailure(diagnostics::DiagnosticEngine& diagnostics,
                                                        const VerifiedParsedModule& parsedModule,
                                                        const ModuleDependencyRequest& request,
                                                        bool ambiguous);

}  // namespace zomlang::compiler::binder
