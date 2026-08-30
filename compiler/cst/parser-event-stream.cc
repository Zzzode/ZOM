// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/cst/parser-event-stream.h"

#include "compiler/ast/schema-verifier.h"
#include "compiler/cst/parse-eligibility.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/source/manager.h"
#include "zc/core/arena.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::cst {
namespace {

zc::Maybe<ByteRange> byteRange(source::SourceLoc sourceStart, uint64_t sourceByteCount,
                               source::SourceRange range) {
  if (range.isInvalid() || sourceStart.isInvalid()) return zc::none;
  const zc::byte* base = sourceStart.getOpaqueValue();
  const zc::byte* start = range.getStart().getOpaqueValue();
  const zc::byte* end = range.getEnd().getOpaqueValue();
  if (start < base || end < start || start > base + sourceByteCount ||
      end > base + sourceByteCount) {
    return zc::none;
  }
  return ByteRange{static_cast<uint64_t>(start - base), static_cast<uint64_t>(end - base)};
}

template <typename Id>
bool sameId(Id left, Id right) {
  return left.value == right.value;
}

ParseSyntaxFailure mapEligibility(ParseIneligibilityReason reason) {
  switch (reason) {
    case ParseIneligibilityReason::RecoveryStreamMismatch:
      return ParseSyntaxFailure::RecoveryStreamMismatch;
    case ParseIneligibilityReason::RecoveryPresent:
      return ParseSyntaxFailure::RecoveryPresent;
    case ParseIneligibilityReason::InvalidLexemePresent:
      return ParseSyntaxFailure::InvalidLexemePresent;
    case ParseIneligibilityReason::ParserErrorPresent:
      return ParseSyntaxFailure::ParserErrorPresent;
  }
  ZC_UNREACHABLE;
}

bool validNodeId(ast::NodeId id, uint32_t nodeCount) {
  return id.value != 0 && id.value <= nodeCount;
}

bool validIdentId(ast::IdentId id, uint32_t identCount) {
  return id.value != 0 && id.value <= identCount;
}

}  // namespace

ParserEventStreamId computeParserEventStreamId(zc::ArrayPtr<const ParserSyntaxEvent> events) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString("zom.cst-parser-events"_zcb);
  encoder.encodeSequenceSize(events.size());
  for (const auto& event : events) {
    if (event.is<ParserNodeEvent>()) {
      const auto& value = event.get<ParserNodeEvent>();
      encoder.encodeUint8(0x01);
      encoder.encodeUint32(static_cast<uint32_t>(value.kind));
      encoder.encodeUint64(value.range.start);
      encoder.encodeUint64(value.range.end);
      for (uint32_t word : value.payload.words) encoder.encodeUint32(word);
      encoder.encodeUint32(value.result.value);
    } else if (event.is<ParserNodeListEvent>()) {
      const auto& value = event.get<ParserNodeListEvent>();
      encoder.encodeUint8(0x02);
      encoder.encodeUint32(value.result.first);
      encoder.encodeUint32(value.result.size);
      encoder.encodeSequenceSize(value.nodes.size());
      for (auto node : value.nodes.asPtr()) encoder.encodeUint32(node.value);
    } else if (event.is<ParserIdentListEvent>()) {
      const auto& value = event.get<ParserIdentListEvent>();
      encoder.encodeUint8(0x03);
      encoder.encodeUint32(value.result.first);
      encoder.encodeUint32(value.result.size);
      encoder.encodeSequenceSize(value.identifiers.size());
      for (auto identifier : value.identifiers.asPtr()) encoder.encodeUint32(identifier.value);
    } else if (event.is<ParserStringEvent>()) {
      const auto& value = event.get<ParserStringEvent>();
      encoder.encodeUint8(0x04);
      encoder.encodeByteString(value.text.asBytes());
      encoder.encodeUint32(value.result.value);
    } else if (event.is<ParserIdentEvent>()) {
      const auto& value = event.get<ParserIdentEvent>();
      encoder.encodeUint8(0x05);
      encoder.encodeByteString(value.text.asBytes());
      encoder.encodeUint32(value.result.value);
    } else if (event.is<ParserBigIntEvent>()) {
      const auto& value = event.get<ParserBigIntEvent>();
      encoder.encodeUint8(0x06);
      encoder.encodeByteString(value.text.asBytes());
      encoder.encodeUint32(value.result.value);
    } else if (event.is<ParserFloatEvent>()) {
      const auto& value = event.get<ParserFloatEvent>();
      encoder.encodeUint8(0x07);
      encoder.encodeByteString(value.text.asBytes());
      encoder.encodeUint32(value.result.value);
    } else {
      encoder.encodeUint8(0x08);
      encoder.encodeUint32(event.get<ParserRootEvent>().root.value);
    }
  }
  auto digest = identity::sha256(encoder.finish().asPtr());
  ZC_IREQUIRE(digest != zc::none, "parser event stream identity must be computable");
  return ParserEventStreamId::fromDigest(ZC_ASSERT_NONNULL(digest));
}

