// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/parser/query/canonical-parsed-source.h"

#include "zc/core/debug.h"
#include "compiler/ast/canonical-tree-codec.h"
#include "compiler/ast/generated/node-schema.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/parser/token-snapshot.h"
#include "compiler/source/manager.h"

namespace zomlang::compiler::parser {
namespace {

constexpr zc::StringPtr kParsedSourceDomain = "zom.canonical-parsed-source"_zc;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumSourceBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumLogicalNameBytes = 64 * 1024;
constexpr uint64_t kMaximumAstBytes = 256 * 1024 * 1024;
constexpr uint64_t kMaximumFactBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumProvenanceBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumValueBodyBytes = 512 * 1024 * 1024;
constexpr uint64_t kMaximumTokens = 16 * 1024 * 1024;
constexpr uint64_t kMaximumTextBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumFacts = 4096;

diagnostics::DiagnosticFactCodecLimits sourceFactLimits(uint64_t sourceByteLength) {
  static_cast<void>(sourceByteLength);
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
  if (bytes.size() == 0 || bytes.size() > kMaximumSourceKeyBytes) { return false; }
  identity::CanonicalDecoder decoder(bytes);
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return false; }
  return ZC_ASSERT_NONNULL(source).encode().asPtr() == bytes;
}

zc::Maybe<uint64_t> offsetFor(const source::SourceManager& sources, const source::BufferId& buffer,
                              source::SourceLoc location) {
  if (location.isInvalid()) { return zc::none; }
  const auto sourceRange = sources.getRangeForBuffer(buffer);
  if (location < sourceRange.getStart() || location > sourceRange.getEnd()) { return zc::none; }
  return static_cast<uint64_t>(sources.getLocOffsetInBuffer(location, buffer));
}

zc::Maybe<zc::Vector<CanonicalParsedToken>> detachTokens(
    const source::SourceManager* snapshotSources, const source::BufferId& snapshotBuffer,
    zc::ArrayPtr<const ParsedTokenRange> snapshotTokens, const source::SourceManager& sources,
    const source::BufferId& buffer, uint64_t sourceByteLength) {
  if (snapshotSources != &sources || snapshotBuffer != buffer || snapshotTokens.size() == 0 ||
      snapshotTokens.size() > kMaximumTokens) {
    return zc::none;
  }
  zc::Vector<CanonicalParsedToken> tokens(snapshotTokens.size());
  uint64_t previousStart = 0;
  for (const auto& token : snapshotTokens) {
    auto start = offsetFor(sources, buffer, token.source.getStart());
    auto end = offsetFor(sources, buffer, token.source.getEnd());
    if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end) ||
        ZC_ASSERT_NONNULL(end) > sourceByteLength ||
        (tokens.size() != 0 && ZC_ASSERT_NONNULL(start) < previousStart) ||
        token.kind <= ast::SyntaxKind::Unknown || token.kind > ast::SyntaxKind::EndOfFile) {
      return zc::none;
    }
    previousStart = ZC_ASSERT_NONNULL(start);
    tokens.add(CanonicalParsedToken{token.kind, ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end),
                                    zc::str(token.canonicalText)});
  }
  if (tokens.back().kind != ast::SyntaxKind::EndOfFile) { return zc::none; }
  return zc::mv(tokens);
}

void encodeTokens(identity::CanonicalEncoder& encoder,
                  zc::ArrayPtr<const CanonicalParsedToken> tokens) {
  encoder.encodeSequenceSize(tokens.size());
  for (const auto& token : tokens) {
    encoder.encodeUint32(static_cast<uint32_t>(token.kind));
    encoder.encodeUint64(token.byteStart);
    encoder.encodeUint64(token.byteEnd);
    encoder.encodeByteString(token.canonicalText.asBytes());
  }
}

