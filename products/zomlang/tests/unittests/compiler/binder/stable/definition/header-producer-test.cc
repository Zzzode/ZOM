// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/definition/header-producer.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/stable/candidate/producer.h"
#include "zomlang/compiler/binder/stable/candidate/verifier.h"
#include "zomlang/compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/tests/unittests/compiler/binder/parsed-module-query-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

DefinitionBodyDisposition disposition(const ast::Tree& tree, ast::NodeId node) {
  const auto& syntax = tree.node(node);
  uint32_t bodyWord = UINT32_MAX;
  switch (syntax.kind) {
    case ast::SyntaxKind::FunctionDecl:
      bodyWord = ast::kFunctionDeclBodyWord;
      break;
    case ast::SyntaxKind::ConstructorDecl:
      bodyWord = ast::kConstructorDeclBodyWord;
      break;
    case ast::SyntaxKind::DestructorDecl:
      bodyWord = ast::kDestructorDeclBodyWord;
      break;
    case ast::SyntaxKind::MethodDecl:
      bodyWord = ast::kMethodDeclBodyWord;
      break;
    case ast::SyntaxKind::FieldDecl:
      bodyWord = ast::kFieldDeclInitWord;
      break;
    case ast::SyntaxKind::ClassConstDecl:
      bodyWord = ast::kClassConstDeclInitWord;
      break;
    default:
      return DefinitionBodyDisposition::NoExecutableBody;
  }
  return syntax.payload.words[bodyWord] == 0 ? DefinitionBodyDisposition::NoExecutableBody
                                             : DefinitionBodyDisposition::ExecutableBody;
}

