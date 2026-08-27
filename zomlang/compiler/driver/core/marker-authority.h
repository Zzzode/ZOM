// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/facts/signature-facts.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/identity/key/source-key.h"
#include "zomlang/compiler/source/core-distribution.h"

namespace zomlang::compiler::driver::core {

/// \brief One resolved initial core role and its stable definition identity.
struct CoreMarkerRole final {
  source::core::CoreSemanticRole role;
  identity::DefinitionKey definition;
  identity::DefId resolved;

  ZC_NODISCARD CoreMarkerRole clone() const;
};

/// \brief One core-scoped marker shape keyed by semantic role and stable definition identity.
struct CoreMarkerShapeEntry final {
  source::core::CoreSemanticRole role;
  identity::DefinitionKey definition;
  checker::signature::InterfaceMarkerShape shape;

  ZC_NODISCARD CoreMarkerShapeEntry clone() const;
};

/// \brief One fully resolved reference rule in the closed core marker policy.
struct CoreResolvedMarkerReferenceRule final {
  type::semantic::Mutability mutability;
  source::core::CoreMarkerReferenceTemplateRuleKind kind;
  zc::Maybe<identity::DefinitionKey> requiredMarker;

  ZC_NODISCARD CoreResolvedMarkerReferenceRule clone() const;
};

/// \brief Full core-specific marker policy after role requirements are resolved.
class CoreResolvedMarkerPolicy final {
public:
  ~CoreResolvedMarkerPolicy() noexcept(false);
  CoreResolvedMarkerPolicy(CoreResolvedMarkerPolicy&&) noexcept;
  CoreResolvedMarkerPolicy& operator=(CoreResolvedMarkerPolicy&&) noexcept;
  ZC_DISALLOW_COPY(CoreResolvedMarkerPolicy);

  ZC_NODISCARD static zc::Maybe<CoreResolvedMarkerPolicy> from(
      const source::core::CoreMarkerPolicyTemplate& policy,
      zc::ArrayPtr<const CoreMarkerRole> roles);
  ZC_NODISCARD CoreResolvedMarkerPolicy clone() const;
  ZC_NODISCARD zc::ArrayPtr<const source::core::CoreMarkerStructuralSubject> structuralSubjects()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const type::semantic::PrimitiveKind> builtinPrimitives() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreResolvedMarkerReferenceRule> referenceRules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const type::semantic::Mutability> rawPointerMutabilities()
      const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreResolvedMarkerPolicy(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief One complete resolved core marker policy keyed by role and stable definition identity.
struct CoreMarkerPolicyEntry final {
  source::core::CoreSemanticRole role;
  identity::DefinitionKey definition;
  CoreResolvedMarkerPolicy policy;

  ZC_NODISCARD CoreMarkerPolicyEntry clone() const;
};

/// \brief Revision of a core-scoped marker-shape inventory.
class CoreMarkerShapeInventoryRevision final {
public:
  ZC_NODISCARD static CoreMarkerShapeInventoryRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreMarkerShapeInventoryRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit CoreMarkerShapeInventoryRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;
};

/// \brief Revision of a core-scoped resolved marker-policy registry.
class CoreMarkerPolicyRegistryRevision final {
public:
  ZC_NODISCARD static CoreMarkerPolicyRegistryRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreMarkerPolicyRegistryRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit CoreMarkerPolicyRegistryRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;
};

/// \brief Revision of the independently authenticated core standard-marker authority.
class CoreStandardMarkerAuthorityRevision final {
public:
  ZC_NODISCARD static CoreStandardMarkerAuthorityRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreStandardMarkerAuthorityRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit CoreStandardMarkerAuthorityRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;
};

/// \brief Immutable closed marker-shape inventory for one source-backed core projection.
class VerifiedCoreMarkerShapeInventory final {
public:
  ~VerifiedCoreMarkerShapeInventory() noexcept(false);
  VerifiedCoreMarkerShapeInventory(VerifiedCoreMarkerShapeInventory&&) noexcept;
  VerifiedCoreMarkerShapeInventory& operator=(VerifiedCoreMarkerShapeInventory&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreMarkerShapeInventory);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreMarkerShapeInventory> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeedRevision,
      zc::Vector<CoreMarkerShapeEntry>&& shapes);
  ZC_NODISCARD VerifiedCoreMarkerShapeInventory clone() const;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& roleSeedRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreMarkerShapeEntry> shapes() const noexcept;
  ZC_NODISCARD const CoreMarkerShapeInventoryRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreMarkerShapeInventory(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Immutable resolved marker-policy registry for one source-backed core projection.
class VerifiedCoreMarkerPolicyRegistry final {
public:
  ~VerifiedCoreMarkerPolicyRegistry() noexcept(false);
  VerifiedCoreMarkerPolicyRegistry(VerifiedCoreMarkerPolicyRegistry&&) noexcept;
  VerifiedCoreMarkerPolicyRegistry& operator=(VerifiedCoreMarkerPolicyRegistry&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreMarkerPolicyRegistry);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreMarkerPolicyRegistry> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& distribution, const identity::Sha256Digest& roleSeedRevision,
      const identity::Sha256Digest& templateRevision,
      const VerifiedCoreMarkerShapeInventory& shapes, zc::Vector<CoreMarkerPolicyEntry>&& entries);
  ZC_NODISCARD VerifiedCoreMarkerPolicyRegistry clone() const;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& roleSeedRevision() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& templateRevision() const noexcept;
  ZC_NODISCARD const CoreMarkerShapeInventoryRevision& shapeRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreMarkerPolicyEntry> entries() const noexcept;
  ZC_NODISCARD const CoreMarkerPolicyRegistryRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreMarkerPolicyRegistry(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Final core-specific authority for the standard Copy and Linear markers.
class VerifiedCoreStandardMarkerAuthority final {
public:
  ~VerifiedCoreStandardMarkerAuthority() noexcept(false);
  VerifiedCoreStandardMarkerAuthority(VerifiedCoreStandardMarkerAuthority&&) noexcept;
  VerifiedCoreStandardMarkerAuthority& operator=(VerifiedCoreStandardMarkerAuthority&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreStandardMarkerAuthority);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreStandardMarkerAuthority> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& configurationRevision,
      const VerifiedCoreMarkerShapeInventory& shapes,
      const VerifiedCoreMarkerPolicyRegistry& policies, identity::ModuleKey&& prelude,
      zc::Vector<CoreMarkerRole>&& roles);
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& configurationRevision() const noexcept;
  ZC_NODISCARD const CoreMarkerShapeInventoryRevision& shapeRevision() const noexcept;
  ZC_NODISCARD const CoreMarkerPolicyRegistryRevision& policyRevision() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& prelude() const noexcept;
  ZC_NODISCARD identity::DefId copy() const noexcept;
  ZC_NODISCARD identity::DefId linear() const noexcept;
  ZC_NODISCARD const CoreStandardMarkerAuthorityRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreStandardMarkerAuthority(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Projects verified core marker policy into Checker configuration.
ZC_NODISCARD zc::Maybe<checker::signature::MarkerPolicyConfiguration> checkerConfig(
    const VerifiedCoreMarkerPolicyRegistry& policies);

}  // namespace zomlang::compiler::driver::core
