// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/parsed-module.h"

#include <climits>
#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/io.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/dump.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kReceiptDomain[] = "zom.parsed-module";

ParsedModuleInvariantFact failure(ParsedModuleInvariantKind kind) { return {kind, 1}; }

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    bytes.add(static_cast<uint8_t>(value >> (56 - index * 8)));
  }
}

zc::Maybe<identity::Sha256Digest> parserSchemaDigest() {
  auto decoded =
      zc::decodeHex(zc::arrayPtr(ast::kAstSchemaFingerprint, ast::kAstSchemaFingerprint + 64));
  if (decoded.hadErrors) { return zc::none; }
  return identity::Sha256Digest::fromBytes(decoded.asPtr());
}

zc::Maybe<identity::SourceFileKey> decodeSourceKey(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(source).encode().asPtr() != bytes) {
    return zc::none;
  }
  return source;
}

zc::Maybe<uint64_t> sourceOffset(const parser::CanonicalParsedSource& parsed,
                                 source::SourceLoc location) {
  if (location.isInvalid()) { return zc::none; }
  const auto range = parsed.sourceManager().getRangeForBuffer(parsed.buffer());
  if (location < range.getStart() || location > range.getEnd()) { return zc::none; }
  return parsed.sourceManager().getLocOffsetInBuffer(location, parsed.buffer());
}

zc::Maybe<zc::Array<uint8_t>> schemaDump(const parser::CanonicalParsedSource& parsed) {
  const auto& tree = parsed.tree();
  if (!ast::verifySchema(tree)) { return zc::none; }
  zc::Vector<uint64_t> offsets;
  for (const auto& node : tree.nodes()) {
    auto start = sourceOffset(parsed, node.range.getStart());
    auto end = sourceOffset(parsed, node.range.getEnd());
    if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end)) {
      return zc::none;
    }
    offsets.add(ZC_ASSERT_NONNULL(start));
    offsets.add(ZC_ASSERT_NONNULL(end));
  }
  zc::VectorOutputStream output;
  if (ast::dumpTree(output, tree, parsed.sourceManager(), ast::AstDumpFormat::Json) != zc::none) {
    return zc::none;
  }
  zc::Vector<uint8_t> bytes;
  bytes.addAll(output.getArray());
  appendUint64(bytes, tree.nodeCount());
  for (const auto offset : offsets) { appendUint64(bytes, offset); }
  return bytes.releaseAsArray();
}

bool hasSyntaxErrors(const parser::CanonicalParsedSource& parsed) {
  for (const auto& fact : parsed.facts()) {
    if (diagnostics::getDiagnosticInfo(fact.code).severity >= diagnostics::DiagSeverity::kError) {
      return true;
    }
  }
  return false;
}

