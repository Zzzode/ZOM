// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/identity/semantic-identity-registry-set.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/parser/canonical-parsed-source.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {

enum class ParsedModuleInvariantKind : uint8_t {
  SourceMismatch,
  InvalidTree,
  InvalidSourceRange,
  InvalidTokenProvenance,
  ReceiptMismatch,
  RegistryMismatch,
  SyntaxDiagnosticsPresent
};

/// \brief Closed failure published before query-owned syntax can enter binding.
struct ParsedModuleInvariantFact final {
  ParsedModuleInvariantKind kind;
  uint32_t occurrence;
};

/// \brief Read-only AST paired with the source manager that owns its locations.
class SourceBackedSyntaxView final {
public:
  ZC_NODISCARD const ast::Tree& tree() const noexcept { return syntaxTree; }
  ZC_NODISCARD const source::SourceManager& sourceManager() const noexcept { return syntaxSources; }

private:
  SourceBackedSyntaxView(const ast::Tree& tree, const source::SourceManager& sources) noexcept
      : syntaxTree(tree), syntaxSources(sources) {}

  const ast::Tree& syntaxTree;
  const source::SourceManager& syntaxSources;

  friend class VerifiedParsedModule;
};

/// \brief Domain-separated receipt for one immutable parser result.
class ParsedModuleReceipt final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  /// \brief Compute the normative RFC 0004 receipt from already canonical components.
  ZC_NODISCARD static zc::Maybe<ParsedModuleReceipt> compute(
      zc::ArrayPtr<const uint8_t> expandedSourceFile, const identity::Sha256Digest& contentDigest,
      uint64_t byteLength, const identity::Sha256Digest& parserSchemaDigest,
      zc::ArrayPtr<const uint8_t> astSchemaDump);

private:
  explicit ParsedModuleReceipt(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;
};

/// \brief Self-owned query parser value admitted into Binder syntax algorithms.
class CanonicalParsedModule final {
public:
  ~CanonicalParsedModule() noexcept(false);
  CanonicalParsedModule(CanonicalParsedModule&&) noexcept;
  CanonicalParsedModule& operator=(CanonicalParsedModule&&) noexcept;
  ZC_DISALLOW_COPY(CanonicalParsedModule);

  /// \brief Admits one exact ParseSource value without consulting mutable session state.
  ZC_NODISCARD static zc::Maybe<CanonicalParsedModule> fromQueryResult(
      parser::CanonicalParsedSource&& parsedSource);
  ZC_NODISCARD CanonicalParsedModule clone() const;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t byteLength() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> spanFor(source::SourceRange range) const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> retainedTokenSpan(
      ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> functionParameterNameSpan(
      ast::NodeId parameter, ast::SyntaxKind expectedKind) const;
  ZC_NODISCARD bool functionParameterHasImplicitSelfType(ast::NodeId parameter) const;

private:
  struct Impl;
  explicit CanonicalParsedModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  ZC_NODISCARD const parser::CanonicalParsedSource& queryResult() const noexcept;

  friend class ParsedModuleVerifier;
  friend class VerifiedParsedModule;
};

/// \brief Query-owned parsed source verified against the frozen source registry.
class VerifiedParsedModule final {
public:
  ~VerifiedParsedModule() noexcept(false);
  VerifiedParsedModule(VerifiedParsedModule&&) noexcept;
  VerifiedParsedModule& operator=(VerifiedParsedModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedParsedModule);

  ZC_NODISCARD identity::SourceFileId sourceFile() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t byteLength() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD SourceBackedSyntaxView sourceBackedSyntax() const noexcept;
  ZC_NODISCARD const CanonicalParsedModule& syntax() const noexcept;
  ZC_NODISCARD const ParsedModuleReceipt& receipt() const noexcept;
  ZC_NODISCARD identity::SourceSpan rootSpan() const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> spanFor(source::SourceRange range) const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> retainedTokenSpan(
      ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> functionParameterNameSpan(
      ast::NodeId parameter, ast::SyntaxKind expectedKind) const;
  ZC_NODISCARD bool functionParameterHasImplicitSelfType(ast::NodeId parameter) const;
  ZC_NODISCARD zc::Maybe<source::SourceLoc> sourceLocFor(const identity::SourceSpan& span) const;

private:
  struct Impl;
  explicit VerifiedParsedModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ParsedModuleVerifier;
};

using ParsedModuleVerificationResult = zc::OneOf<VerifiedParsedModule, ParsedModuleInvariantFact>;

/// \brief Verifies a ParseSource value against the frozen source identity authority.
class ParsedModuleVerifier final {
public:
  ZC_NODISCARD static ParsedModuleVerificationResult verifyQueryResult(
      identity::SemanticContextBrand context,
      const identity::SemanticIdentityRegistrySet& registries,
      const identity::SourceFileKey& materializedSource,
      const source::SourceManager& materializedSources, const source::BufferId& materializedBuffer,
      parser::CanonicalParsedSource&& parsedSource);
};

}  // namespace zomlang::compiler::binder
