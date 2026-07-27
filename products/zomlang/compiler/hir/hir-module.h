// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/hir/checked-module.h"
#include "zomlang/compiler/hir/hir-node-id.h"
#include "zomlang/compiler/type/semantic-type-data.h"

namespace zomlang::compiler::mir {
class BuiltMirBuilder;
class BuiltMirVerifier;
}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::hir {

struct ModuleHirVisibility final {
  identity::ModuleId module;
};
struct ExternalHirVisibility final {};

enum class HirVisibilityKind : uint8_t { Module = 0x01, External = 0x02 };

/// \brief Closed source visibility retained without binder-owned lookup state.
class HirVisibility final {
public:
  ZC_NODISCARD static HirVisibility module(identity::ModuleId module) noexcept;
  ZC_NODISCARD static HirVisibility external() noexcept;
  HirVisibility(HirVisibility&&) noexcept = default;
  HirVisibility& operator=(HirVisibility&&) noexcept = default;
  ZC_DISALLOW_COPY(HirVisibility);

  ZC_NODISCARD HirVisibility clone() const noexcept;
  ZC_NODISCARD HirVisibilityKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<identity::ModuleId> visibleModule() const noexcept;

private:
  explicit HirVisibility(ModuleHirVisibility value) noexcept : value(value) {}
  explicit HirVisibility(ExternalHirVisibility value) noexcept : value(value) {}
  zc::OneOf<ModuleHirVisibility, ExternalHirVisibility> value;
};

enum class HirLinkage : uint8_t {
  Internal = 0x01,
  ExternalCdecl = 0x02,
  ExternalStdcall = 0x03,
  ExternalZomNative = 0x04
};

enum class HirValueCategory : uint8_t { Value = 0x01, Place = 0x02 };

/// \brief One module-owned scalar binding pattern in immutable semantic HIR.
struct HirBindingPattern final {
  HirNodeId node;
  identity::DefId binding;
  identity::SemanticTypeId type;
  identity::SemanticTypeId scrutineeType;
  bool reachable;
  identity::SourceSpan sourceSpan;
};

/// \brief One exact scalar literal expression copied from verified checked facts.
struct HirScalarLiteralExpression final {
  HirNodeId node;
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One scalar return statement in immutable semantic HIR.
struct HirReturnStatement final {
  HirNodeId node;
  identity::SemanticTypeId resultType;
  HirNodeId value;
  identity::SourceSpan sourceSpan;
};

/// \brief One closed lexical block in immutable semantic HIR.
struct HirBlockStatement final {
  HirNodeId node;
  zc::Vector<HirNodeId> statements;
  identity::SourceSpan sourceSpan;
};

/// \brief One module-scope function with a verified scalar return body.
struct HirFunctionDeclaration final {
  HirNodeId node;
  identity::DefId definition;
  identity::SemanticTypeId resultType;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan sourceSpan;
  HirNodeId body;
};

/// \brief One module-scope let, mut, or const declaration in semantic HIR.
struct HirValueDeclaration final {
  HirNodeId node;
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  identity::SemanticTypeId declaredType;
  identity::SemanticTypeId inferredType;
  type::semantic::Mutability mutability;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan sourceSpan;
  HirNodeId pattern;
  HirNodeId initializer;
  zc::Maybe<checker::checked::CanonicalConstValue> constantValue;
};

/// \brief Private mutable build product admitted only by the HIR verifier.
class HirModuleCandidate final {
public:
  ~HirModuleCandidate() noexcept(false);
  HirModuleCandidate(HirModuleCandidate&&) noexcept;
  HirModuleCandidate& operator=(HirModuleCandidate&&) noexcept;
  ZC_DISALLOW_COPY(HirModuleCandidate);

private:
  struct Impl;
  explicit HirModuleCandidate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class HirBuilder;
  friend class HirVerifier;
};

/// \brief NodeId-free immutable semantic HIR module capability.
class VerifiedHirModule final {
public:
  ~VerifiedHirModule() noexcept(false);
  VerifiedHirModule(VerifiedHirModule&&) noexcept;
  VerifiedHirModule& operator=(VerifiedHirModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedHirModule);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& parsedModuleReceiptDigest() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD const ModuleInterfaceLineage& ownInterface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleInterfaceLineage> visibleImportedInterfaces()
      const noexcept;
  ZC_NODISCARD const checker::checked::CheckedEvidenceLease& checkedEvidenceLease() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::VerifiedBorrowEvidenceLease& borrowEvidenceLease()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirValueDeclaration> declarations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirFunctionDeclaration> functions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirBlockStatement> blocks() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirReturnStatement> returns() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirBindingPattern> patterns() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirScalarLiteralExpression> expressions() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::String> dump() const;

private:
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository()
      const noexcept;
  ZC_NODISCARD const identity::SemanticIdentityRegistrySet& registries() const noexcept;
  ZC_NODISCARD const type::SemanticTypeStore& semanticTypes() const noexcept;

  struct Impl;
  explicit VerifiedHirModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class HirVerifier;
  friend class mir::BuiltMirBuilder;
  friend class mir::BuiltMirVerifier;
};

/// \brief Lowers only complete verified frontend handoffs into HIR candidates.
class HirBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<HirModuleCandidate> build(
      VerifiedCheckedModule&& checkedModule);
};

/// \brief Sole publisher of immutable NodeId-free HIR modules.
class HirVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedHirModule> verify(
      HirModuleCandidate&& candidate);
};

}  // namespace zomlang::compiler::hir
