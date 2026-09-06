// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/consumer/terminal-diagnostic-renderer.h"

#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/io.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {
namespace {

zc::Vector<uint32_t> path(uint32_t slot, zc::Maybe<uint32_t> ordinal = zc::none) {
  zc::Vector<uint32_t> value;
  value.add(0);
  value.add(slot);
  ZC_IF_SOME(item, ordinal) { value.add(item); }
  return value;
}

DiagnosticProvenanceKey key(uint32_t slot, zc::Maybe<uint32_t> ordinal = zc::none) {
  return ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, path(slot, ordinal)));
}

DiagnosticPolicyResult policyResult() {
  auto occurrence = ZC_REQUIRE_NONNULL(
      DiagnosticOccurrenceKey::from(tests::test_identity_detail::source(),
                                    SourceDiagnosticPhase::Lex, SourceDiagnosticEmitter::Lexer, 0));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::highlight(key(1, uint32_t{0}))));
  auto fact = ZC_REQUIRE_NONNULL(DiagnosticFact::from(
      zc::mv(occurrence), DiagID::InvalidCharacter, zc::mv(arguments), key(0), zc::mv(secondary)));
  zc::Vector<DiagnosticFact> facts;
  facts.add(zc::mv(fact));
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(SourceDiagnosticProvenanceEntry{key(0), DiagnosticSourceRange{4, 4, false}});
  entries.add(
      SourceDiagnosticProvenanceEntry{key(1, uint32_t{0}), DiagnosticSourceRange{4, 5, true}});
  auto provenance = ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(entries), 8));
  auto source = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(source, provenance);
  auto materialized = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(materialized.is<ResolvedDiagnosticBatch>());
  return applyDiagnosticPolicy(zc::mv(materialized).get<ResolvedDiagnosticBatch>(),
                               DiagnosticPolicy());
}

class TestPresentationResolver final : public DiagnosticPresentationResolver {
public:
  explicit TestPresentationResolver(bool available = true) : available(available) {}

  zc::Maybe<DiagnosticPresentationLocation> resolve(
      const ResolvedDiagnosticLocation& location) const noexcept override {
    if (!available || location.origin() == DiagnosticFactOrigin::Document ||
        !location.source().sameAs(tests::test_identity_detail::source())) {
      return zc::none;
    }
    return DiagnosticPresentationLocation{"test.zom"_zc, "let x =\n"_zc.asBytes(),
                                          location.range()};
  }

private:
  bool available;
};

}  // namespace

ZC_TEST("TerminalDiagnosticRenderer renders a no-color sealed policy result") {
  auto diagnostics = policyResult();
  TestPresentationResolver resolver;
  zc::VectorOutputStream output;

  ZC_EXPECT(renderTerminalDiagnostics(diagnostics, resolver, output, false) == zc::none);
  ZC_EXPECT(output.getArray() ==
            "Error [ZOM2002]: Invalid character\n"
            "  --> test.zom:1:5\n"
            "  | \n"
            "1 | let x =\n"
            "  |     ^\n"
            "\n"
            "1 error generated.\n"_zc.asBytes());
}

ZC_TEST("TerminalDiagnosticRenderer resolves every location before writing") {
  auto diagnostics = policyResult();
  TestPresentationResolver resolver(false);
  zc::VectorOutputStream output;

  ZC_EXPECT(renderTerminalDiagnostics(diagnostics, resolver, output, false) ==
            TerminalDiagnosticRenderFailure::MissingPresentationAuthority);
  ZC_EXPECT(output.getArray().size() == 0);
}

}  // namespace zomlang::compiler::diagnostics
