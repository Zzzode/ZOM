// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/parser/query/recoverable-parse-query.h"

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/parser/parser.h"
#include "compiler/source/manager.h"
#include "zc/core/debug.h"

namespace zomlang::compiler::parser {
namespace {

constexpr zc::StringPtr kRecoverableParseDomain = "zom.recoverable-parse"_zc;

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

zc::Maybe<identity::SourceFileKey> sourceFile(const RecoverableParseQuery::Key& key) {
  identity::CanonicalDecoder decoder(key.canonicalSourceBytes());
  auto value = identity::SourceFileKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) return zc::none;
  return zc::mv(value);
}

zc::Maybe<identity::CrateKey> sourceCrate(const RecoverableParseQuery::Key& key) {
  auto source = sourceFile(key);
  if (source == zc::none) return zc::none;
  return ZC_ASSERT_NONNULL(source).crate().clone();
}

zc::Maybe<zc::String> logicalName(const RecoverableParseQuery::Key& key) {
  auto source = sourceFile(key);
  if (source == zc::none) return zc::none;
  return ZC_ASSERT_NONNULL(source).logicalFileName();
}

diagnostics::DiagnosticFactCodecLimits factLimits() {
  return diagnostics::DiagnosticFactCodecLimits{4096, 64 * 1024 * 1024, 8, 64 * 1024 * 1024, 128};
}

diagnostics::DiagnosticProvenanceCodecLimits provenanceLimits(uint64_t sourceByteLength) {
  return diagnostics::DiagnosticProvenanceCodecLimits{528384, 64 * 1024 * 1024, 8,
                                                      sourceByteLength};
}

uint64_t errorCount(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts) {
  uint64_t count = 0;
  for (const auto& fact : facts) {
    if (diagnostics::getDiagnosticInfo(fact.code()).severity >= diagnostics::DiagSeverity::kError) {
      ++count;
    }
  }
  return count;
}

zc::Maybe<zc::Array<uint8_t>> buildStableWitness(
    zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
    uint64_t sourceByteLength, CanonicalParserOptions options,
    const cst::RecoverableSyntaxTree& syntax, zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
    const diagnostics::SourceDiagnosticProvenanceMap& provenance) {
  auto factBytes = diagnostics::encodeDiagnosticFacts(zc::none, facts, factLimits());
  auto provenanceBytes = diagnostics::encodeSourceDiagnosticProvenance(
      zc::none, provenance, provenanceLimits(sourceByteLength));
  if (factBytes == zc::none || provenanceBytes == zc::none) return zc::none;

  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kRecoverableParseDomain.asBytes());
  encoder.encodeByteString(canonicalSourceKey);
  encoder.encodeDigest(contentDigest);
  encoder.encodeUint64(sourceByteLength);
  encoder.encodeBool(options.useUnicode);
  encoder.encodeBool(options.allowDollarIdentifiers);
  encoder.encodeBool(options.supportRegexLiterals);
  encoder.encodeDigest(syntax.parserEventStreamId().digest());
  encoder.encodeDigest(syntax.lexemes().id().digest());
  encoder.encodeDigest(syntax.recovery().id().digest());
  encoder.encodeUint64(syntax.parserErrorCount());
  encoder.encodeByteString(ZC_ASSERT_NONNULL(factBytes).asPtr());
  encoder.encodeByteString(ZC_ASSERT_NONNULL(provenanceBytes).asPtr());
  return encoder.finish();
}

}  // namespace

struct RecoverableParsedSource::Impl final {
  Impl(zc::Array<uint8_t>&& sourceKey, const identity::Sha256Digest& contentDigest,
       uint64_t sourceByteLength, CanonicalParserOptions options,
       cst::RecoverableSyntaxTree&& syntax, zc::Vector<diagnostics::DiagnosticFact>&& facts,
       diagnostics::SourceDiagnosticProvenanceMap&& provenance, zc::Array<uint8_t>&& witness)
      : sourceKey(zc::mv(sourceKey)),
        contentDigest(contentDigest),
        sourceByteLength(sourceByteLength),
        options(options),
        syntax(zc::mv(syntax)),
        facts(zc::mv(facts)),
        provenance(zc::mv(provenance)),
        witness(zc::mv(witness)) {}

  zc::Array<uint8_t> sourceKey;
  identity::Sha256Digest contentDigest;
  uint64_t sourceByteLength;
  CanonicalParserOptions options;
  cst::RecoverableSyntaxTree syntax;
  zc::Vector<diagnostics::DiagnosticFact> facts;
  diagnostics::SourceDiagnosticProvenanceMap provenance;
  zc::Array<uint8_t> witness;
};

