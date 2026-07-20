// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/canonical-header-type-producer.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang::compiler::binder {
namespace {

using tests::TestFixture;

ast::NodeId makeModulePath(TestFixture& fixture, zc::StringPtr name, uint8_t root = 0) {
  zc::Vector<ast::IdentId> names;
  names.add(fixture.builder().internIdent(name));
  const auto segments = fixture.builder().makeIdentList(names.asPtr());
  ast::NodePayload pathPayload;
  pathPayload.words[ast::kModulePathSegmentsFirstWord] = segments.first;
  pathPayload.words[ast::kModulePathSegmentsSizeWord] = segments.size;
  pathPayload.words[ast::kModulePathRootWord] = root;
  return fixture.builder().makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(),
                                    pathPayload);
}

ast::NodeId makeNamedType(TestFixture& fixture, zc::StringPtr name, uint8_t root = 0) {
  ast::NodePayload typePayload;
  typePayload.words[ast::kNamedTypeExprPathWord] = makeModulePath(fixture, name, root).value;
  typePayload.words[ast::kNamedTypeExprArgsFirstWord] = 0;
  typePayload.words[ast::kNamedTypeExprArgsSizeWord] = 0;
  return fixture.builder().makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(),
                                    typePayload);
}

ast::NodeId makeTypeQuery(TestFixture& fixture, zc::StringPtr name) {
  ast::NodePayload payload;
  payload.words[ast::kTypeQueryExprPathWord] = makeModulePath(fixture, name).value;
  return fixture.builder().makeNode(ast::SyntaxKind::TypeQueryExpr, source::SourceRange(), payload);
}

ast::NodeId makeUnaryType(TestFixture& fixture, ast::SyntaxKind kind, ast::NodeId element) {
  ast::NodePayload payload;
  switch (kind) {
    case ast::SyntaxKind::ArrayTypeExpr:
      payload.words[ast::kArrayTypeExprElemWord] = element.value;
      break;
    case ast::SyntaxKind::SliceArrayTypeExpr:
      payload.words[ast::kSliceArrayTypeExprElemWord] = element.value;
      break;
    default:
      ZC_FAIL_REQUIRE("unsupported unary type fixture");
  }
  return fixture.builder().makeNode(kind, source::SourceRange(), payload);
}

ast::NodeId makeFixedArray(TestFixture& fixture, ast::NodeId element, ast::NodeId length) {
  ast::NodePayload payload;
  payload.words[ast::kFixedArrayTypeExprElemWord] = element.value;
  payload.words[ast::kFixedArrayTypeExprLenExprWord] = length.value;
  return fixture.builder().makeNode(ast::SyntaxKind::FixedArrayTypeExpr, source::SourceRange(),
                                    payload);
}

ast::NodeId makeIntegerLiteral(TestFixture& fixture, zc::StringPtr value, uint8_t base) {
  ast::NodePayload payload;
  payload.words[ast::kIntLiteralBaseWord] = base;
  payload.words[ast::kIntLiteralValueWord] = fixture.builder().internBigInt(value).value;
  return fixture.builder().makeNode(ast::SyntaxKind::IntLiteral, source::SourceRange(), payload);
}

ast::NodeId makeFunctionType(TestFixture& fixture, ast::NodeList parameters, ast::NodeId result,
                             ast::NodeId raises) {
  ast::NodePayload payload;
  payload.words[ast::kFunctionTypeExprParamsFirstWord] = parameters.first;
  payload.words[ast::kFunctionTypeExprParamsSizeWord] = parameters.size;
  payload.words[ast::kFunctionTypeExprRetTyWord] = result.value;
  payload.words[ast::kFunctionTypeExprRaisesWord] = raises.value;
  return fixture.builder().makeNode(ast::SyntaxKind::FunctionTypeExpr, source::SourceRange(),
                                    payload);
}

ast::NodeId makeGenericBinder(TestFixture& fixture, zc::StringPtr name) {
  zc::Vector<ast::NodeId> parameters;
  parameters.add(fixture.makeGenericTypeParam(name));
  return fixture.makeGenericParams(fixture.makeNodeList(parameters));
}

ast::Tree finish(TestFixture& fixture) {
  zc::Vector<ast::NodeId> statements;
  fixture.makeSourceFile(ast::NodeId(), fixture.makeNodeList(statements));
  return fixture.finishTree();
}

