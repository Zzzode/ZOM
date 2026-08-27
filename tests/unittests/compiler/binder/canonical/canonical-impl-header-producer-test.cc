// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/canonical/canonical-impl-header-producer.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "tests/unittests/compiler/test-ast-builder.h"

namespace zomlang::compiler::binder {
namespace {

using tests::TestFixture;

ast::NodeId makeModulePath(TestFixture& fixture, zc::ArrayPtr<const zc::StringPtr> segments,
                           uint8_t root = 0) {
  zc::Vector<ast::IdentId> names(segments.size());
  for (const auto segment : segments) { names.add(fixture.builder().internIdent(segment)); }
  const auto values = fixture.builder().makeIdentList(names.asPtr());
  ast::NodePayload payload;
  payload.words[ast::kModulePathSegmentsFirstWord] = values.first;
  payload.words[ast::kModulePathSegmentsSizeWord] = values.size;
  payload.words[ast::kModulePathRootWord] = root;
  return fixture.builder().makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(), payload);
}

ast::NodeId makeNamedType(TestFixture& fixture, zc::StringPtr name,
                          zc::ArrayPtr<const ast::NodeId> arguments = nullptr) {
  const zc::StringPtr segments[] = {name};
  const auto values = fixture.makeNodeList(arguments);
  ast::NodePayload payload;
  payload.words[ast::kNamedTypeExprPathWord] =
      makeModulePath(fixture, zc::arrayPtr(segments)).value;
  payload.words[ast::kNamedTypeExprArgsFirstWord] = values.first;
  payload.words[ast::kNamedTypeExprArgsSizeWord] = values.size;
  return fixture.builder().makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(), payload);
}

ast::NodeId makeAttributePath(TestFixture& fixture, zc::ArrayPtr<const zc::StringPtr> segments,
                              uint8_t leading = 0) {
  zc::Vector<ast::IdentId> names(segments.size());
  for (const auto segment : segments) { names.add(fixture.builder().internIdent(segment)); }
  const auto values = fixture.builder().makeIdentList(names.asPtr());
  ast::NodePayload payload;
  payload.words[ast::kAttributePathSegmentsFirstWord] = values.first;
  payload.words[ast::kAttributePathSegmentsSizeWord] = values.size;
  payload.words[ast::kAttributePathLeadingWord] = leading;
  return fixture.builder().makeNode(ast::SyntaxKind::AttributePath, source::SourceRange(), payload);
}

ast::NodeId makeGenerics(TestFixture& fixture, zc::StringPtr name,
                         ast::NodeId bounds = ast::NodeId(),
                         ast::NodeId whereClause = ast::NodeId()) {
  zc::Vector<ast::NodeId> parameters;
  parameters.add(fixture.makeGenericTypeParam(name, bounds));
  return fixture.makeGenericParams(fixture.makeNodeList(parameters), whereClause);
}

ast::NodeId makeDuplicateGenerics(TestFixture& fixture, zc::StringPtr name) {
  zc::Vector<ast::NodeId> parameters;
  parameters.add(fixture.makeGenericTypeParam(name));
  parameters.add(fixture.makeGenericTypeParam(name));
  return fixture.makeGenericParams(fixture.makeNodeList(parameters));
}

ast::NodeId makeStandaloneImpl(TestFixture& fixture, ast::NodeId trait, ast::NodeId selfType,
                               ast::NodeId generics = ast::NodeId(),
                               ast::NodeId whereClause = ast::NodeId(), bool isUnsafe = false) {
  ast::NodePayload payload;
  payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = isUnsafe ? 1 : 0;
  payload.words[ast::kStandaloneImplDeclInterfaceWord] = trait.value;
  payload.words[ast::kStandaloneImplDeclForTyWord] = selfType.value;
  payload.words[ast::kStandaloneImplDeclWhereWord] = whereClause.value;
  payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = generics.value;
  payload.words[ast::kStandaloneImplDeclMembersIdWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(),
                                    payload);
}

