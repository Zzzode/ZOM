// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 recoverable-parsing tree-identity slice: prove the
// RecoverableSyntaxTreeCodec produces a deterministic, domain-separated preimage
// that binds a recoverable syntax tree to its three verified components -- the
// lexeme stream, the recovery sequence, and the parser event stream -- by their
// existing identities, plus the retained parser error count. This is
// internal-data progress only: the codec has no downstream carry or consumer
// (parse-source-query still discards the recoverable tree), so KR6.3 stays
// BLOCKED. The tree identity must change whenever any bound component or the
// error count changes, and must not re-serialize the event algebra (it binds the
// parserEventStreamId, which is itself the digest of the event preimage).

#include "compiler/ast/generated/node-factory.h"
#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/parser-event-stream.h"
#include "compiler/cst/recovery-codec.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/source/manager.h"
#include "zc/core/encoding.h"
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

  Fixture() : buffer(sources.addMemBufferCopy({}, "tree.zom"_zc)) {}
};

VerifiedLexemeStream emptyLexemes() {
  auto digest = identity::sha256({});
  ZC_REQUIRE(digest != zc::none);
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{ZC_ASSERT_NONNULL(digest), 0, zc::Array<CstLexeme>()});
  ZC_REQUIRE(result.is<VerifiedLexemeStream>());
  return zc::mv(result.get<VerifiedLexemeStream>());
}

// A verified lexeme stream that covers exactly `content` with one trivia lexeme,
// so the stream identity binds to those bytes.
VerifiedLexemeStream lexemesForBytes(zc::ArrayPtr<const uint8_t> content) {
  auto digest = identity::sha256(content);
  ZC_REQUIRE(digest != zc::none);
  zc::Vector<CstLexeme> lexemes;
  auto lexeme = CstLexeme::trivia(TriviaKind::Whitespace, ByteRange{0, content.size()}, content);
  ZC_REQUIRE(lexeme != zc::none);
  ZC_IF_SOME(value, lexeme) { lexemes.add(zc::mv(value)); }
  auto result = LexemePartitionVerifier::verify(
      LexemeStreamRequest{ZC_ASSERT_NONNULL(digest), content.size(), lexemes.releaseAsArray()});
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
  zc::Array<ast::NodeId> noStatements;
  const ast::NodeList statements = factory.makeList(noStatements.asPtr());
  const ast::NodeId root = factory.makeSourceFile(
      source::SourceRange(fixture.sources.getLocForBufferStart(fixture.buffer),
                          fixture.sources.getLocForBufferStart(fixture.buffer)),
      factory.internString("tree.zom"_zc), ast::NodeId(), statements);
  factory.setRoot(root);
  return factory.finish();
}

// The canonical tree: valid events over an empty lexeme stream with an empty
// recovery sequence and a zero error count. Every component identity is fixed,
// so the tree preimage and identity are a frozen oracle.
RecoverableSyntaxTree canonicalTree(Fixture& fixture, uint64_t parserErrorCount = 0) {
  auto lexemes = emptyLexemes();
  auto sequence = recovery(lexemes);
  auto tree = RecoverableSyntaxTree::from(validEvents(fixture), zc::mv(lexemes), zc::mv(sequence),
                                          parserErrorCount);
  ZC_REQUIRE(tree != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(tree));
}

// The canonical tree encodes to a fixed preimage: assert its exact byte length,
// full lowercase hex, and RecoverableSyntaxTreeId. The bytes are produced by the
// live encoder; the asserted values are the frozen oracle. The preimage is the
// 13-byte domain tag ("zom.cst-tree" + 0x00) followed by three framed 32-byte
// digests (each 8-byte length prefix + 32 bytes) and an 8-byte error count:
// 13 + 3 * (8 + 32) + 8 = 141 bytes.
ZC_TEST("CST recoverable tree encodes to the frozen oracle") {
  Fixture fixture;
  auto tree = canonicalTree(fixture);
  auto bytes = RecoverableSyntaxTreeCodec::encode(tree);
  ZC_EXPECT(bytes.size() == 141);

  // The domain tag prefixes the preimage.
  const char domain[] = "zom.cst-tree";
  ZC_REQUIRE(bytes.size() >= sizeof(domain));
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    ZC_EXPECT(bytes[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(bytes[sizeof(domain) - 1] == 0x00);

  // The three framed digests are bound in the fixed order lexeme, recovery,
  // event; each is length-prefixed with a big-endian uint64 of 32.
  const size_t domainLen = sizeof(domain);  // includes the 0x00 separator
  auto frameLengthAt = [&](size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
      value = (value << 8) | static_cast<uint64_t>(bytes[offset + index]);
    }
    return value;
  };
  ZC_EXPECT(frameLengthAt(domainLen) == 32);
  ZC_EXPECT(frameLengthAt(domainLen + 8 + 32) == 32);
  ZC_EXPECT(frameLengthAt(domainLen + 2 * (8 + 32)) == 32);

  const size_t lexemeDigestOffset = domainLen + 8;
  const size_t recoveryDigestOffset = domainLen + (8 + 32) + 8;
  const size_t eventDigestOffset = domainLen + 2 * (8 + 32) + 8;
  // parserEventStreamId() returns by value; bind it so the digest bytes it lends
  // out live through the comparison below.
  const ParserEventStreamId eventId = tree.parserEventStreamId();
  const auto lexemeDigest = tree.lexemes().id().digest().bytes();
  const auto recoveryDigest = tree.recovery().id().digest().bytes();
  const auto eventDigest = eventId.digest().bytes();
  for (size_t index = 0; index < 32; ++index) {
    ZC_EXPECT(bytes[lexemeDigestOffset + index] == lexemeDigest[index]);
    ZC_EXPECT(bytes[recoveryDigestOffset + index] == recoveryDigest[index]);
    ZC_EXPECT(bytes[eventDigestOffset + index] == eventDigest[index]);
  }

  // The trailing 8 bytes are the big-endian error count (zero here).
  for (size_t index = bytes.size() - 8; index < bytes.size(); ++index) {
    ZC_EXPECT(bytes[index] == 0x00);
  }

  // The identity is the SHA-256 of the preimage.
  auto digest = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_EXPECT(RecoverableSyntaxTreeCodec::computeId(tree).digest() == ZC_ASSERT_NONNULL(digest));
}

// Re-encoding the same tree is byte-identical and the identity is stable.
ZC_TEST("CST recoverable tree encoding is deterministic and domain-separated") {
  Fixture fixture;
  auto tree = canonicalTree(fixture);
  auto first = RecoverableSyntaxTreeCodec::encode(tree);
  auto second = RecoverableSyntaxTreeCodec::encode(tree);
  ZC_EXPECT(first.asPtr() == second.asPtr());
  ZC_EXPECT(RecoverableSyntaxTreeCodec::computeId(tree) ==
            RecoverableSyntaxTreeCodec::computeId(tree));

  const char domain[] = "zom.cst-tree";
  ZC_REQUIRE(first.size() >= sizeof(domain));
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    ZC_EXPECT(first[index] == static_cast<uint8_t>(domain[index]));
  }
  ZC_EXPECT(first[sizeof(domain) - 1] == 0x00);
}

