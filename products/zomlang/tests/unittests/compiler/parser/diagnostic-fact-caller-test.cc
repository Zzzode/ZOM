// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::parser {
namespace {

diagnostics::DiagnosticFact fact(uint64_t primaryByteOffset) {
  zc::Vector<zc::String> arguments;
  zc::Vector<diagnostics::DiagnosticFactRange> ranges;
  zc::Vector<diagnostics::DiagnosticFixItFact> fixIts;
  zc::Vector<diagnostics::SecondaryDiagnosticFact> secondary;
  return diagnostics::DiagnosticFact{
      diagnostics::SourceDiagnosticPhase::Lex,
      zc::str("test.zom"_zc),
      zc::str("fixture"_zc),
      1,
      1,
      0,
      diagnostics::DiagID::InvalidCharacter,
      primaryByteOffset,
      zc::mv(arguments),
      zc::mv(ranges),
      zc::mv(fixIts),
      zc::mv(secondary),
  };
}

zc::Array<uint8_t> sourceKey() {
  auto source = tests::test_identity_detail::source();
  auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
  ZC_REQUIRE(stable != zc::none);
  return zc::heapArray<uint8_t>(ZC_ASSERT_NONNULL(stable).canonicalSourceBytes());
}

zc::Maybe<ParseRejected> rejected(uint64_t sourceByteLength,
                                  zc::Vector<diagnostics::DiagnosticFact>&& facts) {
  auto key = sourceKey();
  return ParseRejected::fromFacts(key.asPtr(), tests::test_identity_detail::digest(0x36),
                                  sourceByteLength, CanonicalParserOptions{true, false, true},
                                  zc::mv(facts));
}

}  // namespace

ZC_TEST("ParseRejected source caller admits exactly 4096 diagnostic facts") {
  zc::Vector<diagnostics::DiagnosticFact> facts(4096);
  for (uint64_t index = 0; index < 4096; ++index) { facts.add(fact(index)); }
  auto value = rejected(4095, zc::mv(facts));
  ZC_REQUIRE(value != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(value).facts().size() == 4096);

  auto encoded = ZC_ASSERT_NONNULL(value).encodeCanonical();
  auto decoded = ParseRejected::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decoded).facts().size() == 4096);
}

ZC_TEST("ParseRejected source caller rejects the 4097th diagnostic fact") {
  zc::Vector<diagnostics::DiagnosticFact> facts(4097);
  for (uint64_t index = 0; index < 4097; ++index) { facts.add(fact(index)); }
  ZC_EXPECT(rejected(4096, zc::mv(facts)) == zc::none);
}

ZC_TEST("ParseRejected propagates diagnostic codec rejection") {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  facts.add(fact(1));
  ZC_EXPECT(rejected(0, zc::mv(facts)) == zc::none);

  facts = nullptr;
  auto invalidArity = fact(0);
  invalidArity.code = diagnostics::DiagID::ExpectedToken;
  facts.add(zc::mv(invalidArity));
  ZC_EXPECT(rejected(0, zc::mv(facts)) == zc::none);
}

}  // namespace zomlang::compiler::parser
