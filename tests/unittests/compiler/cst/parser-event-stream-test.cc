// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/cst/parser-event-stream.h"

#include "compiler/ast/generated/node-factory.h"
#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/recovery-codec.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/source/manager.h"
#include "zc/core/debug.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::cst {
namespace {

class EventFactory final : public ast::TypedNodeFactory<EventFactory> {
public:
  EventFactory(const source::SourceManager& sources, const source::BufferId& buffer)
      : builder(sources, buffer) {}

  ast::NodeList makeList(zc::ArrayPtr<const ast::NodeId> nodes) { return builder.makeList(nodes); }
  ast::IdentList makeIdentList(zc::ArrayPtr<const ast::IdentId> identifiers) {
    return builder.makeIdentList(identifiers);
  }
  ast::StringId internString(zc::StringPtr value) { return builder.internString(value); }
  ast::IdentId internIdent(zc::StringPtr value) { return builder.internIdent(value); }
  ast::BigIntId internBigInt(zc::StringPtr value) { return builder.internBigInt(value); }
  ast::FloatId internFloat(zc::StringPtr value) { return builder.internFloat(value); }
  void setRoot(ast::NodeId root) { builder.setRoot(root); }
  ParserEventStreamRequest finish() { return builder.finish(); }

private:
  template <typename>
  friend class ast::TypedNodeFactory;

  ast::NodeId makeTypedNode(ast::SyntaxKind kind, source::SourceRange range,
                            ast::NodePayload payload = {}) {
    return builder.makeNode(kind, range, payload);
  }

  ParserEventBuilder builder;
};

struct Fixture final {
  source::SourceManager sources;
  source::BufferId buffer;

