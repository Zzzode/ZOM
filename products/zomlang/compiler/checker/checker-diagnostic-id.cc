// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checker-diagnostic-id.h"

namespace zomlang::compiler::checker::checked {

zc::Maybe<CheckerErrorId> CheckerErrorId::fromDiagnosticId(
    diagnostics::DiagID diagnostic) noexcept {
  switch (diagnostic) {
#define CHECKER_ERROR(Name)       \
  case diagnostics::DiagID::Name: \
    return CheckerErrorId::Name();
#define CHECKER_WARNING(Name)
#define CHECKER_NOTE(Name)
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR
    default:
      return zc::none;
  }
}

diagnostics::DiagID CheckerErrorId::diagnosticId() const noexcept { return value; }

zc::Maybe<CheckerWarningId> CheckerWarningId::fromDiagnosticId(
    diagnostics::DiagID diagnostic) noexcept {
  switch (diagnostic) {
#define CHECKER_ERROR(Name)
#define CHECKER_WARNING(Name)     \
  case diagnostics::DiagID::Name: \
    return CheckerWarningId::Name();
#define CHECKER_NOTE(Name)
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR
    default:
      return zc::none;
  }
}

diagnostics::DiagID CheckerWarningId::diagnosticId() const noexcept { return value; }

zc::Maybe<CheckerNoteId> CheckerNoteId::fromDiagnosticId(diagnostics::DiagID diagnostic) noexcept {
  switch (diagnostic) {
#define CHECKER_ERROR(Name)
#define CHECKER_WARNING(Name)
#define CHECKER_NOTE(Name)        \
  case diagnostics::DiagID::Name: \
    return CheckerNoteId::Name();
#include "zomlang/compiler/checker/checker-source-diagnostics.def"
#undef CHECKER_NOTE
#undef CHECKER_WARNING
#undef CHECKER_ERROR
    default:
      return zc::none;
  }
}

diagnostics::DiagID CheckerNoteId::diagnosticId() const noexcept { return value; }

}  // namespace zomlang::compiler::checker::checked
