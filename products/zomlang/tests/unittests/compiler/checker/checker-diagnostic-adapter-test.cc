// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checker-diagnostic-adapter.h"

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker {
namespace {

class RendererFixture final {
public:
  RendererFixture() {
    using namespace tests::test_identity_detail;
    auto issuedContext = factory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    ZC_IF_SOME(value, issuedContext) { context = value; }
    auto created = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(created != zc::none);
    ZC_IF_SOME(value, created) {
      registries = zc::heap<identity::SemanticIdentityRegistrySet>(zc::mv(value));
    }
    ZC_REQUIRE(registries->collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezePackages() == identity::FrozenRegistryFailure::None);
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
    auto foundModule = registries->modules().find(module());
    ZC_REQUIRE(foundModule != zc::none);
    ZC_IF_SOME(value, foundModule) { moduleId = value; }

    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> noOverload;
    auto record = identity::DefinitionIdentityRecord::from(
        module(), zc::mv(owners), identity::DefinitionKind::Class,
        identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("nominal"_zc),
        zc::mv(noOverload));
    ZC_REQUIRE(record != zc::none);
    zc::Maybe<identity::DefinitionKey> retained;
    ZC_IF_SOME(value, record) {
      retained = identity::DefinitionKey::compute(value);
      zc::Maybe<identity::OverloadHeaderAuthority> noAuthority;
      ZC_REQUIRE(registries->collectDefinition(zc::mv(value), zc::mv(noAuthority)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_IF_SOME(value, retained) {
      auto generic = identity::GenericParameterIdentityRecord::type(
          identity::StableGenericParameterOwnerKey::definition(value.clone()), 0);
      ZC_REQUIRE(registries->collectGenericParameter(zc::mv(generic)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries->freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries->freezeCallableParameters() == identity::FrozenRegistryFailure::None);
    ZC_IF_SOME(value, retained) {
      auto found = registries->definitions().find(value);
      ZC_REQUIRE(found != zc::none);
      ZC_IF_SOME(id, found) { definition = id; }
    }
    auto token = factory.issueSemanticTypeStoreConstructionToken(context);
    ZC_REQUIRE(token != zc::none);
    ZC_IF_SOME(value, token) {
      semanticTypes = zc::heap<type::SemanticTypeStore>(zc::mv(value), *registries);
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
    return renderCheckerDisplayArgument(argument, *registries, *semanticTypes);
  }

  const identity::GenericParameterKey& genericParameter() const {
    ZC_IF_SOME(value, registries->genericParameters().keyAt(0)) { return value; }
    ZC_UNREACHABLE
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  zc::Own<identity::SemanticIdentityRegistrySet> registries;
  zc::Own<type::SemanticTypeStore> semanticTypes;
  identity::DefId definition;
  identity::ModuleId moduleId;
  identity::SemanticTypeId i32;
  identity::SemanticTypeId str;
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
  elements.add(fixture.i32);
  elements.add(fixture.str);
  const auto tuple =
      fixture.intern(type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(elements)}));
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  checked::CheckerDisplayArgument typeArgument(checked::TypeDisplayArg{tuple, zc::mv(noAlias)});
  ZC_EXPECT(fixture.render(typeArgument) == "(i32, str)"_zc);

  const auto genericType = fixture.intern(type::semantic::TypeData(
      type::semantic::TypeParameterTypeData{fixture.genericParameter().clone()}));
  zc::Maybe<identity::SemanticIdentifier> noGenericAlias;
  checked::CheckerDisplayArgument genericArgument(
      checked::TypeDisplayArg{genericType, zc::mv(noGenericAlias)});
  ZC_EXPECT(fixture.render(genericArgument) == "<type-parameter#0>"_zc);

  checked::CheckerDisplayArgument primitiveTypeArgument(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::U64});
  ZC_EXPECT(fixture.render(primitiveTypeArgument) == "u64"_zc);

  checked::CheckerDisplayArgument definitionArgument(
      checked::DefinitionDisplayArg{fixture.definition});
  ZC_EXPECT(fixture.render(definitionArgument) == "test::test::test::nominal"_zc);

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
  patterns.add(checked::PatternConstructor(checked::UnionAlternativePattern{1, fixture.i32}));
  patterns.add(checked::PatternConstructor(checked::EnumVariantPattern{fixture.definition}));
  patterns.add(checked::PatternConstructor(checked::NominalPattern{fixture.definition}));
  checked::CheckerDisplayArgument patternsArgument(checked::PatternsDisplayArg{zc::mv(patterns)});
  ZC_EXPECT(fixture.render(patternsArgument) ==
            "_, 7, (..arity=2), {left,right}, union[1]: i32, "
            "test::test::test::nominal, test::test::test::nominal"_zc);
}

ZC_TEST("CheckerDisplayArgumentCodec.DiscriminatesAndValidatesPrimitiveTypePayload") {
  RendererFixture fixture;
  checked::CheckerDisplayArgument u64(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::U64});
  checked::CheckerDisplayArgument f64(
      checked::PrimitiveTypeDisplayArg{type::semantic::PrimitiveKind::F64});
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  checked::CheckerDisplayArgument internedU64(checked::TypeDisplayArg{
      fixture.intern(type::semantic::TypeData(
          type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::U64})),
      zc::mv(noAlias)});
  auto u64Record = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      u64, fixture.moduleId, *fixture.registries, *fixture.semanticTypes);
  auto f64Record = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      f64, fixture.moduleId, *fixture.registries, *fixture.semanticTypes);
  auto internedRecord = checked::CheckedFactsCanonicalCodec::encodeDisplayArgument(
      internedU64, fixture.moduleId, *fixture.registries, *fixture.semanticTypes);
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
                invalid, fixture.moduleId, *fixture.registries, *fixture.semanticTypes) ==
            zc::none);
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
  checked::CheckerDisplayArgument typeArgument(checked::TypeDisplayArg{fixture.i32, zc::mv(alias)});
  ZC_EXPECT(fixture.render(typeArgument) ==
            "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl..."_zc);
}

}  // namespace zomlang::compiler::checker
