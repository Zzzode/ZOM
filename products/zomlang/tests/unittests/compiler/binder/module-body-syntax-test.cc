// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/module-body-syntax.h"

#include "parsed-module-query-test-fixture.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/owner-body-query.h"
#include "zomlang/compiler/binder/owner-body-syntax.h"
#include "zomlang/compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body test scalar");
}

identity::ResolvedVersion version() {
  auto value = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body test version");
}

identity::SortedFeatureSet features() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body feature set");
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body target features");
}

identity::PackageKey packageKey() {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>("module_body"_zc), version(), features());
}

identity::CanonicalTargetSpecificationKey target() {
  auto value = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("aarch64"_zc),
      scalar<identity::TargetComponentName>("apple"_zc),
      scalar<identity::TargetComponentName>("darwin"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body target");
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto value = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body compilation");
}

identity::CrateKey crateKey() {
  auto value =
      identity::CrateKey::from(identity::CompilationUnitIdentity::userPackage(packageKey()),
                               identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("module_body"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body crate");
}

identity::SourceFileKey sourceKey() {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("module-body.zom"_zc));
  return identity::SourceFileKey::from(
      crateKey(), identity::SourceOriginKey::localFile(
                      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::ModuleKey moduleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  auto value = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body module");
}

identity::ModuleKey otherModuleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("other"_zc));
  auto value = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid alternate module-body module");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(context, factory.issue()) { return context; }
  ZC_FAIL_REQUIRE("module-body semantic context exhausted");
}

identity::DefinitionKey definitionKey(uint8_t discriminator) {
  uint8_t bytes[32] = {};
  bytes[31] = discriminator;
  auto value = identity::DefinitionKey::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module-body definition key");
}

struct ModuleBodyFixture final {
  explicit ModuleBodyFixture(zc::StringPtr sourceText)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(sourceText.asBytes(), "module-body.zom")),
        context(requireContext(factory)) {
    diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    auto parsedTree = parser.parse();
    auto tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(parsedTree != zc::none);
    ZC_REQUIRE(tokens != zc::none);
    ZC_REQUIRE(!diagnosticFacts.hasErrors());

    auto snapshot = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshot != zc::none);

    const auto inventory = DefinitionInventory::collect(ZC_ASSERT_NONNULL(parsedTree));
    moduleNode = inventory.modules().size() == 0 ? ZC_ASSERT_NONNULL(parsedTree).root()
                                                 : inventory.modules()[0].node;
    uint8_t definitionDiscriminator = 1;
    for (const auto& definition : inventory.definitions()) {
      if (!definition.site.value().is<DeclarationDefinitionSite>()) { continue; }
      definitions.add(ModuleBodyDefinitionBoundaryInput{definition.node,
                                                        definitionKey(definitionDiscriminator++)});
      definitionNodes.add(definition.node);
    }
    parsed = test::requireVerifiedParsedSource(context, ZC_ASSERT_NONNULL(snapshot), *sources,
                                               buffer, zc::mv(ZC_ASSERT_NONNULL(tokens)),
                                               zc::mv(ZC_ASSERT_NONNULL(parsedTree)));
  }

  ModuleBodySyntaxProjection project() const {
    auto result = ModuleBodySyntaxProducer::produce(ZC_ASSERT_NONNULL(parsed).syntax(), moduleKey(),
                                                    moduleNode, definitions.asPtr());
    ZC_REQUIRE(result.is<ModuleBodySyntaxProjection>());
    return zc::mv(result.get<ModuleBodySyntaxProjection>());
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  ast::NodeId moduleNode;
  zc::Vector<ModuleBodyDefinitionBoundaryInput> definitions;
  zc::Vector<ast::NodeId> definitionNodes;
  zc::Maybe<VerifiedParsedModule> parsed;
};

constexpr zc::StringPtr kBodyItems = "let value = 1;\nclass Outer {}\nimpl Trait for i32 {}\n"_zc;
constexpr zc::StringPtr kDefinitionBodyItems = "let value = 1;\nclass Outer {}\n"_zc;

size_t countKind(const ModuleBodySyntax& syntax, DetachedModuleBodyNodeKind kind) {
  size_t count = 0;
  for (const auto& node : syntax.nodes()) {
    if (node.kind() == kind) { ++count; }
  }
  return count;
}

bool containsProvenanceNode(const ModuleBodyProvenance& provenance, ast::NodeId node) {
  for (const auto& entry : provenance.entries()) {
    if (entry.node == node) { return true; }
  }
  return false;
}

}  // namespace

