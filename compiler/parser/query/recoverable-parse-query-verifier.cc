// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/cst/parser-event-stream.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/query/recoverable-parse-query.h"
#include "compiler/source/manager.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::parser {
namespace {

zc::Maybe<identity::SourceFileKey> sourceFile(const RecoverableParseQuery::Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto value = identity::SourceFileKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) return zc::none;
  return zc::mv(value);
}

basic::LangOptions languageOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  basic::LangOptions result;
  result.useUnicode = options.useUnicode();
  result.allowDollarIdentifiers = options.allowDollarIdentifiers();
  result.supportRegexLiterals = options.supportRegexLiterals();
  return result;
}

CanonicalParserOptions parserOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  return CanonicalParserOptions{options.useUnicode(), options.allowDollarIdentifiers(),
                                options.supportRegexLiterals()};
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> RecoverableParseQuery::verify(
    query::CapabilityQueryContext<RecoverableParseQuery>& context, const Key& key,
    const Capability& candidate) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto sourceKey = sourceFile(key);
  if (sourceResult.isRuntimeFailure() || sourceResult.kind() != query::QueryValueKind::Value ||
      sourceKey == zc::none) {
    return zc::none;
  }
  const auto& source = sourceResult.value();
  const auto crate = ZC_ASSERT_NONNULL(sourceKey).crate().clone();
  auto optionsResult = context.get<identity::source_query::CompilationOptionsInput>(crate);
  if (optionsResult.isRuntimeFailure() || optionsResult.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& options = optionsResult.value();
  if (candidate.canonicalSourceKey() != key.canonicalSourceBytes() ||
      candidate.contentDigest() != source.contentDigest() ||
      candidate.sourceByteLength() != source.bytes().size() ||
      candidate.options() != parserOptions(options) ||
      !diagnostics::validateDiagnosticProvenance(candidate.facts(), candidate.provenance())) {
    return zc::none;
  }

  auto maybeLogicalName = ZC_ASSERT_NONNULL(sourceKey).logicalFileName();
  if (maybeLogicalName == zc::none) { return zc::none; }
  const auto logicalName = zc::mv(ZC_ASSERT_NONNULL(maybeLogicalName));
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy(source.bytes(), logicalName);
  diagnostics::SourceDiagnosticDraftBuffer diagnosticDrafts(sources, buffer);
  basic::StringPool strings;
  const auto lang = languageOptions(options);
  Parser parser(sources, diagnosticDrafts, lang, strings, buffer);
  (void)parser.parse();
  auto syntax = parser.takeRecoverableSyntax();
  auto published = diagnosticDrafts.publish(ZC_ASSERT_NONNULL(sourceKey), source.bytes().size());
  if (syntax == zc::none || published == zc::none || diagnosticDrafts.hasInvariantViolation()) {
    return zc::none;
  }
  auto facts = ZC_ASSERT_NONNULL(published).takeFacts();
  auto provenance = ZC_ASSERT_NONNULL(published).takeProvenance();
  auto bound = cst::RecoverableSyntaxDiagnosticBinder::bind(zc::mv(ZC_ASSERT_NONNULL(syntax)),
                                                            facts.asPtr(), provenance);
  if (!bound.is<cst::RecoverableSyntaxTree>()) return zc::none;
  auto reconstructed = RecoverableParsedSource::from(
      key.canonicalSourceBytes(), source.contentDigest(), source.bytes().size(),
      parserOptions(options), zc::mv(bound.get<cst::RecoverableSyntaxTree>()), zc::mv(facts),
      zc::mv(provenance));
  if (reconstructed == zc::none ||
      ZC_ASSERT_NONNULL(reconstructed).stableWitness() != candidate.stableWitness()) {
    return zc::none;
  }
  return zc::heapArray<uint8_t>(candidate.stableWitness());
}

}  // namespace zomlang::compiler::parser
