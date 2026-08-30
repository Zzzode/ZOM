// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/cst/parser-event-stream.h"
#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/parser/query/canonical-parsed-source.h"
#include "compiler/query/query-database.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::parser {

/// \brief Revision-local recoverable syntax plus its exact diagnostic authority.
class RecoverableParsedSource final {
public:
  RecoverableParsedSource(RecoverableParsedSource&&) noexcept;
  RecoverableParsedSource& operator=(RecoverableParsedSource&&) noexcept;
  ZC_DISALLOW_COPY(RecoverableParsedSource);
  ~RecoverableParsedSource() noexcept(false);

  ZC_NODISCARD static zc::Maybe<RecoverableParsedSource> from(
      zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
      uint64_t sourceByteLength, CanonicalParserOptions options,
      cst::RecoverableSyntaxTree&& syntax, zc::Vector<diagnostics::DiagnosticFact>&& facts,
      diagnostics::SourceDiagnosticProvenanceMap&& provenance);

  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceKey() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t sourceByteLength() const noexcept;
  ZC_NODISCARD CanonicalParserOptions options() const noexcept;
  ZC_NODISCARD const cst::RecoverableSyntaxTree& syntax() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const diagnostics::DiagnosticFact> facts() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const diagnostics::SourceDiagnosticProvenanceMap& provenance() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> stableWitness() const ZC_LIFETIMEBOUND;

private:
  struct Impl;
  explicit RecoverableParsedSource(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Revision-local recoverable parser query that succeeds for clean and
/// malformed source while publishing no compiler authority.
struct RecoverableParseQuery final {
  using Key = identity::source_query::StableSourceQueryKey;
  using Capability = RecoverableParsedSource;
  using FailureAlternatives = query::CapabilityFailureList<>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "RecoverableParseQuery"_zcc,        "zom.query.recoverable-parse"_zcc,
      query::RetentionClass::Retained,    query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,      query::CapabilityAdmission::AnySnapshot,
      query::FinalFailureProjection::None};

  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<RecoverableParseQuery> provide(
      query::CapabilityQueryContext<RecoverableParseQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<RecoverableParseQuery>& context, const Key& key,
      const Capability& candidate);
};

ZC_NODISCARD bool registerRecoverableParseQuery(query::QueryDatabase& database);

}  // namespace zomlang::compiler::parser

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<parser::RecoverableParseQuery> final {
public:
  ZC_NODISCARD static StableWitnessBytes encode(
      const parser::RecoverableParseQuery::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<parser::RecoverableParseQuery::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

}  // namespace zomlang::compiler::query
