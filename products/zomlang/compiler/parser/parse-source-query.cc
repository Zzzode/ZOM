// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/parser/parse-source-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact-buffer.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::parser {
namespace {

constexpr zc::StringPtr kParseRejectedDomain = "zom.parse-rejected"_zc;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumSourceBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumFactBytes = 64 * 1024 * 1024;

bool validSourceKey(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::source_query::StableSourceQueryKey::decodeBounded(bytes) != zc::none;
}

bool containsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

CanonicalParserOptions parserOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  return CanonicalParserOptions{options.useUnicode(), options.allowDollarIdentifiers(),
                                options.supportRegexLiterals()};
}

basic::LangOptions languageOptions(
    const identity::source_query::CanonicalCompilationOptions& options) {
  basic::LangOptions result;
  result.useUnicode = options.useUnicode();
  result.allowDollarIdentifiers = options.allowDollarIdentifiers();
  result.supportRegexLiterals = options.supportRegexLiterals();
  return result;
}

zc::Maybe<zc::String> logicalSourceName(const identity::source_query::StableSourceQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return ZC_ASSERT_NONNULL(source).logicalFileName();
}

zc::Maybe<identity::CrateKey> sourceCrate(const identity::source_query::StableSourceQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return ZC_ASSERT_NONNULL(source).crate().clone();
}

}  // namespace

ParseRejected::ParseRejected(zc::Array<uint8_t>&& canonicalBytes, zc::Array<uint8_t>&& sourceKey,
                             const identity::Sha256Digest& contentDigest, uint64_t sourceByteLength,
                             CanonicalParserOptions options,
                             zc::Vector<diagnostics::DiagnosticFact>&& facts) noexcept
    : canonicalBytesField(zc::mv(canonicalBytes)),
      sourceKeyField(zc::mv(sourceKey)),
      contentDigestField(contentDigest),
      sourceByteLengthField(sourceByteLength),
      optionsField(options),
      factsField(zc::mv(facts)) {}

zc::Maybe<ParseRejected> ParseRejected::fromFacts(zc::ArrayPtr<const uint8_t> canonicalSourceKey,
                                                  const identity::Sha256Digest& contentDigest,
                                                  uint64_t sourceByteLength,
                                                  CanonicalParserOptions options,
                                                  zc::Vector<diagnostics::DiagnosticFact>&& facts) {
  if (!validSourceKey(canonicalSourceKey) || sourceByteLength > kMaximumSourceBytes) {
    return zc::none;
  }
  auto canonicalFacts = diagnostics::canonicalizeDiagnosticFacts(zc::mv(facts));
  if (canonicalFacts.size() == 0 || !containsError(canonicalFacts.asPtr())) { return zc::none; }
  auto factBytes = diagnostics::encodeDiagnosticFacts(canonicalFacts.asPtr());
  if (factBytes.size() > kMaximumFactBytes ||
      diagnostics::decodeDiagnosticFacts(factBytes.asPtr(), sourceByteLength) == zc::none) {
    return zc::none;
  }

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kParseRejectedDomain.asBytes());
  encoder.encodeByteString(canonicalSourceKey);
  encoder.encodeDigest(contentDigest);
  encoder.encodeUint64(sourceByteLength);
  encoder.encodeBool(options.useUnicode);
  encoder.encodeBool(options.allowDollarIdentifiers);
  encoder.encodeBool(options.supportRegexLiterals);
  encoder.encodeByteString(factBytes.asPtr());
  auto encoded = encoder.finish();
  return decodeCanonical(encoded.asPtr());
}

zc::Maybe<ParseRejected> ParseRejected::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto domain = decoder.decodeByteString(kParseRejectedDomain.size());
  auto sourceKey = decoder.decodeByteString(kMaximumSourceKeyBytes);
  auto contentDigest = decoder.decodeDigest();
  auto sourceByteLength = decoder.decodeUint64();
  auto useUnicode = decoder.decodeBool();
  auto allowDollarIdentifiers = decoder.decodeBool();
  auto supportRegexLiterals = decoder.decodeBool();
  auto factBytes = decoder.decodeByteString(kMaximumFactBytes);
  if (domain == zc::none || sourceKey == zc::none || contentDigest == zc::none ||
      sourceByteLength == zc::none || useUnicode == zc::none ||
      allowDollarIdentifiers == zc::none || supportRegexLiterals == zc::none ||
      factBytes == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kParseRejectedDomain.asBytes() ||
      !validSourceKey(ZC_ASSERT_NONNULL(sourceKey).asPtr()) ||
      ZC_ASSERT_NONNULL(sourceByteLength) > kMaximumSourceBytes) {
    return zc::none;
  }
  auto facts = diagnostics::decodeDiagnosticFacts(ZC_ASSERT_NONNULL(factBytes).asPtr(),
                                                  ZC_ASSERT_NONNULL(sourceByteLength));
  if (facts == zc::none || ZC_ASSERT_NONNULL(facts).size() == 0 ||
      !containsError(ZC_ASSERT_NONNULL(facts).asPtr())) {
    return zc::none;
  }
  return ParseRejected(zc::heapArray<uint8_t>(bytes), zc::mv(ZC_ASSERT_NONNULL(sourceKey)),
                       ZC_ASSERT_NONNULL(contentDigest), ZC_ASSERT_NONNULL(sourceByteLength),
                       CanonicalParserOptions{ZC_ASSERT_NONNULL(useUnicode),
                                              ZC_ASSERT_NONNULL(allowDollarIdentifiers),
                                              ZC_ASSERT_NONNULL(supportRegexLiterals)},
                       zc::mv(ZC_ASSERT_NONNULL(facts)));
}

