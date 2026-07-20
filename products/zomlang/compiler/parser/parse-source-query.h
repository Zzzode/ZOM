// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/parser/canonical-parsed-source.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::parser {

/// \brief Canonical deterministic syntax failure published instead of a partial parser value.
class ParseRejected final {
public:
  ParseRejected(ParseRejected&&) noexcept = default;
  ParseRejected& operator=(ParseRejected&&) noexcept = default;
  ZC_DISALLOW_COPY(ParseRejected);

  ZC_NODISCARD static zc::Maybe<ParseRejected> fromFacts(
      zc::ArrayPtr<const uint8_t> canonicalSourceKey,
      const identity::Sha256Digest& contentDigest, uint64_t sourceByteLength,
      CanonicalParserOptions options, zc::Vector<diagnostics::DiagnosticFact>&& facts);
  ZC_NODISCARD static zc::Maybe<ParseRejected> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceKey() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t sourceByteLength() const noexcept;
  ZC_NODISCARD CanonicalParserOptions options() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const diagnostics::DiagnosticFact> facts() const ZC_LIFETIMEBOUND;

private:
  ParseRejected(zc::Array<uint8_t>&& canonicalBytes, zc::Array<uint8_t>&& sourceKey,
                const identity::Sha256Digest& contentDigest, uint64_t sourceByteLength,
                CanonicalParserOptions options,
                zc::Vector<diagnostics::DiagnosticFact>&& facts) noexcept;

  zc::Array<uint8_t> canonicalBytesField;
  zc::Array<uint8_t> sourceKeyField;
  identity::Sha256Digest contentDigestField;
  uint64_t sourceByteLengthField;
  CanonicalParserOptions optionsField;
  zc::Vector<diagnostics::DiagnosticFact> factsField;
};

/// \brief Revision-local whole-source parser query over explicit source and option inputs.
struct ParseSourceQuery final {
  using Key = identity::source_query::StableSourceQueryKey;
  using Value = CanonicalParsedSource;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Registers the production source parser query exactly once.
ZC_NODISCARD bool registerParseSourceQuery(query::QueryDatabase& database);

}  // namespace zomlang::compiler::parser
