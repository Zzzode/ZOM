// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/binder/graph/parsed-module.h"
#include "compiler/checker/borrow/borrow-interface.h"
#include "zc/core/array.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::checker::borrow {

/// \brief Emit the registered source diagnostics for rejected borrow signatures.
void emitBorrowSignatureFailures(diagnostics::DiagnosticEngine& diagnostics,
                                 const binder::VerifiedParsedModule& parsedModule,
                                 zc::ArrayPtr<const BorrowSignatureFailure> failures);

}  // namespace zomlang::compiler::checker::borrow