zc::Array<uint8_t> ParseRejected::encodeCanonical() const {
  return zc::heapArray<uint8_t>(canonicalBytesField.asPtr());
}
zc::ArrayPtr<const uint8_t> ParseRejected::canonicalSourceKey() const {
  return sourceKeyField.asPtr();
}
const identity::Sha256Digest& ParseRejected::contentDigest() const noexcept {
  return contentDigestField;
}
uint64_t ParseRejected::sourceByteLength() const noexcept { return sourceByteLengthField; }
CanonicalParserOptions ParseRejected::options() const noexcept { return optionsField; }
zc::ArrayPtr<const diagnostics::DiagnosticFact> ParseRejected::facts() const {
  return factsField.asPtr();
}

zc::StringPtr ParseSourceQuery::domain() { return "zom.query.parse-source"_zc; }

query::QueryKindContract ParseSourceQuery::contract() {
  auto contract = query::QueryKindContract::derived(domain(), query::ReuseClass::RevisionLocal,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Array<uint8_t> ParseSourceQuery::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalSourceBytes());
}

zc::Maybe<ParseSourceQuery::Key> ParseSourceQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::source_query::StableSourceQueryKey::decodeBounded(bytes);
}

query::CapabilityProviderResult<ParseSourceQuery::Capability> ParseSourceQuery::provide(
    query::CapabilityQueryContext& context, const Key& key) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  if (sourceResult.isRuntimeFailure()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        sourceResult.runtimeFailure());
  }
  if (sourceResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto crate = sourceCrate(key);
  if (crate == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto optionsResult =
      context.get<identity::source_query::CompilationOptionsInput>(ZC_ASSERT_NONNULL(crate));
  if (optionsResult.isRuntimeFailure()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        optionsResult.runtimeFailure());
  }
  if (optionsResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  auto maybeLogicalName = logicalSourceName(key);
  if (maybeLogicalName == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto logicalName = zc::mv(ZC_ASSERT_NONNULL(maybeLogicalName));
  source::SourceManager sourceManager;
  const auto buffer = sourceManager.addMemBufferCopy(source.bytes(), logicalName);
  diagnostics::DiagnosticFactBuffer diagnosticFacts(sourceManager, buffer);
  basic::StringPool stringPool;
  const auto langOptions = languageOptions(options);
  Parser sourceParser(sourceManager, diagnosticFacts, langOptions, stringPool, buffer);
  auto tree = sourceParser.parse();
  if (diagnosticFacts.hasInvariantViolation()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const bool hasSyntaxErrors = diagnosticFacts.hasErrors();
  auto facts = diagnosticFacts.takeFactsCanonical();
  if (tree == zc::none || hasSyntaxErrors) {
    auto failure =
        ParseRejected::fromFacts(key.canonicalSourceBytes(), source.contentDigest(),
                                 source.bytes().size(), parserOptions(options), zc::mv(facts));
    if (failure == zc::none) {
      return query::CapabilityProviderResult<Capability>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<Capability>::semanticFailure(
        ZC_ASSERT_NONNULL(failure).encodeCanonical());
  }
  auto tokens = sourceParser.takeTokenSnapshot();
  if (tokens == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed = CanonicalParsedSource::fromParsed(
      key.canonicalSourceBytes(), source.contentDigest(), source.bytes(), logicalName,
      parserOptions(options), sourceManager, buffer, zc::mv(ZC_ASSERT_NONNULL(tree)),
      zc::mv(ZC_ASSERT_NONNULL(tokens)), zc::mv(facts));
  if (parsed == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto stableWitness = ZC_ASSERT_NONNULL(parsed).encodeCanonical();
  return query::CapabilityProviderResult<Capability>::value(
      zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(parsed))), zc::mv(stableWitness));
}

bool registerParseSourceQuery(query::QueryDatabase& database) {
  return database.registerRevisionLocalCapabilityKind<ParseSourceQuery>() != zc::none;
}

}  // namespace zomlang::compiler::parser