ZC_TEST("Module body syntax canonicalizes implicit, declared, and inline roots equally") {
  ModuleBodyFixture implicit(kDefinitionBodyItems);
  ModuleBodyFixture declared(zc::str("module root;\n", kDefinitionBodyItems));
  ModuleBodyFixture inlineRoot(zc::str("module root {\n", kDefinitionBodyItems, "}\n"));
  auto implicitProjection = implicit.project();
  auto declaredProjection = declared.project();
  auto inlineProjection = inlineRoot.project();
  ZC_EXPECT(implicitProjection.syntax == declaredProjection.syntax);
  ZC_EXPECT(declaredProjection.syntax == inlineProjection.syntax);
}

ZC_TEST("Module body syntax retains implementation headers and prunes definitions") {
  ModuleBodyFixture fixture(zc::str("module root;\n", kBodyItems));
  auto projection = fixture.project();
  ZC_EXPECT(projection.syntax.rootCount() == 3);
  ZC_EXPECT(countKind(projection.syntax, DetachedModuleBodyNodeKind::DefinitionBoundary) == 1);
  for (const auto node : fixture.definitionNodes) {
    ZC_EXPECT(!containsProvenanceNode(projection.provenance, node));
  }
  ZC_EXPECT(ModuleBodySyntaxVerifier::verify(
                ZC_ASSERT_NONNULL(fixture.parsed).syntax(), moduleKey(), fixture.moduleNode,
                fixture.definitions.asPtr(), projection) == ModuleBodySyntaxFailureKind::None);
}

ZC_TEST("Module body syntax backdates across range-only source edits") {
  ModuleBodyFixture first(zc::str("module root;\n", kBodyItems));
  ModuleBodyFixture shifted(zc::str("module root;\n\n\n", kBodyItems));
  auto firstProjection = first.project();
  auto shiftedProjection = shifted.project();
  ZC_EXPECT(firstProjection.syntax == shiftedProjection.syntax);
  ZC_EXPECT(firstProjection.provenance != shiftedProjection.provenance);
}

