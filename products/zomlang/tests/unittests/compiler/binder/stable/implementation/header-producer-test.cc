// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/implementation/header-producer.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/binder/stable/candidate/producer.h"
#include "zomlang/compiler/binder/stable/candidate/verifier.h"
#include "zomlang/compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/tests/unittests/compiler/binder/graph/parsed-module-query-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::SourceFileKey sourceFile(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      tests::test_identity_detail::crate(),
      identity::SourceOriginKey::localFile(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

identity::ModuleKey foreignModule() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("foreign"_zc));
  return require(identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
}

zc::StringPtr implementationSource() {
  return R"zom(module test;
interface Trait {}
class Box<T> {}
struct Item {}
impl<T, U> Trait for Box<T> {}
impl Trait for Item where Item: Trait {}
impl Trait for i64 {}
unsafe impl ShortMarker for i32;
impl !Shared for Item;
)zom"_zc;
}

struct HeaderFixture final {
  HeaderFixture(identity::SourceFileKey&& source)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(implementationSource().asBytes(), "test.zom")) {
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
    auto parsedSource = test::canonicalParsedSource(ZC_ASSERT_NONNULL(snapshot), *sources, buffer,
                                                    zc::mv(ZC_ASSERT_NONNULL(tokens)),
                                                    zc::mv(ZC_ASSERT_NONNULL(tree)));
    parsedValue =
        require(CanonicalParsedModule::fromQueryResult(zc::mv(ZC_ASSERT_NONNULL(parsedSource))));

    const auto syntax = DefinitionInventory::collect(parsed().tree());
    ZC_REQUIRE(syntax.modules().size() == 1);
    moduleNode = syntax.modules()[0].node;
    auto production =
        CandidateProducer::produce(parsed(), tests::test_identity_detail::module(), moduleNode);
    auto verified = CandidateVerifier::verify(parsed(), tests::test_identity_detail::module(),
                                              moduleNode, production);
    ZC_REQUIRE(verified.is<VerifiedStableIdentityCandidateInventory>());
    auto candidates = zc::mv(verified.get<VerifiedStableIdentityCandidateInventory>());
    ZC_REQUIRE(candidates.implementations.size() == 5);

    zc::Vector<NamedDefinitionInventoryInput> definitionInputs;
    zc::Vector<RevisionLocalDefinitionSite> definitionSiteInputs;
    for (const auto& definition : candidates.definitions) {
      definitionInputs.add(NamedDefinitionInventoryInput{
          definition.authority.clone(), DefinitionBodyDisposition::NoExecutableBody});
      definitionSiteInputs.add(require(RevisionLocalDefinitionSite::from(
          definition.node, definition.authority.key().clone(), definition.site.clone(),
          definition.source.byteStart(), definition.source.byteEnd())));
    }
    definitionsValue = require(NamedDefinitionInventory::fromVerified(
        tests::test_identity_detail::module(), definitionInputs.asPtr()));
    definitionSitesValue = require(RevisionLocalDefinitionSites::fromVerified(
        tests::test_identity_detail::module(), parsed().source(), definitions(),
        zc::mv(definitionSiteInputs)));

    zc::Vector<identity::ImplIdentityAuthority> implementationInputs;
    zc::Vector<RevisionLocalImplementationSite> implementationSiteInputs;
    for (const auto& implementation : candidates.implementations) {
      implementationInputs.add(implementation.authority.clone());
      auto occurrence = ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                                      implementation.site.clone());
      implementationSiteInputs.add(require(RevisionLocalImplementationSite::from(
          implementation.node, zc::mv(occurrence), implementation.source.byteStart(),
          implementation.source.byteEnd())));
    }
    implementationsValue = require(NamedImplementationInventory::fromVerified(
        tests::test_identity_detail::module(), implementationInputs.asPtr()));
    implementationSitesValue = require(RevisionLocalImplementationSites::fromVerified(
        tests::test_identity_detail::module(), parsed().source(), implementations(),
        zc::mv(implementationSiteInputs)));
  }

  const CanonicalParsedModule& parsed() const { return ZC_REQUIRE_NONNULL(parsedValue); }
  const NamedDefinitionInventory& definitions() const {
    return ZC_REQUIRE_NONNULL(definitionsValue);
  }
  const RevisionLocalDefinitionSites& definitionSites() const {
    return ZC_REQUIRE_NONNULL(definitionSitesValue);
  }
  const NamedImplementationInventory& implementations() const {
    return ZC_REQUIRE_NONNULL(implementationsValue);
  }
  const RevisionLocalImplementationSites& implementationSites() const {
    return ZC_REQUIRE_NONNULL(implementationSitesValue);
  }

  const RevisionLocalImplementationSite& occurrence(size_t sourceOrdinal) const {
    zc::Maybe<const RevisionLocalImplementationSite&> selected;
    size_t rank = 0;
    for (const auto& candidate : implementationSites().entries()) {
      rank = 0;
      for (const auto& other : implementationSites().entries()) {
        if (other.byteStart() < candidate.byteStart()) { ++rank; }
      }
      if (rank == sourceOrdinal) { selected = candidate; }
    }
    return ZC_REQUIRE_NONNULL(selected);
  }

  const NamedImplementationInventoryEntry& entry(const identity::ImplKey& key) const {
    for (const auto& candidate : implementations().entries()) {
      if (candidate.key() == key) { return candidate; }
    }
    ZC_FAIL_REQUIRE("missing implementation inventory entry");
  }

  zc::Own<source::SourceManager> sources;
  source::BufferId buffer;
  basic::LangOptions options;
  basic::StringPool strings;
  ast::NodeId moduleNode;
  zc::Maybe<CanonicalParsedModule> parsedValue;
  zc::Maybe<NamedDefinitionInventory> definitionsValue;
  zc::Maybe<RevisionLocalDefinitionSites> definitionSitesValue;
  zc::Maybe<NamedImplementationInventory> implementationsValue;
  zc::Maybe<RevisionLocalImplementationSites> implementationSitesValue;
};

