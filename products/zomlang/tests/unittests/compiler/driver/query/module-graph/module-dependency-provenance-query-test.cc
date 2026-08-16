// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/query/module-graph/module-dependency-provenance-query.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

using ExpectedFailureAlternatives =
    query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                 query::KeyRejection<binder::BinderKeyFailure>>;
static_assert(
    zc::isSameType<ModuleDependencyProvenanceQuery::Capability, ModuleDependencyProvenanceMap>());
static_assert(zc::isSameType<ModuleDependencyProvenanceQuery::FailureAlternatives,
                             ExpectedFailureAlternatives>());

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::ModuleResolutionPolicyKey policy() {
  return require(identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence));
}

identity::ModuleResolutionKey sourceRequest(const identity::ModuleKey& requester,
                                            zc::StringPtr segment) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>(segment));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> presentPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> noAlias;
  return require(
      identity::ModuleResolutionKey::from(requester.clone(), identity::ModuleDependencyKind::Import,
                                          zc::mv(presentPath), zc::mv(noAlias), policy()));
}

identity::ModuleResolutionKey preludeRequest(const identity::ModuleKey& requester) {
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> noPath;
  zc::Maybe<identity::DependencyAlias> noAlias;
  return require(identity::ModuleResolutionKey::from(requester.clone(),
                                                     identity::ModuleDependencyKind::Prelude,
                                                     zc::mv(noPath), zc::mv(noAlias), policy()));
}

identity::ImmutableSourceSnapshot sourceSnapshot() {
  return require(identity::ImmutableSourceSnapshot::from(
      tests::test_identity_detail::source(),
      zc::heapArray<uint8_t>("import dependency;\n"_zc.asBytes())));
}

ModuleDependencyProvenanceSite site(const identity::ImmutableSourceSnapshot& snapshot,
                                    uint32_t ordinal, uint32_t node, uint64_t start, uint64_t end) {
  return ModuleDependencyProvenanceSite(ordinal, ast::NodeId(node),
                                        require(snapshot.span(start, end)));
}

ModuleDependencyProvenanceOrigin sourceOrigin(const identity::ImmutableSourceSnapshot& snapshot,
                                              uint32_t ordinal, uint32_t node, uint64_t start,
                                              uint64_t end) {
  zc::Vector<ModuleDependencyProvenanceSite> sites;
  sites.add(site(snapshot, ordinal, node, start, end));
  return require(ModuleDependencyProvenanceOrigin::source(zc::mv(sites)));
}

ModuleDependencyProvenanceMap mapWith(identity::ModuleResolutionKey&& request,
                                      ModuleDependencyProvenanceOrigin&& origin,
                                      const identity::Sha256Digest& witness) {
  auto snapshot = sourceSnapshot();
  zc::Vector<ModuleDependencyProvenanceEntry> entries;
  entries.add(ModuleDependencyProvenanceEntry(zc::mv(request), zc::mv(origin)));
  return require(ModuleDependencyProvenanceMap::from(
      tests::test_identity_detail::module(), snapshot.source().clone(), snapshot.contentDigest(),
      zc::mv(entries), witness));
}

zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  result.first(bytes.size()).copyFrom(bytes);
  result.back() = 0;
  return result;
}

}  // namespace

ZC_TEST("ModuleDependencyProvenanceQueryTest.DescriptorAndKeyContractAreExact") {
  ZC_EXPECT(ModuleDependencyProvenanceQuery::descriptor.admission ==
            query::CapabilityAdmission::FinalSealedSnapshot);
  ZC_EXPECT(ModuleDependencyProvenanceQuery::descriptor.retention ==
            query::RetentionClass::Retained);
  auto key = tests::test_identity_detail::module();
  auto encoded = ModuleDependencyProvenanceQuery::encodeKey(key);
  auto decoded = ModuleDependencyProvenanceQuery::decodeKey(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encode().asPtr() == encoded.asPtr());
  ZC_EXPECT(ModuleDependencyProvenanceQuery::decodeKey(withTrailingByte(encoded.asPtr()).asPtr()) ==
            zc::none);
}

