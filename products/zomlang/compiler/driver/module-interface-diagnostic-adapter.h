// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/driver/module-interface.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::driver {

/// \brief Emit grouped registered diagnostics for sorted interface invariant facts.
void emitModuleInterfaceInvariantFacts(diagnostics::DiagnosticEngine& diagnostics,
                                       const binder::VerifiedParsedModule& parsedModule,
                                       zc::ArrayPtr<const ModuleInterfaceInvariantFact> facts);

}  // namespace zomlang::compiler::driver
