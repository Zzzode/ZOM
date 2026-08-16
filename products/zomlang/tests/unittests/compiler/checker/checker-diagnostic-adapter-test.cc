// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checker-diagnostic-adapter.h"

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/checker/checker-authority-test-fixture.h"

namespace zomlang::compiler::checker {
namespace {

class RendererFixture final {
public:
  RendererFixture() : renderSession("class RecoveryOwner<T> {}\n"_zc) {
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }
    auto createdInterner = identity::IdentityInternerSet::create(factory, context);
    ZC_REQUIRE(createdInterner != zc::none);
    ZC_IF_SOME(value, createdInterner) {
      identities = zc::heap<identity::IdentityInternerSet>(zc::mv(value));
    }
    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *identities);
    }
    i32 = intern(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
    str = intern(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Str}));
  }

  identity::SemanticTypeId intern(type::semantic::TypeData&& data) {
    auto canonical = semanticTypes->canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result = semanticTypes->intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  zc::String render(const checked::CheckerDisplayArgument& argument) const {
    return renderCheckerDisplayArgument(argument, renderSession.identityAuthority(),
                                        renderSession.semanticTypes());
  }

  identity::SemanticTypeId renderIntern(type::semantic::TypeData&& data) const {
    auto canonical = renderSession.semanticTypes().canonicalizeClosed(zc::mv(data));
    ZC_REQUIRE(canonical.is<type::semantic::CanonicalTypeData>());
    auto result = renderSession.semanticTypes().intern(
        zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    ZC_REQUIRE(result.is<type::SemanticTypeInterned>());
    return result.get<type::SemanticTypeInterned>().id;
  }

  identity::DefId renderDefinition() const noexcept { return renderSession.owner(); }

  const identity::GenericParameterKey& renderGenericParameter() const {
    for (const auto& module : renderSession.identityAuthority().modules()) {
      for (const auto& parameter : module.definitions().identities().genericParameters()) {
        return parameter.key();
      }
    }
    ZC_UNREACHABLE
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::IdentityInternerSet> identities;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::SemanticTypeId i32;
  identity::SemanticTypeId str;
  tests::checker_fixture::CheckerAuthoritySession renderSession;
};

identity::SemanticIdentifier identifier(zc::StringPtr text) {
  auto result = identity::SemanticIdentifier::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid diagnostic identifier fixture");
}

checked::CheckerDisplayArgument integerArgument(
    uint8_t high, uint8_t low, signature::IntegerSign sign = signature::IntegerSign::NonNegative) {
  uint8_t bytes[] = {high, low};
  return checked::CheckerDisplayArgument(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::integer(
          signature::CanonicalInteger{sign, zc::heapArray<uint8_t>(zc::arrayPtr(bytes))})});
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

}  // namespace

ZC_TEST("CheckerDiagnosticAdapter.RendersEveryStructuredDisplayVariant") {
  RendererFixture fixture;
  zc::Vector<identity::SemanticTypeId> elements;
  const auto i32 = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
  const auto str = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Str}));
  elements.add(i32);
  elements.add(str);
  const auto tuple = fixture.renderIntern(
      type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(elements)}));
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  checked::CheckerDisplayArgument typeArgument(checked::TypeDisplayArg{tuple, zc::mv(noAlias)});
  ZC_EXPECT(fixture.render(typeArgument) == "(i32, str)"_zc);

  const auto genericType = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::TypeParameterTypeData{fixture.renderGenericParameter().clone()}));
  zc::Maybe<identity::SemanticIdentifier> noGenericAlias;
  checked::CheckerDisplayArgument genericArgument(
      checked::TypeDisplayArg{genericType, zc::mv(noGenericAlias)});
  ZC_EXPECT(fixture.render(genericArgument) == "<type-parameter#0>"_zc);

  checked::CheckerDisplayArgument primitiveTypeArgument(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::U64});
  ZC_EXPECT(fixture.render(primitiveTypeArgument) == "u64"_zc);

  checked::CheckerDisplayArgument definitionArgument(
      checked::DefinitionDisplayArg{fixture.renderDefinition()});
  ZC_EXPECT(fixture.render(definitionArgument) == "app::app::app::RecoveryOwner"_zc);

  checked::CheckerDisplayArgument identifierArgument(
      checked::IdentifierDisplayArg{identifier("value"_zc)});
  ZC_EXPECT(fixture.render(identifierArgument) == "`value`"_zc);

  checked::CheckerDisplayArgument countArgument(checked::CountDisplayArg{7});
  ZC_EXPECT(fixture.render(countArgument) == "7"_zc);

  checked::CheckerDisplayArgument constraintArgument(
      checked::ConstraintContextDisplayArg{checked::ConstraintReasonKind::ConditionalJoin});
  ZC_EXPECT(fixture.render(constraintArgument) == "conditional-join"_zc);

  checked::CheckerDisplayArgument operatorArgument(
      checked::OperatorDisplayArg{OperatorKind(PrimitiveOperation::Add)});
  ZC_EXPECT(fixture.render(operatorArgument) == "+"_zc);

  auto literalArgument = integerArgument(0x01, 0x00);
  ZC_EXPECT(fixture.render(literalArgument) == "256"_zc);

  zc::Vector<checked::PatternConstructor> patterns;
  patterns.add(checked::PatternConstructor(checked::WildcardPattern{}));
  patterns.add(checked::PatternConstructor(
      checked::LiteralPattern{checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(1, uint8_t{7})})}));
  patterns.add(checked::PatternConstructor(checked::TuplePattern{2}));
  zc::Vector<identity::SemanticIdentifier> fields;
  fields.add(identifier("left"_zc));
  fields.add(identifier("right"_zc));
  patterns.add(checked::PatternConstructor(checked::ObjectPattern{zc::mv(fields)}));
  patterns.add(checked::PatternConstructor(checked::UnionAlternativePattern{1, i32}));
  patterns.add(
      checked::PatternConstructor(checked::EnumVariantPattern{fixture.renderDefinition()}));
  patterns.add(checked::PatternConstructor(checked::NominalPattern{fixture.renderDefinition()}));
  checked::CheckerDisplayArgument patternsArgument(checked::PatternsDisplayArg{zc::mv(patterns)});
  ZC_EXPECT(fixture.render(patternsArgument) ==
            "_, 7, (..arity=2), {left,right}, union[1]: i32, "
            "app::app::app::RecoveryOwner, app::app::app::RecoveryOwner"_zc);
}

