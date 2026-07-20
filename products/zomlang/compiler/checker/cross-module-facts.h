// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/checker/module-interface-contract.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver {
class VerifiedModuleInterface;
}

namespace zomlang::compiler::checker::cross_module {

/// \brief Domain-separated revision of one requester-filtered imported signature view.
class ImportedSignatureViewRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<ImportedSignatureViewRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedRequesterModule,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleRecords);

private:
  explicit ImportedSignatureViewRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
};

/// \brief Domain-separated revision of one RFC 0015 v1 frozen coherence view.
class CoherenceViewRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<CoherenceViewRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      const identity::Sha256Digest& markerPolicyRegistryRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleInterfaceRevisionRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords);

private:
  explicit CoherenceViewRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
};

enum class SignatureViewOrigin : uint8_t {
  ExplicitImport = 0x01,
  NamespaceImport = 0x02,
  Prelude = 0x03
};

/// \brief Canonical RFC 0005 codec for one complete imported-module record.
class ImportedSignatureModuleCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      SignatureViewOrigin origin, zc::ArrayPtr<const uint8_t> expandedSourceModule,
      const identity::Sha256Digest& interfaceRevision,
      const identity::Sha256Digest& bindingSurfaceRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> authorizedRootRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> lookupDefinitionRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> supportDefinitionRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleTargetRecords);
};

/// \brief One requester-authorized imported module binding target.
struct ImportedModuleTarget final {
  binder::BindingNameKey name;
  identity::ModuleId module;
  binder::ExportSurfaceRevision surfaceRevision;

  ZC_NODISCARD ImportedModuleTarget clone() const;
};

/// \brief Exact requester binding to verified source-interface definition binding.
struct ImportedDefinitionBindingSelection final {
  binder::BindingTarget requesterBinding;
  identity::DefId sourceBinding;
  SignatureViewOrigin authorizationOrigin;

  ZC_NODISCARD ImportedDefinitionBindingSelection clone() const;
};

/// \brief Exact requester name to verified source-interface module target.
struct ImportedModuleTargetSelection final {
  binder::BindingNameKey requesterName;
  binder::BindingNameKey sourceName;
  SignatureViewOrigin authorizationOrigin;
};

/// \brief Immutable projection of one exact verified source interface.
class ImportedSignatureModule final {
public:
  ~ImportedSignatureModule() noexcept(false);
  ImportedSignatureModule(ImportedSignatureModule&&) noexcept;
  ImportedSignatureModule& operator=(ImportedSignatureModule&&) noexcept;
  ZC_DISALLOW_COPY(ImportedSignatureModule);

  ZC_NODISCARD SignatureViewOrigin origin() const noexcept;
  ZC_NODISCARD identity::ModuleId sourceModule() const noexcept;
  ZC_NODISCARD const module_interface::ModuleInterfaceRevision& interfaceRevision() const noexcept;
  ZC_NODISCARD const binder::ExportSurfaceRevision& bindingSurfaceRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const module_interface::SignatureRootAuthorization> authorizedRoots()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::SemanticSignature> lookupDefinitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const signature::SemanticSignature> supportDefinitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImportedModuleTarget> moduleTargets() const noexcept;

  ZC_NODISCARD zc::Maybe<const module_interface::SignatureRootAuthorization&> authorization(
      const binder::BindingTarget& binding) const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::SemanticSignature&> lookupDefinition(
      identity::DefId definition) const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::SemanticSignature&> supportDefinition(
      identity::DefId definition) const noexcept;
  ZC_NODISCARD zc::Maybe<const ImportedModuleTarget&> moduleTarget(
      const binder::BindingNameKey& name) const noexcept;

private:
  struct Impl;
  explicit ImportedSignatureModule(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static ImportedSignatureModule publish(
      identity::SemanticContextBrand semanticContext, identity::ModuleId requester,
      SignatureViewOrigin origin, identity::ModuleId sourceModule,
      module_interface::ModuleInterfaceRevision interfaceRevision,
      binder::ExportSurfaceRevision bindingSurfaceRevision,
      zc::Vector<module_interface::SignatureRootAuthorization>&& authorizedRoots,
      zc::Vector<signature::SemanticSignature>&& lookupDefinitions,
      zc::Vector<signature::SemanticSignature>&& supportDefinitions,
      zc::Vector<ImportedModuleTarget>&& moduleTargets, zc::Array<uint8_t>&& canonicalRecord);
  ZC_NODISCARD identity::SemanticContextBrand authorizedContext() const noexcept;
  ZC_NODISCARD identity::ModuleId authorizedRequester() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalRecord() const noexcept;
  zc::Own<Impl> impl;
  friend class ::zomlang::compiler::driver::VerifiedModuleInterface;
  friend class ImportedSignatureViewBuilder;
};

/// \brief Immutable requester-filtered cross-module signature capability.
class ImportedSignatureView final {
public:
  ~ImportedSignatureView() noexcept(false);
  ImportedSignatureView(ImportedSignatureView&&) noexcept;
  ImportedSignatureView& operator=(ImportedSignatureView&&) noexcept;
  ZC_DISALLOW_COPY(ImportedSignatureView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD const ImportedSignatureViewRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImportedSignatureModule> modules() const noexcept;

  ZC_NODISCARD zc::Maybe<const ImportedSignatureModule&> source(
      identity::ModuleId module) const noexcept;
  ZC_NODISCARD zc::Maybe<const module_interface::SignatureRootAuthorization&> authorization(
      const binder::BindingTarget& binding) const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::SemanticSignature&> lookupDefinition(
      identity::DefId definition) const noexcept;
  ZC_NODISCARD zc::Maybe<const signature::SemanticSignature&> supportDefinition(
      identity::DefId definition) const noexcept;
  ZC_NODISCARD zc::Maybe<const ImportedModuleTarget&> moduleTarget(
      identity::ModuleId sourceModule, const binder::BindingNameKey& name) const noexcept;

private:
  struct Impl;
  explicit ImportedSignatureView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class ImportedSignatureViewBuilder;
};

/// \brief Seals verified-interface projections into one requester capability.
class ImportedSignatureViewBuilder final {
public:
  ZC_NODISCARD static zc::Maybe<ImportedSignatureView> build(
      identity::SemanticContextBrand semanticContext,
      const identity::SemanticContextFingerprint& contextFingerprint, identity::ModuleId requester,
      zc::Vector<ImportedSignatureModule>&& modules,
      const identity::SemanticIdentityRegistrySet& registries);
};

}  // namespace zomlang::compiler::checker::cross_module
