// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/parser/parse-source-query.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
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
constexpr uint64_t kMaximumProvenanceBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumFacts = 4096;

diagnostics::DiagnosticFactCodecLimits sourceFactLimits() {
  return diagnostics::DiagnosticFactCodecLimits{
      .maximumFacts = kMaximumFacts,
      .maximumEncodedBytes = kMaximumFactBytes,
      .maximumProvenanceComponentsPerKey = 3,
      .maximumArgumentBytesPerRecord = 64 * 1024 * 1024,
      .maximumSecondaryPerFact = 128,
  };
}

diagnostics::DiagnosticProvenanceCodecLimits sourceProvenanceLimits(uint64_t sourceByteLength) {
  return diagnostics::DiagnosticProvenanceCodecLimits{
      .maximumEntries = 528384,
      .maximumEncodedBytes = kMaximumProvenanceBytes,
      .maximumProvenanceComponentsPerKey = 3,
      .maximumSourceByteOffset = sourceByteLength,
  };
}

bool validSourceKey(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::source_query::StableSourceQueryKey::decodeBounded(bytes) != zc::none;
}

bool containsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code()).severity >= diagnostics::DiagSeverity::kError) {
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

zc::Maybe<identity::SourceFileKey> sourceFile(
    const identity::source_query::StableSourceQueryKey& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(source);
}

}  // namespace

struct ParseRejected::Impl final {
  Impl(zc::Array<uint8_t>&& canonicalBytes, zc::Array<uint8_t>&& sourceKey,
       const identity::Sha256Digest& contentDigest, uint64_t sourceByteLength,
       CanonicalParserOptions options, zc::Vector<diagnostics::DiagnosticFact>&& facts,
       diagnostics::SourceDiagnosticProvenanceMap&& provenance)
      : canonicalBytes(zc::mv(canonicalBytes)),
        sourceKey(zc::mv(sourceKey)),
        contentDigest(contentDigest),
        sourceByteLength(sourceByteLength),
        options(options),
        facts(zc::mv(facts)),
        provenance(zc::mv(provenance)) {}
  zc::Array<uint8_t> canonicalBytes;
  zc::Array<uint8_t> sourceKey;
  identity::Sha256Digest contentDigest;
  uint64_t sourceByteLength;
  CanonicalParserOptions options;
  zc::Vector<diagnostics::DiagnosticFact> facts;
  diagnostics::SourceDiagnosticProvenanceMap provenance;
};

