// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/binding-source-diagnostic-projector.h"

#include "compiler/binder/stable/stable-binding-diagnostic-fact.h"

namespace zomlang::compiler::binder {
namespace {

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Maybe<const identity::SourceSpan&> resolveIdentitySite(
    const IdentitySyntaxSiteInventory& sites,
    const diagnostics::DiagnosticProvenanceKey& provenance) {
  if (!provenance.isIdentitySyntaxSite() || !sameModule(provenance.module(), sites.module()) ||
      !provenance.source().sameAs(sites.source())) {
    return zc::none;
  }
  zc::Maybe<const identity::SourceSpan&> resolved;
  for (const auto& entry : sites.entries()) {
    const auto& key = entry.site.key();
    if (!sameModule(key.module(), provenance.module()) ||
        !key.source().sameAs(provenance.source()) ||
        key.moduleSyntaxPath() != provenance.identitySyntaxPath()) {
      continue;
    }
    if (resolved != zc::none) { return zc::none; }
    resolved = entry.site.range();
  }
  return resolved;
}

zc::Maybe<diagnostics::DiagnosticFact> projectLookup(const identity::SourceFileKey& source,
                                                     const StableFailedLookupFact& lookup) {
  const auto& outcome = lookup.outcome().value();
  if (outcome.is<StableMissingLookupOutcome>()) {
    return StableBindingDiagnosticFactFactory::missingLookup(source, lookup);
  }
  if (outcome.is<StableNamespaceMismatchLookupOutcome>()) {
    return StableBindingDiagnosticFactFactory::namespaceMismatchLookup(source, lookup);
  }
  if (outcome.is<StableAmbiguousLookupOutcome>()) {
    return StableBindingDiagnosticFactFactory::ambiguousLookup(source, lookup);
  }
  ZC_UNREACHABLE
}

bool projectSequence(const identity::SourceFileKey& source, const CanonicalParsedModule& parsed,
                     zc::ArrayPtr<const StableFailedLookupFact> stable,
                     zc::ArrayPtr<const MaterializedFailedLookupFact> materialized,
                     size_t& materializedIndex, diagnostics::SemanticDiagnosticFactBatch& batch) {
  for (const auto& lookup : stable) {
    if (materializedIndex >= materialized.size()) { return false; }
    const auto& current = materialized[materializedIndex++];
    if (current.nameSpace != lookup.nameSpace() || current.name != lookup.name() ||
        current.outcome != lookup.outcome() || !parsed.tree().contains(current.node)) {
      return false;
    }
    auto span = parsed.spanFor(parsed.tree().node(current.node).range);
    auto fact = projectLookup(source, lookup);
    if (span == zc::none || fact == zc::none || !ZC_ASSERT_NONNULL(span).belongsTo(source)) {
      return false;
    }
    batch.provenance.add(diagnostics::SourceDiagnosticProvenanceEntry{
        ZC_ASSERT_NONNULL(fact).primary().clone(),
        diagnostics::DiagnosticSourceRange{ZC_ASSERT_NONNULL(span).byteStart(),
                                           ZC_ASSERT_NONNULL(span).byteStart(), false}});
    batch.facts.add(zc::mv(ZC_ASSERT_NONNULL(fact)));
  }
  return true;
}

}  // namespace

diagnostics::SemanticDiagnosticBatchProjectionResult projectBindingSourceDiagnostics(
    const ImmutableBindingMetadata& bindings, const CanonicalParsedModule& parsed) {
  if (!parsed.source().belongsTo(bindings.module().crate())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  }

  diagnostics::SemanticDiagnosticFactBatch batch;
  const auto materialized = bindings.failedLookups();
  batch.facts.reserve(materialized.size());
  batch.provenance.reserve(materialized.size());
  size_t materializedIndex = 0;
  if (!projectSequence(parsed.source(), parsed, bindings.skeleton().failedLookups().values(),
                       materialized, materializedIndex, batch)) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  for (const auto& owner : bindings.ownerBodies()) {
    if (!projectSequence(parsed.source(), parsed, owner.failedLookups().values(), materialized,
                         materializedIndex, batch)) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
  }
  if (materializedIndex != materialized.size()) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
  }
  return batch;
}

diagnostics::SemanticDiagnosticBatchProjectionResult projectIdentityAdmissionSourceDiagnostics(
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts, const IdentitySyntaxSiteInventory& sites,
    const CanonicalParsedModule& parsed) {
  if (!sites.source().sameAs(parsed.source()) || sites.sourceDigest() != parsed.contentDigest() ||
      !sites.source().belongsTo(sites.module().crate())) {
    return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
  }

  diagnostics::SemanticDiagnosticFactBatch batch;
  batch.facts.reserve(facts.size());
  for (const auto& fact : facts) {
    if (!fact.occurrence().isIdentityAdmission() ||
        !sameModule(fact.occurrence().module(), sites.module()) ||
        !fact.occurrence().source().sameAs(sites.source()) ||
        fact.occurrence().identitySyntaxPath() != fact.primary().identitySyntaxPath()) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
    auto primary = resolveIdentitySite(sites, fact.primary());
    if (primary == zc::none || ZC_ASSERT_NONNULL(primary).byteEnd() > parsed.byteLength()) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
    batch.provenance.add(diagnostics::SourceDiagnosticProvenanceEntry{
        fact.primary().clone(),
        diagnostics::DiagnosticSourceRange{ZC_ASSERT_NONNULL(primary).byteStart(),
                                           ZC_ASSERT_NONNULL(primary).byteStart(), false}});
    for (const auto& secondary : fact.secondary()) {
      auto span = resolveIdentitySite(sites, secondary.provenance());
      if (span == zc::none || ZC_ASSERT_NONNULL(span).byteEnd() > parsed.byteLength()) {
        return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
      }
      const bool range = secondary.role() == diagnostics::DiagnosticSecondaryRole::Highlight;
      batch.provenance.add(diagnostics::SourceDiagnosticProvenanceEntry{
          secondary.provenance().clone(),
          diagnostics::DiagnosticSourceRange{
              ZC_ASSERT_NONNULL(span).byteStart(),
              range ? ZC_ASSERT_NONNULL(span).byteEnd() : ZC_ASSERT_NONNULL(span).byteStart(),
              range}});
    }
    batch.facts.add(fact.clone());
  }
  return batch;
}

}  // namespace zomlang::compiler::binder
