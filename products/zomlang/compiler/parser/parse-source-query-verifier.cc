// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/parser/parse-source-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/source-key.h"

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

bool containsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

bool factsAreWarningsOnly(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  return !containsError(facts);
}

}  // namespace

bool ParseSourceQuery::verify(query::QueryContext& context, const Key& key,
                              const query::TypedQueryResult<Value>& result) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto optionsResult = context.get<identity::source_query::CompilationOptionsInput>(
      identity::source_query::CompilationUnitQueryKey::fixed());
  if (sourceResult.isRuntimeFailure() || optionsResult.isRuntimeFailure() ||
      sourceResult.kind() != query::QueryValueKind::Value ||
      optionsResult.kind() != query::QueryValueKind::Value || result.isRuntimeFailure()) {
    return false;
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  const auto expectedOptions = expectedParserOptions(options);
  if (result.kind() == query::QueryValueKind::Value) {
    const auto& parsed = result.value();
    auto logicalName = expectedLogicalName(key);
    if (logicalName == zc::none) { return false; }
    return parsed.canonicalSourceKey() == key.canonicalSourceBytes() &&
           parsed.contentDigest() == source.contentDigest() &&
           parsed.sourceBytes() == source.bytes() && parsed.options() == expectedOptions &&
           parsed.logicalName() == ZC_ASSERT_NONNULL(logicalName) &&
           factsAreWarningsOnly(parsed.facts());
  }
  if (result.kind() != query::QueryValueKind::SemanticFailure) { return false; }
  auto rejected = ParseRejected::decodeCanonical(result.semanticFailureBytes());
  if (rejected == zc::none) { return false; }
  const auto& failure = ZC_ASSERT_NONNULL(rejected);
  return failure.canonicalSourceKey() == key.canonicalSourceBytes() &&
         failure.contentDigest() == source.contentDigest() &&
         failure.sourceByteLength() == source.bytes().size() &&
         failure.options() == expectedOptions && failure.facts().size() != 0 &&
         containsError(failure.facts());
}

}  // namespace zomlang::compiler::parser
