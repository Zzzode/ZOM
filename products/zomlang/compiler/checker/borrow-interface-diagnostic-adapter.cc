// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/borrow-interface-diagnostic-adapter.h"

#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"

namespace zomlang::compiler::checker::borrow {
namespace {

source::SourceLoc diagnosticLocation(const binder::VerifiedParsedModule& parsedModule,
                                     const identity::SourceSpan& span) {
  ZC_IF_SOME(location, parsedModule.sourceLocFor(span)) { return location; }
  return source::SourceLoc();
}

}  // namespace

void emitBorrowSignatureFailures(diagnostics::DiagnosticEngine& diagnostics,
                                 const binder::VerifiedParsedModule& parsedModule,
                                 zc::ArrayPtr<const BorrowSignatureFailure> failures) {
  using diagnostics::DiagID;
  for (const auto& failure : failures) {
    const auto location = diagnosticLocation(parsedModule, failure.primarySpan);
    switch (failure.kind) {
      case BorrowSignatureFailureKind::AmbiguousDirectResult:
        diagnostics.diagnose<DiagID::BorrowOutputRegionAmbiguous>(location).emit();
        break;
      case BorrowSignatureFailureKind::UnexpressibleResult:
        diagnostics.diagnose<DiagID::BorrowOutputRegionUnexpressible>(location).emit();
        break;
      case BorrowSignatureFailureKind::UnverifiedExternContract:
        diagnostics.diagnose<DiagID::BorrowExternContractUnverified>(location).emit();
        break;
    }
  }
}

}  // namespace zomlang::compiler::checker::borrow