ZC_TEST("Module body syntax maps schema child fields to detached child ordinals") {
  ModuleBodyFixture fixture("let value = 1;\n"_zc);
  auto projection = fixture.project();
  zc::Maybe<const DetachedModuleBodyNode&> declarator;
  zc::Maybe<const DetachedModuleBodyNode&> declaratorList;
  zc::Maybe<const DetachedModuleBodyNode&> patternNode;
  for (const auto& node : projection.syntax.nodes()) {
    if (node.syntaxKind() == ast::SyntaxKind::VariableDeclarator) {
      ZC_REQUIRE(declarator == zc::none);
      declarator = node;
    }
    if (node.syntaxKind() == ast::SyntaxKind::VariableDeclaratorList) {
      ZC_REQUIRE(declaratorList == zc::none);
      declaratorList = node;
    }
    if (node.syntaxKind() == ast::SyntaxKind::BindingPattern ||
        node.syntaxKind() == ast::SyntaxKind::IdentifierPattern) {
      ZC_REQUIRE(patternNode == zc::none);
      patternNode = node;
    }
  }
  ZC_REQUIRE(declarator != zc::none);
  ZC_REQUIRE(declaratorList != zc::none);
  ZC_REQUIRE(patternNode != zc::none);

  auto pattern = ZC_ASSERT_NONNULL(declarator).childField(0);
  auto type = ZC_ASSERT_NONNULL(declarator).childField(1);
  auto initializer = ZC_ASSERT_NONNULL(declarator).childField(2);
  auto declarations = ZC_ASSERT_NONNULL(declaratorList).childField(1);
  ZC_REQUIRE(pattern != zc::none);
  ZC_REQUIRE(type != zc::none);
  ZC_REQUIRE(initializer != zc::none);
  ZC_REQUIRE(declarations != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(pattern).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(pattern).firstChildOrdinal == 0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(pattern).childCount == 1);
  ZC_EXPECT(!ZC_ASSERT_NONNULL(type).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(type).firstChildOrdinal == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(type).childCount == 0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(initializer).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(initializer).firstChildOrdinal == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(initializer).childCount == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(declarations).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(declarations).firstChildOrdinal == 0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(declarations).childCount == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(declarator).childField(3) == zc::none);
  auto name = ZC_ASSERT_NONNULL(patternNode).identifierField(0);
  ZC_REQUIRE(name != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(name).text() == "value"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(patternNode).identifierField(1) == zc::none);

  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto rootScope = StableScopeOwnerKey::module(module.clone());
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax, rootScope);
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto bindingsResult =
      OwnerBodyBindingProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(bindingsResult != zc::none);
  auto bindings = zc::mv(ZC_ASSERT_NONNULL(bindingsResult));
  ZC_REQUIRE(bindings.bindings().values().size() == 1);
  ZC_EXPECT(bindings.bindings().values()[0].name().text() == "value"_zc &&
            bindings.bindings().values()[0].declaringScope() == rootScope);
  ZC_EXPECT(OwnerBodyBindingProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               bindings.bindings()));
}

ZC_TEST("Named interface syntax retains an empty member-list child") {
  ModuleBodyFixture fixture("export interface Copy {}\n"_zc);
  ZC_REQUIRE(fixture.definitionNodes.size() == 1);
  ZC_REQUIRE(fixture.definitions.size() == 1);

  auto result = ModuleBodySyntaxProducer::produceNamedItem(
      ZC_ASSERT_NONNULL(fixture.parsed).syntax(), moduleKey(), fixture.moduleNode,
      fixture.definitionNodes[0], fixture.definitions[0].key, fixture.definitions.asPtr());
  ZC_REQUIRE(result.is<ModuleBodySyntaxProjection>());
  auto projection = zc::mv(result.get<ModuleBodySyntaxProjection>());
  ZC_REQUIRE(projection.syntax.rootCount() == 1);
  ZC_REQUIRE(projection.syntax.nodes().size() == 2);

  const auto& interface = projection.syntax.nodes()[0];
  const auto& members = projection.syntax.nodes()[1];
  ZC_EXPECT(interface.syntaxKind() == ast::SyntaxKind::InterfaceDecl);
  ZC_EXPECT(members.syntaxKind() == ast::SyntaxKind::ClassMemberList);
  auto name = interface.identifierField(0);
  auto typeParameters = interface.childField(1);
  auto inheritedInterfaces = interface.childField(2);
  auto memberList = interface.childField(3);
  auto memberItems = members.childField(1);
  ZC_REQUIRE(name != zc::none);
  ZC_REQUIRE(typeParameters != zc::none);
  ZC_REQUIRE(inheritedInterfaces != zc::none);
  ZC_REQUIRE(memberList != zc::none);
  ZC_REQUIRE(memberItems != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(name).text() == "Copy"_zc);
  ZC_EXPECT(!ZC_ASSERT_NONNULL(typeParameters).present);
  ZC_EXPECT(!ZC_ASSERT_NONNULL(inheritedInterfaces).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(memberList).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(memberList).firstChildOrdinal == 0);
  ZC_EXPECT(ZC_ASSERT_NONNULL(memberList).childCount == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(memberItems).present);
  ZC_EXPECT(ZC_ASSERT_NONNULL(memberItems).childCount == 0);
}