ZC_TEST("CheckerDisplayArgumentCodec.DiscriminatesAndValidatesPrimitiveTypePayload") {
  RendererFixture fixture;
  checked::CheckerDisplayArgument u64(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::U64});
  checked::CheckerDisplayArgument f64(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::F64});
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  checked::CheckerDisplayArgument internedU64(checked::TypeDisplayArg{
      fixture.renderIntern(type::semantic::TypeData(
          type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::U64})),
      zc::mv(noAlias)});
  auto u64Record = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      u64, fixture.renderSession.module(), fixture.renderSession.identityAuthority(),
      fixture.renderSession.semanticTypes());
  auto f64Record = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      f64, fixture.renderSession.module(), fixture.renderSession.identityAuthority(),
      fixture.renderSession.semanticTypes());
  auto internedRecord = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      internedU64, fixture.renderSession.module(), fixture.renderSession.identityAuthority(),
      fixture.renderSession.semanticTypes());
  ZC_REQUIRE(u64Record != zc::none);
  ZC_REQUIRE(f64Record != zc::none);
  ZC_REQUIRE(internedRecord != zc::none);
  ZC_IF_SOME(left, u64Record) {
    ZC_IF_SOME(right, f64Record) { ZC_EXPECT(!sameBytes(left.asPtr(), right.asPtr())); }
    ZC_IF_SOME(otherVariant, internedRecord) {
      ZC_EXPECT(!sameBytes(left.asPtr(), otherVariant.asPtr()));
    }
  }

  checked::CheckerDisplayArgument invalid(
      checked::PrimitiveTypeDisplayArg{static_cast<type::semantic::PrimitiveKind>(0xff)});
  ZC_EXPECT(checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
                invalid, fixture.renderSession.module(), fixture.renderSession.identityAuthority(),
                fixture.renderSession.semanticTypes()) == zc::none);
}

