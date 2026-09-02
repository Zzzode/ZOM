// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/parser/query/parse-source-query.h"

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/cst/parser-event-stream.h"
#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/query/effective-source-query.h"
#include "compiler/source/manager.h"
#include "zc/core/debug.h"

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

bool containsOnlySourceSyntaxDiagnostics(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (!diagnostics::isSourceSyntaxDiagnostic(fact.code())) { return false; }
  }
  return true;
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
      !containsOnlySourceSyntaxDiagnostics(facts.asPtr()) ||
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
      !containsOnlySourceSyntaxDiagnostics(ZC_ASSERT_NONNULL(facts).asPtr()) ||
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

zc::Maybe<ParseRejected> reconstructParseRejection(
    const ParseSourceQuery::Key& key,
    const identity::source_query::CanonicalCompilationOptions& options,
    const identity::source_query::CanonicalSourceSnapshot& source,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> expectedFacts) {
  auto sourceFileKey = sourceFile(key);
  auto maybeLogicalName = logicalSourceName(key);
  if (sourceFileKey == zc::none || maybeLogicalName == zc::none) { return zc::none; }

  source::SourceManager sourceManager;
  const auto buffer =
      sourceManager.addMemBufferCopy(source.bytes(), ZC_ASSERT_NONNULL(maybeLogicalName));
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceManager, buffer);
  basic::StringPool stringPool;
  const auto langOptions = languageOptions(options);
  Parser sourceParser(sourceManager, diagnosticFacts, langOptions, stringPool, buffer);
  auto tree = sourceParser.parse();
  auto recoverable = sourceParser.takeRecoverableSyntax();
  const bool hasSyntaxErrors = diagnosticFacts.hasErrors();
  if (diagnosticFacts.hasInvariantViolation() || recoverable == zc::none ||
      (tree != zc::none && !hasSyntaxErrors)) {
    return zc::none;
  }
  auto published = diagnosticFacts.publish(ZC_ASSERT_NONNULL(sourceFileKey), source.bytes().size());
  if (published == zc::none) { return zc::none; }
  auto rejected = ParseRejected::fromFacts(key.canonicalSourceBytes(), source.contentDigest(),
                                           source.bytes().size(), parserOptions(options),
                                           ZC_ASSERT_NONNULL(published).takeFacts(),
                                           ZC_ASSERT_NONNULL(published).takeProvenance());
  if (rejected == zc::none || ZC_ASSERT_NONNULL(rejected).facts() != expectedFacts) {
    return zc::none;
  }
  auto bound = cst::RecoverableSyntaxDiagnosticBinder::bind(
      zc::mv(ZC_ASSERT_NONNULL(recoverable)), ZC_ASSERT_NONNULL(rejected).facts(),
      ZC_ASSERT_NONNULL(rejected).provenance());
  if (!bound.is<cst::RecoverableSyntaxTree>()) return zc::none;
  return zc::mv(rejected);
}

zc::Array<uint8_t> ParseSourceQuery::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalSourceBytes());
}

zc::Maybe<ParseSourceQuery::Key> ParseSourceQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return identity::source_query::StableSourceQueryKey::decodeBounded(bytes);
}