ZC_TEST("Module body syntax decodes identifier-list schema fields") {
  ModuleBodyFixture fixture("let value: Alpha = 1;\n"_zc);
  auto projection = fixture.project();
  zc::Maybe<const DetachedModuleBodyNode&> path;
  for (const auto& node : projection.syntax.nodes()) {
    if (node.syntaxKind() != ast::SyntaxKind::ModulePath) { continue; }
    ZC_REQUIRE(path == zc::none);
    path = node;
  }
  ZC_REQUIRE(path != zc::none);
  auto segments = ZC_ASSERT_NONNULL(path).identifierListField(0);
  ZC_REQUIRE(segments != zc::none);
  ZC_REQUIRE(ZC_ASSERT_NONNULL(segments).size() == 1);
  ZC_EXPECT(ZC_ASSERT_NONNULL(segments)[0].text() == "Alpha"_zc);
  ZC_EXPECT(ZC_ASSERT_NONNULL(path).identifierListField(1) == zc::none);
}

ZC_TEST("Module body syntax projects nested local binding patterns") {
  ModuleBodyFixture fixture("let (first, second) = pair;\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto bindingsResult =
      OwnerBodyBindingProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(bindingsResult != zc::none);
  auto bindings = zc::mv(ZC_ASSERT_NONNULL(bindingsResult));
  ZC_REQUIRE(bindings.bindings().values().size() == 2);
  ZC_EXPECT(OwnerBodyBindingProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               bindings.bindings()));
}

ZC_TEST("Module body syntax projects and verifies nested shadow targets") {
  ModuleBodyFixture fixture("let value = 1; { let value = 2; }\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto bindingsResult =
      OwnerBodyBindingProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(bindingsResult != zc::none);
  auto bindings = zc::mv(ZC_ASSERT_NONNULL(bindingsResult));
  auto shadowsResult =
      OwnerBodyShadowProjection::from(owner.clone(), scopes.scopes(), bindings.bindings());
  ZC_REQUIRE(shadowsResult != zc::none);
  auto shadows = zc::mv(ZC_ASSERT_NONNULL(shadowsResult));

  ZC_REQUIRE(shadows.shadows().values().size() == 1);
  ZC_EXPECT(OwnerBodyShadowProjection::verify(owner.clone(), scopes.scopes(), bindings.bindings(),
                                              shadows.shadows()));
  ZC_EXPECT(!OwnerBodyShadowProjection::verify(owner, scopes.scopes(), bindings.bindings(),
                                               CanonicalSequence<StableShadowTargetFact>::empty()));
}

ZC_TEST("Module body syntax projects lambda closure facts") {
  ModuleBodyFixture fixture("let transform = () => 1;\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto closuresResult =
      OwnerBodyClosureProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(closuresResult != zc::none);
  auto closures = zc::mv(ZC_ASSERT_NONNULL(closuresResult));
  ZC_REQUIRE(closures.closures().values().size() == 1);
  ZC_EXPECT(OwnerBodyClosureProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               closures.closures()));
}

ZC_TEST("Module body syntax projects loop label facts") {
  ModuleBodyFixture fixture("outer: while (true) { continue outer; }\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto labelsResult =
      OwnerBodyLabelProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(labelsResult != zc::none);
  auto labels = zc::mv(ZC_ASSERT_NONNULL(labelsResult));
  ZC_REQUIRE(labels.labels().values().size() == 1);
  ZC_EXPECT(labels.labels().values()[0].name().text() == "outer"_zc &&
            labels.labels().values()[0].target().value().is<StableLoopLabelTarget>());
  ZC_EXPECT(OwnerBodyLabelProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                             labels.labels()));
  auto controlsResult = OwnerBodyControlProjection::from(owner.clone(), projection.syntax,
                                                         scopes.nodeScopes(), labels.labels());
  ZC_REQUIRE(controlsResult != zc::none);
  auto controls = zc::mv(ZC_ASSERT_NONNULL(controlsResult));
  ZC_REQUIRE(controls.transfers().values().size() == 1);
  ZC_EXPECT(
      controls.transfers().values()[0].kind() == ControlTransferKind::Continue &&
      controls.transfers().values()[0].target().value().is<StableExplicitLabelControlTarget>());
  ZC_EXPECT(OwnerBodyControlProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               labels.labels(), controls.transfers()));
}