zc::Maybe<zc::Vector<CanonicalParsedToken>> decodeTokens(identity::CanonicalDecoder& decoder,
                                                         uint64_t sourceByteLength) {
  auto count = decoder.decodeSequenceSize(kMaximumTokens);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<CanonicalParsedToken> tokens(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  uint64_t previousStart = 0;
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto kindValue = decoder.decodeUint32();
    auto start = decoder.decodeUint64();
    auto end = decoder.decodeUint64();
    auto text = decoder.decodeByteString(kMaximumTextBytes);
    if (kindValue == zc::none || start == zc::none || end == zc::none || text == zc::none) {
      return zc::none;
    }
    const auto kind = static_cast<ast::SyntaxKind>(ZC_ASSERT_NONNULL(kindValue));
    if (kind <= ast::SyntaxKind::Unknown || kind > ast::SyntaxKind::EndOfFile ||
        ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end) ||
        ZC_ASSERT_NONNULL(end) > sourceByteLength ||
        (index != 0 && ZC_ASSERT_NONNULL(start) < previousStart) ||
        (index + 1 != ZC_ASSERT_NONNULL(count) && kind == ast::SyntaxKind::EndOfFile)) {
      return zc::none;
    }
    previousStart = ZC_ASSERT_NONNULL(start);
    tokens.add(CanonicalParsedToken{kind, ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end),
                                    zc::str(ZC_ASSERT_NONNULL(text).asChars())});
  }
  if (tokens.back().kind != ast::SyntaxKind::EndOfFile) { return zc::none; }
  return zc::mv(tokens);
}

zc::Maybe<zc::Array<uint8_t>> encodeBody(
    zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
    zc::ArrayPtr<const uint8_t> sourceBytes, zc::StringPtr logicalName,
    CanonicalParserOptions options, const ast::Tree& tree, const source::SourceManager& sources,
    const source::BufferId& buffer, zc::ArrayPtr<const CanonicalParsedToken> tokens,
    zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
    const diagnostics::SourceDiagnosticProvenanceMap& provenance) {
  auto astBytes = ast::encodeCanonicalTree(tree, sources, buffer);
  if (astBytes == zc::none) { return zc::none; }
  auto factBytes =
      diagnostics::encodeDiagnosticFacts(zc::none, facts, sourceFactLimits(sourceBytes.size()));
  if (factBytes == zc::none) { return zc::none; }
  auto provenanceBytes = diagnostics::encodeSourceDiagnosticProvenance(
      zc::none, provenance, sourceProvenanceLimits(sourceBytes.size()));
  if (provenanceBytes == zc::none) { return zc::none; }
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(canonicalSourceKey);
  encoder.encodeDigest(contentDigest);
  encoder.encodeByteString(sourceBytes);
  encoder.encodeByteString(logicalName.asBytes());
  encoder.encodeBool(options.useUnicode);
  encoder.encodeBool(options.allowDollarIdentifiers);
  encoder.encodeBool(options.supportRegexLiterals);
  encoder.encodeByteString(zc::StringPtr(ast::kAstSchemaFingerprint).asBytes());
  encoder.encodeByteString(ZC_ASSERT_NONNULL(astBytes).asPtr());
  encodeTokens(encoder, tokens);
  encoder.encodeByteString(ZC_ASSERT_NONNULL(factBytes).asPtr());
  encoder.encodeByteString(ZC_ASSERT_NONNULL(provenanceBytes).asPtr());
  return encoder.finish();
}

zc::Array<uint8_t> wrapBody(zc::ArrayPtr<const uint8_t> body) {
  const auto fingerprint = ZC_REQUIRE_NONNULL(identity::sha256(body));
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kParsedSourceDomain.asBytes());
  encoder.encodeByteString(body);
  encoder.encodeDigest(fingerprint);
  return encoder.finish();
}

}  // namespace

struct CanonicalParsedSource::Impl final {
  Impl(zc::Array<uint8_t>&& canonicalBytes, zc::Array<uint8_t>&& sourceKey,
       const identity::Sha256Digest& contentDigest, zc::Array<uint8_t>&& sourceBytes,
       zc::String&& logicalName, CanonicalParserOptions options, ast::Tree&& tree,
       zc::Vector<CanonicalParsedToken>&& tokens, zc::Vector<diagnostics::DiagnosticFact>&& facts,
       diagnostics::SourceDiagnosticProvenanceMap&& provenance)
      : canonicalBytes(zc::mv(canonicalBytes)),
        sourceKey(zc::mv(sourceKey)),
        contentDigest(contentDigest),
        sourceBytes(zc::mv(sourceBytes)),
        logicalName(zc::mv(logicalName)),
        options(options),
        sourceManager(),
        buffer(sourceManager.addMemBufferCopy(this->sourceBytes.asPtr(), this->logicalName)),
        tree(zc::mv(tree)),
        tokens(zc::mv(tokens)),
        facts(zc::mv(facts)),
        provenance(zc::mv(provenance)) {}

