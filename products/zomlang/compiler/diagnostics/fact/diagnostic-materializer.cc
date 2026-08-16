// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/fact/diagnostic-materializer.h"

#include <climits>

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/core/diagnostic.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::diagnostics {
namespace {

source::SourceLoc sourceLoc(source::SourceManager& sources, const source::BufferId& buffer,
                            uint64_t offset) {
  return sources.getLocForOffset(buffer, static_cast<unsigned>(offset));
}

source::CharSourceRange sourceRange(source::SourceManager& sources, const source::BufferId& buffer,
                                    const DiagnosticSourceRange& range) {
  return source::CharSourceRange(sourceLoc(sources, buffer, range.byteStart),
                                 sourceLoc(sources, buffer, range.byteEnd), range.isTokenRange);
}

zc::Vector<DiagnosticArgument> materializeArguments(zc::ArrayPtr<const zc::String> arguments) {
  zc::Vector<DiagnosticArgument> result(arguments.size());
  for (const auto& argument : arguments) { result.add(zc::str(argument)); }
  return result;
}

bool validArgumentRecord(DiagID code, zc::ArrayPtr<const zc::String> arguments) {
  return isSourceSyntaxDiagnostic(code) && isKnownDiagnostic(code) &&
         getDiagnosticInfo(code).argCount == arguments.size();
}

DiagnosticMaterializationFailure failureForKey(const DiagnosticProvenanceKey& key,
                                               const DiagnosticProvenanceResolver& resolver) {
  return resolver.owns(key.source()) ? DiagnosticMaterializationFailure::MissingProvenance
                                     : DiagnosticMaterializationFailure::ForeignSource;
}

}  // namespace

SourceDiagnosticProvenanceResolver::SourceDiagnosticProvenanceResolver(
    const identity::SourceFileKey& source, const SourceDiagnosticProvenanceMap& provenance) noexcept
    : source(source), provenance(provenance) {}
SourceDiagnosticProvenanceResolver::~SourceDiagnosticProvenanceResolver() noexcept(false) = default;
bool SourceDiagnosticProvenanceResolver::owns(
    const identity::SourceFileKey& candidate) const noexcept {
  return source.sameAs(candidate);
}
zc::Maybe<const DiagnosticSourceRange&> SourceDiagnosticProvenanceResolver::resolve(
    const DiagnosticProvenanceKey& key) const noexcept {
  if (!owns(key.source())) { return zc::none; }
  return provenance.find(key);
}

struct ResolvedDiagnosticBatch::Impl final {
  explicit Impl(zc::Vector<zc::Own<Diagnostic>>&& diagnostics) : diagnostics(zc::mv(diagnostics)) {}
  zc::Vector<zc::Own<Diagnostic>> diagnostics;
};

ResolvedDiagnosticBatch::ResolvedDiagnosticBatch(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ResolvedDiagnosticBatch::~ResolvedDiagnosticBatch() noexcept(false) = default;
ResolvedDiagnosticBatch::ResolvedDiagnosticBatch(ResolvedDiagnosticBatch&&) noexcept = default;
ResolvedDiagnosticBatch& ResolvedDiagnosticBatch::operator=(ResolvedDiagnosticBatch&&) noexcept =
    default;
size_t ResolvedDiagnosticBatch::size() const noexcept { return impl->diagnostics.size(); }

DiagnosticMaterializationResult materializeDiagnosticFacts(
    zc::ArrayPtr<const DiagnosticFact> facts, const DiagnosticProvenanceResolver& resolver,
    source::SourceManager& sources, const source::BufferId& buffer) {
  const uint64_t sourceByteLength = sources.getEntireTextForBuffer(buffer).size();
  if (sourceByteLength > UINT_MAX) { return DiagnosticMaterializationFailure::OutOfRange; }

  zc::Vector<zc::Own<Diagnostic>> diagnostics(facts.size());
  for (const auto& fact : facts) {
    if (!resolver.owns(fact.occurrence().source()) || !resolver.owns(fact.primary().source())) {
      return DiagnosticMaterializationFailure::ForeignSource;
    }
    if (!validArgumentRecord(fact.code(), fact.arguments())) {
      return DiagnosticMaterializationFailure::ArgumentMismatch;
    }
    auto primary = resolver.resolve(fact.primary());
    if (primary == zc::none) { return failureForKey(fact.primary(), resolver); }
    const auto& primaryRange = ZC_ASSERT_NONNULL(primary);
    if (primaryRange.byteStart != primaryRange.byteEnd || primaryRange.isTokenRange) {
      return DiagnosticMaterializationFailure::RoleMismatch;
    }
    if (primaryRange.byteEnd > sourceByteLength || primaryRange.byteEnd > UINT_MAX) {
      return DiagnosticMaterializationFailure::OutOfRange;
    }

    auto diagnostic =
        zc::heap<Diagnostic>(fact.code(), sourceLoc(sources, buffer, primaryRange.byteStart),
                             materializeArguments(fact.arguments()));
    for (const auto& secondary : fact.secondary()) {
      if (!resolver.owns(secondary.provenance().source())) {
        return DiagnosticMaterializationFailure::ForeignSource;
      }
      auto resolved = resolver.resolve(secondary.provenance());
      if (resolved == zc::none) { return failureForKey(secondary.provenance(), resolver); }
      const auto& range = ZC_ASSERT_NONNULL(resolved);
      if (range.byteEnd > sourceByteLength || range.byteEnd > UINT_MAX) {
        return DiagnosticMaterializationFailure::OutOfRange;
      }
      if (secondary.role() == DiagnosticSecondaryRole::Highlight) {
        if (secondary.code() != zc::none || secondary.arguments().size() != 0) {
          return DiagnosticMaterializationFailure::RoleMismatch;
        }
        diagnostic->addRange(sourceRange(sources, buffer, range));
        continue;
      }
      if (secondary.role() != DiagnosticSecondaryRole::Note || secondary.code() == zc::none ||
          range.byteStart != range.byteEnd || range.isTokenRange) {
        return DiagnosticMaterializationFailure::RoleMismatch;
      }
      const auto code = ZC_ASSERT_NONNULL(secondary.code());
      if (!validArgumentRecord(code, secondary.arguments())) {
        return DiagnosticMaterializationFailure::ArgumentMismatch;
      }
      diagnostic->addChildDiagnostic(
          zc::heap<Diagnostic>(code, sourceLoc(sources, buffer, range.byteStart),
                               materializeArguments(secondary.arguments())));
    }
    diagnostics.add(zc::mv(diagnostic));
  }
  return ResolvedDiagnosticBatch(zc::heap<ResolvedDiagnosticBatch::Impl>(zc::mv(diagnostics)));
}

void publishResolvedDiagnosticBatch(ResolvedDiagnosticBatch&& batch, DiagnosticEngine& engine) {
  for (const auto& diagnostic : batch.impl->diagnostics) { engine.emit(*diagnostic); }
  batch.impl->diagnostics.clear();
}

}  // namespace zomlang::compiler::diagnostics