struct ParserEventBuilder::Impl final {
  Impl(const source::SourceManager& sources, const source::BufferId& buffer)
      : sourceStart(sources.getLocForBufferStart(buffer)),
        sourceByteCount(sources.getEntireTextForBuffer(buffer).size()) {}

  source::SourceLoc sourceStart;
  uint64_t sourceByteCount;
  zc::Arena textArena;
  zc::HashMap<zc::StringPtr, uint32_t> strings;
  zc::HashMap<zc::StringPtr, uint32_t> identifiers;
  zc::HashMap<zc::StringPtr, uint32_t> bigInts;
  zc::HashMap<zc::StringPtr, uint32_t> floats;
  zc::Vector<ParserSyntaxEvent> events;
  uint32_t nodeCount = 0;
  uint32_t nodeListSize = 0;
  uint32_t identListSize = 0;
  uint32_t stringCount = 0;
  uint32_t identCount = 0;
  uint32_t bigIntCount = 0;
  uint32_t floatCount = 0;
  ast::NodeId root;
  bool valid = true;
  bool finished = false;
};

ParserEventBuilder::ParserEventBuilder(const source::SourceManager& sources,
                                       const source::BufferId& buffer)
    : impl(zc::heap<Impl>(sources, buffer)) {}
ParserEventBuilder::~ParserEventBuilder() noexcept(false) = default;
ParserEventBuilder::ParserEventBuilder(ParserEventBuilder&&) noexcept = default;
ParserEventBuilder& ParserEventBuilder::operator=(ParserEventBuilder&&) noexcept = default;

ast::NodeId ParserEventBuilder::makeNode(ast::SyntaxKind kind, source::SourceRange range,
                                         ast::NodePayload payload) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ++impl->nodeCount;
  const ast::NodeId result(impl->nodeCount);
  auto admittedRange = byteRange(impl->sourceStart, impl->sourceByteCount, range);
  if (admittedRange == zc::none) {
    impl->valid = false;
    impl->events.add(ParserSyntaxEvent(ParserNodeEvent{kind, ByteRange{}, payload, result}));
    return result;
  }
  ZC_IF_SOME(value, admittedRange) {
    impl->events.add(ParserSyntaxEvent(ParserNodeEvent{kind, value, payload, result}));
  }
  return result;
}

ast::NodeList ParserEventBuilder::makeList(zc::ArrayPtr<const ast::NodeId> nodes) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::NodeList result{impl->nodeListSize, static_cast<uint32_t>(nodes.size())};
  if (nodes.size() > UINT32_MAX || impl->nodeListSize > UINT32_MAX - nodes.size()) {
    impl->valid = false;
  } else {
    impl->nodeListSize += static_cast<uint32_t>(nodes.size());
  }
  for (ast::NodeId node : nodes) {
    if (!validNodeId(node, impl->nodeCount)) impl->valid = false;
  }
  impl->events.add(
      ParserSyntaxEvent(ParserNodeListEvent{zc::heapArray<ast::NodeId>(nodes), result}));
  return result;
}

