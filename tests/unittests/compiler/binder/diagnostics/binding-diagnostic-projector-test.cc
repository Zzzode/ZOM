// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/binding-diagnostic-projector.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::binder {
namespace {

BinderInvariantFact fact(BinderInvariantKind kind, BinderEmitterSite producer,
                         uint32_t ordinal = 0) {
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  return BinderInvariantFact{kind, identity::ModuleId(), zc::mv(noRange), producer, ordinal};
}

}  // namespace

ZC_TEST("BinderDiagnosticProjector preserves every invariant kind") {
  const BinderInvariantKind kinds[] = {
      BinderInvariantKind::MalformedScopeGraph,
      BinderInvariantKind::MissingRequiredResolution,
      BinderInvariantKind::AliasCycle,
      BinderInvariantKind::InvalidBindingFact,
      BinderInvariantKind::InvalidEmitterOrdinal,
  };
  for (const auto kind : kinds) {
    const auto descriptor =
        BinderDiagnosticProjector::project(fact(kind, BinderEmitterSite::ModuleSkeleton));
    ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Binder);
    ZC_EXPECT(descriptor.phase().tag() == 1);
    ZC_EXPECT(descriptor.kind().tag() == static_cast<uint32_t>(kind));
    ZC_EXPECT(descriptor.producer().tag() ==
              static_cast<uint32_t>(BinderEmitterSite::ModuleSkeleton));
  }
}

ZC_TEST("BinderDiagnosticProjector aggregates by kind and producer") {
  zc::Vector<BinderInvariantFact> facts;
  facts.add(fact(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::ImportBinding, 1));
  facts.add(fact(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::ImportBinding, 9));
  facts.add(fact(BinderInvariantKind::InvalidBindingFact, BinderEmitterSite::LabelAndClosure, 1));

  auto incidents = ZC_REQUIRE_NONNULL(BinderDiagnosticProjector::projectAll(facts.asPtr()));
  ZC_REQUIRE(incidents.size() == 2);
  ZC_EXPECT(incidents.descriptors()[0].occurrences() == 2);
  ZC_EXPECT(incidents.descriptors()[1].occurrences() == 1);
}

ZC_TEST("BinderDiagnosticProjector rejects an empty invariant set") {
  zc::ArrayPtr<const BinderInvariantFact> facts;
  ZC_EXPECT(BinderDiagnosticProjector::projectAll(facts) == zc::none);
}

}  // namespace zomlang::compiler::binder
