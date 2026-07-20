// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zomlang/compiler/binder/binding-input.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

/// \brief Checks whether one binding-input failure has an exact typed projection.
ZC_NODISCARD bool canEmitBindingInputSourceFailure(const VerifiedParsedModule& parsedModule,
                                                   const BindingInputSourceFailure& failure);

/// \brief Emits one member or visibility failure at its verified requester-owned range.
ZC_NODISCARD bool emitBindingInputSourceFailure(diagnostics::DiagnosticEngine& diagnostics,
                                                const VerifiedParsedModule& parsedModule,
                                                const BindingInputSourceFailure& failure);

}  // namespace zomlang::compiler::binder