ast::IdentList ParserEventBuilder::makeIdentList(zc::ArrayPtr<const ast::IdentId> identifiers) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::IdentList result{impl->identListSize, static_cast<uint32_t>(identifiers.size())};
  if (identifiers.size() > UINT32_MAX || impl->identListSize > UINT32_MAX - identifiers.size()) {
    impl->valid = false;
  } else {
    impl->identListSize += static_cast<uint32_t>(identifiers.size());
  }
  for (ast::IdentId identifier : identifiers) {
    if (!validIdentId(identifier, impl->identCount)) impl->valid = false;
  }
  impl->events.add(
      ParserSyntaxEvent(ParserIdentListEvent{zc::heapArray<ast::IdentId>(identifiers), result}));
  return result;
}

ast::StringId ParserEventBuilder::internString(zc::StringPtr value) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::StringId result;
  if (value.size() != 0) {
    ZC_IF_SOME(found, impl->strings.find(value)) {
      result = ast::StringId(found);
    } else {
      const zc::StringPtr retained = impl->textArena.copyString(value);
      ++impl->stringCount;
      impl->strings.insert(retained, impl->stringCount);
      result = ast::StringId(impl->stringCount);
    }
  }
  impl->events.add(ParserSyntaxEvent(ParserStringEvent{zc::str(value), result}));
  return result;
}

ast::IdentId ParserEventBuilder::internIdent(zc::StringPtr value) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::IdentId result;
  if (value.size() != 0) {
    ZC_IF_SOME(found, impl->identifiers.find(value)) {
      result = ast::IdentId(found);
    } else {
      const zc::StringPtr retained = impl->textArena.copyString(value);
      ++impl->identCount;
      impl->identifiers.insert(retained, impl->identCount);
      result = ast::IdentId(impl->identCount);
    }
  }
  impl->events.add(ParserSyntaxEvent(ParserIdentEvent{zc::str(value), result}));
  return result;
}

ast::BigIntId ParserEventBuilder::internBigInt(zc::StringPtr value) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::BigIntId result;
  if (value.size() != 0) {
    ZC_IF_SOME(found, impl->bigInts.find(value)) {
      result = ast::BigIntId(found);
    } else {
      const zc::StringPtr retained = impl->textArena.copyString(value);
      ++impl->bigIntCount;
      impl->bigInts.insert(retained, impl->bigIntCount);
      result = ast::BigIntId(impl->bigIntCount);
    }
  }
  impl->events.add(ParserSyntaxEvent(ParserBigIntEvent{zc::str(value), result}));
  return result;
}

ast::FloatId ParserEventBuilder::internFloat(zc::StringPtr value) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  ast::FloatId result;
  if (value.size() != 0) {
    ZC_IF_SOME(found, impl->floats.find(value)) {
      result = ast::FloatId(found);
    } else {
      const zc::StringPtr retained = impl->textArena.copyString(value);
      ++impl->floatCount;
      impl->floats.insert(retained, impl->floatCount);
      result = ast::FloatId(impl->floatCount);
    }
  }
  impl->events.add(ParserSyntaxEvent(ParserFloatEvent{zc::str(value), result}));
  return result;
}

void ParserEventBuilder::setRoot(ast::NodeId root) {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  if (!validNodeId(root, impl->nodeCount) || impl->root) impl->valid = false;
  impl->root = root;
  impl->events.add(ParserSyntaxEvent(ParserRootEvent{root}));
}

ParserEventStreamRequest ParserEventBuilder::finish() {
  ZC_IREQUIRE(!impl->finished, "parser event builder is already finished");
  impl->finished = true;
  if (!impl->root) impl->valid = false;
  if (!impl->valid) { impl->events.add(ParserSyntaxEvent(ParserRootEvent{ast::NodeId()})); }
  return ParserEventStreamRequest{impl->events.releaseAsArray()};
}