identity::SourceFileKey sourceFile(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      tests::test_identity_detail::crate(),
      identity::SourceOriginKey::localFile(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

struct HeaderFixture final {
  HeaderFixture(zc::StringPtr text, identity::SourceFileKey&& source)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(text.asBytes(), "test.zom")) {
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
    if (syntax.modules().size() == 1) {
      moduleNode = syntax.modules()[0].node;
    } else {
      ZC_REQUIRE(syntax.modules().size() == 0);
      const auto& root = parsed().tree().node(parsed().tree().root());
      ZC_REQUIRE(root.kind == ast::SyntaxKind::SourceFile);
      const ast::NodeId declaration(root.payload.words[ast::kSourceFileModuleWord]);
      ZC_REQUIRE(parsed().tree().node(declaration).kind == ast::SyntaxKind::ModuleDeclaration);
      ZC_REQUIRE(
          static_cast<ast::ModuleDeclarationForm>(
              parsed().tree().node(declaration).payload.words[ast::kModuleDeclarationFormWord]) ==
          ast::ModuleDeclarationForm::Alias);
      moduleNode = ast::NodeId();
    }
    auto production =
        CandidateProducer::produce(parsed(), tests::test_identity_detail::module(), moduleNode);
    auto verified = CandidateVerifier::verify(parsed(), tests::test_identity_detail::module(),
                                              moduleNode, production);
    ZC_REQUIRE(verified.is<VerifiedStableIdentityCandidateInventory>());
    auto candidates = zc::mv(verified.get<VerifiedStableIdentityCandidateInventory>());

    zc::Vector<NamedDefinitionInventoryInput> definitionInputs;
    zc::Vector<RevisionLocalDefinitionSite> definitionSiteInputs;
    for (const auto& definition : candidates.definitions) {
      definitionInputs.add(NamedDefinitionInventoryInput{
          definition.authority.clone(), disposition(parsed().tree(), definition.node)});
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

  const NamedDefinitionInventoryEntry& entry(zc::StringPtr name) const {
    for (const auto& candidate : definitions().entries()) {
      if (candidate.record().name() == name) { return candidate; }
    }
    ZC_FAIL_REQUIRE("missing definition inventory entry");
  }

  const RevisionLocalDefinitionSite& firstSite(const identity::DefinitionKey& key) const {
    zc::Maybe<const RevisionLocalDefinitionSite&> result;
    for (const auto& candidate : definitionSites().entries()) {
      if (candidate.definition() != key) { continue; }
      if (result == zc::none || candidate.byteStart() < ZC_ASSERT_NONNULL(result).byteStart()) {
        result = candidate;
      }
    }
    return ZC_REQUIRE_NONNULL(result);
  }

  const RevisionLocalDefinitionSite& lastSite(const identity::DefinitionKey& key) const {
    zc::Maybe<const RevisionLocalDefinitionSite&> result;
    for (const auto& candidate : definitionSites().entries()) {
      if (candidate.definition() != key) { continue; }
      if (result == zc::none || candidate.byteStart() > ZC_ASSERT_NONNULL(result).byteStart()) {
        result = candidate;
      }
    }
    return ZC_REQUIRE_NONNULL(result);
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

StableDefinitionHeader produce(const HeaderFixture& fixture, zc::StringPtr name) {
  const auto& entry = fixture.entry(name);
  auto query =
      StableDefinitionQueryKey::from(tests::test_identity_detail::module(), entry.key().clone());
  return require(DefinitionHeaderProducer::produce(
      DefinitionHeaderInput{fixture.parsed(), query, entry, fixture.firstSite(entry.key()),
                            fixture.definitionSites(), fixture.implementationSites()}));
}

bool hasRole(const StableDefinitionHeader& header, ScopeRole role) {
  for (const auto candidate : header.declaredScopeRoles().values()) {
    if (candidate == role) { return true; }
  }
  return false;
}

bool hasOnlyDeclarationRole(const StableDefinitionHeader& header) {
  return header.declaredScopeRoles().values().size() == 1 &&
         header.declaredScopeRoles().values()[0] == ScopeRole::Declaration;
}

zc::Maybe<MemberVisibility> visibility(const HeaderFixture& fixture, zc::StringPtr name) {
  return produce(fixture, name).visibility();
}

identity::ModuleKey foreignModule() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("foreign"_zc));
  return require(identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
}

zc::Maybe<NamedDefinitionInventory> definitionsWithFlippedDisposition(
    const HeaderFixture& fixture, const identity::DefinitionKey& target) {
  auto verified = CandidateVerifier::reconstruct(
      fixture.parsed(), tests::test_identity_detail::module(), fixture.moduleNode);
  if (!verified.is<VerifiedStableIdentityCandidateInventory>()) { return zc::none; }
  zc::Vector<NamedDefinitionInventoryInput> inputs;
  for (const auto& definition :
       verified.get<VerifiedStableIdentityCandidateInventory>().definitions) {
    auto value = disposition(fixture.parsed().tree(), definition.node);
    if (definition.authority.key() == target) {
      value = value == DefinitionBodyDisposition::ExecutableBody
                  ? DefinitionBodyDisposition::NoExecutableBody
                  : DefinitionBodyDisposition::ExecutableBody;
    }
    inputs.add(NamedDefinitionInventoryInput{definition.authority.clone(), value});
  }
  return NamedDefinitionInventory::fromVerified(tests::test_identity_detail::module(),
                                                inputs.asPtr());
}

enum class SiteMutation : uint8_t { Key, Node, Range };

RevisionLocalDefinitionSites mutateSite(const HeaderFixture& fixture,
                                        const identity::DefinitionKey& target,
                                        SiteMutation mutation) {
  zc::Vector<RevisionLocalDefinitionSite> sites;
  for (const auto& site : fixture.definitionSites().entries()) {
    if (site.definition() != target) {
      sites.add(site.clone());
      continue;
    }
    auto node = site.node();
    auto key = site.site().clone();
    auto start = site.byteStart();
    if (mutation == SiteMutation::Key) {
      zc::Vector<uint32_t> path;
      path.addAll(site.site().moduleSyntaxPath());
      path.add(UINT32_MAX);
      key = require(IdentitySyntaxSiteKey::from(tests::test_identity_detail::module(),
                                                fixture.parsed().source().clone(), zc::mv(path)));
    } else if (mutation == SiteMutation::Node) {
      const auto& syntax = fixture.parsed().tree().node(node);
      ZC_REQUIRE(syntax.kind == ast::SyntaxKind::MethodDecl);
      node = ast::NodeId(syntax.payload.words[ast::kMethodDeclBodyWord]);
    } else {
      ++start;
    }
    sites.add(require(RevisionLocalDefinitionSite::from(node, site.definition().clone(),
                                                        zc::mv(key), start, site.byteEnd())));
  }
  return require(RevisionLocalDefinitionSites::fromVerified(tests::test_identity_detail::module(),
                                                            fixture.parsed().source(),
                                                            fixture.definitions(), zc::mv(sites)));
}

const RevisionLocalDefinitionSite& matchingSite(const RevisionLocalDefinitionSites& sites,
                                                const identity::DefinitionKey& key) {
  for (const auto& site : sites.entries()) {
    if (site.definition() == key) { return site; }
  }
  ZC_FAIL_REQUIRE("missing mutated definition site");
}

zc::StringPtr matrixSource() {
  return R"zom(module test;
extern "C" {
    fun foreignCall(first: i32, second: i32) -> i32;
    variable errno: i32;
}
enum Choice<T, E> { None, Some(i32), }
struct Record {}
interface Contract {
    type Item;
    type Iter<T>;
    fun required() -> unit;
}
error Problem { Failed: "failed", }
alias Pair = (i32, i32);
class Access {
    let defaultField: i32;
    let initializedField: i32 = 1;
    const VALUE: i32 = 2;
    private let privateField: i32;
    protected let protectedField: i32;
    init() {}
    deinit() {}
    public fun visible() {}
    fun receive(this, value: i32) {}
    fun two(first: i32, second: i32) {}
}
fun noParameters() {}
fun whereOnly() where i32: Eq {}
const MODULE_CONST = 1;
let moduleStatic = 2;
impl Contract for Record {}
)zom"_zc;
}

}  // namespace

ZC_TEST("DefinitionHeaderProducer covers the complete header semantic matrix") {
  HeaderFixture fixture(matrixSource(), tests::test_identity_detail::source());
  ZC_REQUIRE(fixture.implementationSites().entries().size() == 1);

  auto alias = produce(fixture, "Pair"_zc);
  ZC_EXPECT(alias.kind() == identity::DefinitionKind::TypeAlias);
  ZC_EXPECT(hasRole(alias, ScopeRole::Declaration));
  ZC_EXPECT(!hasRole(alias, ScopeRole::Generic));
  ZC_EXPECT(!hasRole(alias, ScopeRole::Parameters));
  ZC_EXPECT(!hasRole(alias, ScopeRole::Members));
  ZC_EXPECT(alias.genericParameters().values().size() == 0);

  auto genericAggregate = produce(fixture, "Choice"_zc);
  ZC_EXPECT(genericAggregate.genericParameters().values().size() == 2);
  size_t genericZero = 0;
  size_t genericOne = 0;
  for (const auto& parameter : genericAggregate.genericParameters().values()) {
    genericZero += parameter.ordinal() == 0 ? 1 : 0;
    genericOne += parameter.ordinal() == 1 ? 1 : 0;
  }
  ZC_EXPECT(genericZero == 1);
  ZC_EXPECT(genericOne == 1);

  auto foreign = produce(fixture, "foreignCall"_zc);
  ZC_EXPECT(foreign.kind() == identity::DefinitionKind::Function);
  ZC_EXPECT(hasRole(foreign, ScopeRole::Declaration));
  ZC_EXPECT(hasRole(foreign, ScopeRole::Parameters));
  ZC_EXPECT(!hasRole(foreign, ScopeRole::Generic));
  ZC_EXPECT(foreign.callableParameters().values().size() == 2);
  size_t ordinaryZero = 0;
  size_t ordinaryOne = 0;
  for (const auto& parameter : foreign.callableParameters().values()) {
    ZC_REQUIRE(parameter.position().kind() == identity::CallableParameterPositionKind::Ordinary);
    ZC_REQUIRE(parameter.position().ordinal() != zc::none);
    ordinaryZero += ZC_ASSERT_NONNULL(parameter.position().ordinal()) == 0 ? 1 : 0;
    ordinaryOne += ZC_ASSERT_NONNULL(parameter.position().ordinal()) == 1 ? 1 : 0;
  }
  ZC_EXPECT(ordinaryZero == 1);
  ZC_EXPECT(ordinaryOne == 1);

  auto empty = produce(fixture, "noParameters"_zc);
  ZC_EXPECT(empty.genericParameters().values().size() == 0);
  ZC_EXPECT(empty.callableParameters().values().size() == 0);
  ZC_EXPECT(!hasRole(empty, ScopeRole::Generic));
  ZC_EXPECT(hasRole(empty, ScopeRole::Parameters));

  auto whereOnly = produce(fixture, "whereOnly"_zc);
  ZC_EXPECT(hasRole(whereOnly, ScopeRole::Generic));
  ZC_EXPECT(hasRole(whereOnly, ScopeRole::Parameters));
  ZC_EXPECT(whereOnly.genericParameters().values().size() == 0);

  for (const auto name : {"Choice"_zc, "Record"_zc, "Contract"_zc, "Problem"_zc}) {
    auto aggregate = produce(fixture, name);
    ZC_EXPECT(hasRole(aggregate, ScopeRole::Declaration));
    ZC_EXPECT(hasRole(aggregate, ScopeRole::Members));
    ZC_EXPECT(!hasRole(aggregate, ScopeRole::Parameters));
  }
  auto associated = produce(fixture, "Item"_zc);
  ZC_EXPECT(associated.kind() == identity::DefinitionKind::AssociatedType);
  ZC_EXPECT(hasRole(associated, ScopeRole::Declaration));
  ZC_EXPECT(!hasRole(associated, ScopeRole::Generic));
  ZC_EXPECT(!hasRole(associated, ScopeRole::Members));
  auto genericAssociated = produce(fixture, "Iter"_zc);
  ZC_EXPECT(hasRole(genericAssociated, ScopeRole::Generic));
  ZC_EXPECT(genericAssociated.genericParameters().values().size() == 1);

  ZC_REQUIRE(visibility(fixture, "required"_zc) != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(visibility(fixture, "required"_zc)) == MemberVisibility::Public);
  ZC_REQUIRE(visibility(fixture, "defaultField"_zc) != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(visibility(fixture, "defaultField"_zc)) == MemberVisibility::Private);
  ZC_EXPECT(ZC_ASSERT_NONNULL(visibility(fixture, "privateField"_zc)) == MemberVisibility::Private);
  ZC_EXPECT(ZC_ASSERT_NONNULL(visibility(fixture, "protectedField"_zc)) ==
            MemberVisibility::Protected);
  ZC_EXPECT(ZC_ASSERT_NONNULL(visibility(fixture, "visible"_zc)) == MemberVisibility::Public);

  auto receiver = produce(fixture, "receive"_zc);
  ZC_EXPECT(receiver.callableParameters().values().size() == 2);
  size_t receivers = 0;
  size_t ordinary = 0;
  for (const auto& parameter : receiver.callableParameters().values()) {
    if (parameter.position().kind() == identity::CallableParameterPositionKind::Receiver) {
      ++receivers;
      ZC_EXPECT(parameter.name() == zc::none);
    } else {
      ++ordinary;
      ZC_REQUIRE(parameter.position().ordinal() != zc::none);
      ZC_EXPECT(ZC_ASSERT_NONNULL(parameter.position().ordinal()) == 0);
      ZC_REQUIRE(parameter.name() != zc::none);
      ZC_EXPECT(ZC_ASSERT_NONNULL(parameter.name()).text() == "value"_zc);
    }
  }
  ZC_EXPECT(receivers == 1);
  ZC_EXPECT(ordinary == 1);

  ZC_EXPECT(produce(fixture, "noParameters"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "two"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "required"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::NoExecutableBody);
  ZC_EXPECT(produce(fixture, "init"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "deinit"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "defaultField"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::NoExecutableBody);
  ZC_EXPECT(produce(fixture, "initializedField"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "VALUE"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::ExecutableBody);
  ZC_EXPECT(produce(fixture, "foreignCall"_zc).bodyDisposition() ==
            DefinitionBodyDisposition::NoExecutableBody);

  for (const auto name : {"errno"_zc, "None"_zc, "Some"_zc, "defaultField"_zc, "VALUE"_zc,
                          "MODULE_CONST"_zc, "moduleStatic"_zc}) {
    ZC_EXPECT(hasOnlyDeclarationRole(produce(fixture, name)));
  }
}

ZC_TEST("DefinitionHeaderProducer maps module aliases to the import surface") {
  HeaderFixture fixture("module geometry = math::geometry;\n"_zc,
                        tests::test_identity_detail::source());
  auto header = produce(fixture, "geometry"_zc);
  ZC_EXPECT(header.kind() == identity::DefinitionKind::ModuleAlias);
  ZC_EXPECT(header.activation() == DefinitionActivation::ImportSurface);
  ZC_EXPECT(hasRole(header, ScopeRole::Declaration));
  ZC_EXPECT(!hasRole(header, ScopeRole::Parameters));
  ZC_EXPECT(!hasRole(header, ScopeRole::Members));
}

ZC_TEST("DefinitionHeaderProducer selects the earliest equal source occurrence") {
  HeaderFixture fixture("module test;\nclass Duplicate {}\nclass Duplicate {}\n"_zc,
                        tests::test_identity_detail::source());
  const auto& entry = fixture.entry("Duplicate"_zc);
  ZC_REQUIRE(fixture.definitionSites().entries().size() == 2);
  auto query =
      StableDefinitionQueryKey::from(tests::test_identity_detail::module(), entry.key().clone());
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, entry, fixture.firstSite(entry.key()),
                fixture.definitionSites(), fixture.implementationSites()}) != zc::none);
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, entry, fixture.lastSite(entry.key()),
                fixture.definitionSites(), fixture.implementationSites()}) == zc::none);
}

