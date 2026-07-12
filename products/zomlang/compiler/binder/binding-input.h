// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

enum class ModuleGraphInvariantKind : uint8_t {
  InputMismatch,
  IncompleteResolution,
  InvalidEdge,
  InvalidPrelude,
  RevisionMismatch
};

/// \brief Deterministic fail-closed module graph or binding handoff failure.
struct ModuleGraphInvariantFact final {
  ModuleGraphInvariantKind kind;
  zc::Maybe<identity::ModuleId> requester;
  zc::Vector<uint32_t> structuralFieldPath;
  uint32_t occurrence;
};

/// \brief Immutable revision of one verified module dependency graph.
class ModuleGraphRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;

  friend class ModuleGraphVerifier;
  friend class BindingInputVerifier;
  friend zc::Maybe<ModuleGraphRevision> computeModuleGraphRevision(
      const identity::SemanticContextFingerprint&, const identity::ModuleKey&);
};

/// \brief Private-publication view of a complete verified module graph.
class VerifiedModuleGraphView final {
public:
  ~VerifiedModuleGraphView() noexcept(false);
  VerifiedModuleGraphView(VerifiedModuleGraphView&&) noexcept;
  VerifiedModuleGraphView& operator=(VerifiedModuleGraphView&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedModuleGraphView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD const ModuleGraphRevision& revision() const noexcept;

private:
  struct Impl;
  explicit VerifiedModuleGraphView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ModuleGraphVerifier;
  friend class BindingInputVerifier;
};

using ModuleGraphVerificationResult = zc::OneOf<VerifiedModuleGraphView, ModuleGraphInvariantFact>;

/// \brief Verifies the dependency-free module-graph slice supported before RFC 0008.
class ModuleGraphVerifier final {
public:
  ZC_NODISCARD static ModuleGraphVerificationResult verifySingleModule(
      identity::SemanticContextBrand context,
      const identity::SemanticContextFingerprint& expectedFingerprint,
      const identity::SemanticIdentityRegistrySet& registries, identity::ModuleId requester,
      const VerifiedParsedModule& parsedModule);
};

/// \brief Emits the fatal ZOM9956 diagnostic for a rejected graph or binding handoff.
void emitModuleGraphInvariant(diagnostics::DiagnosticEngine& diagnostics,
                              const ModuleGraphInvariantFact& fact);

/// \brief Untrusted inputs checked before the binder can observe semantic state.
struct BindingInputCandidate final {
  identity::SemanticContextBrand semanticContext;
  identity::PackageId package;
  identity::CrateId crate;
  identity::ModuleId module;
  const identity::SemanticIdentityRegistrySet& registries;
  const VerifiedModuleGraphView& moduleGraph;
  const VerifiedParsedModule& parsedModule;
  const FrozenDefinitionInventoryView& definitions;
};

/// \brief Move-only binder input published only after complete verification.
class VerifiedBindingInput final {
public:
  ~VerifiedBindingInput() noexcept(false);
  VerifiedBindingInput(VerifiedBindingInput&&) noexcept;
  VerifiedBindingInput& operator=(VerifiedBindingInput&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBindingInput);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::PackageId package() const noexcept;
  ZC_NODISCARD const identity::PackageKey& packageKey() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD const identity::CrateKey& crateKey() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& moduleKey() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const VerifiedParsedModule& parsedModule() const noexcept;
  ZC_NODISCARD const FrozenDefinitionInventoryView& definitions() const noexcept;

private:
  struct Impl;
  explicit VerifiedBindingInput(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class BindingInputVerifier;
  friend class BindingVerifier;
};

using BindingInputVerificationResult = zc::OneOf<VerifiedBindingInput, ModuleGraphInvariantFact>;

/// \brief Recomputes graph and syntax inventories before publishing binder input.
class BindingInputVerifier final {
public:
  ZC_NODISCARD static BindingInputVerificationResult verify(const BindingInputCandidate& candidate);
};

}  // namespace zomlang::compiler::binder