  zc::Array<uint8_t> canonicalBytes;
  zc::Array<uint8_t> sourceKey;
  identity::Sha256Digest contentDigest;
  zc::Array<uint8_t> sourceBytes;
  zc::String logicalName;
  CanonicalParserOptions options;
  source::SourceManager sourceManager;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Vector<CanonicalParsedToken> tokens;
  zc::Vector<diagnostics::DiagnosticFact> facts;
  diagnostics::SourceDiagnosticProvenanceMap provenance;
};

CanonicalParsedToken CanonicalParsedToken::clone() const {
  return CanonicalParsedToken{kind, byteStart, byteEnd, zc::str(canonicalText)};
}

bool CanonicalParsedToken::operator==(const CanonicalParsedToken& other) const noexcept {
  return kind == other.kind && byteStart == other.byteStart && byteEnd == other.byteEnd &&
         canonicalText == other.canonicalText;
}

CanonicalParsedSource::CanonicalParsedSource(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalParsedSource::~CanonicalParsedSource() noexcept(false) = default;
CanonicalParsedSource::CanonicalParsedSource(CanonicalParsedSource&&) noexcept = default;
CanonicalParsedSource& CanonicalParsedSource::operator=(CanonicalParsedSource&&) noexcept = default;

zc::Maybe<CanonicalParsedSource> CanonicalParsedSource::fromParsed(
    zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
    zc::ArrayPtr<const uint8_t> sourceBytes, zc::StringPtr logicalName,
    CanonicalParserOptions options, const source::SourceManager& parsedSources,
    const source::BufferId& parsedBuffer, ast::Tree&& tree, ParsedTokenSnapshot&& tokens,
    zc::Vector<diagnostics::DiagnosticFact>&& facts,
    diagnostics::SourceDiagnosticProvenanceMap&& provenance) {
  auto computedDigest = identity::sha256(sourceBytes);
  if (!validSourceKey(canonicalSourceKey) || sourceBytes.size() > kMaximumSourceBytes ||
      logicalName.size() == 0 || logicalName.size() > kMaximumLogicalNameBytes ||
      computedDigest == zc::none || ZC_ASSERT_NONNULL(computedDigest) != contentDigest) {
    return zc::none;
  }
  auto detachedTokens = detachTokens(tokens.sourceManager, tokens.buffer, tokens.tokenValues,
                                     parsedSources, parsedBuffer, sourceBytes.size());
  if (detachedTokens == zc::none) { return zc::none; }
  const auto limits = sourceFactLimits(sourceBytes.size());
  auto factBytes = diagnostics::encodeDiagnosticFacts(zc::none, facts.asPtr(), limits);
  auto provenanceBytes = diagnostics::encodeSourceDiagnosticProvenance(
      zc::none, provenance, sourceProvenanceLimits(sourceBytes.size()));
  if (factBytes == zc::none || provenanceBytes == zc::none ||
      diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(factBytes).asPtr(), limits) ==
          zc::none ||
      diagnostics::decodeSourceDiagnosticProvenance(
          zc::none, ZC_ASSERT_NONNULL(provenanceBytes).asPtr(), sourceBytes.size(),
          sourceProvenanceLimits(sourceBytes.size())) == zc::none ||
      !diagnostics::validateDiagnosticProvenance(facts.asPtr(), provenance)) {
    return zc::none;
  }
  auto body = encodeBody(canonicalSourceKey, contentDigest, sourceBytes, logicalName, options, tree,
                         parsedSources, parsedBuffer, ZC_ASSERT_NONNULL(detachedTokens).asPtr(),
                         facts.asPtr(), provenance);
  if (body == zc::none) { return zc::none; }
  auto encoded = wrapBody(ZC_ASSERT_NONNULL(body).asPtr());
  return decodeCanonical(encoded.asPtr());
}

