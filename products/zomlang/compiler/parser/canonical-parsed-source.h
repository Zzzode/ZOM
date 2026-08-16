// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace parser {

class ParsedTokenSnapshot;

struct CanonicalParserOptions final {
  bool useUnicode = true;
  bool allowDollarIdentifiers = false;
  bool supportRegexLiterals = true;

  bool operator==(const CanonicalParserOptions& other) const noexcept = default;
};

struct CanonicalParsedToken final {
  ast::SyntaxKind kind = ast::SyntaxKind::Unknown;
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;
  zc::String canonicalText;

  ZC_NODISCARD CanonicalParsedToken clone() const;
  bool operator==(const CanonicalParsedToken& other) const noexcept;
};

/// \brief Self-owned query-safe parsed source reconstructed without lexing or parsing.
class CanonicalParsedSource final {
public:
  ~CanonicalParsedSource() noexcept(false);
  CanonicalParsedSource(CanonicalParsedSource&&) noexcept;
  CanonicalParsedSource& operator=(CanonicalParsedSource&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalParsedSource);

  /// \brief Detaches one successful parser result into canonical bytes and rehydrates ownership.
  ZC_NODISCARD static zc::Maybe<CanonicalParsedSource> fromParsed(
      zc::ArrayPtr<const uint8_t> canonicalSourceKey, const identity::Sha256Digest& contentDigest,
      zc::ArrayPtr<const uint8_t> sourceBytes, zc::StringPtr logicalName,
      CanonicalParserOptions options, const source::SourceManager& parsedSources,
      const source::BufferId& parsedBuffer, ast::Tree&& tree, ParsedTokenSnapshot&& tokens,
      zc::Vector<diagnostics::DiagnosticFact>&& facts,
      diagnostics::SourceDiagnosticProvenanceMap&& provenance);

  /// \brief Strictly decodes a complete canonical parsed-source value.
  ZC_NODISCARD static zc::Maybe<CanonicalParsedSource> decodeCanonical(
      zc::ArrayPtr<const uint8_t> encoded);

  ZC_NODISCARD CanonicalParsedSource clone() const;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceKey() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> sourceBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::StringPtr logicalName() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD CanonicalParserOptions options() const noexcept;
  ZC_NODISCARD const source::SourceManager& sourceManager() const noexcept;
  ZC_NODISCARD const source::BufferId& buffer() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CanonicalParsedToken> tokens() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const diagnostics::DiagnosticFact> facts() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const diagnostics::SourceDiagnosticProvenanceMap& provenance() const noexcept;

private:
  struct Impl;
  explicit CanonicalParsedSource(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace zomlang::compiler
