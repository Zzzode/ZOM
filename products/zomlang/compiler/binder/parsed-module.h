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
#include "zomlang/compiler/parser/token-snapshot.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {

enum class ParsedModuleInvariantKind : uint8_t {
  SourceMismatch,
  InvalidTree,
  InvalidSourceRange,
  InvalidTokenProvenance,
  ReceiptMismatch,
  RegistryMismatch
};

/// \brief Closed failure published before a parsed module can enter binding.
struct ParsedModuleInvariantFact final {
  ParsedModuleInvariantKind kind;
  uint32_t occurrence;
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

/// \brief Move-only parser result structurally bound to one immutable source snapshot.
class UnbrandedParsedModule final {
public:
  ~UnbrandedParsedModule() noexcept(false);
  UnbrandedParsedModule(UnbrandedParsedModule&&) noexcept;
  UnbrandedParsedModule& operator=(UnbrandedParsedModule&&) noexcept;
  ZC_DISALLOW_COPY(UnbrandedParsedModule);

  ZC_NODISCARD const ParsedModuleReceipt& receipt() const noexcept;

private:
  struct Impl;
  explicit UnbrandedParsedModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ParsedModuleVerifier;
};

/// \brief Immutable parser result promoted only after source-registry freeze.
class VerifiedParsedModule final {
public:
  ~VerifiedParsedModule() noexcept(false);
  VerifiedParsedModule(VerifiedParsedModule&&) noexcept;
  VerifiedParsedModule& operator=(VerifiedParsedModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedParsedModule);

  ZC_NODISCARD identity::SourceFileId sourceFile() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& contentDigest() const noexcept;
  ZC_NODISCARD uint64_t byteLength() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const ParsedModuleReceipt& receipt() const noexcept;
  ZC_NODISCARD identity::SourceSpan rootSpan() const;
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> spanFor(source::SourceRange range) const;
  /// \brief Return one parser-retained token when ordinal, kind, and ownership match.
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> retainedTokenSpan(
      ast::NodeId owner, uint32_t tokenOrdinal, ast::SyntaxKind expectedKind) const;
  /// \brief Return the parser-retained name token of one function parameter.
  ZC_NODISCARD zc::Maybe<identity::SourceSpan> functionParameterNameSpan(
      ast::NodeId parameter, ast::SyntaxKind expectedKind) const;
  /// \brief Resolve the start of a checked source span back to its parser source location.
  ZC_NODISCARD zc::Maybe<source::SourceLoc> sourceLocFor(const identity::SourceSpan& span) const;

private:
  struct Impl;
  explicit VerifiedParsedModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ParsedModuleVerifier;
};

using ParsedModuleAdmissionResult = zc::OneOf<UnbrandedParsedModule, ParsedModuleInvariantFact>;
using ParsedModulePromotionResult = zc::OneOf<VerifiedParsedModule, ParsedModuleInvariantFact>;

/// \brief Admits parser output and promotes it after frozen source identity verification.
class ParsedModuleVerifier final {
public:
  ZC_NODISCARD static ParsedModuleAdmissionResult admit(
      const identity::ImmutableSourceSnapshot& snapshot, const source::SourceManager& sources,
      const source::BufferId& buffer, parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree);

  ZC_NODISCARD static ParsedModulePromotionResult promote(
      identity::SemanticContextBrand context,
      const identity::SemanticIdentityRegistrySet& registries,
      UnbrandedParsedModule&& parsedModule);
};

}  // namespace zomlang::compiler::binder