ast::NodeId makeMarkerImpl(TestFixture& fixture, zc::ArrayPtr<const zc::StringPtr> path,
                           ast::NodeId selfType, bool isUnsafe = false, bool isNegated = false,
                           uint8_t leading = 0) {
  ast::NodePayload payload;
  payload.words[ast::kMarkerImplIsUnsafeWord] = isUnsafe ? 1 : 0;
  payload.words[ast::kMarkerImplIsNegatedWord] = isNegated ? 1 : 0;
  payload.words[ast::kMarkerImplMarkerPathWord] = makeAttributePath(fixture, path, leading).value;
  payload.words[ast::kMarkerImplForTyWord] = selfType.value;
  return fixture.builder().makeNode(ast::SyntaxKind::MarkerImpl, source::SourceRange(), payload);
}

ast::Tree finish(TestFixture& fixture) {
  zc::Vector<ast::NodeId> statements;
  fixture.makeSourceFile(ast::NodeId(), fixture.makeNodeList(statements));
  return fixture.finishTree();
}

ImplInventoryEntry entry(ast::NodeId node) {
  zc::Vector<StructuralIdentityParent> parents;
  return ImplInventoryEntry{node, ast::NodeId(), source::SourceRange(), zc::mv(parents)};
}

const identity::ImplHeader& requireHeader(CanonicalImplHeaderProduction& result) {
  ZC_REQUIRE(result.is<identity::ImplHeader>());
  return result.get<identity::ImplHeader>();
}

ZC_TEST("CanonicalImplHeaderProducer alpha-normalizes implementation generic names") {
  TestFixture fixture;
  const auto t = makeNamedType(fixture, "T"_zc);
  const auto u = makeNamedType(fixture, "U"_zc);
  const ast::NodeId tArguments[] = {t};
  const ast::NodeId uArguments[] = {u};
  const auto tImpl = makeStandaloneImpl(
      fixture, makeNamedType(fixture, "Trait"_zc, zc::arrayPtr(tArguments)),
      makeNamedType(fixture, "Box"_zc, zc::arrayPtr(tArguments)), makeGenerics(fixture, "T"_zc));
  const auto uImpl = makeStandaloneImpl(
      fixture, makeNamedType(fixture, "Trait"_zc, zc::arrayPtr(uArguments)),
      makeNamedType(fixture, "Box"_zc, zc::arrayPtr(uArguments)), makeGenerics(fixture, "U"_zc));
  const auto tree = finish(fixture);
  auto tEntry = entry(tImpl);
  auto uEntry = entry(uImpl);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto tResult = CanonicalImplHeaderProducer::produce(tree, tEntry, owners.asPtr());
  auto uResult = CanonicalImplHeaderProducer::produce(tree, uEntry, owners.asPtr());
  ZC_EXPECT(requireHeader(tResult).encode().asPtr() == requireHeader(uResult).encode().asPtr());
}

ZC_TEST("CanonicalImplHeaderProducer merges inline and where obligations") {
  TestFixture fixture;
  const auto a = makeNamedType(fixture, "A"_zc);
  const auto b = makeNamedType(fixture, "B"_zc);
  zc::Vector<ast::NodeId> bounds;
  bounds.add(a);
  bounds.add(b);
  const auto boundList = fixture.makeTypeParameterBoundList(fixture.makeNodeList(bounds));
  const auto inlineGenerics = makeGenerics(fixture, "T"_zc, boundList);

  const auto subject = makeNamedType(fixture, "U"_zc);
  zc::Vector<ast::NodeId> predicates;
  predicates.add(fixture.makeWherePred(subject, a));
  predicates.add(fixture.makeWherePred(subject, b));
  const auto whereClause = fixture.makeWhereClause(fixture.makeNodeList(predicates));
  const auto whereGenerics = makeGenerics(fixture, "U"_zc);
  const auto inlineImpl = makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc),
                                             fixture.makePredefinedTypeExpr(2), inlineGenerics);
  const auto whereImpl =
      makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc),
                         fixture.makePredefinedTypeExpr(2), whereGenerics, whereClause);
  const auto tree = finish(fixture);
  auto inlineEntry = entry(inlineImpl);
  auto whereEntry = entry(whereImpl);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto inlineResult = CanonicalImplHeaderProducer::produce(tree, inlineEntry, owners.asPtr());
  auto whereResult = CanonicalImplHeaderProducer::produce(tree, whereEntry, owners.asPtr());
  const auto& inlineHeader = requireHeader(inlineResult);
  const auto& whereHeader = requireHeader(whereResult);
  ZC_REQUIRE(inlineHeader.obligations().size() == 2);
  ZC_EXPECT(inlineHeader.encode().asPtr() == whereHeader.encode().asPtr());
}

