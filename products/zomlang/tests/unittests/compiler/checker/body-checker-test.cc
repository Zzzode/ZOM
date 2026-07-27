// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/body-checker.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/checker/checker-diagnostic-adapter.h"
#include "zomlang/compiler/checker/scalar-literal-facts.h"
#include "zomlang/compiler/type/semantic-type-data.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::body {
namespace {

using namespace tests::test_identity_detail;

class BodyLiteralFixture final {
public:
  BodyLiteralFixture() {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }

    auto createdRegistries = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(createdRegistries != zc::none);
    ZC_IF_SOME(value, createdRegistries) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
    }

    ZC_REQUIRE(registries->collectCompilationUnit(userUnit()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                            zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_IF_SOME(value, snapshot) {
      ZC_REQUIRE(registries->collectSourceFile(zc::mv(value)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeModules() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    auto handle = registries->modules().find(module());
    ZC_REQUIRE(handle != zc::none);
    ZC_IF_SOME(value, handle) { moduleId = value; }
  }

  identity::SourceSpan sourceSpan() const {
    auto result = registries->sourceSnapshots()[0].span(0, 1);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
    ZC_FAIL_REQUIRE("invalid body-checker source span fixture");
  }

  ast::Tree literalTree(ast::SyntaxKind kind, ast::NodePayload payload = {}) const {
    ast::TreeBuilder builder;
    const auto literal = builder.makeNode(kind, source::SourceRange(), payload);
    builder.setRoot(literal);
    return builder.finish();
  }

  ast::Tree integerTree(ast::SyntaxKind kind, zc::StringPtr text, uint8_t base = 10) const {
    ast::TreeBuilder builder;
    ast::NodePayload payload;
    const auto value = builder.internBigInt(text);
    if (kind == ast::SyntaxKind::IntLiteral) {
      payload.words[ast::kIntLiteralBaseWord] = base;
      payload.words[ast::kIntLiteralValueWord] = value.value;
    } else {
      payload.words[ast::kBigIntLiteralValueWord] = value.value;
    }
    const auto literal = builder.makeNode(kind, source::SourceRange(), payload);
    builder.setRoot(literal);
    return builder.finish();
  }

  ast::Tree floatTree(zc::StringPtr text, uint8_t width) const {
    ast::TreeBuilder builder;
    ast::NodePayload payload;
    payload.words[ast::kFloatLiteralExprWidthWord] = width;
    payload.words[ast::kFloatLiteralExprValueWord] = builder.internFloat(text).value;
    const auto literal =
        builder.makeNode(ast::SyntaxKind::FloatLiteralExpr, source::SourceRange(), payload);
    builder.setRoot(literal);
    return builder.finish();
  }

  ast::Tree textTree(ast::SyntaxKind kind, zc::StringPtr text) const {
    ast::TreeBuilder builder;
    ast::NodePayload payload;
    const uint32_t value = builder.internString(text).value;
    if (kind == ast::SyntaxKind::StringLiteralExpr) {
      payload.words[ast::kStringLiteralExprValueWord] = value;
    } else if (kind == ast::SyntaxKind::CharacterLiteralExpr) {
      payload.words[ast::kCharacterLiteralExprValueWord] = value;
    } else {
      payload.words[ast::kNoSubstitutionTemplateLiteralExprValueWord] = value;
    }
    const auto literal = builder.makeNode(kind, source::SourceRange(), payload);
    builder.setRoot(literal);
    return builder.finish();
  }

  ast::Tree stringTree(zc::StringPtr text) const {
    return textTree(ast::SyntaxKind::StringLiteralExpr, text);
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::ModuleId moduleId;
};

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

void expectAccepted(BodyLiteralFixture& fixture, const ast::Tree& tree,
                    signature::CanonicalConstValueTag expectedTag,
                    type::semantic::PrimitiveKind expectedPrimitive,
                    checked::CanonicalLiteral&& expectedLiteral) {
  const auto literal = tree.root();
  checked::CheckedNodeKey key{static_cast<uint32_t>(tree.node(literal).kind), 0,
                              fixture.sourceSpan()};
  auto result = scalar_literal::FactEmitter::emit(
      scalar_literal::FactEmissionInput{fixture.context, fixture.moduleId, tree, literal, key,
                                        fixture.registries->sourceSnapshots()[0].source(),
                                        *fixture.registries, *fixture.semanticTypes});
  ZC_REQUIRE(result.is<scalar_literal::EmittedFacts>());
  auto facts = zc::mv(result).get<scalar_literal::EmittedFacts>();
  ZC_EXPECT(facts.nodeType.key == literal);
  ZC_EXPECT(facts.literal.key == literal);
  ZC_EXPECT(facts.nodeType.value == facts.literal.value.type);
  ZC_EXPECT(facts.literal.value.literal.tag() == expectedTag);
  ZC_EXPECT(facts.nodeType.canonicalRecord.size() == 0);
  ZC_EXPECT(facts.literal.canonicalRecord.size() == 0);

  auto typeLookup = fixture.semanticTypes->get(facts.nodeType.value);
  ZC_REQUIRE(typeLookup.is<type::SemanticTypeLookup>());
  const auto& data = typeLookup.get<type::SemanticTypeLookup>().data();
  ZC_REQUIRE(data.is<type::semantic::PrimitiveTypeData>());
  ZC_EXPECT(data.get<type::semantic::PrimitiveTypeData>().kind == expectedPrimitive);

  auto actualEncoding = signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      facts.literal.value.literal, fixture.moduleId, *fixture.registries, *fixture.semanticTypes);
  auto expectedEncoding = signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValue(
      expectedLiteral, fixture.moduleId, *fixture.registries, *fixture.semanticTypes);
  ZC_REQUIRE(actualEncoding != zc::none);
  ZC_REQUIRE(expectedEncoding != zc::none);
  ZC_IF_SOME(actual, actualEncoding) {
    ZC_IF_SOME(expected, expectedEncoding) {
      ZC_EXPECT(sameBytes(actual.asPtr(), expected.asPtr()));
    }
  }
}

void expectInvariantRejectedWithoutPublication(BodyLiteralFixture& fixture, const ast::Tree& tree) {
  const size_t sizeBefore = fixture.semanticTypes->size();
  const auto literal = tree.root();
  checked::CheckedNodeKey key{static_cast<uint32_t>(tree.node(literal).kind), 0,
                              fixture.sourceSpan()};
  auto result = scalar_literal::FactEmitter::emit(
      scalar_literal::FactEmissionInput{fixture.context, fixture.moduleId, tree, literal, key,
                                        fixture.registries->sourceSnapshots()[0].source(),
                                        *fixture.registries, *fixture.semanticTypes});
  ZC_REQUIRE(result.is<checked::CheckedFactsInvariantRejected>());
  ZC_EXPECT(fixture.semanticTypes->size() == sizeBefore);
  const auto& rejection = result.get<checked::CheckedFactsInvariantRejected>();
  ZC_REQUIRE(rejection.failures.size() == 1);
  const auto& failure = rejection.failures[0].variant();
  ZC_REQUIRE(failure.is<signature::CheckerInvariantFact>());
  ZC_EXPECT(failure.get<signature::CheckerInvariantFact>().kind ==
            signature::CheckerInvariantKind::InvalidFact);
}

void expectSourceRejectedWithoutPublication(BodyLiteralFixture& fixture, const ast::Tree& tree,
                                            type::semantic::PrimitiveKind expectedTarget,
                                            zc::StringPtr expectedTargetText) {
  const size_t sizeBefore = fixture.semanticTypes->size();
  const auto literal = tree.root();
  checked::CheckedNodeKey key{static_cast<uint32_t>(tree.node(literal).kind), 0,
                              fixture.sourceSpan()};
  auto result = scalar_literal::FactEmitter::emit(
      scalar_literal::FactEmissionInput{fixture.context, fixture.moduleId, tree, literal, key,
                                        fixture.registries->sourceSnapshots()[0].source(),
                                        *fixture.registries, *fixture.semanticTypes});
  ZC_REQUIRE(result.is<checked::CheckedFactsSourceRejected>());
  ZC_EXPECT(fixture.semanticTypes->size() == sizeBefore);
  const auto& rejection = result.get<checked::CheckedFactsSourceRejected>();
  ZC_REQUIRE(rejection.failures.size() == 1);
  ZC_EXPECT(rejection.failures[0].diagnostic == checked::CheckerErrorId::BodyLiteralOutOfRange());
  ZC_EXPECT(rejection.failures[0].stage == checked::CheckerDiagnosticStage::Body);
  ZC_EXPECT(rejection.failures[0].producer == checked::CheckerDiagnosticProducer::Constant);
  ZC_EXPECT(rejection.failures[0].primaryNode == literal);
  ZC_EXPECT(rejection.failures[0].arguments.size() == 2);
  const auto& target = rejection.failures[0].arguments[1].variant();
  ZC_REQUIRE(target.is<checked::PrimitiveTypeDisplayArg>());
  ZC_EXPECT(target.get<checked::PrimitiveTypeDisplayArg>().kind == expectedTarget);
  ZC_EXPECT(renderCheckerDisplayArgument(rejection.failures[0].arguments[1], *fixture.registries,
                                         *fixture.semanticTypes) == expectedTargetText);
  const auto& policy = rejection.failures[0].recoveryPolicy.variant();
  ZC_REQUIRE(policy.is<checked::CreateRootRecoveryPolicy>());
  ZC_EXPECT(policy.get<checked::CreateRootRecoveryPolicy>().recoveryClass ==
            checked::CheckerRecoveryClass::FailedInference);
  ZC_EXPECT(policy.get<checked::CreateRootRecoveryPolicy>().suppressIfChildRecovery);
  ZC_EXPECT(rejection.failures[0].recovery == zc::none);
}

}  // namespace

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalNullLiteral") {
  BodyLiteralFixture fixture;
  auto tree = fixture.literalTree(ast::SyntaxKind::NullLiteral);
  expectAccepted(fixture, tree, signature::CanonicalConstValueTag::Null,
                 type::semantic::PrimitiveKind::Null, checked::CanonicalLiteral::null());
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedNullPayload") {
  BodyLiteralFixture fixture;
  ast::NodePayload payload;
  payload.words[0] = 1;
  auto tree = fixture.literalTree(ast::SyntaxKind::NullLiteral, payload);
  expectInvariantRejectedWithoutPublication(fixture, tree);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalBoolLiteral") {
  BodyLiteralFixture fixture;
  ast::NodePayload payload;
  payload.words[ast::kBoolLiteralValueWord] = 1;
  auto tree = fixture.literalTree(ast::SyntaxKind::BoolLiteral, payload);
  expectAccepted(fixture, tree, signature::CanonicalConstValueTag::Bool,
                 type::semantic::PrimitiveKind::Bool, checked::CanonicalLiteral::boolean(true));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedBoolPayload") {
  BodyLiteralFixture fixture;
  ast::NodePayload payload;
  payload.words[ast::kBoolLiteralValueWord] = 2;
  auto tree = fixture.literalTree(ast::SyntaxKind::BoolLiteral, payload);
  expectInvariantRejectedWithoutPublication(fixture, tree);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.AppliesCanonicalIntegerDefaultLadder") {
  BodyLiteralFixture fixture;
  auto zeroTree = fixture.integerTree(ast::SyntaxKind::IntLiteral, "0"_zcc);
  expectAccepted(fixture, zeroTree, signature::CanonicalConstValueTag::Integer,
                 type::semantic::PrimitiveKind::I32,
                 checked::CanonicalLiteral::integer(signature::CanonicalInteger{
                     signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)}));

  auto i32Tree = fixture.integerTree(ast::SyntaxKind::IntLiteral, "2147483647"_zcc);
  uint8_t i32Bytes[] = {0x7f, 0xff, 0xff, 0xff};
  expectAccepted(
      fixture, i32Tree, signature::CanonicalConstValueTag::Integer,
      type::semantic::PrimitiveKind::I32,
      checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(zc::arrayPtr(i32Bytes))}));

  auto i64Tree = fixture.integerTree(ast::SyntaxKind::IntLiteral, "2147483648"_zcc);
  uint8_t i64Bytes[] = {0x80, 0x00, 0x00, 0x00};
  expectAccepted(
      fixture, i64Tree, signature::CanonicalConstValueTag::Integer,
      type::semantic::PrimitiveKind::I64,
      checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(zc::arrayPtr(i64Bytes))}));

  auto u64Tree = fixture.integerTree(ast::SyntaxKind::IntLiteral, "9223372036854775808"_zcc);
  uint8_t u64Bytes[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  expectAccepted(
      fixture, u64Tree, signature::CanonicalConstValueTag::Integer,
      type::semantic::PrimitiveKind::U64,
      checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(zc::arrayPtr(u64Bytes))}));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedIntPayloadAndPoolHandle") {
  BodyLiteralFixture fixture;
  auto malformedText = fixture.integerTree(ast::SyntaxKind::IntLiteral, "12z"_zcc);
  expectInvariantRejectedWithoutPublication(fixture, malformedText);

  auto malformedBase = fixture.integerTree(ast::SyntaxKind::IntLiteral, "12"_zcc, 3);
  expectInvariantRejectedWithoutPublication(fixture, malformedBase);

  ast::NodePayload payload;
  payload.words[ast::kIntLiteralBaseWord] = 10;
  payload.words[ast::kIntLiteralValueWord] = 2;
  auto invalidHandle = fixture.literalTree(ast::SyntaxKind::IntLiteral, payload);
  expectInvariantRejectedWithoutPublication(fixture, invalidHandle);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsOutOfRangeIntAsSourceFailure") {
  BodyLiteralFixture fixture;
  auto tree = fixture.integerTree(ast::SyntaxKind::IntLiteral, "18446744073709551616"_zcc);
  expectSourceRejectedWithoutPublication(fixture, tree, type::semantic::PrimitiveKind::U64,
                                         "u64"_zcc);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalFloatWidths") {
  BodyLiteralFixture fixture;
  auto f32Tree = fixture.floatTree("1.5"_zcc, 32);
  expectAccepted(fixture, f32Tree, signature::CanonicalConstValueTag::Float,
                 type::semantic::PrimitiveKind::F32,
                 checked::CanonicalLiteral::float32(0x3fc00000));

  auto f64Tree = fixture.floatTree("1.5"_zcc, 64);
  expectAccepted(fixture, f64Tree, signature::CanonicalConstValueTag::Float,
                 type::semantic::PrimitiveKind::F64,
                 checked::CanonicalLiteral::float64(0x3ff8000000000000));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedFloatPayload") {
  BodyLiteralFixture fixture;
  auto malformedWidth = fixture.floatTree("1.5"_zcc, 16);
  expectInvariantRejectedWithoutPublication(fixture, malformedWidth);

  auto malformedText = fixture.floatTree("1.2.3"_zcc, 64);
  expectInvariantRejectedWithoutPublication(fixture, malformedText);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsOutOfRangeFloatAsSourceFailure") {
  BodyLiteralFixture fixture;
  auto overflow = fixture.floatTree("1e9999"_zcc, 64);
  expectSourceRejectedWithoutPublication(fixture, overflow, type::semantic::PrimitiveKind::F64,
                                         "f64"_zcc);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalBigIntLiteral") {
  BodyLiteralFixture fixture;
  auto tree = fixture.integerTree(ast::SyntaxKind::BigIntLiteral, "18446744073709551615n"_zcc);
  uint8_t magnitude[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  expectAccepted(
      fixture, tree, signature::CanonicalConstValueTag::Integer, type::semantic::PrimitiveKind::U64,
      checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(zc::arrayPtr(magnitude))}));

  auto hexadecimal = fixture.integerTree(ast::SyntaxKind::BigIntLiteral, "0xffn"_zcc);
  expectAccepted(
      fixture, hexadecimal, signature::CanonicalConstValueTag::Integer,
      type::semantic::PrimitiveKind::I32,
      checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(1, uint8_t{0xff})}));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedBigIntPayload") {
  BodyLiteralFixture fixture;
  auto missingSuffix = fixture.integerTree(ast::SyntaxKind::BigIntLiteral, "42"_zcc);
  expectInvariantRejectedWithoutPublication(fixture, missingSuffix);

  auto malformedDigits = fixture.integerTree(ast::SyntaxKind::BigIntLiteral, "0xggn"_zcc);
  expectInvariantRejectedWithoutPublication(fixture, malformedDigits);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsOutOfRangeBigIntAsSourceFailure") {
  BodyLiteralFixture fixture;
  auto tree = fixture.integerTree(ast::SyntaxKind::BigIntLiteral, "18446744073709551616n"_zcc);
  expectSourceRejectedWithoutPublication(fixture, tree, type::semantic::PrimitiveKind::U64,
                                         "u64"_zcc);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalUtf8StringLiteral") {
  BodyLiteralFixture fixture;
  auto tree = fixture.stringTree("zom"_zcc);
  uint8_t bytes[] = {'z', 'o', 'm'};
  expectAccepted(fixture, tree, signature::CanonicalConstValueTag::String,
                 type::semantic::PrimitiveKind::Str,
                 checked::CanonicalLiteral::string(zc::heapArray<uint8_t>(zc::arrayPtr(bytes))));

  auto emptyTree = fixture.stringTree(""_zcc);
  expectAccepted(fixture, emptyTree, signature::CanonicalConstValueTag::String,
                 type::semantic::PrimitiveKind::Str,
                 checked::CanonicalLiteral::string(zc::heapArray<uint8_t>(0)));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedStringPayload") {
  BodyLiteralFixture fixture;
  ast::NodePayload extraWordPayload;
  extraWordPayload.words[ast::kStringLiteralExprValueWord] = 0;
  extraWordPayload.words[ast::kStringLiteralExprPayloadWordCount] = 1;
  auto extraWord = fixture.literalTree(ast::SyntaxKind::StringLiteralExpr, extraWordPayload);
  expectInvariantRejectedWithoutPublication(fixture, extraWord);

  const char invalidUtf8Bytes[] = {static_cast<char>(0xc0), static_cast<char>(0x80), '\0'};
  auto invalidUtf8 = fixture.stringTree(zc::StringPtr(invalidUtf8Bytes, 2));
  expectInvariantRejectedWithoutPublication(fixture, invalidUtf8);

  ast::NodePayload invalidHandlePayload;
  invalidHandlePayload.words[ast::kStringLiteralExprValueWord] = 2;
  auto invalidHandle =
      fixture.literalTree(ast::SyntaxKind::StringLiteralExpr, invalidHandlePayload);
  expectInvariantRejectedWithoutPublication(fixture, invalidHandle);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalCharacterLiteral") {
  BodyLiteralFixture fixture;
  auto ascii = fixture.textTree(ast::SyntaxKind::CharacterLiteralExpr, "z"_zcc);
  expectAccepted(fixture, ascii, signature::CanonicalConstValueTag::Char,
                 type::semantic::PrimitiveKind::Char,
                 checked::CanonicalLiteral::character(static_cast<uint32_t>('z')));

  auto unicode = fixture.textTree(ast::SyntaxKind::CharacterLiteralExpr, "\xf0\x9f\x98\x80"_zcc);
  expectAccepted(fixture, unicode, signature::CanonicalConstValueTag::Char,
                 type::semantic::PrimitiveKind::Char,
                 checked::CanonicalLiteral::character(0x1f600));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedCharacterLiteral") {
  BodyLiteralFixture fixture;
  auto empty = fixture.textTree(ast::SyntaxKind::CharacterLiteralExpr, ""_zcc);
  expectInvariantRejectedWithoutPublication(fixture, empty);

  auto multiple = fixture.textTree(ast::SyntaxKind::CharacterLiteralExpr, "ab"_zcc);
  expectInvariantRejectedWithoutPublication(fixture, multiple);
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalNoSubstitutionTemplateLiteral") {
  BodyLiteralFixture fixture;
  auto tree = fixture.textTree(ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr, "zom"_zcc);
  uint8_t bytes[] = {'z', 'o', 'm'};
  expectAccepted(fixture, tree, signature::CanonicalConstValueTag::String,
                 type::semantic::PrimitiveKind::Str,
                 checked::CanonicalLiteral::string(zc::heapArray<uint8_t>(zc::arrayPtr(bytes))));
}

ZC_TEST("ScalarLiteralBodyFactEmitter.EmitsCanonicalUnitLiteral") {
  BodyLiteralFixture fixture;
  auto tree = fixture.literalTree(ast::SyntaxKind::UnitLiteral);
  expectAccepted(fixture, tree, signature::CanonicalConstValueTag::Unit,
                 type::semantic::PrimitiveKind::Unit, checked::CanonicalLiteral::unit());
}

ZC_TEST("ScalarLiteralBodyFactEmitter.RejectsMalformedUnitPayload") {
  BodyLiteralFixture fixture;
  ast::NodePayload payload;
  payload.words[0] = 1;
  auto tree = fixture.literalTree(ast::SyntaxKind::UnitLiteral, payload);
  expectInvariantRejectedWithoutPublication(fixture, tree);
}

}  // namespace zomlang::compiler::checker::body
