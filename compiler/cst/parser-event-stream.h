// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/ast/tree.h"
#include "compiler/cst/lexeme-codec.h"
#include "compiler/cst/recovery-codec.h"
#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace cst {

/// \brief One parser node-construction event in allocation order.
struct ParserNodeEvent final {
  ast::SyntaxKind kind = ast::SyntaxKind::Unknown;
  ByteRange range;
  ast::NodePayload payload;
  ast::NodeId result;
};

/// \brief One parser node-list construction event.
struct ParserNodeListEvent final {
  zc::Array<ast::NodeId> nodes;
  ast::NodeList result;
};

/// \brief One parser identifier-list construction event.
struct ParserIdentListEvent final {
  zc::Array<ast::IdentId> identifiers;
  ast::IdentList result;
};

/// \brief One string-table interning event.
struct ParserStringEvent final {
  zc::String text;
  ast::StringId result;
};

/// \brief One identifier-table interning event.
struct ParserIdentEvent final {
  zc::String text;
  ast::IdentId result;
};

/// \brief One integer-literal-table interning event.
struct ParserBigIntEvent final {
  zc::String text;
  ast::BigIntId result;
};

/// \brief One floating-literal-table interning event.
struct ParserFloatEvent final {
  zc::String text;
  ast::FloatId result;
};

/// \brief The parser event that selects the source-file root.
struct ParserRootEvent final {
  ast::NodeId root;
};

/// \brief The closed parser construction-event algebra.
using ParserSyntaxEvent =
    zc::OneOf<ParserNodeEvent, ParserNodeListEvent, ParserIdentListEvent, ParserStringEvent,
              ParserIdentEvent, ParserBigIntEvent, ParserFloatEvent, ParserRootEvent>;

/// \brief One unverified parser-produced construction stream.
struct ParserEventStreamRequest final {
  zc::Array<ParserSyntaxEvent> events;
};

class ParserEventStreamId final {
public:
  constexpr ParserEventStreamId() noexcept = default;
  ZC_NODISCARD static ParserEventStreamId fromDigest(
      const identity::Sha256Digest& digest) noexcept {
    return ParserEventStreamId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }
  bool operator==(const ParserEventStreamId& other) const noexcept { return value == other.value; }

private:
  explicit ParserEventStreamId(const identity::Sha256Digest& digest) noexcept : value(digest) {}
  identity::Sha256Digest value;
};

/// \brief Computes the domain-separated identity of one parser event stream.
ZC_NODISCARD ParserEventStreamId
computeParserEventStreamId(zc::ArrayPtr<const ParserSyntaxEvent> events);

/// \brief Records the production parser's syntax-construction events without
/// constructing an `ast::Tree`.
class ParserEventBuilder final {
public:
  ParserEventBuilder(const source::SourceManager& sources, const source::BufferId& buffer);
  ~ParserEventBuilder() noexcept(false);
  ParserEventBuilder(ParserEventBuilder&&) noexcept;
  ParserEventBuilder& operator=(ParserEventBuilder&&) noexcept;
  ZC_DISALLOW_COPY(ParserEventBuilder);

  ZC_NODISCARD ast::NodeId makeNode(ast::SyntaxKind kind, source::SourceRange range,
                                    ast::NodePayload payload = {});
  ZC_NODISCARD ast::NodeList makeList(zc::ArrayPtr<const ast::NodeId> nodes);
  ZC_NODISCARD ast::IdentList makeIdentList(zc::ArrayPtr<const ast::IdentId> identifiers);
  ZC_NODISCARD ast::StringId internString(zc::StringPtr value);
  ZC_NODISCARD ast::IdentId internIdent(zc::StringPtr value);
  ZC_NODISCARD ast::BigIntId internBigInt(zc::StringPtr value);
  ZC_NODISCARD ast::FloatId internFloat(zc::StringPtr value);
  void setRoot(ast::NodeId root);
  ZC_NODISCARD ParserEventStreamRequest finish();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Immutable recoverable parser output used by the compiler syntax
/// verifier and, later, IDE recovery consumers.
class RecoverableSyntaxTree final {
public:
  RecoverableSyntaxTree(RecoverableSyntaxTree&&) noexcept;
  RecoverableSyntaxTree& operator=(RecoverableSyntaxTree&&) noexcept;
  ZC_DISALLOW_COPY(RecoverableSyntaxTree);
  ~RecoverableSyntaxTree() noexcept(false);

  ZC_NODISCARD static zc::Maybe<RecoverableSyntaxTree> from(ParserEventStreamRequest&& parserEvents,
                                                            VerifiedLexemeStream&& lexemes,
                                                            VerifiedRecoverySequence&& recovery,
                                                            uint64_t parserErrorCount);

