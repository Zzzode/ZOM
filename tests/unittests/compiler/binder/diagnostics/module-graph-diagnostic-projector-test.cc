// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/module-graph-diagnostic-projector.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::binder {

ZC_TEST("ModuleGraphDiagnosticProjector preserves explicit phase kind and producer") {
  const auto descriptor = ModuleGraphDiagnosticProjector::project(
      ModuleGraphIncidentPhase::SourceParsing, ModuleGraphIncidentKind::MaterializationFailed,
      ModuleGraphIncidentProducer::DiagnosticMaterializer);

  ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Driver);
  ZC_EXPECT(descriptor.phase().tag() ==
            static_cast<uint32_t>(ModuleGraphIncidentPhase::SourceParsing));
  ZC_EXPECT(descriptor.kind().tag() ==
            static_cast<uint32_t>(ModuleGraphIncidentKind::MaterializationFailed));
  ZC_EXPECT(descriptor.producer().tag() ==
            static_cast<uint32_t>(ModuleGraphIncidentProducer::DiagnosticMaterializer));
}

ZC_TEST("ModuleGraphDiagnosticProjector maps resolver failures and occurrence counts") {
  const ModuleResolutionInvariantFact invalidEnvironment{
      ModuleResolutionInvariantKind::InvalidEnvironment, 7};
  const auto descriptor = ModuleGraphDiagnosticProjector::project(
      invalidEnvironment, ModuleGraphIncidentProducer::StructuralResolver);

  ZC_EXPECT(descriptor.phase().tag() ==
            static_cast<uint32_t>(ModuleGraphIncidentPhase::GraphConstruction));
  ZC_EXPECT(descriptor.kind().tag() ==
            static_cast<uint32_t>(ModuleGraphIncidentKind::InvalidDependencyGraph));
  ZC_EXPECT(descriptor.occurrences() == 7);
}

}  // namespace zomlang::compiler::binder