ZC_TEST("CheckerDiagnosticAdapter.RendersCanonicalScalarValues") {
  RendererFixture fixture;
  auto negative = integerArgument(0x01, 0x00, signature::IntegerSign::Negative);
  ZC_EXPECT(fixture.render(negative) == "-256"_zc);

  checked::CheckerDisplayArgument floating(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::float32(0x3fc00000)});
  ZC_EXPECT(fixture.render(floating) == "1.5"_zc);

  checked::CheckerDisplayArgument boolean(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::boolean(true)});
  ZC_EXPECT(fixture.render(boolean) == "true"_zc);

  checked::CheckerDisplayArgument character(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::character(0x202e)});
  ZC_EXPECT(fixture.render(character) == "'\\u{202E}'"_zc);

  uint8_t bytes[] = {'a', '"', '\n'};
  checked::CheckerDisplayArgument string(checked::LiteralDisplayArg{
      checked::CanonicalLiteral::string(zc::heapArray<uint8_t>(zc::arrayPtr(bytes)))});
  ZC_EXPECT(fixture.render(string) == "\"a\\\"\\u{A}\""_zc);

  checked::CheckerDisplayArgument null(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::null()});
  ZC_EXPECT(fixture.render(null) == "null"_zc);
  checked::CheckerDisplayArgument unit(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::unit()});
  ZC_EXPECT(fixture.render(unit) == "unit"_zc);

  checked::CheckerDisplayArgument zero(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::integer(signature::CanonicalInteger{
          signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(0)})});
  ZC_EXPECT(fixture.render(zero) == "0"_zc);
  checked::CheckerDisplayArgument floating64(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::float64(0x4004000000000000)});
  ZC_EXPECT(fixture.render(floating64) == "2.5"_zc);
  checked::CheckerDisplayArgument falseValue(
      checked::LiteralDisplayArg{checked::CanonicalLiteral::boolean(false)});
  ZC_EXPECT(fixture.render(falseValue) == "false"_zc);
}

ZC_TEST("CheckerDiagnosticAdapter.RendersAggregateCanonicalValues") {
  RendererFixture fixture;
  const auto literal = [&](signature::CanonicalConstValue&& value) {
    return fixture.render(
        checked::CheckerDisplayArgument(checked::LiteralDisplayArg{zc::mv(value)}));
  };
  const auto integer = [](uint8_t value) {
    return signature::CanonicalConstValue::integer(signature::CanonicalInteger{
        signature::IntegerSign::NonNegative, zc::heapArray<uint8_t>(1, value)});
  };

  zc::Vector<signature::CanonicalConstValue> tupleValues;
  tupleValues.add(integer(1));
  tupleValues.add(integer(2));
  ZC_EXPECT(literal(signature::CanonicalConstValue::tuple(zc::mv(tupleValues))) == "(1, 2)"_zc);

  zc::Vector<signature::CanonicalConstValue> arrayValues;
  arrayValues.add(signature::CanonicalConstValue::boolean(true));
  arrayValues.add(signature::CanonicalConstValue::boolean(false));
  ZC_EXPECT(literal(signature::CanonicalConstValue::array(zc::mv(arrayValues))) ==
            "[true, false]"_zc);

  zc::Vector<signature::ConstObjectField> fields;
  fields.add(signature::ConstObjectField{identifier("field"_zc), integer(3)});
  ZC_EXPECT(literal(signature::CanonicalConstValue::object(zc::mv(fields))) == "{field: 3}"_zc);

  zc::Vector<signature::CanonicalConstValue> payload;
  payload.add(integer(4));
  ZC_EXPECT(literal(signature::CanonicalConstValue::enumeration(fixture.renderDefinition(),
                                                                zc::mv(payload))) ==
            "app::app::app::RecoveryOwner(4)"_zc);
}

