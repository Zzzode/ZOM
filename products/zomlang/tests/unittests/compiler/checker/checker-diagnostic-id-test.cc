// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checker-diagnostic-id.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::checker::checked {

ZC_TEST("CheckerDiagnosticId.PartitionsTheRFC0005SourceRegistryBySeverity") {
  auto error = CheckerErrorId::fromDiagnosticId(diagnostics::DiagID::BodyLiteralOutOfRange);
  ZC_REQUIRE(error != zc::none);
  ZC_IF_SOME(value, error) {
    ZC_EXPECT(value == CheckerErrorId::BodyLiteralOutOfRange());
    ZC_EXPECT(value.diagnosticId() == diagnostics::DiagID::BodyLiteralOutOfRange);
  }
  ZC_EXPECT(CheckerWarningId::fromDiagnosticId(diagnostics::DiagID::BodyLiteralOutOfRange) ==
            zc::none);
  ZC_EXPECT(CheckerNoteId::fromDiagnosticId(diagnostics::DiagID::BodyLiteralOutOfRange) ==
            zc::none);

  auto warning =
      CheckerWarningId::fromDiagnosticId(diagnostics::DiagID::CheckerUnreachableMatchArm);
  ZC_REQUIRE(warning != zc::none);
  ZC_IF_SOME(value, warning) { ZC_EXPECT(value == CheckerWarningId::CheckerUnreachableMatchArm()); }
  ZC_EXPECT(CheckerErrorId::fromDiagnosticId(diagnostics::DiagID::CheckerUnreachableMatchArm) ==
            zc::none);

  auto note = CheckerNoteId::fromDiagnosticId(diagnostics::DiagID::PreviousImplHere);
  ZC_REQUIRE(note != zc::none);
  ZC_IF_SOME(value, note) { ZC_EXPECT(value == CheckerNoteId::PreviousImplHere()); }
  ZC_EXPECT(CheckerErrorId::fromDiagnosticId(diagnostics::DiagID::PreviousImplHere) == zc::none);
}

ZC_TEST("CheckerDiagnosticId.ExcludesDiagnosticsOwnedByOtherCheckerStages") {
  ZC_EXPECT(CheckerErrorId::fromDiagnosticId(diagnostics::DiagID::UseAfterMove) == zc::none);
  ZC_EXPECT(CheckerNoteId::fromDiagnosticId(diagnostics::DiagID::ValueMovedHere) == zc::none);
  ZC_EXPECT(CheckerErrorId::fromDiagnosticId(
                diagnostics::DiagID::MarkerInterfaceRequiresBodylessImpl) == zc::none);
  ZC_EXPECT(CheckerErrorId::fromDiagnosticId(diagnostics::DiagID::CheckerInvalidFact) == zc::none);
}

}  // namespace zomlang::compiler::checker::checked
