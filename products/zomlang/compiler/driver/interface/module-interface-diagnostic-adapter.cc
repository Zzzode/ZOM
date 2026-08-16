// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/interface/module-interface-diagnostic-adapter.h"

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"

namespace zomlang::compiler::driver {
namespace {

diagnostics::DiagID diagnosticId(ModuleInterfaceInvariantKind kind) {
  using diagnostics::DiagID;
  switch (kind) {
    case ModuleInterfaceInvariantKind::InputMismatch:
      return DiagID::ModuleInterfaceInputMismatch;
    case ModuleInterfaceInvariantKind::MissingProjection:
      return DiagID::ModuleInterfaceMissingProjection;
    case ModuleInterfaceInvariantKind::AdditionalProjection:
      return DiagID::ModuleInterfaceAdditionalProjection;
    case ModuleInterfaceInvariantKind::InvalidProjection:
      return DiagID::ModuleInterfaceInvalidProjection;
    case ModuleInterfaceInvariantKind::CanonicalCodecMismatch:
      return DiagID::ModuleInterfaceCanonicalCodecMismatch;
  }
  ZC_UNREACHABLE
}

source::SourceLoc diagnosticLocation(const binder::VerifiedParsedModule& parsedModule,
                                     const ModuleInterfaceInvariantFact& fact) {
  ZC_IF_SOME(span, fact.sourceSpan) {
    ZC_IF_SOME(location, parsedModule.sourceLocFor(span)) { return location; }
  }
  return source::SourceLoc();
}

}  // namespace

void emitModuleInterfaceInvariantFacts(diagnostics::DiagnosticEngine& diagnostics,
                                       const binder::VerifiedParsedModule& parsedModule,
                                       zc::ArrayPtr<const ModuleInterfaceInvariantFact> facts) {
  auto currentId = diagnostics::DiagID::ModuleInterfaceInputMismatch;
  source::SourceLoc currentLocation;
  uint64_t currentCount = 0;

  const auto flush = [&]() {
    if (currentCount == 0) { return; }
    diagnostics.emit(diagnostics::Diagnostic(currentId, currentLocation, zc::str(currentCount)));
    currentCount = 0;
  };

  for (const auto& fact : facts) {
    const auto id = diagnosticId(fact.kind);
    const auto location = diagnosticLocation(parsedModule, fact);
    if (currentCount != 0 && (currentId != id || currentLocation != location)) { flush(); }
    if (currentCount == 0) {
      currentId = id;
      currentLocation = location;
    }
    ++currentCount;
  }
  flush();
}

}  // namespace zomlang::compiler::driver
