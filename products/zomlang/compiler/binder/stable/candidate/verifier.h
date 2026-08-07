// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/stable/candidate/producer.h"

namespace zomlang::compiler::binder {

enum class StableIdentityCandidateInvariantKind : uint8_t {
  ProductionMismatch = 0x01,
  InvalidDefinitionAuthority = 0x02,
  InvalidImplementationAuthority = 0x03,
  InvalidSyntaxSite = 0x04,
  DigestCollision = 0x05
};

struct StableIdentityCandidateInvariant final {
  StableIdentityCandidateInvariantKind kind;
  ast::NodeId node;
};

enum class StableIdentityCandidateSourceFailureKind : uint8_t {
  ConstantExpressionNotAllowed = 0x01,
  DuplicateGenericParameter = 0x02
};

struct StableIdentityCandidateSourceFailure final {
  StableIdentityCandidateSourceFailureKind kind;
  ast::NodeId node;
  identity::SourceSpan source;
  zc::Maybe<ast::NodeId> previousNode;
  zc::Maybe<identity::SourceSpan> previous;
  zc::Maybe<identity::DeclaredDefinitionName> identifier;
};

/// \brief Independently reconstructed definition authority and current syntax occurrence.
struct VerifiedStableDefinitionCandidate final {
  ast::NodeId node;
  identity::DefinitionIdentityAuthority authority;
  IdentitySyntaxSiteKey site;
  identity::SourceSpan source;
};

/// \brief Independently reconstructed implementation authority and current syntax occurrence.
struct VerifiedStableImplementationCandidate final {
  ast::NodeId node;
  identity::ImplIdentityAuthority authority;
  IdentitySyntaxSiteKey site;
  identity::SourceSpan source;
};

/// \brief Independently verified stable candidate projection used before registry mutation.
struct VerifiedStableIdentityCandidateInventory final {
  zc::Vector<VerifiedStableDefinitionCandidate> definitions;
  zc::Vector<VerifiedStableImplementationCandidate> implementations;
};

using StableIdentityCandidateVerification =
    zc::OneOf<VerifiedStableIdentityCandidateInventory, StableIdentityCandidateSourceFailure,
              StableIdentityCandidateInvariant>;

/// \brief Independently reconstructs the complete identity syntax-site topology.
class IdentitySyntaxSiteInventoryVerifier final {
public:
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSiteInventory> reconstruct(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode);
  ZC_NODISCARD static bool verify(const CanonicalParsedModule& parsedModule,
                                  const identity::ModuleKey& module, ast::NodeId moduleNode,
                                  const IdentitySyntaxSiteInventory& candidate);
  ZC_NODISCARD static zc::Maybe<IdentitySyntaxSiteKey> resolve(
      const CanonicalParsedModule& parsedModule, const IdentitySyntaxSiteInventory& inventory,
      ast::NodeId node, const identity::SourceSpan& source);
};

/// \brief One later equal definition occurrence paired with the canonical first occurrence.
struct StableDefinitionRedeclaration final {
  uint32_t first;
  uint32_t duplicate;
  BinderDiagnosticCode diagnostic;
};

using StableDefinitionRedeclarationValidation =
    zc::OneOf<zc::Vector<StableDefinitionRedeclaration>, StableIdentityCandidateInvariant>;

/// \brief Independent AST oracle for producer candidates and definition collision grouping.
class CandidateVerifier final {
public:
  /// \brief Reconstructs stable identity authority without consulting producer output.
  ZC_NODISCARD static StableIdentityCandidateVerification reconstruct(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode);

  ZC_NODISCARD static StableIdentityCandidateVerification verify(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, const StableIdentityCandidateProduction& production);

  /// \brief Groups verified authorities by complete record in canonical source order.
  ZC_NODISCARD static StableDefinitionRedeclarationValidation findDefinitionRedeclarations(
      zc::ArrayPtr<const VerifiedStableDefinitionCandidate> definitions);
};

using StableIdentityAdmissionVerification =
    zc::OneOf<StableIdentityAdmission, StableIdentityCandidateSourceFailure,
              StableIdentityCandidateInvariant>;

/// \brief Independently verifies and materializes the stable-identity admission capability.
class StableIdentityAdmissionVerifier final {
public:
  ZC_NODISCARD static StableIdentityAdmissionVerification verify(
      const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
      ast::NodeId moduleNode, const IdentitySyntaxSiteInventory& sites,
      const StableIdentityCandidateProduction& production);
};

}  // namespace zomlang::compiler::binder