ZC_TEST("Module body syntax projects implicit loop control facts") {
  ModuleBodyFixture fixture("while (true) { continue; break; }\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto labelsResult =
      OwnerBodyLabelProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(labelsResult != zc::none);
  auto labels = zc::mv(ZC_ASSERT_NONNULL(labelsResult));
  auto controlsResult = OwnerBodyControlProjection::from(owner.clone(), projection.syntax,
                                                         scopes.nodeScopes(), labels.labels());
  ZC_REQUIRE(controlsResult != zc::none);
  auto controls = zc::mv(ZC_ASSERT_NONNULL(controlsResult));
  ZC_REQUIRE(controls.transfers().values().size() == 2);
  for (const auto& transfer : controls.transfers().values()) {
    ZC_EXPECT(transfer.target().value().is<StableLoopControlTarget>());
  }
  ZC_EXPECT(OwnerBodyControlProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               labels.labels(), controls.transfers()));
}

ZC_TEST("Module body syntax rejects continue to block label") {
  ModuleBodyFixture fixture("block: { continue block; }\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto labelsResult =
      OwnerBodyLabelProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(labelsResult != zc::none);
  auto labels = zc::mv(ZC_ASSERT_NONNULL(labelsResult));
  ZC_REQUIRE(labels.labels().values().size() == 1);
  ZC_EXPECT(labels.labels().values()[0].target().value().is<StableBlockLabelTarget>());
  ZC_EXPECT(OwnerBodyControlProjection::from(owner, projection.syntax, scopes.nodeScopes(),
                                             labels.labels()) == zc::none);
}

ZC_TEST("Module body syntax resolves control to nearest nested label") {
  ModuleBodyFixture fixture(
      "loop: while (true) {\n"
      "  loop: while (true) { break loop; }\n"
      "}\n"_zc);
  auto projection = fixture.project();
  auto module = moduleKey();
  auto ownerResult =
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone()));
  ZC_REQUIRE(ownerResult != zc::none);
  auto owner = zc::mv(ZC_ASSERT_NONNULL(ownerResult));
  auto scopesResult = OwnerBodyScopeProjection::from(owner.clone(), projection.syntax,
                                                     StableScopeOwnerKey::module(module.clone()));
  ZC_REQUIRE(scopesResult != zc::none);
  auto scopes = zc::mv(ZC_ASSERT_NONNULL(scopesResult));
  auto labelsResult =
      OwnerBodyLabelProjection::from(owner.clone(), projection.syntax, scopes.nodeScopes());
  ZC_REQUIRE(labelsResult != zc::none);
  auto labels = zc::mv(ZC_ASSERT_NONNULL(labelsResult));
  ZC_REQUIRE(labels.labels().values().size() == 2);
  auto controlsResult = OwnerBodyControlProjection::from(owner.clone(), projection.syntax,
                                                         scopes.nodeScopes(), labels.labels());
  ZC_REQUIRE(controlsResult != zc::none);
  auto controls = zc::mv(ZC_ASSERT_NONNULL(controlsResult));
  ZC_REQUIRE(controls.transfers().values().size() == 1);
  const auto& target = controls.transfers().values()[0].target().value();
  ZC_REQUIRE(target.is<StableExplicitLabelControlTarget>());
  size_t deepestLabelPath = 0;
  for (const auto& label : labels.labels().values()) {
    if (label.key().declarationPath().components().size() > deepestLabelPath) {
      deepestLabelPath = label.key().declarationPath().components().size();
    }
  }
  ZC_EXPECT(
      target.get<StableExplicitLabelControlTarget>().label.declarationPath().components().size() ==
      deepestLabelPath);
  ZC_EXPECT(OwnerBodyControlProjection::verify(owner, projection.syntax, scopes.nodeScopes(),
                                               labels.labels(), controls.transfers()));
}