size_t lowerBoundTokenStart(zc::ArrayPtr<const parser::CanonicalParsedToken> tokens,
                            uint64_t start) {
  size_t first = 0;
  size_t count = tokens.size();
  while (count != 0) {
    const size_t step = count / 2;
    const size_t middle = first + step;
    if (tokens[middle].byteStart < start) {
      first = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first;
}

}  // namespace

ParsedModuleReceipt::ParsedModuleReceipt(const identity::Sha256Digest& digest) noexcept
    : value(digest) {}

const identity::Sha256Digest& ParsedModuleReceipt::digest() const noexcept { return value; }

zc::Maybe<ParsedModuleReceipt> ParsedModuleReceipt::compute(
    zc::ArrayPtr<const uint8_t> expandedSourceFile, const identity::Sha256Digest& contentDigest,
    uint64_t byteLength, const identity::Sha256Digest& parserSchemaDigest,
    zc::ArrayPtr<const uint8_t> astSchemaDump) {
  zc::Vector<uint8_t> bytes;
  for (size_t index = 0; index < sizeof(kReceiptDomain) - 1; ++index) {
    bytes.add(static_cast<uint8_t>(kReceiptDomain[index]));
  }
  bytes.add(0);
  bytes.addAll(expandedSourceFile);
  bytes.addAll(contentDigest.bytes());
  appendUint64(bytes, byteLength);
  bytes.addAll(parserSchemaDigest.bytes());
  appendUint64(bytes, astSchemaDump.size());
  bytes.addAll(astSchemaDump);
  ZC_IF_SOME(digest, identity::sha256(bytes.asPtr())) { return ParsedModuleReceipt(digest); }
  return zc::none;
}

struct CanonicalParsedModule::Impl final {
  Impl(identity::ImmutableSourceSnapshot&& snapshot,
       parser::CanonicalParsedSource&& parsedSource) noexcept
      : snapshot(zc::mv(snapshot)), parsedSource(zc::mv(parsedSource)) {}

  identity::ImmutableSourceSnapshot snapshot;
  parser::CanonicalParsedSource parsedSource;
};

CanonicalParsedModule::CanonicalParsedModule(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CanonicalParsedModule::~CanonicalParsedModule() noexcept(false) = default;
CanonicalParsedModule::CanonicalParsedModule(CanonicalParsedModule&&) noexcept = default;
CanonicalParsedModule& CanonicalParsedModule::operator=(CanonicalParsedModule&&) noexcept = default;

zc::Maybe<CanonicalParsedModule> CanonicalParsedModule::fromQueryResult(
    parser::CanonicalParsedSource&& parsedSource) {
  auto sourceKey = decodeSourceKey(parsedSource.canonicalSourceKey());
  if (sourceKey == zc::none) { return zc::none; }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      zc::mv(ZC_ASSERT_NONNULL(sourceKey)), zc::heapArray(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  return CanonicalParsedModule(
      zc::heap<Impl>(zc::mv(ZC_ASSERT_NONNULL(snapshot)), zc::mv(parsedSource)));
}

CanonicalParsedModule CanonicalParsedModule::clone() const {
  return ZC_REQUIRE_NONNULL(fromQueryResult(impl->parsedSource.clone()));
}
const identity::SourceFileKey& CanonicalParsedModule::source() const noexcept {
  return impl->snapshot.source();
}
const identity::Sha256Digest& CanonicalParsedModule::contentDigest() const noexcept {
  return impl->snapshot.contentDigest();
}
uint64_t CanonicalParsedModule::byteLength() const noexcept {
  return impl->snapshot.bytes().size();
}
const ast::Tree& CanonicalParsedModule::tree() const noexcept { return impl->parsedSource.tree(); }
const parser::CanonicalParsedSource& CanonicalParsedModule::queryResult() const noexcept {
  return impl->parsedSource;
}

struct VerifiedParsedModule::Impl final {
  Impl(identity::SourceFileId sourceFile, source::SourceLoc materializedStart,
       CanonicalParsedModule&& syntax, ParsedModuleReceipt receipt)
      : sourceFile(sourceFile),
        materializedStart(materializedStart),
        syntax(zc::mv(syntax)),
        receipt(receipt) {}

  identity::SourceFileId sourceFile;
  source::SourceLoc materializedStart;
  CanonicalParsedModule syntax;
  ParsedModuleReceipt receipt;
};

VerifiedParsedModule::VerifiedParsedModule(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
VerifiedParsedModule::~VerifiedParsedModule() noexcept(false) = default;
VerifiedParsedModule::VerifiedParsedModule(VerifiedParsedModule&&) noexcept = default;
VerifiedParsedModule& VerifiedParsedModule::operator=(VerifiedParsedModule&&) noexcept = default;

identity::SourceFileId VerifiedParsedModule::sourceFile() const noexcept {
  return impl->sourceFile;
}
const identity::SourceFileKey& VerifiedParsedModule::source() const noexcept {
  return impl->syntax.source();
}
const identity::Sha256Digest& VerifiedParsedModule::contentDigest() const noexcept {
  return impl->syntax.contentDigest();
}
uint64_t VerifiedParsedModule::byteLength() const noexcept { return impl->syntax.byteLength(); }
const ast::Tree& VerifiedParsedModule::tree() const noexcept { return impl->syntax.tree(); }
SourceBackedSyntaxView VerifiedParsedModule::sourceBackedSyntax() const noexcept {
  return SourceBackedSyntaxView(impl->syntax.tree(), impl->syntax.queryResult().sourceManager());
}
const CanonicalParsedModule& VerifiedParsedModule::syntax() const noexcept { return impl->syntax; }
const ParsedModuleReceipt& VerifiedParsedModule::receipt() const noexcept { return impl->receipt; }

zc::Maybe<identity::SourceSpan> CanonicalParsedModule::spanFor(source::SourceRange range) const {
  auto start = sourceOffset(impl->parsedSource, range.getStart());
  auto end = sourceOffset(impl->parsedSource, range.getEnd());
  if (start == zc::none || end == zc::none || ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end)) {
    return zc::none;
  }
  return impl->snapshot.span(ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end));
}

zc::Maybe<identity::SourceSpan> CanonicalParsedModule::retainedTokenSpan(
    ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const {
  if (!tree().contains(owner)) { return zc::none; }
  auto ownerSpan = spanFor(tree().node(owner).range);
  if (ownerSpan == zc::none) { return zc::none; }
  const auto tokens = impl->parsedSource.tokens();
  const auto& span = ZC_ASSERT_NONNULL(ownerSpan);
  const size_t first = lowerBoundTokenStart(tokens, span.byteStart());
  if (first >= tokens.size() || tokens[first].byteStart != span.byteStart() ||
      static_cast<size_t>(tokenOrdinal) >= tokens.size() - first) {
    return zc::none;
  }
  const auto& token = tokens[first + static_cast<size_t>(tokenOrdinal)];
  if (token.kind != expectedKind || token.byteStart < span.byteStart() ||
      token.byteStart >= token.byteEnd || token.byteEnd > span.byteEnd()) {
    return zc::none;
  }
  return impl->snapshot.span(token.byteStart, token.byteEnd);
}

zc::Maybe<identity::SourceSpan> CanonicalParsedModule::functionParameterNameSpan(
    ast::NodeId parameter, ast::SyntaxKind expectedKind) const {
  if (!tree().contains(parameter) ||
      tree().node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl ||
      (expectedKind != ast::SyntaxKind::Identifier &&
       expectedKind != ast::SyntaxKind::ThisKeyword)) {
    return zc::none;
  }
  const auto& syntax = tree().node(parameter);
  auto parameterSpan = spanFor(syntax.range);
  if (parameterSpan == zc::none) { return zc::none; }
  uint64_t nameSearchStart = ZC_ASSERT_NONNULL(parameterSpan).byteStart();
  const ast::NodeId attributes(syntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
  if (attributes) {
    if (!tree().contains(attributes) ||
        tree().node(attributes).kind != ast::SyntaxKind::AttributeList) {
      return zc::none;
    }
    auto attributeSpan = spanFor(tree().node(attributes).range);
    if (attributeSpan == zc::none ||
        ZC_ASSERT_NONNULL(attributeSpan).byteStart() !=
            ZC_ASSERT_NONNULL(parameterSpan).byteStart() ||
        ZC_ASSERT_NONNULL(attributeSpan).byteEnd() > ZC_ASSERT_NONNULL(parameterSpan).byteEnd()) {
      return zc::none;
    }
    nameSearchStart = ZC_ASSERT_NONNULL(attributeSpan).byteEnd();
  }
  const auto tokens = impl->parsedSource.tokens();
  const size_t tokenIndex = lowerBoundTokenStart(tokens, nameSearchStart);
  if (tokenIndex >= tokens.size()) { return zc::none; }
  const auto& token = tokens[tokenIndex];
  if (token.kind != expectedKind || token.byteStart < nameSearchStart ||
      token.byteStart >= token.byteEnd ||
      token.byteEnd > ZC_ASSERT_NONNULL(parameterSpan).byteEnd()) {
    return zc::none;
  }
  const ast::IdentId name(syntax.payload.words[ast::kFunctionParameterDeclNameWord]);
  if (tree().ident(name) != token.canonicalText) { return zc::none; }
  return impl->snapshot.span(token.byteStart, token.byteEnd);
}

bool CanonicalParsedModule::functionParameterHasImplicitSelfType(ast::NodeId parameter) const {
  auto receiver = functionParameterNameSpan(parameter, ast::SyntaxKind::ThisKeyword);
  if (receiver == zc::none || !tree().contains(parameter)) { return false; }
  const auto& parameterSyntax = tree().node(parameter);
  const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
  if (!tree().contains(type) || tree().node(type).kind != ast::SyntaxKind::NamedTypeExpr) {
    return false;
  }
  const auto& typeSyntax = tree().node(type);
  const ast::NodeList arguments{typeSyntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                typeSyntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
  const ast::NodeId path(typeSyntax.payload.words[ast::kNamedTypeExprPathWord]);
  if (arguments.size != 0 || !tree().contains(path) ||
      tree().node(path).kind != ast::SyntaxKind::ModulePath) {
    return false;
  }
  const auto& pathSyntax = tree().node(path);
  const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (segments.size != 1 || !tree().contains(segments)) { return false; }
  const auto names = tree().identList(segments);
  if (names.size() != 1 || tree().ident(names[0]) != "Self"_zc) { return false; }

  auto parameterSpan = spanFor(parameterSyntax.range);
  auto typeSpan = spanFor(typeSyntax.range);
  auto pathSpan = spanFor(pathSyntax.range);
  if (parameterSpan == zc::none || typeSpan == zc::none || pathSpan == zc::none) { return false; }
  const auto& receiverSpan = ZC_ASSERT_NONNULL(receiver);
  const auto hasReceiverSpan = [&](const identity::SourceSpan& candidate) {
    return candidate.byteStart() == receiverSpan.byteStart() &&
           candidate.byteEnd() == receiverSpan.byteEnd();
  };
  return ZC_ASSERT_NONNULL(parameterSpan).byteEnd() == receiverSpan.byteEnd() &&
         hasReceiverSpan(ZC_ASSERT_NONNULL(typeSpan)) &&
         hasReceiverSpan(ZC_ASSERT_NONNULL(pathSpan));
}

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::spanFor(source::SourceRange range) const {
  return impl->syntax.spanFor(range);
}

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::retainedTokenSpan(
    ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const {
  return impl->syntax.retainedTokenSpan(owner, tokenOrdinal, expectedKind);
}

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::functionParameterNameSpan(
    ast::NodeId parameter, ast::SyntaxKind expectedKind) const {
  return impl->syntax.functionParameterNameSpan(parameter, expectedKind);
}

bool VerifiedParsedModule::functionParameterHasImplicitSelfType(ast::NodeId parameter) const {
  return impl->syntax.functionParameterHasImplicitSelfType(parameter);
}

zc::Maybe<source::SourceLoc> VerifiedParsedModule::sourceLocFor(
    const identity::SourceSpan& span) const {
  if (!span.belongsTo(impl->syntax.source()) || span.byteStart() > span.byteEnd() ||
      span.byteEnd() > impl->syntax.byteLength() || span.byteStart() > UINT_MAX) {
    return zc::none;
  }
  return impl->materializedStart.getAdvancedLoc(static_cast<unsigned>(span.byteStart()));
}

identity::SourceSpan VerifiedParsedModule::rootSpan() const {
  auto span = spanFor(tree().node(tree().root()).range);
  return zc::mv(ZC_ASSERT_NONNULL(span));
}

ParsedModuleVerificationResult ParsedModuleVerifier::verifyQueryResult(
    identity::SemanticContextBrand context, const identity::SemanticIdentityRegistrySet& registries,
    const identity::SourceFileKey& materializedSource,
    const source::SourceManager& materializedSources, const source::BufferId& materializedBuffer,
    parser::CanonicalParsedSource&& parsedSource) {
  if (!context.isValid() || !registries.sourceFiles().isFrozen()) {
    return failure(ParsedModuleInvariantKind::RegistryMismatch);
  }
  if (hasSyntaxErrors(parsedSource)) {
    return failure(ParsedModuleInvariantKind::SyntaxDiagnosticsPresent);
  }
  auto sourceKey = decodeSourceKey(parsedSource.canonicalSourceKey());
  if (sourceKey == zc::none) { return failure(ParsedModuleInvariantKind::SourceMismatch); }
  if (ZC_ASSERT_NONNULL(sourceKey).encode().asPtr() != materializedSource.encode().asPtr() ||
      materializedSources.getEntireTextForBuffer(materializedBuffer) !=
          parsedSource.sourceBytes()) {
    return failure(ParsedModuleInvariantKind::SourceMismatch);
  }
  auto sourceFile = registries.sourceFiles().find(ZC_ASSERT_NONNULL(sourceKey));
  if (sourceFile == zc::none || !ZC_ASSERT_NONNULL(sourceFile).belongsTo(context)) {
    return failure(ParsedModuleInvariantKind::RegistryMismatch);
  }
  auto snapshot = registries.sourceSnapshot(ZC_ASSERT_NONNULL(sourceFile));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest() ||
      ZC_ASSERT_NONNULL(snapshot).bytes() != parsedSource.sourceBytes()) {
    return failure(ParsedModuleInvariantKind::SourceMismatch);
  }
  if (!ast::verifySchema(parsedSource.tree())) {
    return failure(ParsedModuleInvariantKind::InvalidTree);
  }
  const auto tokens = parsedSource.tokens();
  if (tokens.size() == 0 || tokens.back().kind != ast::SyntaxKind::EndOfFile ||
      tokens.back().byteStart != parsedSource.sourceBytes().size() ||
      tokens.back().byteEnd != parsedSource.sourceBytes().size()) {
    return failure(ParsedModuleInvariantKind::InvalidTokenProvenance);
  }
  auto dump = schemaDump(parsedSource);
  auto schemaDigest = parserSchemaDigest();
  if (dump == zc::none || schemaDigest == zc::none) {
    return failure(ParsedModuleInvariantKind::InvalidSourceRange);
  }
  auto receipt = ParsedModuleReceipt::compute(
      parsedSource.canonicalSourceKey(), parsedSource.contentDigest(),
      parsedSource.sourceBytes().size(), ZC_ASSERT_NONNULL(schemaDigest),
      ZC_ASSERT_NONNULL(dump).asPtr());
  if (receipt == zc::none) { return failure(ParsedModuleInvariantKind::ReceiptMismatch); }
  auto syntax = CanonicalParsedModule::fromQueryResult(zc::mv(parsedSource));
  if (syntax == zc::none) { return failure(ParsedModuleInvariantKind::InvalidTree); }
  return VerifiedParsedModule(zc::heap<VerifiedParsedModule::Impl>(
      ZC_ASSERT_NONNULL(sourceFile), materializedSources.getLocForBufferStart(materializedBuffer),
      zc::mv(ZC_ASSERT_NONNULL(syntax)), ZC_ASSERT_NONNULL(receipt)));
}

}  // namespace zomlang::compiler::binder
