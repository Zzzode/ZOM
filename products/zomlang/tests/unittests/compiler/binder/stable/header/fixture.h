// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/tests/unittests/compiler/binder/parsed-module-query-test-fixture.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/stable/definition/header-producer.h"
#include "zomlang/compiler/binder/stable/header/verifier.h"
#include "zomlang/compiler/binder/stable/candidate/producer.h"
#include "zomlang/compiler/binder/stable/candidate/verifier.h"
#include "zomlang/compiler/binder/stable/implementation/header-producer.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder::test {

template <typename T>
T requireStableHeaderValue(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

inline identity::SourceFileKey stableHeaderSourceFile(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      tests::test_identity_detail::crate(),
      identity::SourceOriginKey::localFile(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

class StableHeaderFixture final {
public:
  explicit StableHeaderFixture(identity::SourceFileKey&& source)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(
            R"zom(module test;
interface Trait {}
class Box<T> {}
impl<T> Trait for Box<T> {}
impl<T> Trait for Box<T> {}
)zom"_zc.asBytes(),
            "test.zom")) {
    diagnostics::SourceDiagnosticDraftBuffer diagnostics(*sources, buffer);
    parser::Parser parser(*sources, diagnostics, options, strings, buffer);
    auto tree = parser.parse();
    ZC_REQUIRE(tree != zc::none);
    ZC_REQUIRE(!diagnostics.hasErrors());
    auto tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
    auto snapshot = identity::ImmutableSourceSnapshot::from(
        zc::mv(source), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshot != zc::none);
    auto parsedSource =
        canonicalParsedSource(ZC_ASSERT_NONNULL(snapshot), *sources, buffer,
                              zc::mv(ZC_ASSERT_NONNULL(tokens)), zc::mv(ZC_ASSERT_NONNULL(tree)));
    parsedValue = requireStableHeaderValue(
        CanonicalParsedModule::fromQueryResult(zc::mv(ZC_ASSERT_NONNULL(parsedSource))));

    const auto syntax = DefinitionInventory::collect(parsed().tree());
    ZC_REQUIRE(syntax.modules().size() == 1);
    moduleNode = syntax.modules()[0].node;
    auto production = CandidateProducer::produce(
        parsed(), tests::test_identity_detail::module(), moduleNode);
    auto verification = CandidateVerifier::verify(
        parsed(), tests::test_identity_detail::module(), moduleNode, production);
    ZC_REQUIRE(verification.is<VerifiedStableIdentityCandidateInventory>());
    auto candidates = zc::mv(verification.get<VerifiedStableIdentityCandidateInventory>());
    ZC_REQUIRE(candidates.implementations.size() == 2);
    ZC_REQUIRE(candidates.implementations[0].authority.key() ==
               candidates.implementations[1].authority.key());

    zc::Vector<NamedDefinitionInventoryInput> definitionInputs;
    zc::Vector<RevisionLocalDefinitionSite> definitionSiteInputs;
    for (const auto& definition : candidates.definitions) {
      definitionInputs.add(NamedDefinitionInventoryInput{
          definition.authority.clone(), DefinitionBodyDisposition::NoExecutableBody});
      definitionSiteInputs.add(requireStableHeaderValue(RevisionLocalDefinitionSite::from(
          definition.node, definition.authority.key().clone(), definition.site.clone(),
          definition.source.byteStart(), definition.source.byteEnd())));
    }
    definitionsValue = requireStableHeaderValue(NamedDefinitionInventory::fromVerified(
        tests::test_identity_detail::module(), definitionInputs.asPtr()));
    definitionSitesValue = requireStableHeaderValue(RevisionLocalDefinitionSites::fromVerified(
        tests::test_identity_detail::module(), parsed().source(), definitions(),
        zc::mv(definitionSiteInputs)));

    zc::Vector<identity::ImplIdentityAuthority> implementationInputs;
    zc::Vector<RevisionLocalImplementationSite> implementationSiteInputs;
    for (const auto& implementation : candidates.implementations) {
      implementationInputs.add(implementation.authority.clone());
      auto occurrence = ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                                      implementation.site.clone());
      implementationSiteInputs.add(requireStableHeaderValue(RevisionLocalImplementationSite::from(
          implementation.node, zc::mv(occurrence), implementation.source.byteStart(),
          implementation.source.byteEnd())));
    }
    implementationsValue = requireStableHeaderValue(NamedImplementationInventory::fromVerified(
        tests::test_identity_detail::module(), implementationInputs.asPtr()));
    implementationSitesValue =
        requireStableHeaderValue(RevisionLocalImplementationSites::fromVerified(
            tests::test_identity_detail::module(), parsed().source(), implementations(),
            zc::mv(implementationSiteInputs)));
  }

  const CanonicalParsedModule& parsed() const { return ZC_REQUIRE_NONNULL(parsedValue); }
  const NamedDefinitionInventory& definitions() const {
    return ZC_REQUIRE_NONNULL(definitionsValue);
  }
  const NamedImplementationInventory& implementations() const {
    return ZC_REQUIRE_NONNULL(implementationsValue);
  }
  const RevisionLocalDefinitionSites& definitionSites() const {
    return ZC_REQUIRE_NONNULL(definitionSitesValue);
  }
  const RevisionLocalImplementationSites& implementationSites() const {
    return ZC_REQUIRE_NONNULL(implementationSitesValue);
  }

  StableHeaderVerificationContext context() const {
    return StableHeaderVerificationContext{parsed(), definitions(), implementations(),
                                           definitionSites(), implementationSites()};
  }

  const NamedDefinitionInventoryEntry& definition(zc::StringPtr name) const {
    for (const auto& entry : definitions().entries()) {
      if (entry.record().name() == name) { return entry; }
    }
    ZC_FAIL_REQUIRE("missing stable definition");
  }

  const RevisionLocalDefinitionSite& definitionSite(const identity::DefinitionKey& key) const {
    for (const auto& site : definitionSites().entries()) {
      if (site.definition() == key) { return site; }
    }
    ZC_FAIL_REQUIRE("missing stable definition site");
  }

  const RevisionLocalImplementationSite& implementationSite(size_t ordinal) const {
    zc::Maybe<const RevisionLocalImplementationSite&> selected;
    for (const auto& candidate : implementationSites().entries()) {
      size_t rank = 0;
      for (const auto& other : implementationSites().entries()) {
        if (other.byteStart() < candidate.byteStart()) { ++rank; }
      }
      if (rank == ordinal) { selected = candidate; }
    }
    return ZC_REQUIRE_NONNULL(selected);
  }

  const NamedImplementationInventoryEntry& implementation(const identity::ImplKey& key) const {
    for (const auto& entry : implementations().entries()) {
      if (entry.key() == key) { return entry; }
    }
    ZC_FAIL_REQUIRE("missing stable implementation");
  }

  StableDefinitionHeader definitionHeader(zc::StringPtr name) const {
    const auto& entry = definition(name);
    auto query =
        StableDefinitionQueryKey::from(tests::test_identity_detail::module(), entry.key().clone());
    return requireStableHeaderValue(DefinitionHeaderProducer::produce(
        DefinitionHeaderInput{parsed(), query, entry, definitionSite(entry.key()),
                                              definitionSites(), implementationSites()}));
  }

  StableImplementationOccurrenceHeader implementationHeader(size_t ordinal) const {
    const auto& site = implementationSite(ordinal);
    const auto& entry = implementation(site.occurrence().implementation());
    auto query = requireStableHeaderValue(StableImplementationOccurrenceQueryKey::from(
        tests::test_identity_detail::module(), site.occurrence().clone()));
    return requireStableHeaderValue(ImplementationHeaderProducer::produce(
        ImplementationHeaderInput{
            parsed(), query, entry, site, definitionSites(), implementationSites()}));
  }

private:
  zc::Own<source::SourceManager> sources;
  source::BufferId buffer;
  basic::LangOptions options;
  basic::StringPool strings;
  ast::NodeId moduleNode;
  zc::Maybe<CanonicalParsedModule> parsedValue;
  zc::Maybe<NamedDefinitionInventory> definitionsValue;
  zc::Maybe<NamedImplementationInventory> implementationsValue;
  zc::Maybe<RevisionLocalDefinitionSites> definitionSitesValue;
  zc::Maybe<RevisionLocalImplementationSites> implementationSitesValue;
};

}  // namespace zomlang::compiler::binder::test