  ZC_NODISCARD zc::ArrayPtr<const ParserSyntaxEvent> parserEvents() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD ParserEventStreamId parserEventStreamId() const;
  ZC_NODISCARD const VerifiedLexemeStream& lexemes() const noexcept;
  ZC_NODISCARD const VerifiedRecoverySequence& recovery() const noexcept;
  ZC_NODISCARD uint64_t parserErrorCount() const noexcept;

private:
  struct Impl;
  explicit RecoverableSyntaxTree(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class RecoverableSyntaxDiagnosticBinder;
};

/// \brief The domain-separated immutable identity of one recoverable syntax tree.
///
/// Computed by `RecoverableSyntaxTreeCodec` as SHA-256 over the tree's canonical
/// preimage and compared by digest.
class RecoverableSyntaxTreeId final {
public:
  constexpr RecoverableSyntaxTreeId() noexcept = default;

  ZC_NODISCARD static RecoverableSyntaxTreeId fromDigest(
      const identity::Sha256Digest& digest) noexcept {
    return RecoverableSyntaxTreeId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const RecoverableSyntaxTreeId& other) const noexcept {
    return value == other.value;
  }
  bool operator!=(const RecoverableSyntaxTreeId& other) const noexcept { return !(*this == other); }

private:
  explicit RecoverableSyntaxTreeId(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief Canonical codec for the recoverable syntax tree.
///
/// The preimage is a domain-separated, length-framed encoding that binds the
/// tree's three verified components by their existing identities, in a fixed
/// order, followed by the retained parser error count:
///   ASCII("zom.cst-tree") 0x00
///   Frame(lexemeStreamId digest)
///   Frame(recoverySequenceId digest)
///   Frame(parserEventStreamId digest)
///   uint64(parserErrorCount)
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes.
///
/// Binding the `parserEventStreamId` binds the full parser event content: that
/// id is itself the SHA-256 of the domain-separated event preimage produced by
/// `computeParserEventStreamId`, so the tree identity changes whenever any event
/// changes without this codec re-serializing the event algebra. The lexeme and
/// recovery components are likewise bound by their own verified stream ids.
class RecoverableSyntaxTreeCodec final {
public:
  /// \brief Encodes a recoverable syntax tree to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const RecoverableSyntaxTree& tree);
  /// \brief Computes the tree's `RecoverableSyntaxTreeId` (SHA-256 of the preimage).
  ZC_NODISCARD static RecoverableSyntaxTreeId computeId(const RecoverableSyntaxTree& tree);
};

enum class InvalidDiagnosticBindingFailure : uint8_t {
  MissingDiagnostic = 0x01,
  AmbiguousDiagnostic = 0x02,
  InvalidDiagnosticFact = 0x03,
  LexemeReverificationFailed = 0x04,
  RecoveryReverificationFailed = 0x05,
};

using InvalidDiagnosticBindingResult =
    zc::OneOf<RecoverableSyntaxTree, InvalidDiagnosticBindingFailure>;

/// \brief Rebinds every Invalid lexeme to the exact canonical identity of its
/// retained source diagnostic fact.
class RecoverableSyntaxDiagnosticBinder final {
public:
  ZC_NODISCARD static InvalidDiagnosticBindingResult bind(
      RecoverableSyntaxTree&& syntax, zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
      const diagnostics::SourceDiagnosticProvenanceMap& provenance);
};

/// \brief The closed failure algebra for verified AST construction from the
/// recoverable parser output.
enum class ParseSyntaxFailure : uint8_t {
  RecoveryStreamMismatch = 0x01,
  RecoveryPresent = 0x02,
  InvalidLexemePresent = 0x03,
  ParserErrorPresent = 0x04,
  InvalidNodeEvent = 0x05,
  InvalidNodeListEvent = 0x06,
  InvalidIdentListEvent = 0x07,
  InvalidStringEvent = 0x08,
  InvalidIdentEvent = 0x09,
  InvalidBigIntEvent = 0x0a,
  InvalidFloatEvent = 0x0b,
  InvalidRootEvent = 0x0c,
  InvalidAstSchema = 0x0d,
  SourceBufferMismatch = 0x0e,
};

using ParseSyntaxResult = zc::OneOf<ast::Tree, ParseSyntaxFailure>;

/// \brief Independently replays parser construction events and publishes an AST
/// only from a clean, byte-verified recoverable syntax tree.
class ParseSyntaxVerifier final {
public:
  ZC_NODISCARD static ParseSyntaxResult verify(const RecoverableSyntaxTree& syntax,
                                               const source::SourceManager& sources,
                                               const source::BufferId& buffer);
};

}  // namespace cst
}  // namespace zomlang::compiler