ZC_TEST("Named item syntax admits contextual callable declaration names") {
  ModuleBodyFixture fixture(
      "interface Collection {\n"
      "  fun set(index: u64, value: u64) -> unit;\n"
      "}\n"_zc);
  zc::Maybe<size_t> methodIndex;
  const auto& tree = ZC_ASSERT_NONNULL(fixture.parsed).tree();
  for (size_t index = 0; index < fixture.definitionNodes.size(); ++index) {
    const auto& node = tree.node(fixture.definitionNodes[index]);
    if (node.kind != ast::SyntaxKind::MethodDecl ||
        tree.ident(ast::IdentId(node.payload.words[ast::kMethodDeclNameWord])) != "set"_zc) {
      continue;
    }
    ZC_REQUIRE(methodIndex == zc::none);
    methodIndex = index;
  }
  ZC_REQUIRE(methodIndex != zc::none);
  ZC_IF_SOME(index, methodIndex) {
    auto projection = ModuleBodySyntaxProducer::produceNamedItem(
        ZC_ASSERT_NONNULL(fixture.parsed).syntax(), moduleKey(), fixture.moduleNode,
        fixture.definitionNodes[index], fixture.definitions[index].key,
        fixture.definitions.asPtr());
    ZC_REQUIRE(projection.is<ModuleBodySyntaxProjection>());
    const auto& syntax = projection.get<ModuleBodySyntaxProjection>().syntax;
    ZC_REQUIRE(syntax.nodes().size() != 0);
    ZC_REQUIRE(syntax.nodes()[0].syntaxKind() != zc::none);
    ZC_EXPECT(ZC_ASSERT_NONNULL(syntax.nodes()[0].syntaxKind()) == ast::SyntaxKind::MethodDecl);
    const auto encoded = syntax.encodeCanonical();
    ZC_EXPECT(ModuleBodySyntax::decodeCanonical(encoded.asPtr()) != zc::none);
  }
}

ZC_TEST("Parsed module recognizes the implicit Self type of a this receiver") {
  ModuleBodyFixture fixture(
      "interface Collection {\n"
      "  fun consume(this);\n"
      "}\n"_zc);
  const auto& verified = ZC_ASSERT_NONNULL(fixture.parsed);
  const auto& tree = verified.tree();
  zc::Maybe<ast::NodeId> method;
  for (const auto definition : fixture.definitionNodes) {
    if (tree.node(definition).kind == ast::SyntaxKind::MethodDecl) {
      ZC_REQUIRE(method == zc::none);
      method = definition;
    }
  }
  ZC_REQUIRE(method != zc::none);
  const auto& methodSyntax = tree.node(ZC_ASSERT_NONNULL(method));
  const auto& parameters =
      tree.node(ast::NodeId(methodSyntax.payload.words[ast::kMethodDeclParamsIdWord]));
  const ast::NodeList parameterList{
      parameters.payload.words[ast::kFunctionParameterListParamsFirstWord],
      parameters.payload.words[ast::kFunctionParameterListParamsSizeWord]};
  const auto parameterIds = tree.list(parameterList);
  ZC_REQUIRE(parameterIds.size() == 1);
  ZC_EXPECT(verified.functionParameterHasImplicitSelfType(parameterIds[0]));
}