ZC_TEST("CanonicalImplHeaderProducer retains every duplicate bound syntax occurrence") {
  TestFixture fixture;
  const auto first = makeNamedType(fixture, "A"_zc);
  const auto second = makeNamedType(fixture, "A"_zc);
  const auto third = makeNamedType(fixture, "A"_zc);
  zc::Vector<ast::NodeId> bounds;
  bounds.add(first);
  bounds.add(second);
  bounds.add(third);
  const auto boundList = fixture.makeTypeParameterBoundList(fixture.makeNodeList(bounds));
  const auto generics = makeGenerics(fixture, "T"_zc, boundList);
  const auto implementation = makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc),
                                                 fixture.makePredefinedTypeExpr(2), generics);
  const auto tree = finish(fixture);
  auto implementationEntry = entry(implementation);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto result =
      CanonicalImplHeaderProducer::produceWithProvenance(tree, implementationEntry, owners.asPtr());
  ZC_REQUIRE(result.is<CanonicalImplHeaderProvenance>());
  const auto& provenance = result.get<CanonicalImplHeaderProvenance>();
  ZC_EXPECT(provenance.header.obligations().size() == 1);
  ZC_REQUIRE(provenance.boundOccurrences.size() == 3);
  ZC_EXPECT(provenance.boundOccurrences[0].node == first);
  ZC_EXPECT(provenance.boundOccurrences[1].node == second);
  ZC_EXPECT(provenance.boundOccurrences[2].node == third);
}

ZC_TEST("CanonicalImplHeaderProducer rejects a where clause nested in GenericParams") {
  TestFixture fixture;
  const auto t = makeNamedType(fixture, "T"_zc);
  zc::Vector<ast::NodeId> predicates;
  predicates.add(fixture.makeWherePred(t, makeNamedType(fixture, "Trait"_zc)));
  const auto misplacedWhere = fixture.makeWhereClause(fixture.makeNodeList(predicates));
  const auto generics = makeGenerics(fixture, "T"_zc, ast::NodeId(), misplacedWhere);
  const auto impl = makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc),
                                       fixture.makePredefinedTypeExpr(2), generics);
  const auto tree = finish(fixture);
  auto implEntry = entry(impl);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto result = CanonicalImplHeaderProducer::produce(tree, implEntry, owners.asPtr());
  ZC_REQUIRE(result.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(result.get<CanonicalHeaderSyntaxFailure>().kind ==
            CanonicalHeaderSyntaxFailureKind::InvalidBoundSyntax);
}