const identity::CanonicalHeaderTypeSyntax& requireType(CanonicalHeaderTypeProduction& result) {
  ZC_REQUIRE(result.is<identity::CanonicalHeaderTypeSyntax>());
  return result.get<identity::CanonicalHeaderTypeSyntax>();
}

ZC_TEST("CanonicalHeaderTypeProducer alpha-normalizes current generic binder names") {
  TestFixture fixture;
  const auto tBinder = makeGenericBinder(fixture, "T"_zc);
  const auto uBinder = makeGenericBinder(fixture, "U"_zc);
  const auto tType = makeNamedType(fixture, "T"_zc);
  const auto uType = makeNamedType(fixture, "U"_zc);
  const auto tree = finish(fixture);

  zc::Vector<CanonicalGenericBinderFrame> tFrames;
  tFrames.add(CanonicalGenericBinderFrame{tBinder});
  zc::Vector<CanonicalGenericBinderFrame> uFrames;
  uFrames.add(CanonicalGenericBinderFrame{uBinder});
  auto t = CanonicalHeaderTypeProducer::produceType(tree, tType, tFrames.asPtr());
  auto u = CanonicalHeaderTypeProducer::produceType(tree, uType, uFrames.asPtr());

  const auto& tValue = requireType(t);
  const auto& uValue = requireType(u);
  ZC_EXPECT(tValue.encode().asPtr() == uValue.encode().asPtr());
  ZC_EXPECT(zc::encodeHex(tValue.encode().asPtr()) ==
            "0103000000000000000000000000000000000000000000000000"_zc);
}

ZC_TEST("CanonicalHeaderTypeProducer reserves empty owner depth and preserves absolute roots") {
  TestFixture fixture;
  const auto outerBinder = makeGenericBinder(fixture, "T"_zc);
  const auto relative = makeNamedType(fixture, "T"_zc);
  const auto absolute = makeNamedType(fixture, "T"_zc, 1);
  const auto tree = finish(fixture);

  zc::Vector<CanonicalGenericBinderFrame> frames;
  frames.add(CanonicalGenericBinderFrame{ast::NodeId()});
  frames.add(CanonicalGenericBinderFrame{outerBinder});
  auto relativeResult = CanonicalHeaderTypeProducer::produceType(tree, relative, frames.asPtr());
  auto absoluteResult = CanonicalHeaderTypeProducer::produceType(tree, absolute, frames.asPtr());

  const auto& relativeValue = requireType(relativeResult);
  const auto& absoluteValue = requireType(absoluteResult);
  ZC_REQUIRE(relativeValue.namedType() != zc::none);
  ZC_IF_SOME(named, relativeValue.namedType()) {
    ZC_EXPECT(named.name().root().kind() == identity::CanonicalNameRootKind::Generic);
    ZC_EXPECT(named.name().root().binderDepth() == 1);
    ZC_EXPECT(named.name().root().ordinal() == 0);
  }
  ZC_IF_SOME(named, absoluteValue.namedType()) {
    ZC_EXPECT(named.name().root().kind() == identity::CanonicalNameRootKind::Absolute);
  }
  ZC_EXPECT(relativeValue.encode().asPtr() != absoluteValue.encode().asPtr());
}

ZC_TEST("CanonicalHeaderTypeProducer keeps dynamic arrays and slices unequal") {
  TestFixture fixture;
  const auto element = fixture.makePredefinedTypeExpr(2);
  const auto dynamic = makeUnaryType(fixture, ast::SyntaxKind::ArrayTypeExpr, element);
  const auto slice = makeUnaryType(fixture, ast::SyntaxKind::SliceArrayTypeExpr, element);
  const auto tree = finish(fixture);
  zc::Vector<CanonicalGenericBinderFrame> frames;

  auto dynamicResult = CanonicalHeaderTypeProducer::produceType(tree, dynamic, frames.asPtr());
  auto sliceResult = CanonicalHeaderTypeProducer::produceType(tree, slice, frames.asPtr());
  const auto& dynamicValue = requireType(dynamicResult);
  const auto& sliceValue = requireType(sliceResult);
  ZC_EXPECT(dynamicValue.kind() == identity::CanonicalHeaderTypeSyntaxKind::DynamicArray);
  ZC_EXPECT(sliceValue.kind() == identity::CanonicalHeaderTypeSyntaxKind::Slice);
  ZC_EXPECT(dynamicValue.encode().asPtr() != sliceValue.encode().asPtr());
}