zc::Maybe<CanonicalParsedSource> CanonicalParsedSource::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder outer(encoded);
  auto domain = outer.decodeByteString(kParsedSourceDomain.size());
  auto body = outer.decodeByteString(kMaximumValueBodyBytes);
  auto fingerprint = outer.decodeDigest();
  auto computedFingerprint = body == zc::none ? zc::Maybe<identity::Sha256Digest>()
                                              : identity::sha256(ZC_ASSERT_NONNULL(body).asPtr());
  if (domain == zc::none || body == zc::none || fingerprint == zc::none || !outer.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kParsedSourceDomain.asBytes() ||
      computedFingerprint == zc::none ||
      ZC_ASSERT_NONNULL(computedFingerprint) != ZC_ASSERT_NONNULL(fingerprint)) {
    return zc::none;
  }

  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(body).asPtr());
  auto sourceKey = decoder.decodeByteString(kMaximumSourceKeyBytes);
  auto contentDigest = decoder.decodeDigest();
  auto sourceBytes = decoder.decodeByteString(kMaximumSourceBytes);
  auto logicalName = decoder.decodeByteString(kMaximumLogicalNameBytes);
  auto useUnicode = decoder.decodeBool();
  auto allowDollarIdentifiers = decoder.decodeBool();
  auto supportRegexLiterals = decoder.decodeBool();
  auto schema = decoder.decodeByteString(zc::StringPtr(ast::kAstSchemaFingerprint).size());
  auto astBytes = decoder.decodeByteString(kMaximumAstBytes);
  auto computedContent = sourceBytes == zc::none
                             ? zc::Maybe<identity::Sha256Digest>()
                             : identity::sha256(ZC_ASSERT_NONNULL(sourceBytes).asPtr());
  if (sourceKey == zc::none || contentDigest == zc::none || sourceBytes == zc::none ||
      logicalName == zc::none || useUnicode == zc::none || allowDollarIdentifiers == zc::none ||
      supportRegexLiterals == zc::none || schema == zc::none || astBytes == zc::none ||
      !validSourceKey(ZC_ASSERT_NONNULL(sourceKey).asPtr()) ||
      ZC_ASSERT_NONNULL(logicalName).size() == 0 ||
      ZC_ASSERT_NONNULL(schema).asPtr() != zc::StringPtr(ast::kAstSchemaFingerprint).asBytes() ||
      computedContent == zc::none ||
      ZC_ASSERT_NONNULL(computedContent) != ZC_ASSERT_NONNULL(contentDigest)) {
    return zc::none;
  }

  auto retainedName = zc::str(ZC_ASSERT_NONNULL(logicalName).asChars());
  source::SourceManager sources;
  const source::BufferId buffer =
      sources.addMemBufferCopy(ZC_ASSERT_NONNULL(sourceBytes).asPtr(), retainedName);
  auto tree = ast::decodeCanonicalTree(ZC_ASSERT_NONNULL(astBytes).asPtr(), sources, buffer,
                                       ZC_ASSERT_NONNULL(sourceBytes).size());
  auto tokens = decodeTokens(decoder, ZC_ASSERT_NONNULL(sourceBytes).size());
  auto factBytes = decoder.decodeByteString(kMaximumFactBytes);
  auto provenanceBytes = decoder.decodeByteString(kMaximumProvenanceBytes);
  if (tree == zc::none || tokens == zc::none || factBytes == zc::none ||
      provenanceBytes == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto facts =
      diagnostics::decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(factBytes).asPtr(),
                                         sourceFactLimits(ZC_ASSERT_NONNULL(sourceBytes).size()));
  auto provenance = diagnostics::decodeSourceDiagnosticProvenance(
      zc::none, ZC_ASSERT_NONNULL(provenanceBytes).asPtr(), ZC_ASSERT_NONNULL(sourceBytes).size(),
      sourceProvenanceLimits(ZC_ASSERT_NONNULL(sourceBytes).size()));
  if (facts == zc::none || provenance == zc::none ||
      !diagnostics::validateDiagnosticProvenance(ZC_ASSERT_NONNULL(facts).asPtr(),
                                                 ZC_ASSERT_NONNULL(provenance))) {
    return zc::none;
  }
  auto sourceFileName = ast::canonicalSourceFileName(ZC_ASSERT_NONNULL(tree));
  if (sourceFileName == zc::none || ZC_ASSERT_NONNULL(sourceFileName) != retainedName) {
    return zc::none;
  }

  auto canonicalBytes = zc::heapArray<uint8_t>(encoded);
  auto sourceKeyBytes = zc::mv(ZC_ASSERT_NONNULL(sourceKey));
  auto retainedSourceBytes = zc::mv(ZC_ASSERT_NONNULL(sourceBytes));
  auto retainedTree = zc::mv(ZC_ASSERT_NONNULL(tree));
  auto retainedTokens = zc::mv(ZC_ASSERT_NONNULL(tokens));
  auto retainedFacts = zc::mv(ZC_ASSERT_NONNULL(facts));
  auto retainedProvenance = zc::mv(ZC_ASSERT_NONNULL(provenance));
  auto owned = zc::heap<Impl>(zc::mv(canonicalBytes), zc::mv(sourceKeyBytes),
                              ZC_ASSERT_NONNULL(contentDigest), zc::mv(retainedSourceBytes),
                              zc::mv(retainedName),
                              CanonicalParserOptions{ZC_ASSERT_NONNULL(useUnicode),
                                                     ZC_ASSERT_NONNULL(allowDollarIdentifiers),
                                                     ZC_ASSERT_NONNULL(supportRegexLiterals)},
                              zc::mv(retainedTree), zc::mv(retainedTokens), zc::mv(retainedFacts),
                              zc::mv(retainedProvenance));

  auto rehydratedTree =
      ast::decodeCanonicalTree(ZC_ASSERT_NONNULL(astBytes).asPtr(), owned->sourceManager,
                               owned->buffer, owned->sourceBytes.size());
  if (rehydratedTree == zc::none) { return zc::none; }
  owned->tree = zc::mv(ZC_ASSERT_NONNULL(rehydratedTree));
  return CanonicalParsedSource(zc::mv(owned));
}

