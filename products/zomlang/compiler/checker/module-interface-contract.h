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
#include "zomlang/compiler/identity/sha256.h"

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
struct ImportedSignatureAuthorization final {
  ModuleInterfaceRevision interfaceRevision;
};

/// \brief Closed provenance for one canonical signature-root authorization.
class SignatureAuthorizationOrigin final {
public:
  explicit SignatureAuthorizationOrigin(LocalSignatureAuthorization value) noexcept
      : value(value) {}
  explicit SignatureAuthorizationOrigin(ImportedSignatureAuthorization value) noexcept
      : value(value) {}
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
  binder::ExportSurfaceRevision bindingSurfaceRevision;
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