ZC_TEST("CanonicalHeaderTypeProducer covers every RFC 0018 type variant") {
  TestFixture fixture;
  const auto named = makeNamedType(fixture, "Value"_zc);
  const auto predefined = fixture.makePredefinedTypeExpr(2);

  zc::Vector<ast::NodeId> functionParameters;
  functionParameters.add(predefined);
  const auto function = fixture.makeFunctionTypeExpr(fixture.makeNodeList(functionParameters),
                                                     fixture.makePredefinedTypeExpr(14));
  const auto unionType = fixture.makeUnionTypeExpr(named, predefined);
  const auto intersection = fixture.makeIntersectionTypeExpr(named, predefined);
  const auto fixed = makeFixedArray(fixture, predefined, fixture.makeIntLiteral(3));
  const auto dynamic = fixture.makeArrayTypeExpr(predefined);
  const auto slice = fixture.makeSliceArrayTypeExpr(predefined);
  const auto optional = fixture.makeOptionalTypeExpr(predefined);
  const auto reference = fixture.makeReferenceTypeExpr(predefined, true);
  const auto pointer = fixture.makeRawPointerTypeExpr(predefined, false);
  const auto query = makeTypeQuery(fixture, "Value"_zc);

  zc::Vector<ast::NodeId> objectMembers;
  objectMembers.add(fixture.makeObjectTypeMember("field"_zc, predefined, true, true));
  const auto object = fixture.makeObjectTypeExpr(fixture.makeNodeList(objectMembers));
  zc::Vector<ast::NodeId> tupleElements;
  tupleElements.add(named);
  tupleElements.add(predefined);
  const auto tuple = fixture.makeTupleTypeExpr(fixture.makeNodeList(tupleElements));
  const auto projection = fixture.makeAssociatedTypeProjectionExpr(named, predefined, "Item"_zc);
  const auto dynamicTrait = fixture.makeDynTypeExpr(named);

  const auto tree = finish(fixture);
  zc::Vector<CanonicalGenericBinderFrame> frames;
  const ast::NodeId nodes[] = {named,   predefined, function,   unionType,   intersection, fixed,
                               dynamic, slice,      optional,   reference,   pointer,      query,
                               object,  tuple,      projection, dynamicTrait};
  const identity::CanonicalHeaderTypeSyntaxKind kinds[] = {
      identity::CanonicalHeaderTypeSyntaxKind::Named,
      identity::CanonicalHeaderTypeSyntaxKind::Predefined,
      identity::CanonicalHeaderTypeSyntaxKind::Function,
      identity::CanonicalHeaderTypeSyntaxKind::Union,
      identity::CanonicalHeaderTypeSyntaxKind::Intersection,
      identity::CanonicalHeaderTypeSyntaxKind::FixedArray,
      identity::CanonicalHeaderTypeSyntaxKind::DynamicArray,
      identity::CanonicalHeaderTypeSyntaxKind::Slice,
      identity::CanonicalHeaderTypeSyntaxKind::Optional,
      identity::CanonicalHeaderTypeSyntaxKind::Reference,
      identity::CanonicalHeaderTypeSyntaxKind::RawPointer,
      identity::CanonicalHeaderTypeSyntaxKind::TypeQuery,
      identity::CanonicalHeaderTypeSyntaxKind::Object,
      identity::CanonicalHeaderTypeSyntaxKind::Tuple,
      identity::CanonicalHeaderTypeSyntaxKind::AssociatedProjection,
      identity::CanonicalHeaderTypeSyntaxKind::Dynamic,
  };
  for (size_t index = 0; index < zc::size(nodes); ++index) {
    auto result = CanonicalHeaderTypeProducer::produceType(tree, nodes[index], frames.asPtr());
    ZC_EXPECT(requireType(result).kind() == kinds[index]);
  }
}

