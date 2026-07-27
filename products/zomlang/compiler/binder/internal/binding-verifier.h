// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/binding-run.h"

namespace zomlang::compiler::identity {
class CanonicalEncoder;
}

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::binder {

struct ExportSurfaceCandidate final {
  ExportSurfaceCandidate(identity::ModuleId sourceModule,
                         identity::CompilationUnitId sourceCompilationUnit,
                         ExportSurfaceRevision revision,
                         zc::Vector<ExportSurfaceEntry>&& visibleEntries,
                         zc::Vector<ExportSurfaceEntry>&& exports) noexcept;
  ExportSurfaceCandidate(ExportSurfaceCandidate&&) noexcept = default;
  ExportSurfaceCandidate& operator=(ExportSurfaceCandidate&&) noexcept = default;
  ZC_DISALLOW_COPY(ExportSurfaceCandidate);
  ZC_NODISCARD ExportSurfaceCandidate clone() const;
  identity::ModuleId sourceModule;
  identity::CompilationUnitId sourceCompilationUnit;
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
#define ZOM_BINDING_FACT(id, type, member, accessor, publication, tag, domain, mutations, test) \
  zc::Vector<type> member;
#include "zomlang/compiler/binder/binding-fact-schema.def"
#undef ZOM_BINDING_FACT
  ExportSurfaceCandidate currentSurface;

private:
  BindingMetadataCandidate(identity::SemanticContextBrand semanticContext,
                           identity::ModuleId module, zc::Vector<NodeScopeFact>&& nodeScopes,
                           zc::Vector<DefinitionFact>&& definitions,
                           zc::Vector<ImplBindingFact>&& impls, zc::Vector<ScopeRecord>&& scopes,
                           ExportSurfaceCandidate&& currentSurface) noexcept;
  friend class BindingBuilder;
};

using BindingCandidateResult = zc::OneOf<BindingMetadataCandidate, BinderInvariantFact>;

/// \brief Encodes production scope and label records using the RFC 0004 allocation codec.
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeBindingAllocationDump(
    const VerifiedBindingInput& input, zc::ArrayPtr<const ScopeRecord> scopes,
    zc::ArrayPtr<const LabelFact> labels);

/// \brief Encodes the RFC 0014 contextual Self and receiver extension sequences.
ZC_NODISCARD bool encodeBindingExtensionSequences(identity::CanonicalEncoder& encoder,
                                                  const VerifiedBindingInput& input,
                                                  zc::ArrayPtr<const BoundSelfType> selfTypes,
                                                  zc::ArrayPtr<const BoundThis> thisBindings);

class BindingBuilder final {
public:
  ZC_NODISCARD static BindingCandidateResult build(const VerifiedBindingInput& input,
                                                   diagnostics::DiagnosticEngine& diagnostics);

private:
  ZC_NODISCARD static BindingCandidateResult buildCandidate(
      const VerifiedBindingInput& input, zc::Maybe<diagnostics::DiagnosticEngine&> diagnostics);
  friend class BindingDifferentialOracle;
};

class BindingVerifier final {
public:
  /// \brief Verifies production invariants without recomputing binding semantics.
  /// \param input Immutable source, identity, and dependency evidence.
  /// \param candidate Complete metadata candidate produced by BindingBuilder.
  /// \return Verified publication or one closed rejection variant.
  ZC_NODISCARD static BindingVerificationResult verify(const VerifiedBindingInput& input,
                                                       BindingMetadataCandidate&& candidate);

private:
  ZC_NODISCARD static VerifiedBindingOutput publishCandidate(BindingMetadataCandidate&& candidate);
};

/// \brief Runs test-only semantic mutations and differential candidate comparison.
class BindingDifferentialOracle final {
public:
  /// \brief Applies domain checks before comparing a test-only production baseline.
  /// \param input Immutable source, identity, and dependency evidence.
  /// \param candidate Complete metadata candidate under differential test.
  /// \return Verified publication or one closed rejection variant.
  ZC_NODISCARD static BindingVerificationResult verify(const VerifiedBindingInput& input,
                                                       BindingMetadataCandidate&& candidate);
};

}  // namespace zomlang::compiler::binder
