// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::parser {
namespace {

CanonicalParserOptions expectedParserOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  return CanonicalParserOptions{options.useUnicode(), options.allowDollarIdentifiers(),
                                options.supportRegexLiterals()};
}

zc::Maybe<zc::String> expectedLogicalName(const ParseSourceQuery::Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return ZC_ASSERT_NONNULL(source).logicalFileName();
}

zc::Maybe<identity::CrateKey> expectedSourceCrate(const ParseSourceQuery::Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return ZC_ASSERT_NONNULL(source).crate().clone();
}

zc::Maybe<identity::SourceFileKey> expectedSourceFile(const ParseSourceQuery::Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(source);
}

basic::LangOptions expectedLanguageOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  basic::LangOptions result;
  result.useUnicode = options.useUnicode();
  result.allowDollarIdentifiers = options.allowDollarIdentifiers();
  result.supportRegexLiterals = options.supportRegexLiterals();
  return result;
}

bool containsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code()).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

bool factsAreWarningsOnly(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  return !containsError(facts);
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> ParseSourceQuery::verify(
    query::CapabilityQueryContext<ParseSourceQuery>& context, const Key& key,
    const Capability& candidate) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto crate = expectedSourceCrate(key);
  auto sourceFile = expectedSourceFile(key);
  if (crate == zc::none || sourceFile == zc::none) { return zc::none; }
  auto optionsResult =
      context.get<identity::source_query::CompilationOptionsInput>(ZC_ASSERT_NONNULL(crate));
  if (sourceResult.isRuntimeFailure() || optionsResult.isRuntimeFailure() ||
      sourceResult.kind() != query::QueryValueKind::Value ||
      optionsResult.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  const auto expectedOptions = expectedParserOptions(options);
  auto logicalName = expectedLogicalName(key);
  if (logicalName == zc::none || candidate.canonicalSourceKey() != key.canonicalSourceBytes() ||
      candidate.contentDigest() != source.contentDigest() ||
      candidate.sourceBytes() != source.bytes() || candidate.options() != expectedOptions ||
      candidate.logicalName() != ZC_ASSERT_NONNULL(logicalName) ||
      !factsAreWarningsOnly(candidate.facts()) ||
      !diagnostics::validateDiagnosticProvenance(candidate.facts(), candidate.provenance())) {
    return zc::none;
  }

  source::SourceManager sourceManager;
  const auto buffer =
      sourceManager.addMemBufferCopy(source.bytes(), ZC_ASSERT_NONNULL(logicalName));
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceManager, buffer);
  basic::StringPool stringPool;
  const auto langOptions = expectedLanguageOptions(options);
  Parser sourceParser(sourceManager, diagnosticFacts, langOptions, stringPool, buffer);
  auto tree = sourceParser.parse();
  if (tree == zc::none || diagnosticFacts.hasErrors() || diagnosticFacts.hasInvariantViolation()) {
    return zc::none;
  }
  auto published = diagnosticFacts.publish(ZC_ASSERT_NONNULL(sourceFile), source.bytes().size());
  auto tokens = sourceParser.takeTokenSnapshot();
  if (published == zc::none || tokens == zc::none) { return zc::none; }
  auto reconstructed = CanonicalParsedSource::fromParsed(
      key.canonicalSourceBytes(), source.contentDigest(), source.bytes(),
      ZC_ASSERT_NONNULL(logicalName), expectedOptions, sourceManager, buffer,
      zc::mv(ZC_ASSERT_NONNULL(tree)), zc::mv(ZC_ASSERT_NONNULL(tokens)),
      ZC_ASSERT_NONNULL(published).takeFacts(), ZC_ASSERT_NONNULL(published).takeProvenance());
  if (reconstructed == zc::none) { return zc::none; }

  auto witness = ZC_ASSERT_NONNULL(reconstructed).encodeCanonical();
  auto candidateBytes = candidate.encodeCanonical();
  if (candidateBytes.asPtr() != witness.asPtr()) { return zc::none; }
  return zc::mv(witness);
}

static query::CapabilityRejectionCheck verifyParseSourceRejection(
    query::CapabilityQueryContext<ParseSourceQuery>& context, const ParseSourceQuery::Key& key,
    const query::CanonicalNonEmptySequence<diagnostics::DiagnosticFact>& diagnostics) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto crate = expectedSourceCrate(key);
  auto sourceFile = expectedSourceFile(key);
  auto logicalName = expectedLogicalName(key);
  if (crate == zc::none || sourceFile == zc::none || logicalName == zc::none) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto optionsResult =
      context.get<identity::source_query::CompilationOptionsInput>(ZC_ASSERT_NONNULL(crate));
  if (sourceResult.isRuntimeFailure() || optionsResult.isRuntimeFailure() ||
      sourceResult.kind() != query::QueryValueKind::Value ||
      optionsResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityRejectionCheck::Rejected;
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  source::SourceManager sourceManager;
  const auto buffer =
      sourceManager.addMemBufferCopy(source.bytes(), ZC_ASSERT_NONNULL(logicalName));
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceManager, buffer);
  basic::StringPool stringPool;
  const auto langOptions = expectedLanguageOptions(options);
  Parser sourceParser(sourceManager, diagnosticFacts, langOptions, stringPool, buffer);
  auto tree = sourceParser.parse();
  const bool hasSyntaxErrors = diagnosticFacts.hasErrors();
  if (diagnosticFacts.hasInvariantViolation() || (tree != zc::none && !hasSyntaxErrors)) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  auto published = diagnosticFacts.publish(ZC_ASSERT_NONNULL(sourceFile), source.bytes().size());
  if (published == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  auto rejected = ParseRejected::fromFacts(key.canonicalSourceBytes(), source.contentDigest(),
                                           source.bytes().size(), expectedParserOptions(options),
                                           ZC_ASSERT_NONNULL(published).takeFacts(),
                                           ZC_ASSERT_NONNULL(published).takeProvenance());
  if (rejected == zc::none || ZC_ASSERT_NONNULL(rejected).facts() != diagnostics.values()) {
    return query::CapabilityRejectionCheck::Rejected;
  }
  return query::CapabilityRejectionCheck::Verified;
}

}  // namespace zomlang::compiler::parser

namespace zomlang::compiler::query {
CapabilityRejectionCheck
CapabilityFailureContract<parser::ParseSourceQuery, SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<parser::ParseSourceQuery>& context,
           const parser::ParseSourceQuery::Key& key, const Sequence& diagnostics) {
  return parser::verifyParseSourceRejection(context, key, diagnostics);
}

}  // namespace zomlang::compiler::query