ZC_TEST("DefinitionHeaderProducer rejects one-field cross-input mutations") {
  HeaderFixture fixture(matrixSource(), tests::test_identity_detail::source());
  const auto& entry = fixture.entry("two"_zc);
  const auto& site = fixture.firstSite(entry.key());
  auto query =
      StableDefinitionQueryKey::from(tests::test_identity_detail::module(), entry.key().clone());

  auto wrongKey = StableDefinitionQueryKey::from(tests::test_identity_detail::module(),
                                                 fixture.entry("noParameters"_zc).key().clone());
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), wrongKey, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  auto wrongModule = StableDefinitionQueryKey::from(foreignModule(), entry.key().clone());
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), wrongModule, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  HeaderFixture otherSource(matrixSource(), sourceFile("other.zom"_zc));
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                otherSource.parsed(), query, entry, site, fixture.definitionSites(),
                fixture.implementationSites()}) == zc::none);

  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, fixture.entry("noParameters"_zc), site,
                fixture.definitionSites(), fixture.implementationSites()}) == zc::none);

  auto wrongDisposition = definitionsWithFlippedDisposition(fixture, entry.key());
  ZC_REQUIRE(wrongDisposition != zc::none);
  zc::Maybe<const NamedDefinitionInventoryEntry&> wrongDispositionEntry;
  for (const auto& candidate : ZC_ASSERT_NONNULL(wrongDisposition).entries()) {
    if (candidate.key() == entry.key()) { wrongDispositionEntry = candidate; }
  }
  ZC_REQUIRE(wrongDispositionEntry != zc::none);
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, ZC_ASSERT_NONNULL(wrongDispositionEntry), site,
                fixture.definitionSites(), fixture.implementationSites()}) == zc::none);

  auto emptyImplementations = require(NamedImplementationInventory::fromVerified(
      tests::test_identity_detail::module(),
      zc::ArrayPtr<const identity::ImplIdentityAuthority>()));
  zc::Vector<RevisionLocalImplementationSite> emptyImplementationSitesInput;
  auto emptyImplementationSites = require(RevisionLocalImplementationSites::fromVerified(
      tests::test_identity_detail::module(), fixture.parsed().source(), emptyImplementations,
      zc::mv(emptyImplementationSitesInput)));
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, entry, site, fixture.definitionSites(),
                emptyImplementationSites}) == zc::none);

  zc::Vector<RevisionLocalImplementationSite> wrongImplementationInputs;
  for (const auto& implementation : fixture.implementationSites().entries()) {
    wrongImplementationInputs.add(require(RevisionLocalImplementationSite::from(
        site.node(), implementation.occurrence().clone(), implementation.byteStart(),
        implementation.byteEnd())));
  }
  auto wrongImplementationSites = require(RevisionLocalImplementationSites::fromVerified(
      tests::test_identity_detail::module(), fixture.parsed().source(), fixture.implementations(),
      zc::mv(wrongImplementationInputs)));
  ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                fixture.parsed(), query, entry, site, fixture.definitionSites(),
                wrongImplementationSites}) == zc::none);

  for (const auto mutation : {SiteMutation::Key, SiteMutation::Node, SiteMutation::Range}) {
    auto mutated = mutateSite(fixture, entry.key(), mutation);
    ZC_EXPECT(DefinitionHeaderProducer::produce(DefinitionHeaderInput{
                  fixture.parsed(), query, entry, matchingSite(mutated, entry.key()),
                  fixture.definitionSites(), fixture.implementationSites()}) == zc::none);
  }
}

}  // namespace zomlang::compiler::binder