ZC_TEST("Module body codecs reject trailing data and preserve exact values") {
  ModuleBodyFixture fixture(zc::str("module root;\n", kBodyItems));
  auto projection = fixture.project();
  const auto syntaxBytes = projection.syntax.encodeCanonical();
  auto decodedSyntax = ModuleBodySyntax::decodeCanonical(syntaxBytes.asPtr());
  ZC_REQUIRE(decodedSyntax != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedSyntax) == projection.syntax);
  zc::Vector<uint8_t> malformedSyntax(syntaxBytes.size() + 1);
  malformedSyntax.addAll(syntaxBytes);
  malformedSyntax.add(0);
  ZC_EXPECT(ModuleBodySyntax::decodeCanonical(malformedSyntax.asPtr()) == zc::none);

  const auto provenanceBytes = projection.provenance.encodeCanonical();
  auto decodedProvenance = ModuleBodyProvenance::decodeCanonical(provenanceBytes.asPtr());
  ZC_REQUIRE(decodedProvenance != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedProvenance) == projection.provenance);
  zc::Vector<uint8_t> malformedProvenance(provenanceBytes.size() + 1);
  malformedProvenance.addAll(provenanceBytes);
  malformedProvenance.add(0);
  ZC_EXPECT(ModuleBodyProvenance::decodeCanonical(malformedProvenance.asPtr()) == zc::none);
}

ZC_TEST("Module body producer and verifier reject incomplete boundary inventories") {
  ModuleBodyFixture fixture(zc::str("module root;\n", kBodyItems));
  auto projection = fixture.project();
  zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> noDefinitions;
  auto produced = ModuleBodySyntaxProducer::produce(ZC_ASSERT_NONNULL(fixture.parsed).syntax(),
                                                    moduleKey(), fixture.moduleNode, noDefinitions);
  ZC_REQUIRE(produced.is<ModuleBodySyntaxFailure>());
  ZC_EXPECT(produced.get<ModuleBodySyntaxFailure>().kind ==
            ModuleBodySyntaxFailureKind::InvalidBoundaryInventory);
  ZC_EXPECT(ModuleBodySyntaxVerifier::verify(ZC_ASSERT_NONNULL(fixture.parsed).syntax(),
                                             moduleKey(), fixture.moduleNode, noDefinitions,
                                             projection) ==
            ModuleBodySyntaxFailureKind::InvalidBoundaryInventory);
}