ZC_TEST("ModuleDependencyProvenanceQueryTest.SourceOriginRequiresStrictNonemptySites") {
  zc::Vector<ModuleDependencyProvenanceSite> empty;
  ZC_EXPECT(ModuleDependencyProvenanceOrigin::source(zc::mv(empty)) == zc::none);

  auto snapshot = sourceSnapshot();
  zc::Vector<ModuleDependencyProvenanceSite> duplicate;
  duplicate.add(site(snapshot, 3, 1, 0, 1));
  duplicate.add(site(snapshot, 3, 2, 1, 2));
  ZC_EXPECT(ModuleDependencyProvenanceOrigin::source(zc::mv(duplicate)) == zc::none);

  zc::Vector<ModuleDependencyProvenanceSite> reordered;
  reordered.add(site(snapshot, 4, 2, 1, 2));
  reordered.add(site(snapshot, 3, 1, 0, 1));
  ZC_EXPECT(ModuleDependencyProvenanceOrigin::source(zc::mv(reordered)) == zc::none);
}

ZC_TEST("ModuleDependencyProvenanceQueryTest.RequestOriginAlternativesCannotDrift") {
  auto module = tests::test_identity_detail::module();
  auto snapshot = sourceSnapshot();
  const auto witness = tests::test_identity_detail::digest(0x71);

  zc::Vector<ModuleDependencyProvenanceEntry> sourceAsPrelude;
  sourceAsPrelude.add(ModuleDependencyProvenanceEntry(sourceRequest(module, "dependency"_zc),
                                                      ModuleDependencyProvenanceOrigin::prelude()));
  ZC_EXPECT(ModuleDependencyProvenanceMap::from(module.clone(), snapshot.source().clone(),
                                                snapshot.contentDigest(), zc::mv(sourceAsPrelude),
                                                witness) == zc::none);

  zc::Vector<ModuleDependencyProvenanceEntry> preludeAsSource;
  preludeAsSource.add(
      ModuleDependencyProvenanceEntry(preludeRequest(module), sourceOrigin(snapshot, 1, 1, 0, 1)));
  ZC_EXPECT(ModuleDependencyProvenanceMap::from(module.clone(), snapshot.source().clone(),
                                                snapshot.contentDigest(), zc::mv(preludeAsSource),
                                                witness) == zc::none);
}

ZC_TEST("ModuleDependencyProvenanceQueryTest.RuntimeWitnessDetectsCandidateMutations") {
  auto module = tests::test_identity_detail::module();
  auto firstSnapshot = sourceSnapshot();
  const auto firstWitness = tests::test_identity_detail::digest(0x81);
  const auto secondWitness = tests::test_identity_detail::digest(0x82);
  auto first = mapWith(sourceRequest(module, "dependency"_zc),
                       sourceOrigin(firstSnapshot, 1, 1, 0, 6), firstWitness);

  auto cloned = first.clone();
  ZC_EXPECT(first.sameAs(cloned));

  auto secondSnapshot = sourceSnapshot();
  auto changedNode = mapWith(sourceRequest(module, "dependency"_zc),
                             sourceOrigin(secondSnapshot, 1, 2, 0, 6), firstWitness);
  ZC_EXPECT(!first.sameAs(changedNode));

  auto thirdSnapshot = sourceSnapshot();
  auto changedSpan = mapWith(sourceRequest(module, "dependency"_zc),
                             sourceOrigin(thirdSnapshot, 1, 1, 1, 6), firstWitness);
  ZC_EXPECT(!first.sameAs(changedSpan));

  auto fourthSnapshot = sourceSnapshot();
  auto changedWitness = mapWith(sourceRequest(module, "dependency"_zc),
                                sourceOrigin(fourthSnapshot, 1, 1, 0, 6), secondWitness);
  ZC_EXPECT(!first.sameAs(changedWitness));

  auto encoded = query::CapabilityCandidateContract<ModuleDependencyProvenanceQuery>::encode(first);
  ZC_EXPECT(encoded.bytes() == firstWitness.bytes());
  ZC_EXPECT(query::CapabilityCandidateContract<ModuleDependencyProvenanceQuery>::decode(
                encoded.bytes()) == zc::none);
}

}  // namespace zomlang::compiler::driver::module_graph_query