StableImplementationOccurrenceHeader produce(const HeaderFixture& fixture, size_t ordinal) {
  const auto& site = fixture.occurrence(ordinal);
  const auto& entry = fixture.entry(site.occurrence().implementation());
  auto query = require(StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), site.occurrence().clone()));
  return require(ImplementationHeaderProducer::produce(
      ImplementationHeaderInput{fixture.parsed(), query, entry, site, fixture.definitionSites(),
                                fixture.implementationSites()}));
}

bool hasRole(const StableImplementationOccurrenceHeader& header, ScopeRole role) {
  for (const auto candidate : header.declaredScopeRoles().values()) {
    if (candidate == role) { return true; }
  }
  return false;
}

enum class SiteMutation : uint8_t { Key, Node, Range };

RevisionLocalImplementationSites mutateOccurrence(const HeaderFixture& fixture,
                                                  const RevisionLocalImplementationSite& target,
                                                  SiteMutation mutation) {
  zc::Vector<RevisionLocalImplementationSite> sites;
  for (const auto& site : fixture.implementationSites().entries()) {
    if (site.node() != target.node()) {
      sites.add(site.clone());
      continue;
    }
    auto node = site.node();
    auto occurrence = site.occurrence().clone();
    auto start = site.byteStart();
    if (mutation == SiteMutation::Key) {
      zc::Vector<uint32_t> path;
      path.addAll(site.occurrence().site().moduleSyntaxPath());
      path.add(UINT32_MAX);
      auto key = require(IdentitySyntaxSiteKey::from(
          tests::test_identity_detail::module(), fixture.parsed().source().clone(), zc::mv(path)));
      occurrence =
          ImplSourceOccurrenceKey::from(site.occurrence().implementation().clone(), zc::mv(key));
    } else if (mutation == SiteMutation::Node) {
      node = fixture.definitionSites().entries()[0].node();
    } else {
      ++start;
    }
    sites.add(require(
        RevisionLocalImplementationSite::from(node, zc::mv(occurrence), start, site.byteEnd())));
  }
  return require(RevisionLocalImplementationSites::fromVerified(
      tests::test_identity_detail::module(), fixture.parsed().source(), fixture.implementations(),
      zc::mv(sites)));
}

const RevisionLocalImplementationSite& matchingOccurrence(
    const RevisionLocalImplementationSites& sites, const identity::ImplKey& key) {
  for (const auto& site : sites.entries()) {
    if (site.occurrence().implementation() == key) { return site; }
  }
  ZC_FAIL_REQUIRE("missing mutated implementation occurrence");
}

}  // namespace

