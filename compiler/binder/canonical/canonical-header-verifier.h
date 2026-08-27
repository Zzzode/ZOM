// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/binder/canonical/canonical-bound-syntax-occurrence.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/identity/canonical/impl-header.h"
#include "compiler/identity/crypto/overload-header-digest.h"

namespace zomlang::compiler::binder {

/// \brief Independent AST-oracle failure; no producer diagnostic is reused.
struct CanonicalHeaderVerificationFailure final {
  ast::NodeId node;
};

/// \brief Independently reconstructed callable header and complete bound occurrence stream.
struct VerifiedCanonicalDefinitionHeader final {
  identity::OverloadHeaderAuthority authority;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
};

/// \brief Independently reconstructed implementation header and complete bound occurrence stream.
struct VerifiedCanonicalImplHeader final {
  identity::ImplHeader header;
  zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
};

using CanonicalDefinitionHeaderVerification =
    zc::OneOf<VerifiedCanonicalDefinitionHeader, CanonicalHeaderVerificationFailure>;
using CanonicalImplHeaderVerification =
    zc::OneOf<VerifiedCanonicalImplHeader, CanonicalHeaderVerificationFailure>;

/// \brief Read-only syntax authority consumed by the independent header oracle.
class CanonicalHeaderSyntaxView final {
public:
  CanonicalHeaderSyntaxView(zc::ArrayPtr<const DefinitionInventoryEntry> definitions,
                            zc::ArrayPtr<const ImplInventoryEntry> implementations) noexcept
      : definitionEntries(definitions), implementationEntries(implementations) {}

  ZC_NODISCARD zc::ArrayPtr<const DefinitionInventoryEntry> definitions() const noexcept {
    return definitionEntries;
  }
  ZC_NODISCARD zc::ArrayPtr<const ImplInventoryEntry> implementations() const noexcept {
    return implementationEntries;
  }

private:
  zc::ArrayPtr<const DefinitionInventoryEntry> definitionEntries;
  zc::ArrayPtr<const ImplInventoryEntry> implementationEntries;
};

/// \brief Independent RFC 0018 normalization oracle used only by frozen-input verification.
///
/// This verifier shares canonical record types and codecs with admission, but owns its AST walk,
/// binder-stack construction, type normalization, receiver normalization, and bound extraction.
class CanonicalHeaderVerifier final {
public:
  ZC_NODISCARD static CanonicalDefinitionHeaderVerification reconstructDefinition(
      const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax,
      const DefinitionInventoryEntry& definition);
  ZC_NODISCARD static CanonicalImplHeaderVerification reconstructImpl(
      const ast::Tree& tree, const CanonicalHeaderSyntaxView& syntax,
      const ImplInventoryEntry& implementation);
};

}  // namespace zomlang::compiler::binder