// Editing the bound lexeme content changes the lexeme stream id and therefore
// the tree identity, even though the events and recovery are unchanged.
ZC_TEST("CST recoverable tree identity changes when the lexeme content changes") {
  Fixture baseFixture;
  auto base = canonicalTree(baseFixture);

  Fixture editedFixture;
  auto lexemes = lexemesForBytes(" "_zcb);
  auto sequence = recovery(lexemes);
  auto edited =
      RecoverableSyntaxTree::from(validEvents(editedFixture), zc::mv(lexemes), zc::mv(sequence), 0);
  ZC_REQUIRE(edited != zc::none);

  ZC_EXPECT(RecoverableSyntaxTreeCodec::computeId(base) !=
            RecoverableSyntaxTreeCodec::computeId(ZC_ASSERT_NONNULL(edited)));
}

// Changing the parser error count changes the tree identity while every bound
// component id stays fixed: the count is bound into the preimage tail.
ZC_TEST("CST recoverable tree identity changes when the parser error count changes") {
  Fixture zeroFixture;
  auto zero = canonicalTree(zeroFixture, 0);
  Fixture oneFixture;
  auto one = canonicalTree(oneFixture, 1);

  // The lexeme, recovery, and event ids are identical across the two trees; only
  // the error count differs, so only the tree identity may differ.
  ZC_EXPECT(zero.lexemes().id() == one.lexemes().id());
  ZC_EXPECT(zero.recovery().id() == one.recovery().id());
  ZC_EXPECT(zero.parserEventStreamId() == one.parserEventStreamId());
  ZC_EXPECT(RecoverableSyntaxTreeCodec::computeId(zero) !=
            RecoverableSyntaxTreeCodec::computeId(one));
}

// Changing an interned event value changes the parser event stream id and thus
// the tree identity without the codec re-serializing the event algebra.
ZC_TEST("CST recoverable tree identity changes when an event value changes") {
  Fixture baseFixture;
  auto base = canonicalTree(baseFixture);

  Fixture editedFixture;
  EventFactory factory(editedFixture.sources, editedFixture.buffer);
  const ast::IdentId identifier = factory.internIdent("other"_zc);  // was "name"
  zc::Vector<ast::IdentId> identifiers;
  identifiers.add(identifier);
  (void)factory.makeIdentList(identifiers.asPtr());
  zc::Array<ast::NodeId> noStatements;
  const ast::NodeList statements = factory.makeList(noStatements.asPtr());
  const ast::NodeId root = factory.makeSourceFile(
      source::SourceRange(editedFixture.sources.getLocForBufferStart(editedFixture.buffer),
                          editedFixture.sources.getLocForBufferStart(editedFixture.buffer)),
      factory.internString("tree.zom"_zc), ast::NodeId(), statements);
  factory.setRoot(root);
  auto lexemes = emptyLexemes();
  auto sequence = recovery(lexemes);
  auto edited = RecoverableSyntaxTree::from(factory.finish(), zc::mv(lexemes), zc::mv(sequence), 0);
  ZC_REQUIRE(edited != zc::none);

  ZC_EXPECT(base.parserEventStreamId() != ZC_ASSERT_NONNULL(edited).parserEventStreamId());
  ZC_EXPECT(RecoverableSyntaxTreeCodec::computeId(base) !=
            RecoverableSyntaxTreeCodec::computeId(ZC_ASSERT_NONNULL(edited)));
}

}  // namespace
}  // namespace zomlang::compiler::cst
