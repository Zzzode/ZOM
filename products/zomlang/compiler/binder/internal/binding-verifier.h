// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/identity/identity-invariant.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

struct ExportSurfaceCandidate final {
  ExportSurfaceCandidate(identity::ModuleId sourceModule, identity::PackageId sourcePackage,
                         ExportSurfaceRevision revision,
                         zc::Vector<ExportSurfaceEntry>&& visibleEntries,
                         zc::Vector<ExportSurfaceEntry>&& exports) noexcept;
  ExportSurfaceCandidate(ExportSurfaceCandidate&&) noexcept = default;
  ExportSurfaceCandidate& operator=(ExportSurfaceCandidate&&) noexcept = default;
  ZC_DISALLOW_COPY(ExportSurfaceCandidate);
  ZC_NODISCARD ExportSurfaceCandidate clone() const;
  identity::ModuleId sourceModule;
  identity::PackageId sourcePackage;
  ExportSurfaceRevision revision;
  zc::Vector<ExportSurfaceEntry> visibleEntries;
  zc::Vector<ExportSurfaceEntry> exports;
};

struct BindingMetadataCandidate final {
  BindingMetadataCandidate(BindingMetadataCandidate&&) noexcept = default;
  BindingMetadataCandidate& operator=(BindingMetadataCandidate&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingMetadataCandidate);
  identity::SemanticContextBrand semanticContext;
  identity::ModuleId module;
  zc::Vector<BindingFailureRef> sourceFailures;
  zc::Vector<NodeScopeFact> nodeScopes;
  zc::Vector<BindingResolution> nodeBindings;
  zc::Vector<DefinitionFact> definitions;
  zc::Vector<ImplBindingFact> impls;
  zc::Vector<ScopeRecord> scopes;
  zc::Vector<ModuleAliasBindingFact> moduleAliases;
  zc::Vector<ImportBindingFact> imports;
  zc::Vector<LocalExportFact> localExports;
  zc::Vector<DeferredMemberFact> deferredMembers;
  zc::Vector<LabelFact> labels;
  zc::Vector<ControlTransferFact> controlTransfers;
  zc::Vector<ShadowTargetFact> shadowTargets;
  zc::Vector<ClosureFreeVariableFact> closureFreeVariables;
  ExportSurfaceCandidate currentSurface;

private:
  BindingMetadataCandidate(identity::SemanticContextBrand semanticContext,
                           identity::ModuleId module, zc::Vector<NodeScopeFact>&& nodeScopes,
                           zc::Vector<DefinitionFact>&& definitions,
                           zc::Vector<ScopeRecord>&& scopes,
                           ExportSurfaceCandidate&& currentSurface) noexcept;
  friend class BindingBuilder;
};

struct VerifiedBindingOutput final {
  VerifiedBindingOutput(VerifiedBindingMetadata&& metadata,
                        VerifiedExportSurface&& surface) noexcept;
  VerifiedBindingOutput(VerifiedBindingOutput&&) noexcept = default;
  VerifiedBindingOutput& operator=(VerifiedBindingOutput&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedBindingOutput);
  VerifiedBindingMetadata metadata;
  VerifiedExportSurface surface;
};

class SourceRejected final {
public:
  SourceRejected(SourceRejected&&) noexcept = default;
  SourceRejected& operator=(SourceRejected&&) noexcept = default;
  ZC_DISALLOW_COPY(SourceRejected);
  ZC_NODISCARD zc::ArrayPtr<const BindingFailureRef> failures() const noexcept;

private:
  explicit SourceRejected(zc::Vector<BindingFailureRef>&& failures) noexcept;
  zc::Vector<BindingFailureRef> failureValues;
  friend class BindingVerifier;
};

using BindingVerificationFailureValue = zc::OneOf<identity::IdentityInvariant, BinderInvariantFact>;

struct BindingVerificationFailure final {
  explicit BindingVerificationFailure(BindingVerificationFailureValue&& value) noexcept;
  BindingVerificationFailure(BindingVerificationFailure&&) noexcept = default;
  BindingVerificationFailure& operator=(BindingVerificationFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingVerificationFailure);
  BindingVerificationFailureValue value;
};

class InvariantRejected final {
public:
  InvariantRejected(InvariantRejected&&) noexcept = default;
  InvariantRejected& operator=(InvariantRejected&&) noexcept = default;
  ZC_DISALLOW_COPY(InvariantRejected);
  ZC_NODISCARD static InvariantRejected single(BindingVerificationFailure&& failure);
  ZC_NODISCARD zc::ArrayPtr<const BindingVerificationFailure> failures() const noexcept;

private:
  explicit InvariantRejected(zc::Vector<BindingVerificationFailure>&& failures) noexcept;
  zc::Vector<BindingVerificationFailure> failureValues;
};

using BindingCandidateResult = zc::OneOf<BindingMetadataCandidate, BinderInvariantFact>;
using BindingVerificationResult =
    zc::OneOf<VerifiedBindingOutput, SourceRejected, InvariantRejected>;

/// \brief Encodes production scope records using the RFC 0004 allocation-dump codec.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeBindingAllocationDump(
    const VerifiedBindingInput& input, zc::ArrayPtr<const ScopeRecord> scopes);

class BindingBuilder final {
public:
  ZC_NODISCARD static BindingCandidateResult build(const VerifiedBindingInput& input,
                                                   diagnostics::DiagnosticEngine& diagnostics);

private:
  ZC_NODISCARD static BindingCandidateResult buildCandidate(
      const VerifiedBindingInput& input, zc::Maybe<diagnostics::DiagnosticEngine&> diagnostics);
  friend class BindingVerifier;
};

class BindingVerifier final {
public:
  ZC_NODISCARD static BindingVerificationResult verify(const VerifiedBindingInput& input,
                                                       BindingMetadataCandidate&& candidate);
};

}  // namespace zomlang::compiler::binder