ZC_TEST("CheckerDiagnosticAdapter.RendersEveryPrimitiveSpelling") {
  RendererFixture fixture;
  struct PrimitiveExpectation final {
    type::semantic::PrimitiveKind kind;
    zc::StringPtr text;
  };
  const PrimitiveExpectation expectations[] = {
      {type::semantic::PrimitiveKind::I8, "i8"_zc},
      {type::semantic::PrimitiveKind::I16, "i16"_zc},
      {type::semantic::PrimitiveKind::I32, "i32"_zc},
      {type::semantic::PrimitiveKind::I64, "i64"_zc},
      {type::semantic::PrimitiveKind::U8, "u8"_zc},
      {type::semantic::PrimitiveKind::U16, "u16"_zc},
      {type::semantic::PrimitiveKind::U32, "u32"_zc},
      {type::semantic::PrimitiveKind::U64, "u64"_zc},
      {type::semantic::PrimitiveKind::Isize, "isize"_zc},
      {type::semantic::PrimitiveKind::Usize, "usize"_zc},
      {type::semantic::PrimitiveKind::F32, "f32"_zc},
      {type::semantic::PrimitiveKind::F64, "f64"_zc},
      {type::semantic::PrimitiveKind::Bool, "bool"_zc},
      {type::semantic::PrimitiveKind::Char, "char"_zc},
      {type::semantic::PrimitiveKind::Str, "str"_zc},
      {type::semantic::PrimitiveKind::Unit, "unit"_zc},
      {type::semantic::PrimitiveKind::Never, "never"_zc},
      {type::semantic::PrimitiveKind::Any, "any"_zc},
      {type::semantic::PrimitiveKind::Null, "null"_zc},
  };
  for (const auto expectation : expectations) {
    ZC_EXPECT(fixture.render(checked::CheckerDisplayArgument(
                  checked::PrimitiveTypeDisplayArg{expectation.kind})) == expectation.text);
  }
}

ZC_TEST("CheckerDiagnosticAdapter.RendersEveryConstraintReason") {
  RendererFixture fixture;
  struct ReasonExpectation final {
    checked::ConstraintReasonKind reason;
    zc::StringPtr text;
  };
  const ReasonExpectation expectations[] = {
      {checked::ConstraintReasonKind::Annotation, "annotation"_zc},
      {checked::ConstraintReasonKind::Initializer, "initializer"_zc},
      {checked::ConstraintReasonKind::Argument, "argument"_zc},
      {checked::ConstraintReasonKind::Return, "return"_zc},
      {checked::ConstraintReasonKind::Assignment, "assignment"_zc},
      {checked::ConstraintReasonKind::ConditionalJoin, "conditional-join"_zc},
      {checked::ConstraintReasonKind::Operator, "operator"_zc},
      {checked::ConstraintReasonKind::Projection, "projection"_zc},
      {checked::ConstraintReasonKind::Bound, "bound"_zc},
      {checked::ConstraintReasonKind::Raises, "raises"_zc},
      {checked::ConstraintReasonKind::Pattern, "pattern"_zc},
      {checked::ConstraintReasonKind::Cast, "cast"_zc},
  };
  for (const auto expectation : expectations) {
    ZC_EXPECT(fixture.render(checked::CheckerDisplayArgument(
                  checked::ConstraintContextDisplayArg{expectation.reason})) == expectation.text);
  }
}