RecoverableParsedSource::RecoverableParsedSource(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
RecoverableParsedSource::~RecoverableParsedSource() noexcept(false) = default;
RecoverableParsedSource::RecoverableParsedSource(RecoverableParsedSource&&) noexcept = default;
RecoverableParsedSource& RecoverableParsedSource::operator=(RecoverableParsedSource&&) noexcept =
    default;

zc::Maybe<RecoverableParsedSource> RecoverableParsedSource::from(
    zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
    uint64_t sourceByteLength, CanonicalParserOptions options, cst::RecoverableSyntaxTree&& syntax,
    zc::Vector<diagnostics::DiagnosticFact>&& facts,
    diagnostics::SourceDiagnosticProvenanceMap&& provenance) {
  if (identity::source_query::StableSourceQueryKey::decodeBounded(canonicalSourceKey) == zc::none ||
      syntax.lexemes().contentDigest() != contentDigest ||
      syntax.lexemes().sourceByteCount() != sourceByteLength ||
      syntax.parserErrorCount() != errorCount(facts.asPtr()) ||
      !diagnostics::validateDiagnosticProvenance(facts.asPtr(), provenance)) {
    return zc::none;
  }
  auto witness = buildStableWitness(canonicalSourceKey, contentDigest, sourceByteLength, options,
                                    syntax, facts.asPtr(), provenance);
  if (witness == zc::none) return zc::none;
  return RecoverableParsedSource(zc::heap<Impl>(
      zc::heapArray<uint8_t>(canonicalSourceKey), contentDigest, sourceByteLength, options,
      zc::mv(syntax), zc::mv(facts), zc::mv(provenance), zc::mv(ZC_ASSERT_NONNULL(witness))));
}

zc::ArrayPtr<const uint8_t> RecoverableParsedSource::canonicalSourceKey() const {
  return impl->sourceKey.asPtr();
}
const identity::Sha256Digest& RecoverableParsedSource::contentDigest() const noexcept {
  return impl->contentDigest;
}
uint64_t RecoverableParsedSource::sourceByteLength() const noexcept {
  return impl->sourceByteLength;
}
CanonicalParserOptions RecoverableParsedSource::options() const noexcept { return impl->options; }
const cst::RecoverableSyntaxTree& RecoverableParsedSource::syntax() const noexcept {
  return impl->syntax;
}
zc::ArrayPtr<const diagnostics::DiagnosticFact> RecoverableParsedSource::facts() const {
  return impl->facts.asPtr();
}
const diagnostics::SourceDiagnosticProvenanceMap& RecoverableParsedSource::provenance()
    const noexcept {
  return impl->provenance;
}
zc::ArrayPtr<const uint8_t> RecoverableParsedSource::stableWitness() const {
  return impl->witness.asPtr();
}

zc::Array<uint8_t> RecoverableParseQuery::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalSourceBytes());
}
zc::Maybe<RecoverableParseQuery::Key> RecoverableParseQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return identity::source_query::StableSourceQueryKey::decodeBounded(bytes);
}

query::CapabilityProviderResult<RecoverableParseQuery> RecoverableParseQuery::provide(
    query::CapabilityQueryContext<RecoverableParseQuery>& context, const Key& key) {
  auto sourceResult = context.get<identity::source_query::SourceSnapshotInput>(key);
  auto crate = sourceCrate(key);
  auto sourceKey = sourceFile(key);
  auto name = logicalName(key);
  if (sourceResult.isRuntimeFailure() || sourceResult.kind() != query::QueryValueKind::Value ||
      crate == zc::none || sourceKey == zc::none || name == zc::none) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto optionsResult =
      context.get<identity::source_query::CompilationOptionsInput>(ZC_ASSERT_NONNULL(crate));
  if (optionsResult.isRuntimeFailure() || optionsResult.kind() != query::QueryValueKind::Value) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  const auto& source = sourceResult.value();
  const auto& options = optionsResult.value();
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy(source.bytes(), ZC_ASSERT_NONNULL(name));
  diagnostics::SourceDiagnosticDraftBuffer diagnosticDrafts(sources, buffer);
  basic::StringPool strings;
  const auto lang = languageOptions(options);
  Parser parser(sources, diagnosticDrafts, lang, strings, buffer);
  (void)parser.parse();
  auto syntax = parser.takeRecoverableSyntax();
  if (syntax == zc::none || diagnosticDrafts.hasInvariantViolation()) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto published = diagnosticDrafts.publish(ZC_ASSERT_NONNULL(sourceKey), source.bytes().size());
  if (published == zc::none) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto facts = ZC_ASSERT_NONNULL(published).takeFacts();
  auto provenance = ZC_ASSERT_NONNULL(published).takeProvenance();
  auto bound = cst::RecoverableSyntaxDiagnosticBinder::bind(zc::mv(ZC_ASSERT_NONNULL(syntax)),
                                                            facts.asPtr(), provenance);
  if (!bound.is<cst::RecoverableSyntaxTree>()) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = RecoverableParsedSource::from(key.canonicalSourceBytes(), source.contentDigest(),
                                                 source.bytes().size(), parserOptions(options),
                                                 zc::mv(bound.get<cst::RecoverableSyntaxTree>()),
                                                 zc::mv(facts), zc::mv(provenance));
  if (candidate == zc::none) {
    return query::CapabilityProviderResult<RecoverableParseQuery>::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto witness = query::CapabilityCandidateContract<RecoverableParseQuery>::encode(*owned);
  return query::CapabilityProviderResult<RecoverableParseQuery>::candidate(zc::mv(owned),
                                                                           zc::mv(witness));
}

bool registerRecoverableParseQuery(query::QueryDatabase& database) {
  return database.registerDescriptor<RecoverableParseQuery>().isRegistered();
}

}  // namespace zomlang::compiler::parser

namespace zomlang::compiler::query {

StableWitnessBytes CapabilityCandidateContract<parser::RecoverableParseQuery>::encode(
    const parser::RecoverableParseQuery::Capability& candidate) {
  return StableWitnessBytes(zc::heapArray<uint8_t>(candidate.stableWitness()));
}

zc::Maybe<zc::Own<parser::RecoverableParseQuery::Capability>>
CapabilityCandidateContract<parser::RecoverableParseQuery>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

}  // namespace zomlang::compiler::query