ZC_TEST("CanonicalImplHeaderProducer admits positive safe marker paths and exact tags") {
  TestFixture fixture;
  const auto selfType = fixture.makePredefinedTypeExpr(2);
  const zc::StringPtr shortPath[] = {"Sendable"_zc};
  const zc::StringPtr qualifiedPath[] = {"marker"_zc, "Sendable"_zc};
  const auto safe = makeMarkerImpl(fixture, zc::arrayPtr(shortPath), selfType);
  const auto qualified = makeMarkerImpl(fixture, zc::arrayPtr(qualifiedPath), selfType);
  const auto unsafe = makeMarkerImpl(fixture, zc::arrayPtr(shortPath), selfType, true);
  const auto negative = makeMarkerImpl(fixture, zc::arrayPtr(shortPath), selfType, false, true);
  const auto invalid = makeMarkerImpl(fixture, zc::arrayPtr(shortPath), selfType, true, true);
  const auto tree = finish(fixture);
  auto safeEntry = entry(safe);
  auto qualifiedEntry = entry(qualified);
  auto unsafeEntry = entry(unsafe);
  auto negativeEntry = entry(negative);
  auto invalidEntry = entry(invalid);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto safeResult = CanonicalImplHeaderProducer::produce(tree, safeEntry, owners.asPtr());
  auto qualifiedResult = CanonicalImplHeaderProducer::produce(tree, qualifiedEntry, owners.asPtr());
  auto unsafeResult = CanonicalImplHeaderProducer::produce(tree, unsafeEntry, owners.asPtr());
  auto negativeResult = CanonicalImplHeaderProducer::produce(tree, negativeEntry, owners.asPtr());
  auto invalidResult = CanonicalImplHeaderProducer::produce(tree, invalidEntry, owners.asPtr());
  ZC_EXPECT(requireHeader(safeResult).safety() == identity::ImplSafety::Safe);
  ZC_EXPECT(requireHeader(safeResult).polarity() == identity::ImplPolarity::Positive);
  ZC_EXPECT(requireHeader(qualifiedResult).trait().name().suffix().size() == 2);
  ZC_EXPECT(requireHeader(unsafeResult).safety() == identity::ImplSafety::Unsafe);
  ZC_EXPECT(requireHeader(negativeResult).polarity() == identity::ImplPolarity::Negative);
  ZC_REQUIRE(invalidResult.is<CanonicalHeaderSyntaxFailure>());
}

ZC_TEST("CanonicalImplHeaderProducer reserves an empty current binder depth") {
  TestFixture fixture;
  const auto ownerGenerics = makeGenerics(fixture, "T"_zc);
  const auto t = makeNamedType(fixture, "T"_zc);
  const ast::NodeId arguments[] = {t};
  const auto impl =
      makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc, zc::arrayPtr(arguments)), t);
  const auto tree = finish(fixture);
  auto implEntry = entry(impl);
  zc::Vector<CanonicalGenericBinderFrame> owners;
  owners.add(CanonicalGenericBinderFrame{ownerGenerics});

  auto result = CanonicalImplHeaderProducer::produce(tree, implEntry, owners.asPtr());
  const auto& header = requireHeader(result);
  ZC_REQUIRE(header.trait().arguments().size() == 1);
  ZC_IF_SOME(named, header.trait().arguments()[0].namedType()) {
    ZC_EXPECT(named.name().root().binderDepth() == 1);
  } else {
    ZC_FAIL_REQUIRE("generic trait argument was not a canonical named type");
  }
}

ZC_TEST("CanonicalImplHeaderProducer rejects generic traits and admits duplicate binder names") {
  TestFixture fixture;
  const auto genericTrait =
      makeStandaloneImpl(fixture, makeNamedType(fixture, "T"_zc), fixture.makePredefinedTypeExpr(2),
                         makeGenerics(fixture, "T"_zc));
  const auto duplicateBinder =
      makeStandaloneImpl(fixture, makeNamedType(fixture, "Trait"_zc),
                         fixture.makePredefinedTypeExpr(2), makeDuplicateGenerics(fixture, "T"_zc));
  const auto tree = finish(fixture);
  auto genericTraitEntry = entry(genericTrait);
  auto duplicateBinderEntry = entry(duplicateBinder);
  zc::Vector<CanonicalGenericBinderFrame> owners;

  auto genericTraitResult =
      CanonicalImplHeaderProducer::produce(tree, genericTraitEntry, owners.asPtr());
  auto duplicateBinderResult =
      CanonicalImplHeaderProducer::produce(tree, duplicateBinderEntry, owners.asPtr());
  ZC_REQUIRE(genericTraitResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(requireHeader(duplicateBinderResult).genericParameters().size() == 2);
}

}  // namespace
}  // namespace zomlang::compiler::binder