ZC_TEST("CanonicalHeaderTypeProducer evaluates fixed array lengths and rejects non-literals") {
  TestFixture fixture;
  const auto element = fixture.makePredefinedTypeExpr(4);
  const auto length = fixture.makeIntLiteral(42);
  const auto maximumLength = makeIntegerLiteral(fixture, "ffffffffffffffff"_zc, 16);
  const auto overflowLength = makeIntegerLiteral(fixture, "10000000000000000"_zc, 16);
  const auto invalidLength = fixture.makeIdentExpr("N"_zc);
  const auto fixed = makeFixedArray(fixture, element, length);
  const auto maximum = makeFixedArray(fixture, element, maximumLength);
  const auto overflow = makeFixedArray(fixture, element, overflowLength);
  const auto invalid = makeFixedArray(fixture, element, invalidLength);
  const auto tree = finish(fixture);
  zc::Vector<CanonicalGenericBinderFrame> frames;

  auto fixedResult = CanonicalHeaderTypeProducer::produceType(tree, fixed, frames.asPtr());
  auto maximumResult = CanonicalHeaderTypeProducer::produceType(tree, maximum, frames.asPtr());
  auto overflowResult = CanonicalHeaderTypeProducer::produceType(tree, overflow, frames.asPtr());
  auto invalidResult = CanonicalHeaderTypeProducer::produceType(tree, invalid, frames.asPtr());
  const auto& fixedValue = requireType(fixedResult);
  const auto& maximumValue = requireType(maximumResult);
  ZC_EXPECT(fixedValue.kind() == identity::CanonicalHeaderTypeSyntaxKind::FixedArray);
  ZC_EXPECT(fixedValue.fixedArrayLength() == 42);
  ZC_EXPECT(maximumValue.fixedArrayLength() == UINT64_MAX);
  ZC_REQUIRE(overflowResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(overflowResult.get<CanonicalHeaderSyntaxFailure>().kind ==
            CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression);
  ZC_REQUIRE(invalidResult.is<CanonicalHeaderSyntaxFailure>());
  ZC_EXPECT(invalidResult.get<CanonicalHeaderSyntaxFailure>().kind ==
            CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression);
}

ZC_TEST("CanonicalHeaderTypeProducer flattens union members and function raises") {
  TestFixture fixture;
  const auto first = makeNamedType(fixture, "First"_zc);
  const auto second = makeNamedType(fixture, "Second"_zc);
  const auto nested = fixture.makeUnionTypeExpr(first, second);
  const auto repeated = fixture.makeUnionTypeExpr(second, nested);
  zc::Vector<ast::NodeId> noParameters;
  const auto function = makeFunctionType(fixture, fixture.makeNodeList(noParameters),
                                         fixture.makePredefinedTypeExpr(14), repeated);
  const auto tree = finish(fixture);
  zc::Vector<CanonicalGenericBinderFrame> frames;

  auto unionResult = CanonicalHeaderTypeProducer::produceType(tree, repeated, frames.asPtr());
  auto functionResult = CanonicalHeaderTypeProducer::produceType(tree, function, frames.asPtr());
  const auto& unionValue = requireType(unionResult);
  const auto& functionValue = requireType(functionResult);
  ZC_REQUIRE(unionValue.members() != zc::none);
  ZC_IF_SOME(members, unionValue.members()) { ZC_EXPECT(members.size() == 2); }
  ZC_REQUIRE(functionValue.functionRaises() != zc::none);
  ZC_IF_SOME(raises, functionValue.functionRaises()) { ZC_EXPECT(raises.size() == 2); }
}

ZC_TEST("CanonicalHeaderTypeProducer resolves duplicate generic names to the first ordinal") {
  TestFixture fixture;
  zc::Vector<ast::NodeId> parameters;
  parameters.add(fixture.makeGenericTypeParam("T"_zc));
  parameters.add(fixture.makeGenericTypeParam("T"_zc));
  const auto binder = fixture.makeGenericParams(fixture.makeNodeList(parameters));
  const auto type = makeNamedType(fixture, "T"_zc);
  const auto tree = finish(fixture);
  zc::Vector<CanonicalGenericBinderFrame> frames;
  frames.add(CanonicalGenericBinderFrame{binder});

  auto result = CanonicalHeaderTypeProducer::produceType(tree, type, frames.asPtr());
  const auto& produced = requireType(result);
  ZC_REQUIRE(produced.namedType() != zc::none);
  ZC_IF_SOME(named, produced.namedType()) {
    ZC_EXPECT(named.name().root().kind() == identity::CanonicalNameRootKind::Generic);
    ZC_EXPECT(named.name().root().binderDepth() == 0);
    ZC_EXPECT(named.name().root().ordinal() == 0);
  }
}

}  // namespace
}  // namespace zomlang::compiler::binder
