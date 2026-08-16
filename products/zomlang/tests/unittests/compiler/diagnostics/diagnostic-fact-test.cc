// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"

#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::diagnostics {

ZC_TEST("DiagnosticFact preserves move-only identity through explicit clone") {
  auto occurrence = ZC_REQUIRE_NONNULL(
      DiagnosticOccurrenceKey::from(tests::test_identity_detail::source(),
                                    SourceDiagnosticPhase::Lex, SourceDiagnosticEmitter::Lexer, 0));
  zc::Vector<uint32_t> path;
  path.add(0);
  path.add(0);
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, zc::mv(path)));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  auto value = ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrence), DiagID::InvalidCharacter,
                                                       zc::mv(arguments), zc::mv(primary),
                                                       zc::mv(secondary)));
  auto cloned = value.clone();
  ZC_EXPECT(cloned == value);
  ZC_EXPECT(cloned.occurrence().source().sameAs(value.occurrence().source()));
}

ZC_TEST("DiagnosticFact preserves module identity-admission provenance") {
  zc::Vector<uint32_t> occurrencePath;
  occurrencePath.add(2);
  occurrencePath.add(1);
  auto occurrence = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::identityAdmission(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(),
      zc::mv(occurrencePath), IdentityDiagnosticEmitter::ConstantExpressionNotAllowed));

  zc::Vector<uint32_t> provenancePath;
  provenancePath.add(2);
  provenancePath.add(1);
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::identitySyntaxSite(
      tests::test_identity_detail::module(), tests::test_identity_detail::source(),
      zc::mv(provenancePath)));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  auto value = ZC_REQUIRE_NONNULL(
      DiagnosticFact::from(zc::mv(occurrence), DiagID::ConstantExpressionNotAllowed,
                           zc::mv(arguments), zc::mv(primary), zc::mv(secondary)));

  ZC_EXPECT(value.occurrence().origin() == DiagnosticFactOrigin::Module);
  ZC_EXPECT(value.occurrence().isIdentityAdmission());
  ZC_EXPECT(value.primary().isIdentitySyntaxSite());
  ZC_EXPECT(value.clone() == value);
}

}  // namespace zomlang::compiler::diagnostics
