// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/parser/parse-source-query.h"

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

zc::Maybe<zc::Array<uint8_t>> ParseSourceQuery::verify(query::CapabilityQueryContext& context,
                                                       const Key& key,
                                                       const Capability& candidate) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto crate = expectedSourceCrate(key);
  if (crate == zc::none) { return zc::none; }
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
  auto witness = candidate.encodeCanonical();
  auto decoded = CanonicalParsedSource::decodeCanonical(witness.asPtr());
  if (decoded == zc::none ||
      ZC_ASSERT_NONNULL(decoded).encodeCanonical().asPtr() != witness.asPtr()) {
    return zc::none;
  }
  return zc::mv(witness);
}

}  // namespace zomlang::compiler::parser
