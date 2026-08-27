// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"

#include "zc/ztest/test.h"
#include "compiler/diagnostics/core/diagnostic.h"
#include "compiler/source/manager.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::diagnostics {
namespace {

void emit(DiagnosticEmitter& emitter, const Diagnostic& diagnostic) {
  emitter.emitDiagnostic(diagnostic, {});
}

}  // namespace

ZC_TEST("SourceDiagnosticDraftBuffer publishes deterministic facts and exact provenance") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("0123456789"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);

  Diagnostic parse(DiagID::InvalidCharacter, sources.getLocForOffset(buffer, 5));
  emit(drafts.parserEmitter(), parse);
  Diagnostic lex(DiagID::InvalidCharacter, sources.getLocForOffset(buffer, 1));
  lex.addRange(source::CharSourceRange(sources.getLocForOffset(buffer, 1),
                                       sources.getLocForOffset(buffer, 3), true));
  lex.addChildDiagnostic(zc::heap<Diagnostic>(DiagID::ExpectedToken,
                                              sources.getLocForOffset(buffer, 4), "identifier"_zc));
  emit(drafts.lexerEmitter(), lex);

  auto published = ZC_REQUIRE_NONNULL(drafts.publish(tests::test_identity_detail::source(), 10));
  ZC_REQUIRE(published.facts().size() == 2);
  ZC_EXPECT(published.facts()[0].occurrence().phase() == SourceDiagnosticPhase::Lex);
  ZC_EXPECT(published.facts()[0].occurrence().occurrence() == 0);
  ZC_EXPECT(published.facts()[0].secondary().size() == 2);
  ZC_EXPECT(published.facts()[1].occurrence().phase() == SourceDiagnosticPhase::Parse);
  ZC_EXPECT(published.facts()[1].occurrence().occurrence() == 1);
  ZC_EXPECT(published.provenance().entries().size() == 4);
  ZC_EXPECT(validateDiagnosticProvenance(published.facts(), published.provenance()));
}

ZC_TEST("SourceDiagnosticDraftBuffer retains duplicates and rolls back parser speculation") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abc"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);
  Diagnostic duplicate(DiagID::InvalidCharacter, sources.getLocForOffset(buffer, 1));
  emit(drafts.lexerEmitter(), duplicate);
  emit(drafts.lexerEmitter(), duplicate);
  auto checkpoint = drafts.checkpoint();
  Diagnostic speculative(DiagID::InvalidCharacter, sources.getLocForOffset(buffer, 2));
  emit(drafts.parserEmitter(), speculative);
  drafts.rollback(checkpoint);

  auto published = ZC_REQUIRE_NONNULL(drafts.publish(tests::test_identity_detail::source(), 3));
  ZC_REQUIRE(published.facts().size() == 2);
  ZC_EXPECT(published.facts()[0].occurrence().occurrence() == 0);
  ZC_EXPECT(published.facts()[1].occurrence().occurrence() == 1);
}

ZC_TEST("SourceDiagnosticDraftBuffer fails closed on escaped topology") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abc"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);
  Diagnostic invalid(DiagID::UndefinedIdentifier, sources.getLocForOffset(buffer, 1), "x"_zc);
  emit(drafts.parserEmitter(), invalid);
  ZC_EXPECT(drafts.hasInvariantViolation());
  ZC_EXPECT(drafts.publish(tests::test_identity_detail::source(), 3) == zc::none);
}

}  // namespace zomlang::compiler::diagnostics
