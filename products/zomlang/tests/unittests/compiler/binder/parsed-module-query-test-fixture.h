// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/debug.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/parser/query/canonical-parsed-source.h"
#include "zomlang/compiler/parser/token-snapshot.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder::test {

inline zc::Maybe<parser::CanonicalParsedSource> canonicalParsedSource(
    const identity::ImmutableSourceSnapshot& snapshot, const source::SourceManager& sources,
    const source::BufferId& buffer, parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree) {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  zc::Vector<diagnostics::SourceDiagnosticProvenanceEntry> entries;
  auto provenance =
      diagnostics::SourceDiagnosticProvenanceMap::from(zc::mv(entries), snapshot.bytes().size());
  if (provenance == zc::none) { return zc::none; }
  const auto sourceKey = snapshot.source().encode();
  return parser::CanonicalParsedSource::fromParsed(
      sourceKey.asPtr(), snapshot.contentDigest(), snapshot.bytes(),
      sources.getIdentifierForBuffer(buffer), parser::CanonicalParserOptions{}, sources, buffer,
      zc::mv(tree), zc::mv(tokens), zc::mv(facts), zc::mv(ZC_ASSERT_NONNULL(provenance)));
}

inline ParsedModuleVerificationResult verifyParsedSource(
    identity::SemanticContextBrand context, const identity::ImmutableSourceSnapshot& snapshot,
    const source::SourceManager& sources, const source::BufferId& buffer,
    parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree) {
  auto parsed = canonicalParsedSource(snapshot, sources, buffer, zc::mv(tokens), zc::mv(tree));
  if (parsed == zc::none) {
    return ParsedModuleInvariantFact{ParsedModuleInvariantKind::InvalidTree, 1};
  }
  return ParsedModuleVerifier::verifyQueryResult(context, snapshot, snapshot.source(), sources,
                                                 buffer, zc::mv(ZC_ASSERT_NONNULL(parsed)));
}

inline VerifiedParsedModule requireVerifiedParsedSource(
    identity::SemanticContextBrand context, const identity::ImmutableSourceSnapshot& snapshot,
    const source::SourceManager& sources, const source::BufferId& buffer,
    parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree) {
  auto result =
      verifyParsedSource(context, snapshot, sources, buffer, zc::mv(tokens), zc::mv(tree));
  ZC_REQUIRE(result.is<VerifiedParsedModule>());
  return zc::mv(result.get<VerifiedParsedModule>());
}

}  // namespace zomlang::compiler::binder::test