query::CapabilityProviderResult<ParseSourceQuery> ParseSourceQuery::provide(
    query::CapabilityQueryContext<ParseSourceQuery>& context, const Key& key) {
  auto sourceResult = context.get<EffectiveSourceSnapshot>(key);
  if (sourceResult.isRuntimeFailure()) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        sourceResult.runtimeFailure());
  }
  if (sourceResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto crate = sourceCrate(key);
  auto sourceFileKey = sourceFile(key);
  if (crate == zc::none || sourceFileKey == zc::none) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto optionsResult =
      context.get<identity::source_query::CompilationOptionsInput>(ZC_ASSERT_NONNULL(crate));
  if (optionsResult.isRuntimeFailure()) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        optionsResult.runtimeFailure());
  }
  if (optionsResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  auto maybeLogicalName = logicalSourceName(key);
  if (maybeLogicalName == zc::none) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
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
  auto recoverable = sourceParser.takeRecoverableSyntax();
  if (diagnosticFacts.hasInvariantViolation()) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const bool hasSyntaxErrors = diagnosticFacts.hasErrors();
  auto published = diagnosticFacts.publish(ZC_ASSERT_NONNULL(sourceFileKey), source.bytes().size());
  if (published == zc::none || recoverable == zc::none) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto facts = ZC_ASSERT_NONNULL(published).takeFacts();
  auto provenance = ZC_ASSERT_NONNULL(published).takeProvenance();
  auto boundRecoverable = cst::RecoverableSyntaxDiagnosticBinder::bind(
      zc::mv(ZC_ASSERT_NONNULL(recoverable)), facts.asPtr(), provenance);
  if (!boundRecoverable.is<cst::RecoverableSyntaxTree>()) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  if (tree == zc::none || hasSyntaxErrors) {
    auto failure = ParseRejected::fromFacts(key.canonicalSourceBytes(), source.contentDigest(),
                                            source.bytes().size(), parserOptions(options),
                                            zc::mv(facts), zc::mv(provenance));
    if (failure == zc::none) {
      return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto factBytes = diagnostics::encodeDiagnosticFacts(
        zc::none, ZC_ASSERT_NONNULL(failure).facts(), sourceFactLimits());
    if (factBytes == zc::none) {
      return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto rejected =
        query::CapabilityFailureContract<ParseSourceQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>::
            decode(ZC_ASSERT_NONNULL(factBytes).asPtr());
    if (rejected == zc::none) {
      return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return query::CapabilityProviderResult<ParseSourceQuery>::sourceRejected<
        diagnostics::DiagnosticFact>(zc::mv(ZC_ASSERT_NONNULL(rejected)));
  }
  auto tokens = sourceParser.takeTokenSnapshot();
  if (tokens == zc::none) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed = CanonicalParsedSource::fromParsed(
      key.canonicalSourceBytes(), source.contentDigest(), source.bytes(), logicalName,
      parserOptions(options), sourceManager, buffer, zc::mv(ZC_ASSERT_NONNULL(tree)),
      zc::mv(ZC_ASSERT_NONNULL(tokens)), zc::mv(facts), zc::mv(provenance));
  if (parsed == zc::none) {
    return query::CapabilityProviderResult<ParseSourceQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(parsed)));
  auto stableWitness = query::CapabilityCandidateContract<ParseSourceQuery>::encode(*candidate);
  return query::CapabilityProviderResult<ParseSourceQuery>::candidate(zc::mv(candidate),
                                                                      zc::mv(stableWitness));
}

bool registerParseSourceQuery(query::QueryDatabase& database) {
  return database.registerDescriptor<ParseSourceQuery>().isRegistered();
}

}  // namespace zomlang::compiler::parser

namespace zomlang::compiler::query {
namespace {

diagnostics::DiagnosticFactCodecLimits parseFailureFactLimits() {
  return diagnostics::DiagnosticFactCodecLimits{
      .maximumFacts = 4096,
      .maximumEncodedBytes = 64 * 1024 * 1024,
      .maximumProvenanceComponentsPerKey = 3,
      .maximumArgumentBytesPerRecord = 64 * 1024 * 1024,
      .maximumSecondaryPerFact = 128,
  };
}

bool parseFailureContainsError(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code()).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

bool containsOnlyParseDiagnostics(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  for (const auto& fact : facts) {
    if (!diagnostics::isSourceSyntaxDiagnostic(fact.code())) { return false; }
  }
  return true;
}

}  // namespace

StableWitnessBytes CapabilityCandidateContract<parser::ParseSourceQuery>::encode(
    const parser::ParseSourceQuery::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<parser::ParseSourceQuery::Capability>>
CapabilityCandidateContract<parser::ParseSourceQuery>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto candidate = parser::CanonicalParsedSource::decodeCanonical(bytes);
  if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::heap<parser::ParseSourceQuery::Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
}

zc::Array<uint8_t> CapabilityFailureContract<
    parser::ParseSourceQuery,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded =
      diagnostics::encodeDiagnosticFacts(zc::none, diagnostics.values(), parseFailureFactLimits());
  ZC_IREQUIRE(encoded != zc::none, "parse rejection contains non-canonical diagnostic facts");
  return zc::mv(ZC_REQUIRE_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<parser::ParseSourceQuery,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<parser::ParseSourceQuery, SourceRejection<diagnostics::DiagnosticFact>>::
    decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = diagnostics::decodeDiagnosticFacts(zc::none, bytes, parseFailureFactLimits());
  if (facts == zc::none || ZC_ASSERT_NONNULL(facts).size() == 0 ||
      !parseFailureContainsError(ZC_ASSERT_NONNULL(facts).asPtr()) ||
      !containsOnlyParseDiagnostics(ZC_ASSERT_NONNULL(facts).asPtr())) {
    return zc::none;
  }
  auto canonical = diagnostics::encodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(facts).asPtr(),
                                                      parseFailureFactLimits());
  if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical).asPtr() != bytes) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

}  // namespace zomlang::compiler::query
