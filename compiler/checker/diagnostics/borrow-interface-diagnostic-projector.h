// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/borrow/borrow-interface.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"
#include "zc/core/array.h"

namespace zomlang::compiler::checker::borrow {

/// \brief Project rejected borrow signatures into immutable diagnostic facts.
ZC_NODISCARD diagnostics::SemanticDiagnosticBatchProjectionResult projectBorrowSignatureFailures(
    const CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const BorrowSignatureFailure> failures);

}  // namespace zomlang::compiler::checker::borrow