ZC_TEST("Owner body codecs preserve canonical records and reject malformed inventories") {
  ModuleBodyFixture fixture(zc::str("module root;\n", kBodyItems));
  auto moduleProjection = fixture.project();
  ZC_REQUIRE(fixture.definitions.size() == 1);
  ZC_REQUIRE(fixture.definitionNodes.size() == 1);
  auto namedProjection = ModuleBodySyntaxProducer::produceNamedItem(
      ZC_ASSERT_NONNULL(fixture.parsed).syntax(), moduleKey(), fixture.moduleNode,
      fixture.definitionNodes[0], fixture.definitions[0].key, fixture.definitions.asPtr());
  ZC_REQUIRE(namedProjection.is<ModuleBodySyntaxProjection>());

  zc::Vector<StableBodyOwnerKey> ownerValues;
  ownerValues.add(StableBodyOwnerKey::module(moduleKey()));
  ownerValues.add(StableBodyOwnerKey::definition(fixture.definitions[0].key.clone()));
  auto owners = ModuleBodyOwners::from(moduleKey(), zc::mv(ownerValues));
  ZC_REQUIRE(owners != zc::none);
  auto ownersBytes = ZC_REQUIRE_NONNULL(owners).encodeCanonical();
  auto decodedOwners = ModuleBodyOwners::decodeCanonical(ownersBytes.asPtr());
  ZC_REQUIRE(decodedOwners != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedOwners) == ZC_REQUIRE_NONNULL(owners));
  zc::Vector<uint8_t> trailingOwners(ownersBytes.size() + 1);
  trailingOwners.addAll(ownersBytes);
  trailingOwners.add(0);
  ZC_EXPECT(ModuleBodyOwners::decodeCanonical(trailingOwners.asPtr()) == zc::none);

  zc::Vector<StableBodyOwnerKey> duplicateModule;
  duplicateModule.add(StableBodyOwnerKey::module(moduleKey()));
  duplicateModule.add(StableBodyOwnerKey::module(moduleKey()));
  ZC_EXPECT(ModuleBodyOwners::from(moduleKey(), zc::mv(duplicateModule)) == zc::none);
  zc::Vector<StableBodyOwnerKey> missingModule;
  missingModule.add(StableBodyOwnerKey::definition(fixture.definitions[0].key.clone()));
  ZC_EXPECT(ModuleBodyOwners::from(moduleKey(), zc::mv(missingModule)) == zc::none);
  zc::Vector<StableBodyOwnerKey> foreignModule;
  foreignModule.add(StableBodyOwnerKey::module(otherModuleKey()));
  ZC_EXPECT(ModuleBodyOwners::from(moduleKey(), zc::mv(foreignModule)) == zc::none);

  auto moduleOwner = StableBodyOwnerKey::module(moduleKey());
  auto syntax =
      OwnerBodySyntax::from(moduleOwner.clone(), moduleKey(), moduleProjection.syntax.clone());
  ZC_REQUIRE(syntax != zc::none);
  auto syntaxBytes = ZC_REQUIRE_NONNULL(syntax).encodeCanonical();
  auto decodedSyntax = OwnerBodySyntax::decodeCanonical(syntaxBytes.asPtr());
  ZC_REQUIRE(decodedSyntax != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax) == ZC_REQUIRE_NONNULL(syntax));
  zc::Vector<uint8_t> trailingSyntax(syntaxBytes.size() + 1);
  trailingSyntax.addAll(syntaxBytes);
  trailingSyntax.add(0);
  ZC_EXPECT(OwnerBodySyntax::decodeCanonical(trailingSyntax.asPtr()) == zc::none);
  ZC_EXPECT(OwnerBodySyntax::from(moduleOwner.clone(), otherModuleKey(),
                                  moduleProjection.syntax.clone()) == zc::none);

  auto provenance =
      OwnerBodyProvenance::from(moduleOwner.clone(), moduleProjection.provenance.clone());
  ZC_REQUIRE(provenance != zc::none);
  auto provenanceBytes = ZC_REQUIRE_NONNULL(provenance).encodeCanonical();
  auto decodedProvenance = OwnerBodyProvenance::decodeCanonical(provenanceBytes.asPtr());
  ZC_REQUIRE(decodedProvenance != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedProvenance) == ZC_REQUIRE_NONNULL(provenance));
  zc::Vector<uint8_t> trailingProvenance(provenanceBytes.size() + 1);
  trailingProvenance.addAll(provenanceBytes);
  trailingProvenance.add(0);
  ZC_EXPECT(OwnerBodyProvenance::decodeCanonical(trailingProvenance.asPtr()) == zc::none);

  auto definitionOwner = StableBodyOwnerKey::definition(fixture.definitions[0].key.clone());
  auto definitionSyntax =
      OwnerBodySyntax::from(definitionOwner.clone(), moduleKey(),
                            namedProjection.get<ModuleBodySyntaxProjection>().syntax.clone());
  auto definitionProvenance = OwnerBodyProvenance::from(
      definitionOwner.clone(),
      namedProjection.get<ModuleBodySyntaxProjection>().provenance.clone());
  ZC_REQUIRE(definitionSyntax != zc::none);
  ZC_REQUIRE(definitionProvenance != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(definitionSyntax).owner() == definitionOwner);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(definitionProvenance).owner() == definitionOwner);
}

}  // namespace zomlang::compiler::binder
