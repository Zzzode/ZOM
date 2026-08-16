// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/checker/coherence-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/module-interface-contract.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::checker::borrow {
class VerifiedBorrowInterfaceSurface;
}

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
}

namespace zomlang::compiler::driver {

enum class ModuleInterfaceInvariantKind : uint8_t {
  InputMismatch = 0x01,
  MissingProjection = 0x02,
  AdditionalProjection = 0x03,
  InvalidProjection = 0x04,
  CanonicalCodecMismatch = 0x05
};

enum class ModuleInterfaceInvariantStage : uint8_t {
  Input = 0x01,
  Projection = 0x02,
  Verification = 0x03,
  Encoding = 0x04
};

/// \brief One complete RFC 0008 module-interface invariant fact.
struct ModuleInterfaceInvariantFact final {
  ModuleInterfaceInvariantKind kind;
  ModuleInterfaceInvariantStage stage;
  identity::ModuleId module;
  zc::Maybe<identity::DefId> binding;
  zc::Maybe<identity::SourceSpan> sourceSpan;
  zc::Vector<uint32_t> structuralFieldPath;
  zc::Maybe<identity::Sha256Digest> expectedRevision;
  zc::Maybe<identity::Sha256Digest> actualRevision;
  uint32_t traversalOrdinal;
};

struct DefinitionTypeEnrichedTarget final {
  identity::DefId definition;
  checker::signature::SemanticSignature signature;
};
struct ModuleTypeEnrichedTarget final {
  identity::ModuleId module;
  binder::ExportSurfaceRevision surfaceRevision;
};

/// \brief Closed type-enriched binding target used only after signature verification.
class TypeEnrichedBindingTarget final {
public:
  explicit TypeEnrichedBindingTarget(DefinitionTypeEnrichedTarget&& value) : value(zc::mv(value)) {}
  explicit TypeEnrichedBindingTarget(ModuleTypeEnrichedTarget value) noexcept : value(value) {}
  TypeEnrichedBindingTarget(TypeEnrichedBindingTarget&&) noexcept = default;
  TypeEnrichedBindingTarget& operator=(TypeEnrichedBindingTarget&&) noexcept = default;
  ZC_DISALLOW_COPY(TypeEnrichedBindingTarget);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD TypeEnrichedBindingTarget clone() const;

private:
  zc::OneOf<DefinitionTypeEnrichedTarget, ModuleTypeEnrichedTarget> value;
};

struct VisibleBinding final {
  binder::BindingTarget bindingIdentity;
  binder::BindingNameKey name;
  TypeEnrichedBindingTarget target;
  binder::VisibilityEnvelope visibility;
  identity::SourceSpan bindingSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;

  ZC_NODISCARD VisibleBinding clone() const;
};

struct ExportedBinding final {
  binder::BindingTarget bindingIdentity;
  binder::BindingNameKey name;
  TypeEnrichedBindingTarget target;
  binder::VisibilityEnvelope visibility;
  identity::SourceSpan bindingSpan;
  identity::SourceSpan canonicalDeclarationSpan;
  zc::Maybe<identity::SourceSpan> aliasSpan;
  identity::SourceSpan exportSpan;

  ZC_NODISCARD ExportedBinding clone() const;
};

/// \brief Canonical encoders for records embedded by the interface revision.
class ModuleInterfaceCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeSignatureRoot(
      const module_interface::SignatureRootAuthorization& root,
      const checker::CheckerIdentityAuthority& identities);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeVisibleBinding(
      const VisibleBinding& binding, const checker::CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeExportedBinding(
      const ExportedBinding& binding, const checker::CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
};

/// \brief Immutable complete RFC 0015 module interface published by one verifier.
class VerifiedModuleInterface final {
public:
  ~VerifiedModuleInterface() noexcept(false);
  VerifiedModuleInterface(VerifiedModuleInterface&&) noexcept;
  VerifiedModuleInterface& operator=(VerifiedModuleInterface&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedModuleInterface);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const module_interface::ModuleInterfaceRevision& revision() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const binder::VerifiedExportSurface& bindingSurface() const noexcept;
  ZC_NODISCARD const checker::signature::SignatureFactsRevision& signatureFactsRevision()
      const noexcept;
  ZC_NODISCARD const checker::signature::MarkerPolicyRegistryRevision&
  markerPolicyRegistryRevision() const noexcept;
  ZC_NODISCARD const checker::cross_module::ImportedSignatureViewRevision&
  importedSignatureViewRevision() const noexcept;
  ZC_NODISCARD const checker::borrow::VerifiedBorrowInterfaceSurface& borrowSurface()
      const noexcept;
  ZC_NODISCARD const module_interface::AuthorizedSignatureBundle& signatures() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VisibleBinding> visibleBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ExportedBinding> exportedBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checker::signature::ImplHead> coherenceImplHeads() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checker::signature::MarkerFact> markerFacts() const noexcept;
  /// \brief Retains the materialized module capability that authorized this interface.
  ZC_NODISCARD module_graph_query::CheckerBoundModuleView retainBoundModule() const;

  /// \brief Project the exact frozen facts and canonical records accepted by coherence.
  ZC_NODISCARD checker::coherence::CoherenceModuleInput projectCoherenceInput() const;

  /// \brief Project one requester-authorized imported-signature module capability.
  ZC_NODISCARD zc::Maybe<checker::cross_module::ImportedSignatureModule> projectImportedSignatures(
      const ownership::OwnershipAdmittedBoundModule& requester,
      checker::cross_module::SignatureViewOrigin origin,
      zc::ArrayPtr<const checker::cross_module::ImportedDefinitionBindingSelection>
          definitionBindings,
      zc::ArrayPtr<const checker::cross_module::ImportedModuleTargetSelection> moduleTargetNames,
      const type::SemanticTypeStore& semanticTypes,
      const checker::CheckerIdentityAuthority& identities) const;

private:
  struct Impl;
  explicit VerifiedModuleInterface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class ModuleInterfaceVerifier;
};

/// \brief Complete verified-only inputs for atomic module-interface publication.
struct ModuleInterfaceBuildInput final {
  const ownership::OwnershipAdmittedBoundModule& boundModule;
  const checker::signature::VerifiedSignatureFacts& signatureFacts;
  const checker::cross_module::ImportedSignatureView& importedSignatures;
  const checker::signature::VerifiedMarkerPolicyRegistry& markerPolicies;
  checker::borrow::VerifiedBorrowInterfaceSurface&& borrowSurface;
  const type::SemanticTypeStore& semanticTypes;
  const checker::CheckerIdentityAuthority& identities;
};

struct ModuleInterfaceInvariantRejected final {
  zc::Vector<ModuleInterfaceInvariantFact> failures;
};

using ModuleInterfaceBuildResult =
    zc::OneOf<VerifiedModuleInterface, ModuleInterfaceInvariantRejected>;

/// \brief Sole producer of complete RFC 0015 verified module interfaces.
class ModuleInterfaceVerifier final {
public:
  ZC_NODISCARD static ModuleInterfaceBuildResult build(ModuleInterfaceBuildInput&& input);
};

}  // namespace zomlang::compiler::driver
