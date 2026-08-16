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
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/driver/core/revision.h"
#include "zomlang/compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::module_interface {

/// \brief Domain-separated revision of one RFC 0015 verified module interface.
class ModuleInterfaceRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

  /// \brief Frame already-canonical interface components in the normative order.
  ZC_NODISCARD static zc::Maybe<ModuleInterfaceRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedOwningModule,
      const identity::Sha256Digest& sourceContentDigest,
      const identity::Sha256Digest& bindingSurfaceRevision,
      const identity::Sha256Digest& signatureFactsRevision,
      const identity::Sha256Digest& markerPolicyRegistryRevision,
      const identity::Sha256Digest& importedSignatureViewRevision,
      const identity::Sha256Digest& borrowInterfaceRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureRootRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> signatureDefinitionRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> supportDefinitionRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> visibleBindingRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> exportedBindingRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implHeadRecords,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerFactRecords);

private:
  explicit ModuleInterfaceRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
};

struct LocalSignatureAuthorization final {};

struct UserImportedInterfaceRevision final {
  ModuleInterfaceRevision value;
};

struct ToolchainCoreImportedInterfaceRevision final {
  driver::core_library_query::CoreModuleInterfaceRevision value;
};

/// \brief Exact tagged revision of one source interface accepted by an ordinary consumer.
class ImportedInterfaceRevision final {
public:
  explicit ImportedInterfaceRevision(UserImportedInterfaceRevision value) noexcept
      : value(zc::mv(value)) {}
  explicit ImportedInterfaceRevision(ToolchainCoreImportedInterfaceRevision value) noexcept
      : value(zc::mv(value)) {}
  ImportedInterfaceRevision(ImportedInterfaceRevision&&) noexcept = default;
  ImportedInterfaceRevision& operator=(ImportedInterfaceRevision&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportedInterfaceRevision);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD ImportedInterfaceRevision clone() const;

private:
  zc::OneOf<UserImportedInterfaceRevision, ToolchainCoreImportedInterfaceRevision> value;
};

struct UserImportedBindingSurfaceRevision final {
  binder::ExportSurfaceRevision value;
};

struct ToolchainCoreImportedBindingSurfaceRevision final {
  driver::core_library_query::CoreBindingSurfaceRevision value;
};

/// \brief Exact tagged revision of one source binding surface accepted by an ordinary consumer.
class ImportedBindingSurfaceRevision final {
public:
  explicit ImportedBindingSurfaceRevision(UserImportedBindingSurfaceRevision value) noexcept
      : value(zc::mv(value)) {}
  explicit ImportedBindingSurfaceRevision(
      ToolchainCoreImportedBindingSurfaceRevision value) noexcept
      : value(zc::mv(value)) {}
  ImportedBindingSurfaceRevision(ImportedBindingSurfaceRevision&&) noexcept = default;
  ImportedBindingSurfaceRevision& operator=(ImportedBindingSurfaceRevision&&) noexcept = default;
  ZC_DISALLOW_COPY(ImportedBindingSurfaceRevision);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD ImportedBindingSurfaceRevision clone() const;

private:
  zc::OneOf<UserImportedBindingSurfaceRevision, ToolchainCoreImportedBindingSurfaceRevision> value;
};

struct ImportedSignatureAuthorization final {
  ImportedInterfaceRevision interfaceRevision;
};

/// \brief Closed provenance for one canonical signature-root authorization.
class SignatureAuthorizationOrigin final {
public:
  explicit SignatureAuthorizationOrigin(LocalSignatureAuthorization value) noexcept
      : value(zc::mv(value)) {}
  explicit SignatureAuthorizationOrigin(ImportedSignatureAuthorization value) noexcept
      : value(zc::mv(value)) {}
  SignatureAuthorizationOrigin(SignatureAuthorizationOrigin&&) noexcept = default;
  SignatureAuthorizationOrigin& operator=(SignatureAuthorizationOrigin&&) noexcept = default;
  ZC_DISALLOW_COPY(SignatureAuthorizationOrigin);

  ZC_NODISCARD const auto& variant() const noexcept { return value; }
  ZC_NODISCARD SignatureAuthorizationOrigin clone() const;

private:
  zc::OneOf<LocalSignatureAuthorization, ImportedSignatureAuthorization> value;
};

/// \brief Returns whether a binding can authorize a canonical signature root.
ZC_NODISCARD bool isSignatureRootBinding(const binder::BindingTarget& binding) noexcept;

/// \brief Compares the closed definition-or-semantic-import signature binding domain.
ZC_NODISCARD bool sameSignatureRootBinding(const binder::BindingTarget& left,
                                           const binder::BindingTarget& right) noexcept;

/// \brief One binding-specific authority for a canonical semantic signature root.
struct SignatureRootAuthorization final {
  binder::BindingTarget binding;
  identity::DefId canonicalDefinition;
  binder::VisibilityEnvelope visibility;
  identity::ModuleId sourceModule;
  ImportedBindingSurfaceRevision bindingSurfaceRevision;
  SignatureAuthorizationOrigin origin;

  ZC_NODISCARD SignatureRootAuthorization clone() const;
};

/// \brief Canonical root, lookup, and support records published by one interface.
struct AuthorizedSignatureBundle final {
  zc::Vector<SignatureRootAuthorization> roots;
  zc::Vector<checker::signature::SemanticSignature> definitions;
  zc::Vector<checker::signature::SemanticSignature> supportDefinitions;

  ZC_NODISCARD AuthorizedSignatureBundle clone() const;
};

}  // namespace zomlang::compiler::module_interface