struct RecoverableSyntaxTree::Impl final {
  Impl(ParserEventStreamRequest&& parserEvents, VerifiedLexemeStream&& lexemes,
       VerifiedRecoverySequence&& recovery, uint64_t parserErrorCount)
      : parserEvents(zc::mv(parserEvents.events)),
        lexemes(zc::mv(lexemes)),
        recovery(zc::mv(recovery)),
        parserErrorCount(parserErrorCount) {}

  zc::Array<ParserSyntaxEvent> parserEvents;
  VerifiedLexemeStream lexemes;
  VerifiedRecoverySequence recovery;
  uint64_t parserErrorCount;
};

RecoverableSyntaxTree::RecoverableSyntaxTree(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
RecoverableSyntaxTree::~RecoverableSyntaxTree() noexcept(false) = default;
RecoverableSyntaxTree::RecoverableSyntaxTree(RecoverableSyntaxTree&&) noexcept = default;
RecoverableSyntaxTree& RecoverableSyntaxTree::operator=(RecoverableSyntaxTree&&) noexcept = default;

zc::Maybe<RecoverableSyntaxTree> RecoverableSyntaxTree::from(
    ParserEventStreamRequest&& parserEvents, VerifiedLexemeStream&& lexemes,
    VerifiedRecoverySequence&& recovery, uint64_t parserErrorCount) {
  if (recovery.lexemeStreamId() != lexemes.id() || parserEvents.events.size() == 0) {
    return zc::none;
  }
  return RecoverableSyntaxTree(
      zc::heap<Impl>(zc::mv(parserEvents), zc::mv(lexemes), zc::mv(recovery), parserErrorCount));
}

zc::ArrayPtr<const ParserSyntaxEvent> RecoverableSyntaxTree::parserEvents() const {
  return impl->parserEvents.asPtr();
}
ParserEventStreamId RecoverableSyntaxTree::parserEventStreamId() const {
  return computeParserEventStreamId(impl->parserEvents.asPtr());
}
const VerifiedLexemeStream& RecoverableSyntaxTree::lexemes() const noexcept {
  return impl->lexemes;
}
const VerifiedRecoverySequence& RecoverableSyntaxTree::recovery() const noexcept {
  return impl->recovery;
}
uint64_t RecoverableSyntaxTree::parserErrorCount() const noexcept { return impl->parserErrorCount; }

InvalidDiagnosticBindingResult RecoverableSyntaxDiagnosticBinder::bind(
    RecoverableSyntaxTree&& syntax, zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
    const diagnostics::SourceDiagnosticProvenanceMap& provenance) {
  zc::Vector<CstLexeme> lexemes(syntax.impl->lexemes.lexemes().size());
  for (const auto& lexeme : syntax.impl->lexemes.lexemes()) {
    if (lexeme.tag() != CstLexemeTag::Invalid) {
      lexemes.add(lexeme.clone());
      continue;
    }

    const diagnostics::DiagnosticFact* selected = nullptr;
    for (const auto& fact : facts) {
      if (fact.occurrence().origin() != diagnostics::DiagnosticFactOrigin::Source ||
          fact.occurrence().phase() != diagnostics::SourceDiagnosticPhase::Lex) {
        continue;
      }
      auto primary = provenance.find(fact.primary());
      if (primary == zc::none) continue;
      const uint64_t offset = ZC_ASSERT_NONNULL(primary).byteStart;
      if (offset < lexeme.range().start || offset >= lexeme.range().end) continue;
      if (selected != nullptr) {
        return InvalidDiagnosticBindingResult(InvalidDiagnosticBindingFailure::AmbiguousDiagnostic);
      }
      selected = &fact;
    }
    if (selected == nullptr) {
      return InvalidDiagnosticBindingResult(InvalidDiagnosticBindingFailure::MissingDiagnostic);
    }
    auto factId = diagnostics::computeDiagnosticFactId(*selected);
    if (factId == zc::none) {
      return InvalidDiagnosticBindingResult(InvalidDiagnosticBindingFailure::InvalidDiagnosticFact);
    }
    auto rebound =
        CstLexeme::invalid(lexeme.range(), lexeme.spelling(), ZC_ASSERT_NONNULL(factId).digest());
    if (rebound == zc::none) {
      return InvalidDiagnosticBindingResult(
          InvalidDiagnosticBindingFailure::LexemeReverificationFailed);
    }
    lexemes.add(zc::mv(ZC_ASSERT_NONNULL(rebound)));
  }

  auto reboundLexemes = LexemePartitionVerifier::verify(
      LexemeStreamRequest{syntax.impl->lexemes.contentDigest(),
                          syntax.impl->lexemes.sourceByteCount(), lexemes.releaseAsArray()});
  if (!reboundLexemes.is<VerifiedLexemeStream>()) {
    return InvalidDiagnosticBindingResult(
        InvalidDiagnosticBindingFailure::LexemeReverificationFailed);
  }
  zc::Vector<RecoveryElement> recovery(syntax.impl->recovery.elements().size());
  for (const auto& element : syntax.impl->recovery.elements()) recovery.add(element.clone());
  auto reboundRecovery = RecoverySequenceVerifier::verify(
      reboundLexemes.get<VerifiedLexemeStream>(), recovery.releaseAsArray());
  if (!reboundRecovery.is<VerifiedRecoverySequence>()) {
    return InvalidDiagnosticBindingResult(
        InvalidDiagnosticBindingFailure::RecoveryReverificationFailed);
  }

  ParserEventStreamRequest parserEvents{zc::mv(syntax.impl->parserEvents)};
  auto rebound = RecoverableSyntaxTree::from(
      zc::mv(parserEvents), zc::mv(reboundLexemes.get<VerifiedLexemeStream>()),
      zc::mv(reboundRecovery.get<VerifiedRecoverySequence>()), syntax.impl->parserErrorCount);
  if (rebound == zc::none) {
    return InvalidDiagnosticBindingResult(
        InvalidDiagnosticBindingFailure::RecoveryReverificationFailed);
  }
  return InvalidDiagnosticBindingResult(zc::mv(ZC_ASSERT_NONNULL(rebound)));
}

ParseSyntaxResult ParseSyntaxVerifier::verify(const RecoverableSyntaxTree& syntax,
                                              const source::SourceManager& sources,
                                              const source::BufferId& buffer) {
  if (syntax.lexemes().sourceByteCount() > UINT32_MAX) {
    return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeEvent);
  }
  // The lexeme stream binds its identifiers, spellings, and byte ranges to one
  // exact source content. The AST payloads replayed below come from that stream,
  // but every SourceRange is rebuilt from the caller's buffer, so a buffer of the
  // same length but different bytes would attach A's intern/payload data to B's
  // locations. Bind the caller's buffer to the stream before trusting it: its
  // byte count and content digest must equal the stream's own.
  const zc::ArrayPtr<const zc::byte> bufferBytes = sources.getEntireTextForBuffer(buffer);
  if (bufferBytes.size() != syntax.lexemes().sourceByteCount()) {
    return ParseSyntaxResult(ParseSyntaxFailure::SourceBufferMismatch);
  }
  zc::Maybe<identity::Sha256Digest> bufferDigest = identity::sha256(bufferBytes);
  if (bufferDigest == zc::none ||
      ZC_REQUIRE_NONNULL(bufferDigest) != syntax.lexemes().contentDigest()) {
    return ParseSyntaxResult(ParseSyntaxFailure::SourceBufferMismatch);
  }
  const ParseEligibility eligibility =
      parseEligibility(syntax.lexemes(), syntax.recovery(), syntax.parserErrorCount());
  if (!eligibility.isEligible()) return ParseSyntaxResult(mapEligibility(eligibility.reason()));

  ast::TreeBuilder builder;
  uint32_t nodeCount = 0;
  uint32_t identCount = 0;
  uint32_t listSize = 0;
  uint32_t identListSize = 0;
  bool rootSet = false;

  for (const auto& event : syntax.parserEvents()) {
    if (event.is<ParserNodeEvent>()) {
      const auto& node = event.get<ParserNodeEvent>();
      if (node.result.value != nodeCount + 1 || node.range.end < node.range.start ||
          node.range.end > syntax.lexemes().sourceByteCount()) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeEvent);
      }
      const auto range = source::SourceRange(sources.getLocForOffset(buffer, node.range.start),
                                             sources.getLocForOffset(buffer, node.range.end));
      const ast::NodeId actual = builder.makeNode(node.kind, range, node.payload);
      if (!sameId(actual, node.result)) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeEvent);
      }
      ++nodeCount;
      continue;
    }
    if (event.is<ParserNodeListEvent>()) {
      const auto& list = event.get<ParserNodeListEvent>();
      if (list.result.first != listSize || list.result.size != list.nodes.size()) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeListEvent);
      }
      for (ast::NodeId node : list.nodes.asPtr()) {
        if (!validNodeId(node, nodeCount)) {
          return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeListEvent);
        }
      }
      const ast::NodeList actual = builder.makeList(list.nodes.asPtr());
      if (actual.first != list.result.first || actual.size != list.result.size) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidNodeListEvent);
      }
      listSize += list.result.size;
      continue;
    }
    if (event.is<ParserIdentListEvent>()) {
      const auto& list = event.get<ParserIdentListEvent>();
      if (list.result.first != identListSize || list.result.size != list.identifiers.size()) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidIdentListEvent);
      }
      for (ast::IdentId identifier : list.identifiers.asPtr()) {
        if (!validIdentId(identifier, identCount)) {
          return ParseSyntaxResult(ParseSyntaxFailure::InvalidIdentListEvent);
        }
      }
      const ast::IdentList actual = builder.makeIdentList(list.identifiers.asPtr());
      if (actual.first != list.result.first || actual.size != list.result.size) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidIdentListEvent);
      }
      identListSize += list.result.size;
      continue;
    }
    if (event.is<ParserStringEvent>()) {
      const auto& value = event.get<ParserStringEvent>();
      if (!sameId(builder.internString(value.text), value.result)) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidStringEvent);
      }
      continue;
    }
    if (event.is<ParserIdentEvent>()) {
      const auto& value = event.get<ParserIdentEvent>();
      const ast::IdentId actual = builder.internIdent(value.text);
      if (!sameId(actual, value.result)) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidIdentEvent);
      }
      if (actual.value > identCount) identCount = actual.value;
      continue;
    }
    if (event.is<ParserBigIntEvent>()) {
      const auto& value = event.get<ParserBigIntEvent>();
      if (!sameId(builder.internBigInt(value.text), value.result)) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidBigIntEvent);
      }
      continue;
    }
    if (event.is<ParserFloatEvent>()) {
      const auto& value = event.get<ParserFloatEvent>();
      if (!sameId(builder.internFloat(value.text), value.result)) {
        return ParseSyntaxResult(ParseSyntaxFailure::InvalidFloatEvent);
      }
      continue;
    }
    const auto& root = event.get<ParserRootEvent>();
    if (rootSet || !validNodeId(root.root, nodeCount)) {
      return ParseSyntaxResult(ParseSyntaxFailure::InvalidRootEvent);
    }
    builder.setRoot(root.root);
    rootSet = true;
  }

  if (!rootSet) return ParseSyntaxResult(ParseSyntaxFailure::InvalidRootEvent);
  ast::Tree tree = builder.finish();
  if (!ast::verifySchema(tree)) return ParseSyntaxResult(ParseSyntaxFailure::InvalidAstSchema);
  return ParseSyntaxResult(zc::mv(tree));
}

}  // namespace zomlang::compiler::cst
