// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/diagnostic-fact.h"

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

}  // namespace zomlang::compiler::diagnostics