  Fixture() : buffer(sources.addMemBufferCopy({}, "event.zom"_zc)) {}
};

VerifiedLexemeStream emptyLexemes() {
  auto digest = identity::sha256({});
  ZC_REQUIRE(digest != zc::none);
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{ZC_ASSERT_NONNULL(digest), 0, zc::Array<CstLexeme>()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

VerifiedRecoverySequence recovery(VerifiedLexemeStream& lexemes,
                                  zc::Array<RecoveryElement>&& elements = {}) {
  auto result = RecoverySequenceVerifier::verify(lexemes, zc::mv(elements));
  ZC_REQUIRE(result.is<VerifiedRecoverySequence>());
  return zc::mv(result.get<VerifiedRecoverySequence>());
}

ParserEventStreamRequest validEvents(Fixture& fixture) {
  EventFactory factory(fixture.sources, fixture.buffer);
  const ast::IdentId identifier = factory.internIdent("name"_zc);
  zc::Vector<ast::IdentId> identifiers;
  identifiers.add(identifier);
  (void)factory.makeIdentList(identifiers.asPtr());
  (void)factory.internBigInt("123"_zc);
  (void)factory.internFloat("1.0"_zc);
  zc::Array<ast::NodeId> noStatements;
  const ast::NodeList statements = factory.makeList(noStatements.asPtr());
  const ast::NodeId root = factory.makeSourceFile(
      source::SourceRange(fixture.sources.getLocForBufferStart(fixture.buffer),
                          fixture.sources.getLocForBufferStart(fixture.buffer)),
      factory.internString("event.zom"_zc), ast::NodeId(), statements);
  factory.setRoot(root);
  return factory.finish();
}

RecoverableSyntaxTree cleanSyntax(Fixture& fixture, ParserEventStreamRequest&& events) {
  auto lexemes = emptyLexemes();
  auto sequence = recovery(lexemes);
  auto tree = RecoverableSyntaxTree::from(zc::mv(events), zc::mv(lexemes), zc::mv(sequence), 0);
  ZC_REQUIRE(tree != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(tree));
}

template <typename Event, typename Mutator>
ParseSyntaxFailure mutateAndVerify(Fixture& fixture, Mutator&& mutator) {
  auto events = validEvents(fixture);
  bool mutated = false;
  for (auto& event : events.events) {
    if (event.is<Event>()) {
      mutator(event.get<Event>());
      mutated = true;
      break;
    }
  }
  ZC_REQUIRE(mutated);
  auto syntax = cleanSyntax(fixture, zc::mv(events));
  auto result = ParseSyntaxVerifier::verify(syntax, fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ParseSyntaxFailure>());
  return result.get<ParseSyntaxFailure>();
}

}  // namespace

ZC_TEST("ParseSyntaxVerifier replays the production event algebra into a schema-valid AST") {
  Fixture fixture;
  auto syntax = cleanSyntax(fixture, validEvents(fixture));
  auto result = ParseSyntaxVerifier::verify(syntax, fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ast::Tree>());
  const auto& tree = result.get<ast::Tree>();
  ZC_EXPECT(tree.nodeCount() == 1);
  ZC_EXPECT(tree.node(tree.root()).kind == ast::SyntaxKind::SourceFile);
  ZC_EXPECT(tree.string(ast::StringId(1)) == "event.zom");
}

ZC_TEST("ParseSyntaxVerifier rejects every mutated parser event-result family") {
  Fixture fixture;
  ZC_EXPECT(mutateAndVerify<ParserNodeEvent>(fixture, [](ParserNodeEvent& event) {
              event.result = ast::NodeId(event.result.value + 1);
            }) == ParseSyntaxFailure::InvalidNodeEvent);
  ZC_EXPECT(mutateAndVerify<ParserNodeListEvent>(fixture, [](ParserNodeListEvent& event) {
              ++event.result.first;
            }) == ParseSyntaxFailure::InvalidNodeListEvent);
  ZC_EXPECT(mutateAndVerify<ParserIdentListEvent>(fixture, [](ParserIdentListEvent& event) {
              ++event.result.first;
            }) == ParseSyntaxFailure::InvalidIdentListEvent);
  ZC_EXPECT(mutateAndVerify<ParserStringEvent>(fixture, [](ParserStringEvent& event) {
              ++event.result.value;
            }) == ParseSyntaxFailure::InvalidStringEvent);
  ZC_EXPECT(mutateAndVerify<ParserIdentEvent>(fixture, [](ParserIdentEvent& event) {
              ++event.result.value;
            }) == ParseSyntaxFailure::InvalidIdentEvent);
  ZC_EXPECT(mutateAndVerify<ParserBigIntEvent>(fixture, [](ParserBigIntEvent& event) {
              ++event.result.value;
            }) == ParseSyntaxFailure::InvalidBigIntEvent);
  ZC_EXPECT(mutateAndVerify<ParserFloatEvent>(fixture, [](ParserFloatEvent& event) {
              ++event.result.value;
            }) == ParseSyntaxFailure::InvalidFloatEvent);
  ZC_EXPECT(mutateAndVerify<ParserRootEvent>(fixture, [](ParserRootEvent& event) {
              ++event.root.value;
            }) == ParseSyntaxFailure::InvalidRootEvent);
}

ZC_TEST("ParseSyntaxVerifier rejects an event stream that reconstructs an invalid AST schema") {
  Fixture fixture;
  ParserEventBuilder builder(fixture.sources, fixture.buffer);
  const auto loc = fixture.sources.getLocForBufferStart(fixture.buffer);
  const ast::NodeId root =
      builder.makeNode(ast::SyntaxKind::SourceFile, source::SourceRange(loc, loc));
  builder.setRoot(root);
  auto syntax = cleanSyntax(fixture, builder.finish());
  auto result = ParseSyntaxVerifier::verify(syntax, fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ParseSyntaxFailure>());
  ZC_EXPECT(result.get<ParseSyntaxFailure>() == ParseSyntaxFailure::InvalidAstSchema);
}

ZC_TEST("ParseSyntaxVerifier rejects a SourceFile file name interned outside the tree") {
  // End-to-end: replay a well-formed event stream whose SourceFile carries a
  // file-name StringId that was never interned into this tree. The schema
  // verifier must reject the reconstructed AST rather than publish a tree that
  // crashes on the out-of-range interned access.
  Fixture fixture;
  EventFactory factory(fixture.sources, fixture.buffer);
  zc::Array<ast::NodeId> noStatements;
  const ast::NodeList statements = factory.makeList(noStatements.asPtr());
  const auto loc = fixture.sources.getLocForBufferStart(fixture.buffer);
  const ast::NodeId root = factory.makeSourceFile(
      source::SourceRange(loc, loc), ast::StringId(0xffffffffu), ast::NodeId(), statements);
  factory.setRoot(root);
  auto syntax = cleanSyntax(fixture, factory.finish());
  auto result = ParseSyntaxVerifier::verify(syntax, fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ParseSyntaxFailure>());
  ZC_EXPECT(result.get<ParseSyntaxFailure>() == ParseSyntaxFailure::InvalidAstSchema);
}

ZC_TEST("ParseSyntaxVerifier rejects recovery before replaying parser events") {
  Fixture fixture;
  auto lexemes = emptyLexemes();
  zc::Vector<RecoveryElement> elements;
  elements.add(RecoveryElement::missingSubtree(1, 0));
  auto sequence = recovery(lexemes, elements.releaseAsArray());
  auto syntax =
      RecoverableSyntaxTree::from(validEvents(fixture), zc::mv(lexemes), zc::mv(sequence), 0);
  ZC_REQUIRE(syntax != zc::none);
  auto result =
      ParseSyntaxVerifier::verify(ZC_ASSERT_NONNULL(syntax), fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ParseSyntaxFailure>());
  ZC_EXPECT(result.get<ParseSyntaxFailure>() == ParseSyntaxFailure::RecoveryPresent);
}

ZC_TEST("ParseSyntaxVerifier rejects parser errors before AST publication") {
  Fixture fixture;
  auto lexemes = emptyLexemes();
  auto sequence = recovery(lexemes);
  auto syntax =
      RecoverableSyntaxTree::from(validEvents(fixture), zc::mv(lexemes), zc::mv(sequence), 1);
  ZC_REQUIRE(syntax != zc::none);
  auto result =
      ParseSyntaxVerifier::verify(ZC_ASSERT_NONNULL(syntax), fixture.sources, fixture.buffer);
  ZC_REQUIRE(result.is<ParseSyntaxFailure>());
  ZC_EXPECT(result.get<ParseSyntaxFailure>() == ParseSyntaxFailure::ParserErrorPresent);
}

}  // namespace zomlang::compiler::cst
