// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/diagnostic-materializer.h"

#include <climits>

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/identity/key/source-key.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::diagnostics {
namespace {

zc::Array<zc::String> cloneStrings(zc::ArrayPtr<const zc::String> arguments) {
  auto result = zc::heapArray<zc::String>(arguments.size());
  for (size_t index = 0; index < arguments.size(); ++index) {
    result[index] = zc::str(arguments[index]);
  }
  return result;
}

bool validArgumentRecord(DiagID code, zc::ArrayPtr<const zc::String> arguments) {
  return isKnownDiagnostic(code) && getDiagnosticInfo(code).argCount == arguments.size();
}

DiagnosticMaterializationFailure failureForKey(const DiagnosticProvenanceKey& key,
                                               const DiagnosticProvenanceResolver& resolver) {
  return resolver.owns(key) ? DiagnosticMaterializationFailure::MissingProvenance
                            : DiagnosticMaterializationFailure::ForeignSource;
}

}  // namespace

SourceDiagnosticProvenanceResolver::SourceDiagnosticProvenanceResolver(
    const identity::SourceFileKey& source, const SourceDiagnosticProvenanceMap& provenance) noexcept
    : source(source), provenance(provenance) {}
SourceDiagnosticProvenanceResolver::~SourceDiagnosticProvenanceResolver() noexcept(false) = default;
bool SourceDiagnosticProvenanceResolver::owns(const DiagnosticProvenanceKey& key) const noexcept {
  return key.origin() != DiagnosticFactOrigin::Document && source.sameAs(key.source());
}
zc::Maybe<uint64_t> SourceDiagnosticProvenanceResolver::byteLength(
    const DiagnosticProvenanceKey& key) const noexcept {
  if (!owns(key)) { return zc::none; }
  return provenance.sourceByteLength();
}
zc::Maybe<const DiagnosticSourceRange&> SourceDiagnosticProvenanceResolver::resolve(
    const DiagnosticProvenanceKey& key) const noexcept {
  if (!owns(key)) { return zc::none; }
  return provenance.find(key);
}

CompilationDiagnosticProvenanceResolver::CompilationDiagnosticProvenanceResolver(
    zc::Vector<DiagnosticSourceProvenance>&& sources) noexcept
    : sources(zc::mv(sources)) {}
CompilationDiagnosticProvenanceResolver::~CompilationDiagnosticProvenanceResolver() noexcept(
    false) = default;
CompilationDiagnosticProvenanceResolver::CompilationDiagnosticProvenanceResolver(
    CompilationDiagnosticProvenanceResolver&&) noexcept = default;
CompilationDiagnosticProvenanceResolver& CompilationDiagnosticProvenanceResolver::operator=(
    CompilationDiagnosticProvenanceResolver&&) noexcept = default;
zc::Maybe<CompilationDiagnosticProvenanceResolver> CompilationDiagnosticProvenanceResolver::from(
    zc::Vector<DiagnosticSourceProvenance>&& sources) {
  for (size_t index = 0; index < sources.size(); ++index) {
    const auto entries = sources[index].provenance.entries();
    if (entries.size() != 0 && !entries[0].key.source().sameAs(sources[index].source)) {
      return zc::none;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (sources[prior].source.sameAs(sources[index].source)) { return zc::none; }
    }
  }
  return CompilationDiagnosticProvenanceResolver(zc::mv(sources));
}
zc::Maybe<CompilationDiagnosticProvenanceResolver> CompilationDiagnosticProvenanceResolver::from(
    const CompilationDiagnosticFacts& facts) {
  zc::Vector<DiagnosticSourceProvenance> sources(facts.sources().size());
  for (const auto& source : facts.sources()) {
    sources.add(DiagnosticSourceProvenance{source.source.clone(), source.provenance.clone()});
  }
  auto resolver = from(zc::mv(sources));
  if (resolver == zc::none) { return zc::none; }
  for (const auto& document : facts.documents()) {
    zc::Vector<SourceDiagnosticProvenanceEntry> copied(document.provenance.size());
    for (const auto& entry : document.provenance) { copied.add(entry.clone()); }
    ZC_ASSERT_NONNULL(resolver).documents.add(
        CompilationDiagnosticDocument{zc::heapArray<uint8_t>(document.identity.asPtr()),
                                      document.byteLength, copied.releaseAsArray()});
  }
  return zc::mv(ZC_ASSERT_NONNULL(resolver));
}
bool CompilationDiagnosticProvenanceResolver::owns(
    const DiagnosticProvenanceKey& key) const noexcept {
  return byteLength(key) != zc::none;
}
zc::Maybe<uint64_t> CompilationDiagnosticProvenanceResolver::byteLength(
    const DiagnosticProvenanceKey& key) const noexcept {
  if (key.origin() == DiagnosticFactOrigin::Document) {
    for (const auto& item : documents) {
      if (item.identity.asPtr() == key.documentIdentityBytes()) { return item.byteLength; }
    }
    return zc::none;
  }
  for (const auto& item : sources) {
    if (item.source.sameAs(key.source())) { return item.provenance.sourceByteLength(); }
  }
  return zc::none;
}
zc::Maybe<const DiagnosticSourceRange&> CompilationDiagnosticProvenanceResolver::resolve(
    const DiagnosticProvenanceKey& key) const noexcept {
  if (key.origin() == DiagnosticFactOrigin::Document) {
    for (const auto& item : documents) {
      if (item.identity.asPtr() != key.documentIdentityBytes()) { continue; }
      for (const auto& entry : item.provenance) {
        if (entry.key == key) { return entry.range; }
      }
    }
    return zc::none;
  }
  for (const auto& item : sources) {
    if (item.source.sameAs(key.source())) { return item.provenance.find(key); }
  }
  return zc::none;
}

ResolvedDiagnosticLocation::ResolvedDiagnosticLocation(identity::SourceFileKey&& source,
                                                       DiagnosticSourceRange range) noexcept
    : identityValue(zc::mv(source)), rangeValue(range) {}
ResolvedDiagnosticLocation::ResolvedDiagnosticLocation(zc::Array<uint8_t>&& document,
                                                       DiagnosticSourceRange range) noexcept
    : identityValue(zc::mv(document)), rangeValue(range) {}
DiagnosticFactOrigin ResolvedDiagnosticLocation::origin() const noexcept {
  return identityValue.is<identity::SourceFileKey>() ? DiagnosticFactOrigin::Source
                                                     : DiagnosticFactOrigin::Document;
}
const identity::SourceFileKey& ResolvedDiagnosticLocation::source() const {
  ZC_IREQUIRE(origin() != DiagnosticFactOrigin::Document,
              "document diagnostic location has no source identity");
  return identityValue.get<identity::SourceFileKey>();
}
zc::ArrayPtr<const uint8_t> ResolvedDiagnosticLocation::documentIdentityBytes() const {
  ZC_IREQUIRE(origin() == DiagnosticFactOrigin::Document,
              "source diagnostic location has no document identity");
  return identityValue.get<zc::Array<uint8_t>>().asPtr();
}
ResolvedDiagnosticLocation ResolvedDiagnosticLocation::clone() const {
  if (origin() == DiagnosticFactOrigin::Document) {
    return ResolvedDiagnosticLocation(zc::heapArray<uint8_t>(documentIdentityBytes()), rangeValue);
  }
  return ResolvedDiagnosticLocation(source().clone(), rangeValue);
}

ResolvedDiagnosticRelated ResolvedDiagnosticRelated::clone() const {
  return ResolvedDiagnosticRelated{role, code, location.clone(), cloneStrings(arguments.asPtr())};
}

ResolvedDiagnostic::ResolvedDiagnostic(DiagID code, ResolvedDiagnosticLocation&& primary,
                                       zc::Array<zc::String>&& arguments,
                                       zc::Array<ResolvedDiagnosticRelated>&& related) noexcept
    : codeValue(code),
      primaryValue(zc::mv(primary)),
      argumentValues(zc::mv(arguments)),
      relatedValues(zc::mv(related)) {}
DiagSeverity ResolvedDiagnostic::severity() const noexcept {
  return getDiagnosticInfo(codeValue).severity;
}
ResolvedDiagnostic ResolvedDiagnostic::clone() const {
  zc::Vector<ResolvedDiagnosticRelated> related(relatedValues.size());
  for (const auto& item : relatedValues) { related.add(item.clone()); }
  return ResolvedDiagnostic(codeValue, primaryValue.clone(), cloneStrings(argumentValues.asPtr()),
                            related.releaseAsArray());
}

struct ResolvedDiagnosticBatch::Impl final {
  explicit Impl(zc::Vector<ResolvedDiagnostic>&& diagnostics) : diagnostics(zc::mv(diagnostics)) {}
  zc::Vector<ResolvedDiagnostic> diagnostics;
};

ResolvedDiagnosticBatch::ResolvedDiagnosticBatch(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ResolvedDiagnosticBatch::~ResolvedDiagnosticBatch() noexcept(false) = default;
ResolvedDiagnosticBatch::ResolvedDiagnosticBatch(ResolvedDiagnosticBatch&&) noexcept = default;
ResolvedDiagnosticBatch& ResolvedDiagnosticBatch::operator=(ResolvedDiagnosticBatch&&) noexcept =
    default;
size_t ResolvedDiagnosticBatch::size() const noexcept { return impl->diagnostics.size(); }
zc::ArrayPtr<const ResolvedDiagnostic> ResolvedDiagnosticBatch::diagnostics() const {
  return impl->diagnostics.asPtr();
}
ResolvedDiagnosticBatch ResolvedDiagnosticBatch::clone() const {
  zc::Vector<ResolvedDiagnostic> diagnostics(impl->diagnostics.size());
  for (const auto& diagnostic : impl->diagnostics) { diagnostics.add(diagnostic.clone()); }
  return ResolvedDiagnosticBatch(zc::heap<Impl>(zc::mv(diagnostics)));
}

DiagnosticMaterializationResult materializeDiagnosticFacts(
    zc::ArrayPtr<const DiagnosticFact> facts, const DiagnosticProvenanceResolver& resolver) {
  zc::Vector<ResolvedDiagnostic> diagnostics(facts.size());
  for (const auto& fact : facts) {
    if (!resolver.owns(fact.primary())) { return DiagnosticMaterializationFailure::ForeignSource; }
    if (fact.occurrence().origin() != fact.primary().origin()) {
      return DiagnosticMaterializationFailure::ForeignSource;
    }
    if (fact.occurrence().origin() == DiagnosticFactOrigin::Document) {
      if (fact.occurrence().documentIdentityBytes() != fact.primary().documentIdentityBytes()) {
        return DiagnosticMaterializationFailure::ForeignSource;
      }
    } else if (!fact.occurrence().source().sameAs(fact.primary().source())) {
      return DiagnosticMaterializationFailure::ForeignSource;
    }
    if (!validArgumentRecord(fact.code(), fact.arguments())) {
      return DiagnosticMaterializationFailure::ArgumentMismatch;
    }
    auto primary = resolver.resolve(fact.primary());
    if (primary == zc::none) { return failureForKey(fact.primary(), resolver); }
    const auto& primaryRange = ZC_ASSERT_NONNULL(primary);
    auto primarySourceByteLength = resolver.byteLength(fact.primary());
    if (primarySourceByteLength == zc::none) {
      return DiagnosticMaterializationFailure::ForeignSource;
    }
    if (primaryRange.byteStart != primaryRange.byteEnd || primaryRange.isTokenRange) {
      return DiagnosticMaterializationFailure::RoleMismatch;
    }
    if (primaryRange.byteEnd > ZC_ASSERT_NONNULL(primarySourceByteLength) ||
        primaryRange.byteEnd > UINT_MAX) {
      return DiagnosticMaterializationFailure::OutOfRange;
    }

    zc::Vector<ResolvedDiagnosticRelated> related(fact.secondary().size());
    for (const auto& secondary : fact.secondary()) {
      if (!resolver.owns(secondary.provenance())) {
        return DiagnosticMaterializationFailure::ForeignSource;
      }
      auto resolved = resolver.resolve(secondary.provenance());
      if (resolved == zc::none) { return failureForKey(secondary.provenance(), resolver); }
      const auto& range = ZC_ASSERT_NONNULL(resolved);
      auto sourceByteLength = resolver.byteLength(secondary.provenance());
      if (sourceByteLength == zc::none) { return DiagnosticMaterializationFailure::ForeignSource; }
      if (range.byteEnd > ZC_ASSERT_NONNULL(sourceByteLength) || range.byteEnd > UINT_MAX) {
        return DiagnosticMaterializationFailure::OutOfRange;
      }
      if (secondary.role() == DiagnosticSecondaryRole::Highlight) {
        if (secondary.code() != zc::none || secondary.arguments().size() != 0) {
          return DiagnosticMaterializationFailure::RoleMismatch;
        }
      } else if (secondary.role() == DiagnosticSecondaryRole::Note) {
        if (secondary.code() == zc::none || range.byteStart != range.byteEnd ||
            range.isTokenRange ||
            !validArgumentRecord(ZC_ASSERT_NONNULL(secondary.code()), secondary.arguments())) {
          return DiagnosticMaterializationFailure::RoleMismatch;
        }
      } else if (secondary.role() == DiagnosticSecondaryRole::PreviousDeclaration) {
        if (secondary.code() != DiagID::PreviousDeclarationHere ||
            secondary.arguments().size() != 0 || range.byteStart != range.byteEnd ||
            range.isTokenRange) {
          return DiagnosticMaterializationFailure::RoleMismatch;
        }
      } else {
        return DiagnosticMaterializationFailure::RoleMismatch;
      }
      auto location =
          secondary.provenance().origin() == DiagnosticFactOrigin::Document
              ? ResolvedDiagnosticLocation(
                    zc::heapArray<uint8_t>(secondary.provenance().documentIdentityBytes()), range)
              : ResolvedDiagnosticLocation(secondary.provenance().source().clone(), range);
      related.add(ResolvedDiagnosticRelated{secondary.role(), secondary.code(), zc::mv(location),
                                            cloneStrings(secondary.arguments())});
    }
    auto primaryLocation =
        fact.primary().origin() == DiagnosticFactOrigin::Document
            ? ResolvedDiagnosticLocation(
                  zc::heapArray<uint8_t>(fact.primary().documentIdentityBytes()), primaryRange)
            : ResolvedDiagnosticLocation(fact.primary().source().clone(), primaryRange);
    diagnostics.add(ResolvedDiagnostic(fact.code(), zc::mv(primaryLocation),
                                       cloneStrings(fact.arguments()), related.releaseAsArray()));
  }
  return ResolvedDiagnosticBatch(zc::heap<ResolvedDiagnosticBatch::Impl>(zc::mv(diagnostics)));
}

DiagnosticMaterializationResult materializeDiagnosticFacts(
    const CompilationDiagnosticFacts& facts, const DiagnosticProvenanceResolver& resolver) {
  return materializeDiagnosticFacts(facts.facts(), resolver);
}

}  // namespace zomlang::compiler::diagnostics