ParseRejected::ParseRejected(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
ParseRejected::~ParseRejected() noexcept(false) = default;
ParseRejected::ParseRejected(ParseRejected&&) noexcept = default;
ParseRejected& ParseRejected::operator=(ParseRejected&&) noexcept = default;

zc::Maybe<ParseRejected> ParseRejected::fromFacts(
    zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
    uint64_t sourceByteLength, CanonicalParserOptions options,
    zc::Vector<diagnostics::DiagnosticFact>&& facts,
    diagnostics::SourceDiagnosticProvenanceMap&& provenance) {
  if (!validSourceKey(canonicalSourceKey) || sourceByteLength > kMaximumSourceBytes) {
    return zc::none;
  }
  if (facts.size() == 0 || !containsError(facts.asPtr()) ||
      !diagnostics::validateDiagnosticProvenance(facts.asPtr(), provenance)) {
    return zc::none;
  }
  const auto limits = sourceFactLimits();
  auto factBytes = diagnostics::encodeDiagnosticFacts(zc::none, facts.asPtr(), limits);
  auto provenanceBytes = diagnostics::encodeSourceDiagnosticProvenance(
      zc::none, provenance, sourceProvenanceLimits(sourceByteLength));
  if (factBytes == zc::none || provenanceBytes == zc::none ||
      diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(factBytes).asPtr(), limits) ==
          zc::none ||
      diagnostics::decodeSourceDiagnosticProvenance(
          zc::none, ZC_ASSERT_NONNULL(provenanceBytes).asPtr(), sourceByteLength,
          sourceProvenanceLimits(sourceByteLength)) == zc::none) {
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
  encoder.encodeByteString(ZC_ASSERT_NONNULL(factBytes).asPtr());
  encoder.encodeByteString(ZC_ASSERT_NONNULL(provenanceBytes).asPtr());
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
  auto provenanceBytes = decoder.decodeByteString(kMaximumProvenanceBytes);
  if (domain == zc::none || sourceKey == zc::none || contentDigest == zc::none ||
      sourceByteLength == zc::none || useUnicode == zc::none ||
      allowDollarIdentifiers == zc::none || supportRegexLiterals == zc::none ||
      factBytes == zc::none || provenanceBytes == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kParseRejectedDomain.asBytes() ||
      !validSourceKey(ZC_ASSERT_NONNULL(sourceKey).asPtr()) ||
      ZC_ASSERT_NONNULL(sourceByteLength) > kMaximumSourceBytes) {
    return zc::none;
  }
  auto facts = diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(factBytes).asPtr(),
                                                  sourceFactLimits());
  auto provenance = diagnostics::decodeSourceDiagnosticProvenance(
      zc::none, ZC_ASSERT_NONNULL(provenanceBytes).asPtr(), ZC_ASSERT_NONNULL(sourceByteLength),
      sourceProvenanceLimits(ZC_ASSERT_NONNULL(sourceByteLength)));
  if (facts == zc::none || provenance == zc::none || ZC_ASSERT_NONNULL(facts).size() == 0 ||
      !containsError(ZC_ASSERT_NONNULL(facts).asPtr()) ||
      !diagnostics::validateDiagnosticProvenance(ZC_ASSERT_NONNULL(facts).asPtr(),
                                                 ZC_ASSERT_NONNULL(provenance))) {
    return zc::none;
  }
  return ParseRejected(
      zc::heap<Impl>(zc::heapArray<uint8_t>(bytes), zc::mv(ZC_ASSERT_NONNULL(sourceKey)),
                     ZC_ASSERT_NONNULL(contentDigest), ZC_ASSERT_NONNULL(sourceByteLength),
                     CanonicalParserOptions{ZC_ASSERT_NONNULL(useUnicode),
                                            ZC_ASSERT_NONNULL(allowDollarIdentifiers),
                                            ZC_ASSERT_NONNULL(supportRegexLiterals)},
                     zc::mv(ZC_ASSERT_NONNULL(facts)), zc::mv(ZC_ASSERT_NONNULL(provenance))));
}

zc::Array<uint8_t> ParseRejected::encodeCanonical() const {
  return zc::heapArray<uint8_t>(impl->canonicalBytes.asPtr());
}
zc::ArrayPtr<const uint8_t> ParseRejected::canonicalSourceKey() const {
  return impl->sourceKey.asPtr();
}
const identity::Sha256Digest& ParseRejected::contentDigest() const noexcept {
  return impl->contentDigest;
}
uint64_t ParseRejected::sourceByteLength() const noexcept { return impl->sourceByteLength; }
CanonicalParserOptions ParseRejected::options() const noexcept { return impl->options; }
zc::ArrayPtr<const diagnostics::DiagnosticFact> ParseRejected::facts() const {
  return impl->facts.asPtr();
}
const diagnostics::SourceDiagnosticProvenanceMap& ParseRejected::provenance() const noexcept {
  return impl->provenance;
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
  auto sourceFileKey = sourceFile(key);
  if (crate == zc::none || sourceFileKey == zc::none) {
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
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceManager, buffer);
  basic::StringPool stringPool;
  const auto langOptions = languageOptions(options);
  Parser sourceParser(sourceManager, diagnosticFacts, langOptions, stringPool, buffer);
  auto tree = sourceParser.parse();
  if (diagnosticFacts.hasInvariantViolation()) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const bool hasSyntaxErrors = diagnosticFacts.hasErrors();
  auto published = diagnosticFacts.publish(ZC_ASSERT_NONNULL(sourceFileKey), source.bytes().size());
  if (published == zc::none) {
    return query::CapabilityProviderResult<Capability>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto facts = ZC_ASSERT_NONNULL(published).takeFacts();
  auto provenance = ZC_ASSERT_NONNULL(published).takeProvenance();
  if (tree == zc::none || hasSyntaxErrors) {
    auto failure = ParseRejected::fromFacts(key.canonicalSourceBytes(), source.contentDigest(),
                                            source.bytes().size(), parserOptions(options),
                                            zc::mv(facts), zc::mv(provenance));
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
      zc::mv(ZC_ASSERT_NONNULL(tokens)), zc::mv(facts), zc::mv(provenance));
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