CanonicalParsedSource CanonicalParsedSource::clone() const {
  return ZC_REQUIRE_NONNULL(decodeCanonical(impl->canonicalBytes.asPtr()));
}

zc::Array<uint8_t> CanonicalParsedSource::encodeCanonical() const {
  return zc::heapArray<uint8_t>(impl->canonicalBytes.asPtr());
}

zc::ArrayPtr<const uint8_t> CanonicalParsedSource::canonicalSourceKey() const {
  return impl->sourceKey.asPtr();
}
const identity::Sha256Digest& CanonicalParsedSource::contentDigest() const noexcept {
  return impl->contentDigest;
}
zc::ArrayPtr<const uint8_t> CanonicalParsedSource::sourceBytes() const {
  return impl->sourceBytes.asPtr();
}
zc::StringPtr CanonicalParsedSource::logicalName() const { return impl->logicalName; }
CanonicalParserOptions CanonicalParsedSource::options() const noexcept { return impl->options; }
const source::SourceManager& CanonicalParsedSource::sourceManager() const noexcept {
  return impl->sourceManager;
}
const source::BufferId& CanonicalParsedSource::buffer() const noexcept { return impl->buffer; }
const ast::Tree& CanonicalParsedSource::tree() const noexcept { return impl->tree; }
zc::ArrayPtr<const CanonicalParsedToken> CanonicalParsedSource::tokens() const {
  return impl->tokens.asPtr();
}
zc::ArrayPtr<const diagnostics::DiagnosticFact> CanonicalParsedSource::facts() const {
  return impl->facts.asPtr();
}
const diagnostics::SourceDiagnosticProvenanceMap& CanonicalParsedSource::provenance()
    const noexcept {
  return impl->provenance;
}

}  // namespace zomlang::compiler::parser
