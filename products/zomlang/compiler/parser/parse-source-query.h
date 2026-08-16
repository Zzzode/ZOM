// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/parser/canonical-parsed-source.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::parser {

/// \brief Canonical deterministic syntax failure published instead of a partial parser value.
class ParseRejected final {
public:
  ~ParseRejected() noexcept(false);
  ParseRejected(ParseRejected&&) noexcept;
  ParseRejected& operator=(ParseRejected&&) noexcept;
  ZC_DISALLOW_COPY(ParseRejected);

  ZC_NODISCARD static zc::Maybe<ParseRejected> fromFacts(
      zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
      uint64_t sourceByteLength, CanonicalParserOptions options,
      zc::Vector<diagnostics::DiagnosticFact>&& facts,
      diagnostics::SourceDiagnosticProvenanceMap&& provenance);
  ZC_NODISCARD static zc::Maybe<ParseRejected> decodeCanonical(zc::ArrayPtr<const uint8_t> bytes);

  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceKey() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t sourceByteLength() const noexcept;
  ZC_NODISCARD CanonicalParserOptions options() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const diagnostics::DiagnosticFact> facts() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const diagnostics::SourceDiagnosticProvenanceMap& provenance() const noexcept;

private:
  struct Impl;
  explicit ParseRejected(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Revision-local whole-source parser query over explicit source and option inputs.
struct ParseSourceQuery final {
  using Key = identity::source_query::StableSourceQueryKey;
  using Capability = CanonicalParsedSource;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "ParseSourceQuery"_zcc,
      "zom.query.parse-source"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::AnySnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<ParseSourceQuery> provide(
      query::CapabilityQueryContext<ParseSourceQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<ParseSourceQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Reconstructs revision-local provenance for one verified parse rejection.
ZC_NODISCARD zc::Maybe<ParseRejected> reconstructParseRejection(
    const ParseSourceQuery::Key& key,
    const identity::source_query::CanonicalCompilationOptions& options,
    const identity::source_query::CanonicalSourceSnapshot& source,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> expectedFacts);

/// \brief Registers the production source parser query exactly once.
ZC_NODISCARD bool registerParseSourceQuery(query::QueryDatabase& database);

}  // namespace zomlang::compiler::parser

namespace zomlang::compiler::query {

/// \brief Canonical candidate codec owned by the parse capability descriptor.
template <>
class CapabilityCandidateContract<parser::ParseSourceQuery> final {
public:
  ZC_NODISCARD static StableWitnessBytes encode(
      const parser::ParseSourceQuery::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<parser::ParseSourceQuery::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Canonical source-rejection codec and verifier owned by the parse descriptor.
template <>
class CapabilityFailureContract<parser::ParseSourceQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;

  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(
      CapabilityQueryContext<parser::ParseSourceQuery>& context,
      const parser::ParseSourceQuery::Key& key, const Sequence& diagnostics);
};

}  // namespace zomlang::compiler::query
