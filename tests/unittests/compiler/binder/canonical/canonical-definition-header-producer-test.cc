// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/canonical/canonical-definition-header-producer.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "tests/unittests/compiler/test-ast-builder.h"

namespace zomlang::compiler::binder {
namespace {

using tests::TestFixture;

ast::NodeId makeModulePath(TestFixture& fixture, zc::StringPtr name, uint8_t root = 0) {
  zc::Vector<ast::IdentId> names;
  names.add(fixture.builder().internIdent(name));
  const auto segments = fixture.builder().makeIdentList(names.asPtr());
  ast::NodePayload payload;
  payload.words[ast::kModulePathSegmentsFirstWord] = segments.first;
  payload.words[ast::kModulePathSegmentsSizeWord] = segments.size;
  payload.words[ast::kModulePathRootWord] = root;
  return fixture.builder().makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(), payload);
}

ast::NodeId makeNamedType(TestFixture& fixture, zc::StringPtr name, uint8_t root = 0) {
  ast::NodePayload payload;
  payload.words[ast::kNamedTypeExprPathWord] = makeModulePath(fixture, name, root).value;
  payload.words[ast::kNamedTypeExprArgsFirstWord] = 0;
  payload.words[ast::kNamedTypeExprArgsSizeWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(), payload);
}

ast::NodeId makeParameter(TestFixture& fixture, zc::StringPtr name, ast::NodeId type,
                          ast::NodeId attributes = ast::NodeId()) {
  ast::NodePayload payload;
  payload.words[ast::kFunctionParameterDeclNameWord] = fixture.builder().internIdent(name).value;
  payload.words[ast::kFunctionParameterDeclTyWord] = type.value;
  payload.words[ast::kFunctionParameterDeclDefaultWord] = 0;
  payload.words[ast::kFunctionParameterDeclAttrsWord] = attributes.value;
  return fixture.builder().makeNode(ast::SyntaxKind::FunctionParameterDecl, source::SourceRange(),
                                    payload);
}

ast::NodeId makeMoveReceiverAttributes(TestFixture& fixture) {
  zc::Vector<ast::IdentId> segments;
  segments.add(fixture.builder().internIdent("zom"_zc));
  segments.add(fixture.builder().internIdent("param"_zc));
  segments.add(fixture.builder().internIdent("move"_zc));
  const auto names = fixture.builder().makeIdentList(segments.asPtr());
  ast::NodePayload pathPayload;
  pathPayload.words[ast::kAttributePathSegmentsFirstWord] = names.first;
  pathPayload.words[ast::kAttributePathSegmentsSizeWord] = names.size;
  pathPayload.words[ast::kAttributePathLeadingWord] = 0;
  const auto path = fixture.builder().makeNode(ast::SyntaxKind::AttributePath,
                                               source::SourceRange(), pathPayload);
  ast::NodePayload attributePayload;
  attributePayload.words[ast::kAttributePathWord] = path.value;
  attributePayload.words[ast::kAttributeArgsFirstWord] = 0;
  attributePayload.words[ast::kAttributeArgsSizeWord] = 0;
  const auto attribute = fixture.builder().makeNode(ast::SyntaxKind::Attribute,
                                                    source::SourceRange(), attributePayload);
  zc::Vector<ast::NodeId> attributes;
  attributes.add(attribute);
  const auto list = fixture.makeNodeList(attributes);
  ast::NodePayload listPayload;
  listPayload.words[ast::kAttributeListAttrsFirstWord] = list.first;
  listPayload.words[ast::kAttributeListAttrsSizeWord] = list.size;
  return fixture.builder().makeNode(ast::SyntaxKind::AttributeList, source::SourceRange(),
                                    listPayload);
}

ast::NodeId makeParameterList(TestFixture& fixture, zc::ArrayPtr<const ast::NodeId> parameters) {
  return fixture.makeFunctionParamList(fixture.makeNodeList(parameters));
}

ast::NodeId makeGenericParameters(TestFixture& fixture, zc::StringPtr name,
                                  ast::NodeId bounds = ast::NodeId(),
                                  ast::NodeId whereClause = ast::NodeId()) {
  zc::Vector<ast::NodeId> parameters;
  parameters.add(fixture.makeGenericTypeParam(name, bounds));
  return fixture.makeGenericParams(fixture.makeNodeList(parameters), whereClause);
}

ast::NodeId makeMethod(TestFixture& fixture, zc::StringPtr name, ast::NodeId parameters,
                       uint8_t mode, ast::NodeId typeParameters = ast::NodeId()) {
  ast::NodePayload payload;
  payload.words[ast::kMethodDeclNameWord] = fixture.builder().internIdent(name).value;
  payload.words[ast::kMethodDeclParamsIdWord] = parameters.value;
  payload.words[ast::kMethodDeclTypeParamsIdWord] = typeParameters.value;
  payload.words[ast::kMethodDeclRetTyWord] = 0;
  payload.words[ast::kMethodDeclRaisesTyWord] = 0;
  payload.words[ast::kMethodDeclBodyWord] = 0;
  payload.words[ast::kMethodDeclModeWord] = mode;
  payload.words[ast::kMethodDeclVisibilityWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::MethodDecl, source::SourceRange(), payload);
}

ast::NodeId makeConstructor(TestFixture& fixture, zc::StringPtr name, ast::NodeId parameters) {
  ast::NodePayload payload;
  payload.words[ast::kConstructorDeclNameWord] = fixture.builder().internIdent(name).value;
  payload.words[ast::kConstructorDeclParamsIdWord] = parameters.value;
  payload.words[ast::kConstructorDeclRaisesTyWord] = 0;
  payload.words[ast::kConstructorDeclBodyWord] = 0;
  payload.words[ast::kConstructorDeclVisibilityWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::ConstructorDecl, source::SourceRange(),
                                    payload);
}

ast::NodeId makeExtern(TestFixture& fixture, zc::StringPtr name,
                       zc::ArrayPtr<const ast::NodeId> parameters, uint16_t abi) {
  const auto list = fixture.makeNodeList(parameters);
  ast::NodePayload payload;
  payload.words[ast::kExternDeclNameWord] = fixture.builder().internIdent(name).value;
  payload.words[ast::kExternDeclAbiWord] = abi;
  payload.words[ast::kExternDeclParamsFirstWord] = list.first;
  payload.words[ast::kExternDeclParamsSizeWord] = list.size;
  payload.words[ast::kExternDeclRetTyWord] = 0;
  payload.words[ast::kExternDeclRaisesTyWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::ExternDecl, source::SourceRange(), payload);
}

ast::Tree finish(TestFixture& fixture) {
  zc::Vector<ast::NodeId> statements;
  fixture.makeSourceFile(ast::NodeId(), fixture.makeNodeList(statements));
  return fixture.finishTree();
}

ast::IdentId definitionName(const ast::Tree& tree, ast::NodeId node) {
  const auto& syntax = tree.node(node);
  switch (syntax.kind) {
    case ast::SyntaxKind::FunctionDecl:
      return ast::IdentId(syntax.payload.words[ast::kFunctionDeclNameWord]);
    case ast::SyntaxKind::ExternDecl:
      return ast::IdentId(syntax.payload.words[ast::kExternDeclNameWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::IdentId(syntax.payload.words[ast::kMethodDeclNameWord]);
    case ast::SyntaxKind::ConstructorDecl:
      return ast::IdentId(syntax.payload.words[ast::kConstructorDeclNameWord]);
    default:
      ZC_FAIL_REQUIRE("non-callable definition fixture");
  }
}

DefinitionInventoryEntry entry(const ast::Tree& tree, ast::NodeId node,
                               identity::DefinitionKind kind) {
  zc::Vector<StructuralIdentityParent> parents;
  return DefinitionInventoryEntry{node,
                                  DefinitionSite::declaration(node),
                                  ast::NodeId(),
                                  kind,
                                  InventoryDefinitionNameKind::Declared,
                                  definitionName(tree, node),
                                  zc::none,
                                  source::SourceRange(),
                                  zc::mv(parents)};
}

const identity::OverloadHeaderAuthority& requireAuthority(
    CanonicalDefinitionHeaderProduction& result) {
  ZC_REQUIRE(result.is<identity::OverloadHeaderAuthority>());
  return result.get<identity::OverloadHeaderAuthority>();
}

ZC_TEST("CanonicalDefinitionHeaderProducer alpha-normalizes callable generic names") {
  TestFixture fixture;
  const auto tType = makeNamedType(fixture, "T"_zc);
  const auto uType = makeNamedType(fixture, "U"_zc);
  zc::Vector<ast::NodeId> tParameters;
  tParameters.add(makeParameter(fixture, "value"_zc, tType));
  zc::Vector<ast::NodeId> uParameters;
  uParameters.add(makeParameter(fixture, "value"_zc, uType));
  const auto tFunction = fixture.makeFunctionDecl(
      "map"_zc, ast::NodeId(), makeParameterList(fixture, tParameters.asPtr()), ast::NodeId(),
      ast::NodeId(), makeGenericParameters(fixture, "T"_zc));
  const auto uFunction = fixture.makeFunctionDecl(
      "map"_zc, ast::NodeId(), makeParameterList(fixture, uParameters.asPtr()), ast::NodeId(),
      ast::NodeId(), makeGenericParameters(fixture, "U"_zc));
  const auto tree = finish(fixture);
  auto tEntry = entry(tree, tFunction, identity::DefinitionKind::Function);
  auto uEntry = entry(tree, uFunction, identity::DefinitionKind::Function);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto t = CanonicalDefinitionHeaderProducer::produce(tree, tEntry, owners.asPtr());
  auto u = CanonicalDefinitionHeaderProducer::produce(tree, uEntry, owners.asPtr());
  const auto& tAuthority = requireAuthority(t);
  const auto& uAuthority = requireAuthority(u);
  ZC_EXPECT(tAuthority.header().encode().asPtr() == uAuthority.header().encode().asPtr());
  ZC_EXPECT(tAuthority.digest() == uAuthority.digest());
}

ZC_TEST("CanonicalDefinitionHeaderProducer merges inline and where obligations") {
  TestFixture fixture;
  const auto a = makeNamedType(fixture, "A"_zc);
  const auto b = makeNamedType(fixture, "B"_zc);
  zc::Vector<ast::NodeId> bounds;
  bounds.add(a);
  bounds.add(b);
  const auto boundList = fixture.makeTypeParameterBoundList(fixture.makeNodeList(bounds));
  const auto inlineGenerics = makeGenericParameters(fixture, "T"_zc, boundList);

  const auto subject = makeNamedType(fixture, "U"_zc);
  zc::Vector<ast::NodeId> predicates;
  predicates.add(fixture.makeWherePred(subject, a));
  predicates.add(fixture.makeWherePred(subject, b));
  const auto whereClause = fixture.makeWhereClause(fixture.makeNodeList(predicates));
  const auto whereGenerics = makeGenericParameters(fixture, "U"_zc, ast::NodeId(), whereClause);

  zc::Vector<ast::NodeId> noParameters;
  const auto inlineFunction = fixture.makeFunctionDecl(
      "bounded"_zc, ast::NodeId(), makeParameterList(fixture, noParameters.asPtr()), ast::NodeId(),
      ast::NodeId(), inlineGenerics);
  const auto whereFunction = fixture.makeFunctionDecl(
      "bounded"_zc, ast::NodeId(), makeParameterList(fixture, noParameters.asPtr()), ast::NodeId(),
      ast::NodeId(), whereGenerics);
  const auto tree = finish(fixture);
  auto inlineEntry = entry(tree, inlineFunction, identity::DefinitionKind::Function);
  auto whereEntry = entry(tree, whereFunction, identity::DefinitionKind::Function);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto inlineResult = CanonicalDefinitionHeaderProducer::produce(tree, inlineEntry, owners.asPtr());
  auto whereResult = CanonicalDefinitionHeaderProducer::produce(tree, whereEntry, owners.asPtr());
  const auto& inlineHeader = requireAuthority(inlineResult).header();
  const auto& whereHeader = requireAuthority(whereResult).header();
  ZC_EXPECT(inlineHeader.obligations().size() == 2);
  ZC_EXPECT(inlineHeader.encode().asPtr() == whereHeader.encode().asPtr());
}

ZC_TEST("CanonicalDefinitionHeaderProducer retains every duplicate bound syntax occurrence") {
  TestFixture fixture;
  const auto first = makeNamedType(fixture, "A"_zc);
  const auto second = makeNamedType(fixture, "A"_zc);
  const auto third = makeNamedType(fixture, "A"_zc);
  zc::Vector<ast::NodeId> bounds;
  bounds.add(first);
  bounds.add(second);
  bounds.add(third);
  const auto boundList = fixture.makeTypeParameterBoundList(fixture.makeNodeList(bounds));
  const auto generics = makeGenericParameters(fixture, "T"_zc, boundList);
  zc::Vector<ast::NodeId> noParameters;
  const auto function = fixture.makeFunctionDecl("bounded"_zc, ast::NodeId(),
                                                 makeParameterList(fixture, noParameters.asPtr()),
                                                 ast::NodeId(), ast::NodeId(), generics);
  const auto tree = finish(fixture);
  auto definition = entry(tree, function, identity::DefinitionKind::Function);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto result =
      CanonicalDefinitionHeaderProducer::produceWithProvenance(tree, definition, owners.asPtr());
  ZC_REQUIRE(result.is<CanonicalDefinitionHeaderProvenance>());
  const auto& provenance = result.get<CanonicalDefinitionHeaderProvenance>();
  ZC_EXPECT(provenance.authority.header().obligations().size() == 1);
  ZC_REQUIRE(provenance.boundOccurrences.size() == 3);
  ZC_EXPECT(provenance.boundOccurrences[0].node == first);
  ZC_EXPECT(provenance.boundOccurrences[1].node == second);
  ZC_EXPECT(provenance.boundOccurrences[2].node == third);
}

ZC_TEST("CanonicalDefinitionHeaderProducer normalizes receivers and removes them from parameters") {
  TestFixture fixture;
  const auto selfType = makeNamedType(fixture, "Self"_zc);
  const auto valueType = fixture.makePredefinedTypeExpr(2);
  zc::Vector<ast::NodeId> parameters;
  parameters.add(makeParameter(fixture, "this"_zc, selfType));
  parameters.add(makeParameter(fixture, "value"_zc, valueType));
  const auto shared =
      makeMethod(fixture, "update"_zc, makeParameterList(fixture, parameters.asPtr()), 0);
  const auto mutableMethod =
      makeMethod(fixture, "update"_zc, makeParameterList(fixture, parameters.asPtr()), 2);
  const auto invalidStatic =
      makeMethod(fixture, "update"_zc, makeParameterList(fixture, parameters.asPtr()), 1);
  const auto tree = finish(fixture);
  auto sharedEntry = entry(tree, shared, identity::DefinitionKind::Method);
  auto mutableEntry = entry(tree, mutableMethod, identity::DefinitionKind::Method);
  auto staticEntry = entry(tree, invalidStatic, identity::DefinitionKind::Method);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto sharedResult = CanonicalDefinitionHeaderProducer::produce(tree, sharedEntry, owners.asPtr());
  auto mutableResult =
      CanonicalDefinitionHeaderProducer::produce(tree, mutableEntry, owners.asPtr());
  auto staticResult = CanonicalDefinitionHeaderProducer::produce(tree, staticEntry, owners.asPtr());
  const auto& sharedHeader = requireAuthority(sharedResult).header();
  const auto& mutableHeader = requireAuthority(mutableResult).header();
  ZC_EXPECT(sharedHeader.receiver() == identity::ReceiverShape::Shared);
  ZC_EXPECT(mutableHeader.receiver() == identity::ReceiverShape::Mutable);
  ZC_EXPECT(sharedHeader.parameters().size() == 1);
  ZC_REQUIRE(staticResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(staticResult.get<CanonicalHeaderSyntaxFailure>().kind ==
            CanonicalHeaderSyntaxFailureKind::InvalidReceiver);
}

ZC_TEST("CanonicalDefinitionHeaderProducer admits exact move and mutable-reference receivers") {
  TestFixture fixture;
  const auto selfType = makeNamedType(fixture, "Self"_zc);
  const auto mutableSelf = fixture.makeReferenceTypeExpr(selfType, true);
  zc::Vector<ast::NodeId> moveParameters;
  moveParameters.add(
      makeParameter(fixture, "this"_zc, selfType, makeMoveReceiverAttributes(fixture)));
  zc::Vector<ast::NodeId> referenceParameters;
  referenceParameters.add(makeParameter(fixture, "this"_zc, mutableSelf));
  const auto moveMethod =
      makeMethod(fixture, "consume"_zc, makeParameterList(fixture, moveParameters.asPtr()), 0);
  const auto referenceMethod =
      makeMethod(fixture, "consume"_zc, makeParameterList(fixture, referenceParameters.asPtr()), 2);
  const auto invalidMove =
      makeMethod(fixture, "consume"_zc, makeParameterList(fixture, moveParameters.asPtr()), 2);
  const auto tree = finish(fixture);
  auto moveEntry = entry(tree, moveMethod, identity::DefinitionKind::Method);
  auto referenceEntry = entry(tree, referenceMethod, identity::DefinitionKind::Method);
  auto invalidEntry = entry(tree, invalidMove, identity::DefinitionKind::Method);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto moveResult = CanonicalDefinitionHeaderProducer::produce(tree, moveEntry, owners.asPtr());
  auto referenceResult =
      CanonicalDefinitionHeaderProducer::produce(tree, referenceEntry, owners.asPtr());
  auto invalidResult =
      CanonicalDefinitionHeaderProducer::produce(tree, invalidEntry, owners.asPtr());
  ZC_EXPECT(requireAuthority(moveResult).header().receiver() == identity::ReceiverShape::Move);
  ZC_EXPECT(requireAuthority(referenceResult).header().receiver() ==
            identity::ReceiverShape::Mutable);
  ZC_REQUIRE(invalidResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(invalidResult.get<CanonicalHeaderSyntaxFailure>().kind ==
            CanonicalHeaderSyntaxFailureKind::InvalidReceiver);
}

ZC_TEST("CanonicalDefinitionHeaderProducer preserves callable kind result and ABI contracts") {
  TestFixture fixture;
  zc::Vector<ast::NodeId> noParameters;
  const auto parameters = makeParameterList(fixture, noParameters.asPtr());
  const auto function = fixture.makeFunctionDecl("create"_zc, ast::NodeId(), parameters);
  const auto explicitUnit = fixture.makeFunctionDecl("create"_zc, ast::NodeId(), parameters,
                                                     fixture.makePredefinedTypeExpr(14));
  const auto constructor = makeConstructor(fixture, "create"_zc, parameters);
  const auto external = makeExtern(fixture, "create"_zc, noParameters.asPtr(), 1);
  const auto tree = finish(fixture);
  auto functionEntry = entry(tree, function, identity::DefinitionKind::Function);
  auto unitEntry = entry(tree, explicitUnit, identity::DefinitionKind::Function);
  auto constructorEntry = entry(tree, constructor, identity::DefinitionKind::Constructor);
  auto externEntry = entry(tree, external, identity::DefinitionKind::Function);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto functionResult =
      CanonicalDefinitionHeaderProducer::produce(tree, functionEntry, owners.asPtr());
  auto unitResult = CanonicalDefinitionHeaderProducer::produce(tree, unitEntry, owners.asPtr());
  auto constructorResult =
      CanonicalDefinitionHeaderProducer::produce(tree, constructorEntry, owners.asPtr());
  auto externResult = CanonicalDefinitionHeaderProducer::produce(tree, externEntry, owners.asPtr());
  const auto& functionHeader = requireAuthority(functionResult).header();
  const auto& unitHeader = requireAuthority(unitResult).header();
  const auto& constructorHeader = requireAuthority(constructorResult).header();
  const auto& externalHeader = requireAuthority(externResult).header();
  ZC_EXPECT(functionHeader.encode().asPtr() == unitHeader.encode().asPtr());
  ZC_EXPECT(constructorHeader.callableKind() == identity::CallableHeaderKind::Constructor);
  ZC_EXPECT(constructorHeader.result().kind() ==
            identity::CanonicalCallableResultKind::ConstructorSelf);
  ZC_EXPECT(externalHeader.externalAbi() == identity::ExternalAbi::Stdcall);
}

ZC_TEST("CanonicalDefinitionHeaderProducer rejects inventory kind and name mismatches") {
  TestFixture fixture;
  zc::Vector<ast::NodeId> noParameters;
  const auto function = fixture.makeFunctionDecl("run"_zc, ast::NodeId(),
                                                 makeParameterList(fixture, noParameters.asPtr()));
  const auto otherName = fixture.builder().internIdent("other"_zc);
  const auto tree = finish(fixture);
  auto wrongKind = entry(tree, function, identity::DefinitionKind::Method);
  auto wrongName = entry(tree, function, identity::DefinitionKind::Function);
  wrongName.declaredName = otherName;
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto kindResult = CanonicalDefinitionHeaderProducer::produce(tree, wrongKind, owners.asPtr());
  auto nameResult = CanonicalDefinitionHeaderProducer::produce(tree, wrongName, owners.asPtr());
  ZC_EXPECT(kindResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(nameResult.is<CanonicalHeaderSyntaxFailure>());
}

ZC_TEST("CanonicalDefinitionHeaderProducer admits duplicate unused generic names") {
  TestFixture fixture;
  zc::Vector<ast::NodeId> genericParameters;
  genericParameters.add(fixture.makeGenericTypeParam("T"_zc));
  genericParameters.add(fixture.makeGenericTypeParam("T"_zc));
  const auto generics = fixture.makeGenericParams(fixture.makeNodeList(genericParameters));
  zc::Vector<ast::NodeId> noParameters;
  const auto function = fixture.makeFunctionDecl("run"_zc, ast::NodeId(),
                                                 makeParameterList(fixture, noParameters.asPtr()),
                                                 ast::NodeId(), ast::NodeId(), generics);
  const auto tree = finish(fixture);
  auto definition = entry(tree, function, identity::DefinitionKind::Function);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto result = CanonicalDefinitionHeaderProducer::produce(tree, definition, owners.asPtr());
  const auto& authority = requireAuthority(result);
  ZC_EXPECT(authority.header().genericParameters().size() == 2);
}

}  // namespace
}  // namespace zomlang::compiler::binder