ZC_TEST("CheckerDiagnosticAdapter.RendersEveryStructuredSemanticType") {
  RendererFixture fixture;
  const auto i32 = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
  const auto str = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Str}));
  const auto renderType = [&](type::semantic::TypeData&& data) {
    zc::Maybe<identity::SemanticIdentifier> noAlias;
    return fixture.render(checked::CheckerDisplayArgument(
        checked::TypeDisplayArg{fixture.renderIntern(zc::mv(data)), zc::mv(noAlias)}));
  };

  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::DynamicArrayTypeData{i32})) ==
            "[i32]"_zc);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::SliceTypeData{i32})) ==
            "slice<i32>"_zc);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::FixedArrayTypeData{i32, 4})) ==
            "[i32; 4]"_zc);

  zc::Vector<identity::SemanticTypeId> parameters;
  parameters.add(i32);
  zc::Maybe<identity::SemanticTypeId> raises(i32);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::FunctionTypeData{
                zc::mv(parameters), str, zc::mv(raises)})) == "(i32) -> str raises i32"_zc);

  zc::Vector<identity::SemanticTypeId> nominalArguments;
  nominalArguments.add(i32);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::NominalTypeData{
                fixture.renderDefinition(), zc::mv(nominalArguments)})) ==
            "app::app::app::RecoveryOwner<i32>"_zc);

  zc::Vector<identity::SemanticTypeId> unionAlternatives;
  unionAlternatives.add(i32);
  unionAlternatives.add(str);
  ZC_EXPECT(renderType(type::semantic::TypeData(
                type::semantic::UnionTypeData{zc::mv(unionAlternatives)})) == "i32 | str"_zc);

  zc::Vector<identity::SemanticTypeId> intersectionConjuncts;
  intersectionConjuncts.add(i32);
  intersectionConjuncts.add(str);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::IntersectionTypeData{
                zc::mv(intersectionConjuncts)})) == "i32 & str"_zc);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::ReferenceTypeData{
                type::semantic::Mutability::Mutable, i32})) == "&mut i32"_zc);
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::RawPointerTypeData{
                type::semantic::Mutability::Const, i32})) == "*const i32"_zc);

  zc::Vector<type::semantic::ObjectFieldData> fields;
  fields.add(type::semantic::ObjectFieldData{identifier("field"_zc), i32,
                                             type::semantic::Mutability::Mutable,
                                             type::semantic::FieldPresence::Optional});
  ZC_EXPECT(renderType(type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(fields)})) ==
            "{mut field?: i32}"_zc);

  auto nested = i32;
  for (uint32_t depth = 0; depth < 12; ++depth) {
    nested = fixture.renderIntern(
        type::semantic::TypeData(type::semantic::DynamicArrayTypeData{nested}));
  }
  zc::Maybe<identity::SemanticIdentifier> noNestedAlias;
  ZC_EXPECT(fixture.render(checked::CheckerDisplayArgument(checked::TypeDisplayArg{
                nested, zc::mv(noNestedAlias)})) == "[[[[[[[[[[[[...]]]]]]]]]]]]"_zc);
}

ZC_TEST("CheckerDiagnosticAdapter.EscapesAndTruncatesSourceHints") {
  RendererFixture fixture;
  const char unicodeIdentifierBytes[] = {
      'c', 'a', 'f', static_cast<char>(0xc3), static_cast<char>(0xa9), '\0'};
  checked::CheckerDisplayArgument quoted(
      checked::IdentifierDisplayArg{identifier(zc::StringPtr(unicodeIdentifierBytes, 5))});
  ZC_EXPECT(fixture.render(quoted) == "`caf\\u{E9}`"_zc);

  auto longAlias =
      identifier("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst"_zc);
  zc::Maybe<identity::SemanticIdentifier> alias(zc::mv(longAlias));
  const auto i32 = fixture.renderIntern(type::semantic::TypeData(
      type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
  checked::CheckerDisplayArgument typeArgument(checked::TypeDisplayArg{i32, zc::mv(alias)});
  ZC_EXPECT(fixture.render(typeArgument) ==
            "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl..."_zc);
}

}  // namespace zomlang::compiler::checker
