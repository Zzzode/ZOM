// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/compilation-diagnostic-facts.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {
namespace {

void swapFacts(DiagnosticFact& left, DiagnosticFact& right) {
  DiagnosticFact retained = zc::mv(left);
  left = zc::mv(right);
  right = zc::mv(retained);
}

template <typename Less>
void siftDown(zc::Vector<DiagnosticFact>& facts, size_t root, size_t count, Less&& less) {
  while (root < count / 2) {
    size_t child = root * 2 + 1;
    if (child + 1 < count && less(facts[child], facts[child + 1])) { ++child; }
    if (!less(facts[root], facts[child])) { return; }
    swapFacts(facts[root], facts[child]);
    root = child;
  }
}

template <typename Less>
void sortFacts(zc::Vector<DiagnosticFact>& facts, Less&& less) {
  if (facts.size() < 2) { return; }
  for (size_t root = facts.size() / 2; root != 0; --root) {
    siftDown(facts, root - 1, facts.size(), less);
  }
  for (size_t remaining = facts.size(); remaining > 1; --remaining) {
    swapFacts(facts[0], facts[remaining - 1]);
    siftDown(facts, 0, remaining - 1, less);
  }
}

void swapProvenance(SourceDiagnosticProvenanceEntry& left, SourceDiagnosticProvenanceEntry& right) {
  SourceDiagnosticProvenanceEntry retained = zc::mv(left);
  left = zc::mv(right);
  right = zc::mv(retained);
}

void siftDownProvenance(zc::Vector<SourceDiagnosticProvenanceEntry>& entries, size_t root,
                        size_t count) {
  while (root < count / 2) {
    size_t child = root * 2 + 1;
    if (child + 1 < count && entries[child].key < entries[child + 1].key) { ++child; }
    if (!(entries[root].key < entries[child].key)) { return; }
    swapProvenance(entries[root], entries[child]);
    root = child;
  }
}

void sortProvenance(zc::Vector<SourceDiagnosticProvenanceEntry>& entries) {
  if (entries.size() < 2) { return; }
  for (size_t root = entries.size() / 2; root != 0; --root) {
    siftDownProvenance(entries, root - 1, entries.size());
  }
  for (size_t remaining = entries.size(); remaining > 1; --remaining) {
    swapProvenance(entries[0], entries[remaining - 1]);
    siftDownProvenance(entries, 0, remaining - 1);
  }
}

template <typename T>
int compareScalar(const T& left, const T& right) {
  if (left < right) { return -1; }
  if (right < left) { return 1; }
  return 0;
}

int compareStrings(zc::ArrayPtr<const zc::String> left, zc::ArrayPtr<const zc::String> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    const int comparison = compareScalar(zc::StringPtr(left[index]), zc::StringPtr(right[index]));
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareSecondary(const DiagnosticSecondary& left, const DiagnosticSecondary& right) {
  int comparison =
      compareScalar(static_cast<uint8_t>(left.role()), static_cast<uint8_t>(right.role()));
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(left.code() != zc::none, right.code() != zc::none);
  if (comparison != 0) { return comparison; }
  if (left.code() != zc::none) {
    comparison = compareScalar(static_cast<uint32_t>(ZC_ASSERT_NONNULL(left.code())),
                               static_cast<uint32_t>(ZC_ASSERT_NONNULL(right.code())));
    if (comparison != 0) { return comparison; }
  }
  comparison = compareScalar(left.provenance(), right.provenance());
  if (comparison != 0) { return comparison; }
  return compareStrings(left.arguments(), right.arguments());
}

int compareSecondaryLists(zc::ArrayPtr<const DiagnosticSecondary> left,
                          zc::ArrayPtr<const DiagnosticSecondary> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    const int comparison = compareSecondary(left[index], right[index]);
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

bool canonicalLess(const DiagnosticFact& left, const DiagnosticFact& right) {
  int comparison = compareScalar(left.primary(), right.primary());
  if (comparison != 0) { return comparison < 0; }
  comparison = compareScalar(static_cast<uint8_t>(getDiagnosticInfo(left.code()).severity),
                             static_cast<uint8_t>(getDiagnosticInfo(right.code()).severity));
  if (comparison != 0) { return comparison < 0; }
  comparison =
      compareScalar(static_cast<uint32_t>(left.code()), static_cast<uint32_t>(right.code()));
  if (comparison != 0) { return comparison < 0; }
  comparison = compareScalar(left.occurrence(), right.occurrence());
  if (comparison != 0) { return comparison < 0; }
  comparison = compareStrings(left.arguments(), right.arguments());
  if (comparison != 0) { return comparison < 0; }
  return compareSecondaryLists(left.secondary(), right.secondary()) < 0;
}

bool rootProvenanceIsComplete(zc::ArrayPtr<const DiagnosticFact> facts,
                              zc::ArrayPtr<const SourceDiagnosticProvenanceEntry> provenance) {
  size_t expected = 0;
  for (const auto& fact : facts) {
    if (fact.secondary().size() > provenance.size() - expected) { return false; }
    expected += 1 + fact.secondary().size();
    if (expected > provenance.size()) { return false; }
  }
  if (expected != provenance.size()) { return false; }

  size_t index = 0;
  for (const auto& fact : facts) {
    const auto& primary = provenance[index++];
    if (primary.key != fact.primary() || primary.range.byteStart != primary.range.byteEnd ||
        primary.range.isTokenRange) {
      return false;
    }
    for (const auto& secondary : fact.secondary()) {
      const auto& entry = provenance[index++];
      if (entry.key != secondary.provenance()) { return false; }
      if (secondary.role() != DiagnosticSecondaryRole::Highlight &&
          (entry.range.byteStart != entry.range.byteEnd || entry.range.isTokenRange)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

struct CompilationDiagnosticFacts::Impl final {
  explicit Impl(zc::Vector<DiagnosticFact>&& facts,
                zc::Vector<CompilationDiagnosticSource>&& sources = {},
                zc::Vector<CompilationDiagnosticDocument>&& documents = {})
      : facts(zc::mv(facts)), sources(zc::mv(sources)), documents(zc::mv(documents)) {}
  zc::Vector<DiagnosticFact> facts;
  zc::Vector<CompilationDiagnosticSource> sources;
  zc::Vector<CompilationDiagnosticDocument> documents;
};

CompilationDiagnosticFacts::CompilationDiagnosticFacts(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
CompilationDiagnosticFacts::~CompilationDiagnosticFacts() noexcept(false) = default;
CompilationDiagnosticFacts::CompilationDiagnosticFacts(CompilationDiagnosticFacts&&) noexcept =
    default;
CompilationDiagnosticFacts& CompilationDiagnosticFacts::operator=(
    CompilationDiagnosticFacts&&) noexcept = default;
zc::ArrayPtr<const DiagnosticFact> CompilationDiagnosticFacts::facts() const {
  return impl->facts.asPtr();
}
zc::ArrayPtr<const CompilationDiagnosticSource> CompilationDiagnosticFacts::sources() const {
  return impl->sources.asPtr();
}
zc::ArrayPtr<const CompilationDiagnosticDocument> CompilationDiagnosticFacts::documents() const {
  return impl->documents.asPtr();
}

struct DiagnosticFactCollector::Impl final {
  zc::Vector<DiagnosticFact> facts;
  bool capacityExceeded = false;
};

DiagnosticFactCollector::DiagnosticFactCollector() : impl(zc::heap<Impl>()) {}
DiagnosticFactCollector::~DiagnosticFactCollector() noexcept(false) = default;
DiagnosticFactCollector::DiagnosticFactCollector(DiagnosticFactCollector&&) noexcept = default;
DiagnosticFactCollector& DiagnosticFactCollector::operator=(DiagnosticFactCollector&&) noexcept =
    default;

bool DiagnosticFactCollector::addRoot(zc::ArrayPtr<const DiagnosticFact> facts) {
  if (facts.size() > maximumFacts || impl->facts.size() > maximumFacts - facts.size()) {
    impl->capacityExceeded = true;
    return false;
  }
  for (const auto& fact : facts) { impl->facts.add(fact.clone()); }
  return true;
}

DiagnosticCollectionResult DiagnosticFactCollector::seal() && {
  if (impl->capacityExceeded) { return DiagnosticCollectionFailure::CapacityExceeded; }
  sortFacts(impl->facts, [](const DiagnosticFact& left, const DiagnosticFact& right) {
    return left.occurrence() < right.occurrence();
  });

  zc::Vector<DiagnosticFact> canonical(impl->facts.size());
  for (auto& fact : impl->facts) {
    if (canonical.size() == 0 || !(canonical.back().occurrence() == fact.occurrence())) {
      canonical.add(zc::mv(fact));
      continue;
    }
    if (!(canonical.back() == fact)) { return DiagnosticCollectionFailure::ConflictingOccurrence; }
  }
  sortFacts(canonical, canonicalLess);
  return CompilationDiagnosticFacts(zc::heap<CompilationDiagnosticFacts::Impl>(zc::mv(canonical)));
}

struct CompilationDiagnosticCollector::Impl final {
  struct SourceAccumulator final {
    identity::SourceFileKey source;
    uint64_t sourceByteLength;
    zc::Vector<SourceDiagnosticProvenanceEntry> provenance;
  };
  struct DocumentAccumulator final {
    zc::Array<uint8_t> identity;
    uint64_t byteLength;
    zc::Vector<SourceDiagnosticProvenanceEntry> provenance;
  };

  DiagnosticFactCollector facts;
  zc::TreeMap<zc::String, SourceAccumulator> sources;
  zc::TreeMap<zc::String, DocumentAccumulator> documents;
  size_t factCount = 0;
  size_t provenanceCount = 0;
  bool containsErrors = false;
  zc::Maybe<DiagnosticCollectionFailure> failure;
};

CompilationDiagnosticCollector::CompilationDiagnosticCollector() : impl(zc::heap<Impl>()) {}
CompilationDiagnosticCollector::~CompilationDiagnosticCollector() noexcept(false) = default;
CompilationDiagnosticCollector::CompilationDiagnosticCollector(
    CompilationDiagnosticCollector&&) noexcept = default;
CompilationDiagnosticCollector& CompilationDiagnosticCollector::operator=(
    CompilationDiagnosticCollector&&) noexcept = default;

bool CompilationDiagnosticCollector::addSourceAuthority(const identity::SourceFileKey& source,
                                                        uint64_t sourceByteLength) {
  if (impl->failure != zc::none) { return false; }
  auto key = zc::encodeHex(source.encode().asPtr());
  auto existing = impl->sources.find(key);
  if (existing != zc::none) {
    const auto& authority = ZC_ASSERT_NONNULL(existing);
    if (!authority.source.sameAs(source) || authority.sourceByteLength != sourceByteLength) {
      impl->failure = DiagnosticCollectionFailure::ConflictingAuthority;
      return false;
    }
    return true;
  }
  impl->sources.insert(zc::mv(key),
                       Impl::SourceAccumulator{source.clone(), sourceByteLength,
                                               zc::Vector<SourceDiagnosticProvenanceEntry>()});
  return true;
}

bool CompilationDiagnosticCollector::addDocumentAuthority(zc::ArrayPtr<const uint8_t> identity,
                                                          uint64_t documentByteLength) {
  if (impl->failure != zc::none || identity.size() == 0) {
    impl->failure = DiagnosticCollectionFailure::InvalidProvenance;
    return false;
  }
  auto key = zc::encodeHex(identity);
  auto existing = impl->documents.find(key);
  if (existing != zc::none) {
    const auto& authority = ZC_ASSERT_NONNULL(existing);
    if (authority.identity.asPtr() != identity || authority.byteLength != documentByteLength) {
      impl->failure = DiagnosticCollectionFailure::ConflictingAuthority;
      return false;
    }
    return true;
  }
  impl->documents.insert(
      zc::mv(key),
      Impl::DocumentAccumulator{zc::heapArray<uint8_t>(identity), documentByteLength, {}});
  return true;
}

bool CompilationDiagnosticCollector::addRoot(
    zc::ArrayPtr<const DiagnosticFact> facts,
    zc::ArrayPtr<const SourceDiagnosticProvenanceEntry> provenance) {
  if (impl->failure != zc::none) { return false; }
  if (facts.size() > DiagnosticFactCollector::maximumFacts ||
      impl->factCount > DiagnosticFactCollector::maximumFacts - facts.size() ||
      provenance.size() > maximumProvenanceEntries ||
      impl->provenanceCount > maximumProvenanceEntries - provenance.size()) {
    impl->failure = DiagnosticCollectionFailure::CapacityExceeded;
    return false;
  }
  if (!rootProvenanceIsComplete(facts, provenance)) {
    impl->failure = DiagnosticCollectionFailure::InvalidProvenance;
    return false;
  }
  for (const auto& entry : provenance) {
    if (entry.key.origin() == DiagnosticFactOrigin::Document) {
      const auto key = zc::encodeHex(entry.key.documentIdentityBytes());
      auto document = impl->documents.find(key);
      if (document == zc::none ||
          ZC_ASSERT_NONNULL(document).identity.asPtr() != entry.key.documentIdentityBytes() ||
          entry.range.byteEnd > ZC_ASSERT_NONNULL(document).byteLength) {
        impl->failure = DiagnosticCollectionFailure::MissingAuthority;
        return false;
      }
    } else {
      const auto key = zc::encodeHex(entry.key.source().encode().asPtr());
      auto source = impl->sources.find(key);
      if (source == zc::none || !ZC_ASSERT_NONNULL(source).source.sameAs(entry.key.source())) {
        impl->failure = DiagnosticCollectionFailure::MissingAuthority;
        return false;
      }
      if (entry.range.byteEnd > ZC_ASSERT_NONNULL(source).sourceByteLength) {
        impl->failure = DiagnosticCollectionFailure::InvalidProvenance;
        return false;
      }
    }
  }
  if (!impl->facts.addRoot(facts)) {
    impl->failure = DiagnosticCollectionFailure::CapacityExceeded;
    return false;
  }
  for (const auto& fact : facts) {
    if (getDiagnosticInfo(fact.code()).severity >= DiagSeverity::kError) {
      impl->containsErrors = true;
    }
  }
  for (const auto& entry : provenance) {
    if (entry.key.origin() == DiagnosticFactOrigin::Document) {
      const auto key = zc::encodeHex(entry.key.documentIdentityBytes());
      ZC_ASSERT_NONNULL(impl->documents.find(key)).provenance.add(entry.clone());
    } else {
      const auto key = zc::encodeHex(entry.key.source().encode().asPtr());
      ZC_ASSERT_NONNULL(impl->sources.find(key)).provenance.add(entry.clone());
    }
  }
  impl->factCount += facts.size();
  impl->provenanceCount += provenance.size();
  return true;
}

bool CompilationDiagnosticCollector::addSourceRoot(
    const identity::SourceFileKey& source, zc::ArrayPtr<const DiagnosticFact> facts,
    const SourceDiagnosticProvenanceMap& provenance) {
  if (!addSourceAuthority(source, provenance.sourceByteLength())) { return false; }
  for (const auto& entry : provenance.entries()) {
    if (entry.key.origin() == DiagnosticFactOrigin::Document ||
        !entry.key.source().sameAs(source)) {
      impl->failure = DiagnosticCollectionFailure::InvalidProvenance;
      return false;
    }
  }
  return addRoot(facts, provenance.entries());
}

bool CompilationDiagnosticCollector::hasErrors() const noexcept { return impl->containsErrors; }

zc::Maybe<DiagnosticCollectionFailure> CompilationDiagnosticCollector::failure() const noexcept {
  return impl->failure;
}

DiagnosticCollectionResult CompilationDiagnosticCollector::seal() && {
  if (impl->failure != zc::none) { return ZC_ASSERT_NONNULL(impl->failure); }
  auto facts = zc::mv(impl->facts).seal();
  if (facts.is<DiagnosticCollectionFailure>()) { return facts.get<DiagnosticCollectionFailure>(); }
  auto sealed = zc::mv(facts).get<CompilationDiagnosticFacts>();

  zc::Vector<CompilationDiagnosticSource> sources(impl->sources.size());
  for (auto& source : impl->sources) {
    sortProvenance(source.value.provenance);
    zc::Vector<SourceDiagnosticProvenanceEntry> canonical(source.value.provenance.size());
    for (auto& entry : source.value.provenance) {
      if (canonical.size() == 0 || !(canonical.back().key == entry.key)) {
        canonical.add(zc::mv(entry));
        continue;
      }
      if (!(canonical.back() == entry)) {
        return DiagnosticCollectionFailure::ConflictingProvenance;
      }
    }
    auto provenance =
        SourceDiagnosticProvenanceMap::from(zc::mv(canonical), source.value.sourceByteLength);
    if (provenance == zc::none) { return DiagnosticCollectionFailure::InvalidProvenance; }
    sources.add(CompilationDiagnosticSource{source.value.source.clone(),
                                            zc::mv(ZC_ASSERT_NONNULL(provenance))});
  }

  zc::Vector<CompilationDiagnosticDocument> documents(impl->documents.size());
  for (auto& document : impl->documents) {
    sortProvenance(document.value.provenance);
    zc::Vector<SourceDiagnosticProvenanceEntry> canonical(document.value.provenance.size());
    for (auto& entry : document.value.provenance) {
      if (entry.key.origin() != DiagnosticFactOrigin::Document ||
          entry.key.documentIdentityBytes() != document.value.identity.asPtr() ||
          entry.range.byteStart > entry.range.byteEnd ||
          entry.range.byteEnd > document.value.byteLength) {
        return DiagnosticCollectionFailure::InvalidProvenance;
      }
      if (canonical.size() == 0 || !(canonical.back().key == entry.key)) {
        canonical.add(zc::mv(entry));
        continue;
      }
      if (!(canonical.back() == entry)) {
        return DiagnosticCollectionFailure::ConflictingProvenance;
      }
    }
    documents.add(
        CompilationDiagnosticDocument{zc::heapArray<uint8_t>(document.value.identity.asPtr()),
                                      document.value.byteLength, canonical.releaseAsArray()});
  }

  zc::Vector<DiagnosticFact> canonicalFacts(sealed.facts().size());
  for (const auto& fact : sealed.facts()) { canonicalFacts.add(fact.clone()); }
  return CompilationDiagnosticFacts(zc::heap<CompilationDiagnosticFacts::Impl>(
      zc::mv(canonicalFacts), zc::mv(sources), zc::mv(documents)));
}

}  // namespace zomlang::compiler::diagnostics
