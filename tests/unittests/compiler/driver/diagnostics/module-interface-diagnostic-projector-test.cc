// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/diagnostics/module-interface-diagnostic-projector.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::driver {
namespace {

ModuleInterfaceInvariantFact invariant(ModuleInterfaceInvariantKind kind,
                                       ModuleInterfaceInvariantStage stage, uint32_t ordinal) {
  return ModuleInterfaceInvariantFact{kind,     stage,    identity::ModuleId(),
                                      zc::none, zc::none, zc::Vector<uint32_t>(),
                                      zc::none, zc::none, ordinal};
}

}  // namespace

ZC_TEST("ModuleInterfaceDiagnosticProjector preserves stage kind and producer") {
  const auto descriptor = ModuleInterfaceDiagnosticProjector::project(
      invariant(ModuleInterfaceInvariantKind::InvalidProjection,
                ModuleInterfaceInvariantStage::Verification, 7));

  ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Driver);
  ZC_EXPECT(descriptor.phase().tag() ==
            static_cast<uint32_t>(ModuleInterfaceInvariantStage::Verification));
  ZC_EXPECT(descriptor.kind().tag() ==
            static_cast<uint32_t>(ModuleInterfaceInvariantKind::InvalidProjection));
  ZC_EXPECT(descriptor.producer().tag() == 0x101U);
}

ZC_TEST("ModuleInterfaceDiagnosticProjector aggregates equal shapes without evidence") {
  zc::Vector<ModuleInterfaceInvariantFact> invariants;
  invariants.add(invariant(ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                           ModuleInterfaceInvariantStage::Encoding, 1));
  invariants.add(invariant(ModuleInterfaceInvariantKind::CanonicalCodecMismatch,
                           ModuleInterfaceInvariantStage::Encoding, 99));

  auto incidents =
      ZC_REQUIRE_NONNULL(ModuleInterfaceDiagnosticProjector::projectAll(invariants.asPtr()));
  ZC_REQUIRE(incidents.size() == 1);
  ZC_EXPECT(incidents.descriptors()[0].occurrences() == 2);
}

ZC_TEST("ModuleInterfaceDiagnosticProjector rejects an empty invariant set") {
  zc::ArrayPtr<const ModuleInterfaceInvariantFact> invariants;
  ZC_EXPECT(ModuleInterfaceDiagnosticProjector::projectAll(invariants) == zc::none);
}

}  // namespace zomlang::compiler::driver
