// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::diagnostics {
namespace {

zc::Array<uint8_t> cloneBytes(zc::ArrayPtr<const uint8_t> bytes) {
  return zc::heapArray<uint8_t>(bytes);
}

zc::Vector<uint32_t> clonePath(zc::ArrayPtr<const uint32_t> path) {
  zc::Vector<uint32_t> result(path.size());
  result.addAll(path);
  return result;
}

SemanticDiagnosticSiteRole siteRole(DiagnosticSecondaryRole role) {
  switch (role) {
    case DiagnosticSecondaryRole::Highlight:
      return SemanticDiagnosticSiteRole::Highlight;
    case DiagnosticSecondaryRole::Note:
      return SemanticDiagnosticSiteRole::Note;
    case DiagnosticSecondaryRole::PreviousDeclaration:
      return SemanticDiagnosticSiteRole::PreviousDeclaration;
  }
  ZC_UNREACHABLE
}

}  // namespace

SemanticDiagnosticProjectionResult projectSemanticDiagnosticFact(
    const identity::ModuleKey& module, SemanticDiagnosticDomain domain,
    zc::ArrayPtr<const uint8_t> canonicalOwner, zc::ArrayPtr<const uint32_t> eventPath, DiagID code,
    zc::Vector<zc::String>&& arguments, const identity::SourceSpan& primary,
    zc::Vector<SemanticDiagnosticRelatedSite>&& related) {
  if (canonicalOwner.size() == 0 || eventPath.size() == 0 ||
      !primary.source().belongsTo(module.crate())) {
    return SemanticDiagnosticProjectionFailure::InvalidOwner;
  }
  auto occurrence =
      DiagnosticOccurrenceKey::semantic(module.clone(), primary.source().clone(), domain,
                                        cloneBytes(canonicalOwner), clonePath(eventPath));
  if (occurrence == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidOccurrence; }
  auto primaryKey = DiagnosticProvenanceKey::semanticSite(
      module.clone(), primary.source().clone(), domain, cloneBytes(canonicalOwner),
      clonePath(eventPath), SemanticDiagnosticSiteRole::Primary, 0);
  if (primaryKey == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidPrimary; }

  zc::Vector<DiagnosticSecondary> secondary(related.size());
  zc::Vector<SourceDiagnosticProvenanceEntry> provenance(1 + related.size());
  provenance.add(SourceDiagnosticProvenanceEntry{
      ZC_ASSERT_NONNULL(primaryKey).clone(),
      // The primary range spans the flagged node's full source span so the
      // renderer underlines the whole token (^ followed by ~) rather than a
      // single caret.
      DiagnosticSourceRange{primary.byteStart(), primary.byteEnd(), true}});
  uint32_t highlightOrdinal = 0;
  uint32_t noteOrdinal = 0;
  uint32_t previousOrdinal = 0;
  for (auto& item : related) {
    if (!item.span.source().belongsTo(module.crate())) {
      return SemanticDiagnosticProjectionFailure::InvalidRelated;
    }
    uint32_t ordinal = 0;
    switch (item.role) {
      case DiagnosticSecondaryRole::Highlight:
        ordinal = highlightOrdinal++;
        break;
      case DiagnosticSecondaryRole::Note:
        ordinal = noteOrdinal++;
        break;
      case DiagnosticSecondaryRole::PreviousDeclaration:
        ordinal = previousOrdinal++;
        break;
    }
    auto key = DiagnosticProvenanceKey::semanticSite(
        module.clone(), item.span.source().clone(), domain, cloneBytes(canonicalOwner),
        clonePath(eventPath), siteRole(item.role), ordinal);
    if (key == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidRelated; }
    auto secondaryKey = ZC_ASSERT_NONNULL(key).clone();
    zc::Maybe<DiagnosticSecondary> projected;
    switch (item.role) {
      case DiagnosticSecondaryRole::Highlight:
        if (item.code != zc::none || item.arguments.size() != 0) {
          return SemanticDiagnosticProjectionFailure::InvalidRelated;
        }
        projected = DiagnosticSecondary::highlight(zc::mv(secondaryKey));
        break;
      case DiagnosticSecondaryRole::Note:
        if (item.code == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidRelated; }
        projected = DiagnosticSecondary::note(ZC_ASSERT_NONNULL(item.code), zc::mv(secondaryKey),
                                              zc::mv(item.arguments));
        break;
      case DiagnosticSecondaryRole::PreviousDeclaration:
        if (item.code != DiagID::PreviousDeclarationHere || item.arguments.size() != 0) {
          return SemanticDiagnosticProjectionFailure::InvalidRelated;
        }
        projected = DiagnosticSecondary::previousDeclaration(DiagID::PreviousDeclarationHere,
                                                             zc::mv(secondaryKey));
        break;
    }
    if (projected == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidRelated; }
    const bool range = item.role == DiagnosticSecondaryRole::Highlight;
    provenance.add(SourceDiagnosticProvenanceEntry{
        zc::mv(ZC_ASSERT_NONNULL(key)),
        DiagnosticSourceRange{item.span.byteStart(),
                              range ? item.span.byteEnd() : item.span.byteStart(), range}});
    secondary.add(zc::mv(ZC_ASSERT_NONNULL(projected)));
  }
  auto fact = DiagnosticFact::from(zc::mv(ZC_ASSERT_NONNULL(occurrence)), code, zc::mv(arguments),
                                   zc::mv(ZC_ASSERT_NONNULL(primaryKey)), zc::mv(secondary));
  if (fact == zc::none) { return SemanticDiagnosticProjectionFailure::InvalidFact; }
  return SemanticDiagnosticFactProjection{zc::mv(ZC_ASSERT_NONNULL(fact)), zc::mv(provenance)};
}

}  // namespace zomlang::compiler::diagnostics
