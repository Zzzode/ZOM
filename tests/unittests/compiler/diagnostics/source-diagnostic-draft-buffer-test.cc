// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"

#include "compiler/source/manager.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {
ZC_TEST("SourceDiagnosticDraftBuffer publishes deterministic facts and exact provenance") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("0123456789"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);

  drafts.parserSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 5));
  const auto lex =
      drafts.lexerSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 1));
  drafts.lexerSink().addHighlight(
      lex, source::CharSourceRange(sources.getLocForOffset(buffer, 1),
                                   sources.getLocForOffset(buffer, 3), true));
  drafts.lexerSink().addNote<DiagID::ExpectedToken>(lex, sources.getLocForOffset(buffer, 4),
                                                    "identifier"_zc);

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
  drafts.lexerSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 1));
  drafts.lexerSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 1));
  auto checkpoint = drafts.checkpoint();
  drafts.parserSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 2));
  drafts.rollback(checkpoint);

  auto published = ZC_REQUIRE_NONNULL(drafts.publish(tests::test_identity_detail::source(), 3));
  ZC_REQUIRE(published.facts().size() == 2);
  ZC_EXPECT(published.facts()[0].occurrence().occurrence() == 0);
  ZC_EXPECT(published.facts()[1].occurrence().occurrence() == 1);
}

ZC_TEST("SourceDiagnosticDraftBuffer retains facts beyond the presentation error budget") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("x"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);

  for (size_t index = 0; index < 101; ++index) {
    const auto handle =
        drafts.lexerSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 0));
    ZC_EXPECT(handle.isValid());
  }

  ZC_EXPECT(drafts.errorCount() == 101);
  auto published = ZC_REQUIRE_NONNULL(drafts.publish(tests::test_identity_detail::source(), 1));
  ZC_EXPECT(published.facts().size() == 101);
}

ZC_TEST("SourceDiagnosticDraftBuffer fails closed on escaped topology") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abc"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);
  drafts.parserSink().report<DiagID::UndefinedIdentifier>(sources.getLocForOffset(buffer, 1),
                                                          "x"_zc);
  ZC_EXPECT(drafts.hasInvariantViolation());
  ZC_EXPECT(drafts.publish(tests::test_identity_detail::source(), 3) == zc::none);
}

ZC_TEST("SourceDiagnosticDraftBuffer rejects a handle invalidated by rollback") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abc"_zc.asBytes(), "test.zom"_zc);
  SourceDiagnosticDraftBuffer drafts(sources, buffer);
  auto checkpoint = drafts.checkpoint();
  const auto stale =
      drafts.parserSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 1));
  drafts.rollback(checkpoint);
  drafts.parserSink().report<DiagID::InvalidCharacter>(sources.getLocForOffset(buffer, 2));
  drafts.parserSink().addHighlight(
      stale, source::CharSourceRange(sources.getLocForOffset(buffer, 0),
                                     sources.getLocForOffset(buffer, 1), true));

  ZC_EXPECT(drafts.hasInvariantViolation());
  ZC_EXPECT(drafts.publish(tests::test_identity_detail::source(), 3) == zc::none);
}

}  // namespace zomlang::compiler::diagnostics