ZC_TEST("ImplementationHeaderProducer covers source forms scopes and generics") {
  HeaderFixture fixture(tests::test_identity_detail::source());

  auto generic = produce(fixture, 0);
  ZC_EXPECT(generic.sourceForm() == ImplementationSourceForm::Ordinary);
  ZC_EXPECT(hasRole(generic, ScopeRole::Implementation));
  ZC_EXPECT(hasRole(generic, ScopeRole::Generic));
  ZC_EXPECT(generic.declaredScopeRoles().values().size() == 2);
  ZC_EXPECT(generic.genericParameters().values().size() == 2);
  size_t ordinalZero = 0;
  size_t ordinalOne = 0;
  for (const auto& parameter : generic.genericParameters().values()) {
    ordinalZero += parameter.ordinal() == 0 ? 1 : 0;
    ordinalOne += parameter.ordinal() == 1 ? 1 : 0;
    ZC_EXPECT(parameter.site().value().is<ImplementationOccurrenceSite>());
    ZC_EXPECT(parameter.record().owner().kind() ==
              identity::StableGenericParameterOwnerKind::Implementation);
    ZC_EXPECT(ZC_ASSERT_NONNULL(parameter.record().owner().implKey()) ==
              generic.authority().implementation());
    ZC_EXPECT(parameter.key() == identity::GenericParameterKey::compute(parameter.record()));
    if (parameter.ordinal() == 0) {
      ZC_EXPECT(parameter.name().text() == "T"_zc);
    } else if (parameter.ordinal() == 1) {
      ZC_EXPECT(parameter.name().text() == "U"_zc);
    }
  }
  ZC_EXPECT(ordinalZero == 1);
  ZC_EXPECT(ordinalOne == 1);

  auto whereOnly = produce(fixture, 1);
  ZC_EXPECT(whereOnly.sourceForm() == ImplementationSourceForm::Ordinary);
  ZC_EXPECT(hasRole(whereOnly, ScopeRole::Implementation));
  ZC_EXPECT(hasRole(whereOnly, ScopeRole::Generic));
  ZC_EXPECT(whereOnly.genericParameters().values().size() == 0);

  auto ordinary = produce(fixture, 2);
  ZC_EXPECT(ordinary.sourceForm() == ImplementationSourceForm::Ordinary);
  ZC_EXPECT(ordinary.declaredScopeRoles().values().size() == 1);
  ZC_EXPECT(hasRole(ordinary, ScopeRole::Implementation));
  ZC_EXPECT(!hasRole(ordinary, ScopeRole::Generic));
  ZC_EXPECT(ordinary.genericParameters().values().size() == 0);

  for (const size_t ordinal : {size_t{3}, size_t{4}}) {
    auto marker = produce(fixture, ordinal);
    ZC_EXPECT(marker.sourceForm() == ImplementationSourceForm::BodylessMarker);
    ZC_EXPECT(marker.declaredScopeRoles().values().size() == 1);
    ZC_EXPECT(hasRole(marker, ScopeRole::Implementation));
    ZC_EXPECT(!hasRole(marker, ScopeRole::Generic));
    ZC_EXPECT(marker.genericParameters().values().size() == 0);
  }
}

ZC_TEST("ImplementationHeaderProducer rejects one-field input mutations") {
  HeaderFixture fixture(tests::test_identity_detail::source());
  const auto& site = fixture.occurrence(0);
  const auto& entry = fixture.entry(site.occurrence().implementation());
  auto query = require(StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), site.occurrence().clone()));

  const auto& otherSite = fixture.occurrence(2);
  const auto& otherEntry = fixture.entry(otherSite.occurrence().implementation());
  auto wrongOccurrence = require(StableImplementationOccurrenceQueryKey::from(
      tests::test_identity_detail::module(), otherSite.occurrence().clone()));
  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                fixture.parsed(), wrongOccurrence, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  auto module = foreignModule();
  zc::Vector<uint32_t> syntaxPath;
  syntaxPath.addAll(site.occurrence().site().moduleSyntaxPath());
  auto foreignSite = require(IdentitySyntaxSiteKey::from(
      module.clone(), fixture.parsed().source().clone(), zc::mv(syntaxPath)));
  auto foreignOccurrence = ImplSourceOccurrenceKey::from(entry.key().clone(), zc::mv(foreignSite));
  auto wrongModule = require(
      StableImplementationOccurrenceQueryKey::from(zc::mv(module), zc::mv(foreignOccurrence)));
  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                fixture.parsed(), wrongModule, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  HeaderFixture otherSource(sourceFile("other.zom"_zc));
  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                otherSource.parsed(), query, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                fixture.parsed(), query, otherEntry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);
  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                fixture.parsed(), query, entry, otherSite, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  zc::Vector<RevisionLocalDefinitionSite> emptyDefinitionsInput;
  auto emptyDefinitions = require(NamedDefinitionInventory::fromVerified(
      tests::test_identity_detail::module(), zc::ArrayPtr<const NamedDefinitionInventoryInput>()));
  auto emptyDefinitionSites = require(RevisionLocalDefinitionSites::fromVerified(
      tests::test_identity_detail::module(), fixture.parsed().source(), emptyDefinitions,
      zc::mv(emptyDefinitionsInput)));
  ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                fixture.parsed(), query, entry, site, emptyDefinitionSites,
                fixture.implementationSites()}) == zc::none);

  zc::Vector<RevisionLocalImplementationSite> incompleteInput;
  incompleteInput.add(site.clone());
  auto incompleteSites = require(RevisionLocalImplementationSites::fromVerified(
      tests::test_identity_detail::module(), fixture.parsed().source(), fixture.implementations(),
      zc::mv(incompleteInput)));
  ZC_EXPECT(ImplementationHeaderProducer::produce(
                ImplementationHeaderInput{fixture.parsed(), query, entry, site,
                                          fixture.definitionSites(), incompleteSites}) == zc::none);

  for (const auto mutation : {SiteMutation::Key, SiteMutation::Node, SiteMutation::Range}) {
    auto mutated = mutateOccurrence(fixture, site, mutation);
    ZC_EXPECT(ImplementationHeaderProducer::produce(ImplementationHeaderInput{
                  fixture.parsed(), query, entry, matchingOccurrence(mutated, entry.key()),
                  fixture.definitionSites(), fixture.implementationSites()}) == zc::none);
  }
}

}  // namespace zomlang::compiler::binder
