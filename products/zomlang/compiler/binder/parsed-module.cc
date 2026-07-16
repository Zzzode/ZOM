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
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::binder {
namespace {

constexpr char kReceiptDomain[] = "zom.parsed-module.v0";

struct ParsedTokenOffset final {
  ParsedTokenOffset(ast::SyntaxKind kind, uint64_t start, uint64_t end,
                    zc::String&& canonicalText) noexcept
      : kind(kind), start(start), end(end), canonicalText(zc::mv(canonicalText)) {}
  ParsedTokenOffset(ParsedTokenOffset&&) noexcept = default;
  ParsedTokenOffset& operator=(ParsedTokenOffset&&) noexcept = default;
  ZC_DISALLOW_COPY(ParsedTokenOffset);
  ast::SyntaxKind kind;
  uint64_t start;
  uint64_t end;
  zc::String canonicalText;
};

size_t lowerBoundTokenStart(zc::ArrayPtr<const ParsedTokenOffset> tokens, uint64_t start) {
  size_t first = 0;
  size_t count = tokens.size();
  while (count != 0) {
    const size_t step = count / 2;
    const size_t middle = first + step;
    if (tokens[middle].start < start) {
      first = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first;
}

ParsedModuleInvariantFact failure(ParsedModuleInvariantKind kind) { return {kind, 1}; }

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    bytes.add(static_cast<uint8_t>(value >> (56 - index * 8)));
  }
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

zc::Maybe<identity::Sha256Digest> parserSchemaDigest() {
  auto decoded =
      zc::decodeHex(zc::arrayPtr(ast::kAstSchemaFingerprint, ast::kAstSchemaFingerprint + 64));
  if (decoded.hadErrors) { return zc::none; }
  return identity::Sha256Digest::fromBytes(decoded.asPtr());
}

bool offsetsFor(source::SourceRange range, zc::ArrayPtr<const zc::byte> sourceBytes,
                uint64_t& start, uint64_t& end) {
  if (!range.isValid()) { return false; }
  const auto baseAddress = reinterpret_cast<uintptr_t>(sourceBytes.begin());
  const auto limitAddress = baseAddress + sourceBytes.size();
  if (limitAddress < baseAddress) { return false; }
  const auto startAddress = reinterpret_cast<uintptr_t>(range.getStart().getOpaqueValue());
  const auto endAddress = reinterpret_cast<uintptr_t>(range.getEnd().getOpaqueValue());
  if (startAddress < baseAddress || startAddress > endAddress || endAddress > limitAddress) {
    return false;
  }
  start = startAddress - baseAddress;
  end = endAddress - baseAddress;
  return true;
}

zc::Maybe<zc::Array<ParsedTokenOffset>> admitTokenOffsets(
    zc::ArrayPtr<parser::ParsedTokenRange> tokens, zc::ArrayPtr<const zc::byte> sourceBytes) {
  if (tokens.size() == 0) { return zc::none; }
  zc::Vector<ParsedTokenOffset> offsets;
  uint64_t previousEnd = 0;
  for (size_t index = 0; index < tokens.size(); ++index) {
    auto& token = tokens[index];
    uint64_t start = 0;
    uint64_t end = 0;
    if (!offsetsFor(token.source, sourceBytes, start, end) || start < previousEnd) {
      return zc::none;
    }
    const bool isEof = token.kind == ast::SyntaxKind::EndOfFile;
    if (isEof != (index + 1 == tokens.size()) ||
        (isEof && (start != end || end != sourceBytes.size())) ||
        (!isEof && (token.kind == ast::SyntaxKind::Unknown || start == end))) {
      return zc::none;
    }
    offsets.add(ParsedTokenOffset(token.kind, start, end, zc::mv(token.canonicalText)));
    previousEnd = end;
  }
  return offsets.releaseAsArray();
}

zc::Maybe<zc::Array<uint8_t>> schemaDump(const ast::Tree& tree,
                                         const source::SourceManager& sources,
                                         zc::ArrayPtr<const zc::byte> sourceBytes) {
  if (!ast::verifySchema(tree)) { return zc::none; }
  zc::Vector<uint64_t> offsets;
  for (const auto& node : tree.nodes()) {
    uint64_t start = 0;
    uint64_t end = 0;
    if (!offsetsFor(node.range, sourceBytes, start, end)) { return zc::none; }
    offsets.add(start);
    offsets.add(end);
  }
  zc::VectorOutputStream output;
  if (ast::dumpTree(output, tree, sources, ast::AstDumpFormat::Json) != zc::none) {
    return zc::none;
  }
  zc::Vector<uint8_t> bytes;
  bytes.addAll(output.getArray());
  appendUint64(bytes, tree.nodeCount());
  for (const auto offset : offsets) { appendUint64(bytes, offset); }
  return bytes.releaseAsArray();
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

struct UnbrandedParsedModule::Impl final {
  Impl(identity::SourceFileKey&& source, const identity::Sha256Digest& contentDigest,
       uint64_t byteLength, const source::SourceManager& sources, const source::BufferId& buffer,
       zc::Array<ParsedTokenOffset>&& tokens, ast::Tree&& tree,
       identity::Sha256Digest parserSchemaDigest, zc::Array<uint8_t>&& schemaDump,
       ParsedModuleReceipt receipt)
      : source(zc::mv(source)),
        contentDigest(contentDigest),
        byteLength(byteLength),
        sources(sources),
        buffer(buffer),
        tokens(zc::mv(tokens)),
        tree(zc::mv(tree)),
        parserSchemaDigest(parserSchemaDigest),
        schemaDump(zc::mv(schemaDump)),
        receipt(receipt) {}

  identity::SourceFileKey source;
  identity::Sha256Digest contentDigest;
  uint64_t byteLength;
  const source::SourceManager& sources;
  source::BufferId buffer;
  zc::Array<ParsedTokenOffset> tokens;
  ast::Tree tree;
  identity::Sha256Digest parserSchemaDigest;
  zc::Array<uint8_t> schemaDump;
  ParsedModuleReceipt receipt;
};

UnbrandedParsedModule::UnbrandedParsedModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
UnbrandedParsedModule::~UnbrandedParsedModule() noexcept(false) = default;
UnbrandedParsedModule::UnbrandedParsedModule(UnbrandedParsedModule&&) noexcept = default;
UnbrandedParsedModule& UnbrandedParsedModule::operator=(UnbrandedParsedModule&&) noexcept = default;
const ParsedModuleReceipt& UnbrandedParsedModule::receipt() const noexcept { return impl->receipt; }

struct VerifiedParsedModule::Impl final {
  Impl(identity::SourceFileId sourceFile, identity::ImmutableSourceSnapshot&& snapshot,
       const source::SourceManager& sources, const source::BufferId& buffer,
       zc::Array<ParsedTokenOffset>&& tokens, ast::Tree&& tree, ParsedModuleReceipt receipt)
      : sourceFile(sourceFile),
        snapshot(zc::mv(snapshot)),
        sources(sources),
        buffer(buffer),
        tokens(zc::mv(tokens)),
        tree(zc::mv(tree)),
        receipt(receipt) {}

  identity::SourceFileId sourceFile;
  identity::ImmutableSourceSnapshot snapshot;
  const source::SourceManager& sources;
  source::BufferId buffer;
  zc::Array<ParsedTokenOffset> tokens;
  ast::Tree tree;
  ParsedModuleReceipt receipt;
};

VerifiedParsedModule::VerifiedParsedModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedParsedModule::~VerifiedParsedModule() noexcept(false) = default;
VerifiedParsedModule::VerifiedParsedModule(VerifiedParsedModule&&) noexcept = default;
VerifiedParsedModule& VerifiedParsedModule::operator=(VerifiedParsedModule&&) noexcept = default;
identity::SourceFileId VerifiedParsedModule::sourceFile() const noexcept {
  return impl->sourceFile;
}
const identity::Sha256Digest& VerifiedParsedModule::contentDigest() const noexcept {
  return impl->snapshot.contentDigest();
}
uint64_t VerifiedParsedModule::byteLength() const noexcept { return impl->snapshot.bytes().size(); }
const ast::Tree& VerifiedParsedModule::tree() const noexcept { return impl->tree; }
const ParsedModuleReceipt& VerifiedParsedModule::receipt() const noexcept { return impl->receipt; }

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::spanFor(source::SourceRange range) const {
  const auto sourceBytes = impl->sources.getEntireTextForBuffer(impl->buffer);
  uint64_t start = 0;
  uint64_t end = 0;
  if (!offsetsFor(range, sourceBytes, start, end)) { return zc::none; }
  return impl->snapshot.span(start, end);
}

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::retainedTokenSpan(
    ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const {
  if (!impl->tree.contains(owner)) { return zc::none; }
  auto ownerSpan = spanFor(impl->tree.node(owner).range);
  if (ownerSpan == zc::none) { return zc::none; }
  ZC_IF_SOME(span, ownerSpan) {
    const size_t first = lowerBoundTokenStart(impl->tokens.asPtr(), span.byteStart());
    if (first >= impl->tokens.size()) { return zc::none; }
    if (impl->tokens[first].start != span.byteStart() ||
        static_cast<size_t>(tokenOrdinal) >= impl->tokens.size() - first) {
      return zc::none;
    }
    const auto& token = impl->tokens[first + static_cast<size_t>(tokenOrdinal)];
    if (token.kind != expectedKind || token.start < span.byteStart() || token.start >= token.end ||
        token.end > span.byteEnd()) {
      return zc::none;
    }
    return impl->snapshot.span(token.start, token.end);
  }
  ZC_UNREACHABLE;
}

zc::Maybe<identity::SourceSpan> VerifiedParsedModule::functionParameterNameSpan(
    ast::NodeId parameter, ast::SyntaxKind expectedKind) const {
  if (!impl->tree.contains(parameter) ||
      impl->tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl ||
      (expectedKind != ast::SyntaxKind::Identifier &&
       expectedKind != ast::SyntaxKind::ThisKeyword)) {
    return zc::none;
  }
  const auto& syntax = impl->tree.node(parameter);
  auto parameterSpan = spanFor(syntax.range);
  if (parameterSpan == zc::none) { return zc::none; }
  uint64_t nameSearchStart = ZC_ASSERT_NONNULL(parameterSpan).byteStart();
  const ast::NodeId attributes(syntax.payload.words[ast::kFunctionParameterDeclAttrsWord]);
  if (attributes) {
    if (!impl->tree.contains(attributes) ||
        impl->tree.node(attributes).kind != ast::SyntaxKind::AttributeList) {
      return zc::none;
    }
    auto attributeSpan = spanFor(impl->tree.node(attributes).range);
    if (attributeSpan == zc::none ||
        ZC_ASSERT_NONNULL(attributeSpan).byteStart() !=
            ZC_ASSERT_NONNULL(parameterSpan).byteStart() ||
        ZC_ASSERT_NONNULL(attributeSpan).byteEnd() > ZC_ASSERT_NONNULL(parameterSpan).byteEnd()) {
      return zc::none;
    }
    nameSearchStart = ZC_ASSERT_NONNULL(attributeSpan).byteEnd();
  }
  const size_t tokenIndex = lowerBoundTokenStart(impl->tokens.asPtr(), nameSearchStart);
  if (tokenIndex >= impl->tokens.size()) { return zc::none; }
  const auto& token = impl->tokens[tokenIndex];
  if (token.kind != expectedKind || token.start < nameSearchStart || token.start >= token.end ||
      token.end > ZC_ASSERT_NONNULL(parameterSpan).byteEnd()) {
    return zc::none;
  }
  const ast::IdentId name(syntax.payload.words[ast::kFunctionParameterDeclNameWord]);
  if (impl->tree.ident(name) != token.canonicalText) { return zc::none; }
  return impl->snapshot.span(token.start, token.end);
}

bool VerifiedParsedModule::functionParameterHasImplicitSelfType(ast::NodeId parameter) const {
  auto receiver = functionParameterNameSpan(parameter, ast::SyntaxKind::ThisKeyword);
  if (receiver == zc::none || !impl->tree.contains(parameter)) { return false; }
  const auto& parameterSyntax = impl->tree.node(parameter);
  const ast::NodeId type(parameterSyntax.payload.words[ast::kFunctionParameterDeclTyWord]);
  if (!impl->tree.contains(type) || impl->tree.node(type).kind != ast::SyntaxKind::NamedTypeExpr) {
    return false;
  }
  const auto& typeSyntax = impl->tree.node(type);
  const ast::NodeList arguments{typeSyntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                typeSyntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
  const ast::NodeId path(typeSyntax.payload.words[ast::kNamedTypeExprPathWord]);
  if (arguments.size != 0 || !impl->tree.contains(path) ||
      impl->tree.node(path).kind != ast::SyntaxKind::ModulePath) {
    return false;
  }
  const auto& pathSyntax = impl->tree.node(path);
  const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (segments.size != 1 || !impl->tree.contains(segments)) { return false; }
  const auto names = impl->tree.identList(segments);
  if (names.size() != 1 || impl->tree.ident(names[0]) != "Self"_zc) { return false; }

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

zc::Maybe<source::SourceLoc> VerifiedParsedModule::sourceLocFor(
    const identity::SourceSpan& span) const {
  if (!span.belongsTo(impl->snapshot.source()) || span.byteStart() > span.byteEnd() ||
      span.byteEnd() > impl->snapshot.bytes().size() || span.byteStart() > UINT_MAX) {
    return zc::none;
  }
  return impl->sources.getLocForOffset(impl->buffer, static_cast<unsigned>(span.byteStart()));
}

identity::SourceSpan VerifiedParsedModule::rootSpan() const {
  auto span = spanFor(impl->tree.node(impl->tree.root()).range);
  return zc::mv(ZC_ASSERT_NONNULL(span));
}

ParsedModuleAdmissionResult ParsedModuleVerifier::admit(
    const identity::ImmutableSourceSnapshot& snapshot, const source::SourceManager& sources,
    const source::BufferId& buffer, parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree) {
  const auto sourceBytes = sources.getEntireTextForBuffer(buffer);
  if (tokens.sourceManager != &sources || tokens.buffer != buffer ||
      !sameBytes(sourceBytes, snapshot.bytes())) {
    return failure(ParsedModuleInvariantKind::SourceMismatch);
  }
  auto tokenOffsets = admitTokenOffsets(tokens.tokenValues.asPtr(), sourceBytes);
  if (tokenOffsets == zc::none) {
    return failure(ParsedModuleInvariantKind::InvalidTokenProvenance);
  }
  if (!ast::verifySchema(tree)) { return failure(ParsedModuleInvariantKind::InvalidTree); }
  auto dump = schemaDump(tree, sources, sourceBytes);
  if (dump == zc::none) { return failure(ParsedModuleInvariantKind::InvalidSourceRange); }
  auto schemaDigest = parserSchemaDigest();
  if (schemaDigest == zc::none) { return failure(ParsedModuleInvariantKind::InvalidTree); }
  ZC_IF_SOME(dumpValue, dump) {
    ZC_IF_SOME(schemaDigestValue, schemaDigest) {
      const auto sourceKey = snapshot.source().encode();
      auto receipt = ParsedModuleReceipt::compute(sourceKey.asPtr(), snapshot.contentDigest(),
                                                  snapshot.bytes().size(), schemaDigestValue,
                                                  dumpValue.asPtr());
      ZC_IF_SOME(receiptValue, receipt) {
        ZC_IF_SOME(tokenValues, tokenOffsets) {
          return UnbrandedParsedModule(zc::heap<UnbrandedParsedModule::Impl>(
              snapshot.source().clone(), snapshot.contentDigest(), snapshot.bytes().size(), sources,
              buffer, zc::mv(tokenValues), zc::mv(tree), schemaDigestValue, zc::mv(dumpValue),
              receiptValue));
        }
      }
    }
  }
  return failure(ParsedModuleInvariantKind::ReceiptMismatch);
}

ParsedModulePromotionResult ParsedModuleVerifier::promote(
    identity::SemanticContextBrand context, const identity::SemanticIdentityRegistrySet& registries,
    UnbrandedParsedModule&& parsedModule) {
  auto& parsed = *parsedModule.impl;
  auto sourceFile = registries.sourceFiles().find(parsed.source);
  if (!context.isValid() || !registries.sourceFiles().isFrozen() || sourceFile == zc::none) {
    return failure(ParsedModuleInvariantKind::RegistryMismatch);
  }
  ZC_IF_SOME(sourceId, sourceFile) {
    if (!sourceId.belongsTo(context)) {
      return failure(ParsedModuleInvariantKind::RegistryMismatch);
    }
    auto snapshot = registries.sourceSnapshot(sourceId);
    if (snapshot == zc::none) { return failure(ParsedModuleInvariantKind::RegistryMismatch); }
    ZC_IF_SOME(snapshotValue, snapshot) {
      const auto currentBytes = parsed.sources.getEntireTextForBuffer(parsed.buffer);
      if (!sameBytes(currentBytes, snapshotValue.bytes()) ||
          parsed.contentDigest != snapshotValue.contentDigest() ||
          parsed.byteLength != snapshotValue.bytes().size()) {
        return failure(ParsedModuleInvariantKind::SourceMismatch);
      }
      auto dump = schemaDump(parsed.tree, parsed.sources, currentBytes);
      if (dump == zc::none) { return failure(ParsedModuleInvariantKind::InvalidSourceRange); }
      ZC_IF_SOME(dumpValue, dump) {
        const auto sourceKey = parsed.source.encode();
        auto receipt =
            ParsedModuleReceipt::compute(sourceKey.asPtr(), parsed.contentDigest, parsed.byteLength,
                                         parsed.parserSchemaDigest, dumpValue.asPtr());
        if (receipt == zc::none) { return failure(ParsedModuleInvariantKind::ReceiptMismatch); }
        ZC_IF_SOME(receiptValue, receipt) {
          if (receiptValue.digest() != parsed.receipt.digest()) {
            return failure(ParsedModuleInvariantKind::ReceiptMismatch);
          }
          return VerifiedParsedModule(zc::heap<VerifiedParsedModule::Impl>(
              sourceId, snapshotValue.clone(), parsed.sources, parsed.buffer, zc::mv(parsed.tokens),
              zc::mv(parsed.tree), receiptValue));
        }
      }
    }
  }
  return failure(ParsedModuleInvariantKind::RegistryMismatch);
}

}  // namespace zomlang::compiler::binder
