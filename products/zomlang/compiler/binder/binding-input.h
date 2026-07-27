// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-metadata.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/module-resolution.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/diagnostics/toolchain-module-root-argument.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"

namespace zomlang::compiler::diagnostics {
class DiagnosticEngine;
}

namespace zomlang::compiler::driver::core_library_query {
class VerifiedCoreDistributionInputTransaction;
}

namespace zomlang::compiler::driver::module_graph_query {
class ModuleGraphRecord;
class ModuleGraphSccRecord;
}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::driver::package {
class VerifiedPackageCompilationRequest;
}

namespace zomlang::compiler::query {
class QuerySnapshot;
}

namespace zomlang::compiler::binder {

namespace test {
class VerifiedModuleGraphFixture;
}

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

class VerifiedModuleDependencyEdge;

/// \brief Immutable revision of one verified module dependency graph.
class ModuleGraphRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;

private:
  explicit ModuleGraphRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest value;

  friend class BindingInputVerifier;
  friend class VerifiedModuleGraphBuilder;
  friend class VerifiedModuleGraphVerifier;
  friend class test::VerifiedModuleGraphFixture;
};

/// \brief One canonical module endpoint supplied to the global graph verifier.
class ModuleGraphModule final {
public:
  ModuleGraphModule(identity::ModuleKey&& key, identity::ModuleId module) noexcept;
  ModuleGraphModule(ModuleGraphModule&&) noexcept = default;
  ModuleGraphModule& operator=(ModuleGraphModule&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphModule);

  ZC_NODISCARD const identity::ModuleKey& key() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;

private:
  identity::ModuleKey keyValue;
  identity::ModuleId moduleValue;
};

/// \brief One verified parser result assigned to its selected semantic module.
struct ParsedModuleGraphInput final {
  identity::ModuleId module;
  const VerifiedParsedModule& parsedModule;
};

class VerifiedModuleGraph;

/// \brief Dependency edge published only after request, receipt, and endpoints verify.
class VerifiedModuleDependencyEdge final {
public:
  VerifiedModuleDependencyEdge(VerifiedModuleDependencyEdge&&) noexcept = default;
  VerifiedModuleDependencyEdge& operator=(VerifiedModuleDependencyEdge&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedModuleDependencyEdge);

  ZC_NODISCARD const ModuleDependencyRequest& request() const noexcept;
  ZC_NODISCARD identity::ModuleId target() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> encodedKey() const noexcept;

private:
  VerifiedModuleDependencyEdge(ModuleDependencyRequest&& request, identity::ModuleId target,
                               zc::Array<uint8_t>&& encodedKey) noexcept;

  ModuleDependencyRequest requestValue;
  identity::ModuleId targetValue;
  zc::Array<uint8_t> encodedKeyValue;

  friend class VerifiedModuleGraph;
  friend class VerifiedModuleGraphBuilder;
  friend class VerifiedModuleGraphVerifier;
  friend class test::VerifiedModuleGraphFixture;
};

/// \brief One canonically anchored source failure that prevents graph publication.
class ModuleGraphSourceFailure final {
public:
  ~ModuleGraphSourceFailure() noexcept(false);
  ModuleGraphSourceFailure(ModuleGraphSourceFailure&&) noexcept;
  ModuleGraphSourceFailure& operator=(ModuleGraphSourceFailure&&) noexcept;
  ZC_DISALLOW_COPY(ModuleGraphSourceFailure);

  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  /// \brief Returns the source-root path to the declaration that owns the declared-name token.
  ZC_NODISCARD const LocalSyntaxPath& declaredNamePath() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD const diagnostics::ToolchainModuleRootArgument& argument() const noexcept;

private:
  ModuleGraphSourceFailure(identity::ModuleKey&& module, identity::SourceFileKey&& source,
                           LocalSyntaxPath&& declaredNamePath, uint32_t schemaPreorderOrdinal,
                           diagnostics::ToolchainModuleRootArgument&& argument) noexcept;

  struct Impl;
  zc::Own<Impl> impl;

  friend class ModuleGraphSourceFailureBuilder;
};

/// \brief Builds candidate source-root reservation failures from verified parser input.
class ModuleGraphSourceFailureBuilder final {
public:
  /// \brief Reconstructs the reserved-root failure for one selected source module.
  /// \param module Selected stable module identity and branded handle.
  /// \param parsed Verified immutable parser result assigned to the module.
  /// \return The complete failure when the root declaration is `core`, otherwise none.
  ZC_NODISCARD static zc::Maybe<ModuleGraphSourceFailure> buildToolchainModuleRootReserved(
      const ModuleGraphModule& module, const ParsedModuleGraphInput& parsed);
};

/// \brief Private-publication view of a complete verified module graph.
class VerifiedModuleGraphView final {
public:
  ~VerifiedModuleGraphView() noexcept(false);
  VerifiedModuleGraphView(VerifiedModuleGraphView&&) noexcept;
  VerifiedModuleGraphView& operator=(VerifiedModuleGraphView&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedModuleGraphView);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD const ModuleGraphRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedModuleDependencyEdge> edges() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceFileKey&> sourceFile(
      identity::ModuleId module) const noexcept;

private:
  struct Impl;
  explicit VerifiedModuleGraphView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class VerifiedModuleGraph;
  friend class BindingInputVerifier;
};

/// \brief Immutable complete module graph published before requester binding begins.
class VerifiedModuleGraph final {
public:
  ~VerifiedModuleGraph() noexcept(false);
  VerifiedModuleGraph(VerifiedModuleGraph&&) noexcept;
  VerifiedModuleGraph& operator=(VerifiedModuleGraph&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedModuleGraph);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const ModuleGraphRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedModuleDependencyEdge> edges() const noexcept;
  /// \brief Returns the verified selected source without cloning a requester graph view.
  ZC_NODISCARD zc::Maybe<const identity::SourceFileKey&> sourceFile(
      identity::ModuleId module) const noexcept;
  ZC_NODISCARD zc::Maybe<VerifiedModuleGraphView> view(identity::ModuleId requester) const;

private:
  struct Impl;
  explicit VerifiedModuleGraph(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class VerifiedModuleGraphVerifier;
  friend class test::VerifiedModuleGraphFixture;
};

/// \brief Complete final-snapshot inputs for revision-local Binder graph materialization.
struct ModuleGraphMaterializationInput final {
  const driver::package::VerifiedPackageCompilationRequest& packageRequest;
  const driver::core_library_query::VerifiedCoreDistributionInputTransaction& coreInputs;
  identity::SemanticContextBrand semanticContext;
  const identity::SemanticContextFingerprint& semanticContextFingerprint;
  const driver::module_graph_query::ModuleGraphRecord& stableGraph;
  const driver::module_graph_query::ModuleGraphSccRecord& stableScc;
  const identity::SemanticIdentityRegistrySet& registries;
  zc::ArrayPtr<const identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges;
  zc::ArrayPtr<const identity::CrateDependencyEdgeKey> crateEdges;
  zc::ArrayPtr<const ParsedModuleGraphInput> parsedModules;
  const query::QuerySnapshot& finalSnapshot;
};

/// \brief Untrusted handleful graph candidate built from one complete final snapshot.
class BinderModuleGraphCandidate final {
public:
  ~BinderModuleGraphCandidate() noexcept(false);
  BinderModuleGraphCandidate(BinderModuleGraphCandidate&&) noexcept;
  BinderModuleGraphCandidate& operator=(BinderModuleGraphCandidate&&) noexcept;
  ZC_DISALLOW_COPY(BinderModuleGraphCandidate);

private:
  struct Impl;
  explicit BinderModuleGraphCandidate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class VerifiedModuleGraphBuilder;
  friend class VerifiedModuleGraphVerifier;
};

using ModuleGraphCandidateResult = zc::OneOf<BinderModuleGraphCandidate, ModuleGraphInvariantFact>;
using ModuleGraphMaterializationResult = zc::OneOf<VerifiedModuleGraph, ModuleGraphInvariantFact>;

/// \brief Builds an untrusted handleful graph from complete final-snapshot authorities.
class VerifiedModuleGraphBuilder final {
public:
  ZC_NODISCARD static ModuleGraphCandidateResult produce(
      const ModuleGraphMaterializationInput& input);
  ZC_NODISCARD static ModuleGraphMaterializationResult build(
      const ModuleGraphMaterializationInput& input);
};

/// \brief Independently verifies and publishes a handleful Binder module graph.
class VerifiedModuleGraphVerifier final {
public:
  ZC_NODISCARD static ModuleGraphMaterializationResult verify(
      const ModuleGraphMaterializationInput& input, const BinderModuleGraphCandidate& candidate);
};

/// \brief Emits the fatal ZOM9956 diagnostic for a rejected graph or binding handoff.
void emitModuleGraphInvariant(diagnostics::DiagnosticEngine& diagnostics,
                              const ModuleGraphInvariantFact& fact);

/// \brief One complete dependency surface explicitly keyed by its graph target.
struct DependencyExportSurface final {
  identity::ModuleId sourceModule;
  const VerifiedExportSurface& surface;
};

enum class BindingInputDiagnostic : uint16_t {
  ImportMemberNotFound = 3013,
  ReexportMemberNotFound = 3016,
  ImportTargetNotVisible = 3018,
  ReexportTargetNotVisible = 3019
};

/// \brief One requester-owned source failure discovered before binder publication.
class BindingInputSourceFailure final {
public:
  BindingInputSourceFailure(BindingInputSourceFailure&&) noexcept = default;
  BindingInputSourceFailure& operator=(BindingInputSourceFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(BindingInputSourceFailure);

  ZC_NODISCARD BindingInputDiagnostic diagnostic() const noexcept;
  ZC_NODISCARD ast::NodeId syntax() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& source() const noexcept;
  ZC_NODISCARD zc::StringPtr modulePath() const noexcept;
  ZC_NODISCARD const identity::SemanticIdentifier& memberName() const noexcept;

private:
  BindingInputSourceFailure(BindingInputDiagnostic diagnostic, ast::NodeId syntax,
                            identity::SourceSpan&& source, zc::String&& modulePath,
                            identity::SemanticIdentifier&& memberName) noexcept;

  BindingInputDiagnostic diagnosticValue;
  ast::NodeId syntaxValue;
  identity::SourceSpan sourceValue;
  zc::String modulePathValue;
  identity::SemanticIdentifier memberNameValue;
  friend class BindingInputVerifier;
};

/// \brief Sorted non-empty member or visibility rejection that publishes no binder input.
class BindingInputSourceRejected final {
public:
  explicit BindingInputSourceRejected(zc::Vector<BindingInputSourceFailure>&& failures) noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BindingInputSourceFailure> failures() const noexcept;

private:
  zc::Vector<BindingInputSourceFailure> failureValues;
};

/// \brief Immutable requester-filtered projection of one verified dependency surface.
class VerifiedExportSurfaceView final {
public:
  ~VerifiedExportSurfaceView() noexcept(false);
  VerifiedExportSurfaceView(VerifiedExportSurfaceView&&) noexcept;
  VerifiedExportSurfaceView& operator=(VerifiedExportSurfaceView&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedExportSurfaceView);

  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD identity::ModuleId sourceModule() const noexcept;
  ZC_NODISCARD const ExportSurfaceRevision& sourceRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ExportSurfaceEntry> visibleEntries() const noexcept;

private:
  struct Impl;
  explicit VerifiedExportSurfaceView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BindingInputVerifier;
};

/// \brief One exact import or foreign re-export selected from a verified surface view.
class ResolvedImportEdge final {
public:
  ResolvedImportEdge(ResolvedImportEdge&&) noexcept = default;
  ResolvedImportEdge& operator=(ResolvedImportEdge&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedImportEdge);

  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD ast::NodeId syntax() const noexcept;
  ZC_NODISCARD ImportBindingKind kind() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD identity::ModuleId sourceModule() const noexcept;
  ZC_NODISCARD const ExportSurfaceRevision& sourceRevision() const noexcept;
  ZC_NODISCARD zc::Maybe<const BindingNameKey&> requestedName() const noexcept;
  ZC_NODISCARD const BindingNameKey& localName() const noexcept;
  ZC_NODISCARD const identity::SemanticImportBindingKey& binding() const noexcept;
  ZC_NODISCARD const BindingTarget& canonicalTarget() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& declarationSpan() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> aliasSpan() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& canonicalDeclarationSpan() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> exportSpan() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ReexportProvenanceStep> sourceReexportChain() const noexcept;

private:
  ResolvedImportEdge(identity::ModuleId requester, ast::NodeId syntax,
                     uint32_t schemaPreorderOrdinal, ImportBindingKind kind,
                     identity::ModuleId sourceModule, ExportSurfaceRevision sourceRevision,
                     zc::Maybe<BindingNameKey>&& requestedName, BindingNameKey&& localName,
                     identity::SemanticImportBindingKey&& binding, BindingTarget&& canonicalTarget,
                     identity::SourceSpan&& declarationSpan,
                     zc::Maybe<identity::SourceSpan>&& aliasSpan,
                     identity::SourceSpan&& canonicalDeclarationSpan,
                     zc::Maybe<identity::SourceSpan>&& exportSpan,
                     zc::Vector<ReexportProvenanceStep>&& sourceReexportChain) noexcept;

  identity::ModuleId requesterValue;
  ast::NodeId syntaxValue;
  uint32_t schemaPreorderOrdinalValue;
  ImportBindingKind kindValue;
  identity::ModuleId sourceModuleValue;
  ExportSurfaceRevision sourceRevisionValue;
  zc::Maybe<BindingNameKey> requestedNameValue;
  BindingNameKey localNameValue;
  identity::SemanticImportBindingKey bindingValue;
  BindingTarget canonicalTargetValue;
  identity::SourceSpan declarationSpanValue;
  zc::Maybe<identity::SourceSpan> aliasSpanValue;
  identity::SourceSpan canonicalDeclarationSpanValue;
  zc::Maybe<identity::SourceSpan> exportSpanValue;
  zc::Vector<ReexportProvenanceStep> sourceReexportChainValue;
  friend class BindingInputVerifier;
};

/// \brief One module-alias declaration paired with its exact graph target surface.
class ResolvedModuleAlias final {
public:
  ResolvedModuleAlias(ResolvedModuleAlias&&) noexcept = default;
  ResolvedModuleAlias& operator=(ResolvedModuleAlias&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedModuleAlias);

  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD ast::NodeId syntax() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD identity::DefId alias() const noexcept;
  ZC_NODISCARD const BindingNameKey& localName() const noexcept;
  ZC_NODISCARD identity::ModuleId targetModule() const noexcept;
  ZC_NODISCARD const ExportSurfaceRevision& targetRevision() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& declarationSpan() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& targetSpan() const noexcept;
  ZC_NODISCARD bool exported() const noexcept;

private:
  ResolvedModuleAlias(identity::ModuleId requester, ast::NodeId syntax,
                      uint32_t schemaPreorderOrdinal, identity::DefId alias,
                      BindingNameKey&& localName, identity::ModuleId targetModule,
                      ExportSurfaceRevision targetRevision, identity::SourceSpan&& declarationSpan,
                      identity::SourceSpan&& targetSpan, bool exported) noexcept;

  identity::ModuleId requesterValue;
  ast::NodeId syntaxValue;
  uint32_t schemaPreorderOrdinalValue;
  identity::DefId aliasValue;
  BindingNameKey localNameValue;
  identity::ModuleId targetModuleValue;
  ExportSurfaceRevision targetRevisionValue;
  identity::SourceSpan declarationSpanValue;
  identity::SourceSpan targetSpanValue;
  bool exportedValue;
  friend class BindingInputVerifier;
};

/// \brief One verified local-export specifier awaiting current-module name lookup.
class ResolvedLocalExportSpecifier final {
public:
  ResolvedLocalExportSpecifier(ResolvedLocalExportSpecifier&&) noexcept = default;
  ResolvedLocalExportSpecifier& operator=(ResolvedLocalExportSpecifier&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedLocalExportSpecifier);

  ZC_NODISCARD ast::NodeId syntax() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD const identity::SemanticIdentifier& sourceName() const noexcept;
  ZC_NODISCARD const identity::SemanticIdentifier& exportedName() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& sourceNameSpan() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& declarationSpan() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> aliasSpan() const noexcept;
  ZC_NODISCARD const identity::SourceSpan& exportSpan() const noexcept;

private:
  ResolvedLocalExportSpecifier(ast::NodeId syntax, uint32_t schemaPreorderOrdinal,
                               identity::SemanticIdentifier&& sourceName,
                               identity::SemanticIdentifier&& exportedName,
                               identity::SourceSpan&& sourceNameSpan,
                               identity::SourceSpan&& declarationSpan,
                               zc::Maybe<identity::SourceSpan>&& aliasSpan,
                               identity::SourceSpan&& exportSpan) noexcept;

  ast::NodeId syntaxValue;
  uint32_t schemaPreorderOrdinalValue;
  identity::SemanticIdentifier sourceNameValue;
  identity::SemanticIdentifier exportedNameValue;
  identity::SourceSpan sourceNameSpanValue;
  identity::SourceSpan declarationSpanValue;
  zc::Maybe<identity::SourceSpan> aliasSpanValue;
  identity::SourceSpan exportSpanValue;
  friend class BindingInputVerifier;
};

/// \brief Untrusted inputs checked before the binder can observe semantic state.
struct BindingInputCandidate final {
  identity::SemanticContextBrand semanticContext;
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
  identity::ModuleId module;
  const identity::SemanticIdentityRegistrySet& registries;
  const VerifiedModuleGraphView& moduleGraph;
  const VerifiedParsedModule& parsedModule;
  const FrozenDefinitionInventoryView& definitions;
  zc::ArrayPtr<const DependencyExportSurface> dependencySurfaces;
  zc::Maybe<const VerifiedExportSurface&> preludeSurface;
};

/// \brief Move-only binder input published only after complete verification.
class VerifiedBindingInput final {
public:
  ~VerifiedBindingInput() noexcept(false);
  VerifiedBindingInput(VerifiedBindingInput&&) noexcept;
  VerifiedBindingInput& operator=(VerifiedBindingInput&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBindingInput);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD const identity::CompilationUnitIdentity& compilationUnitKey() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD const identity::CrateKey& crateKey() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& moduleKey() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ModuleKey&> moduleKey(
      identity::ModuleId module) const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::DefinitionKey&> definitionKey(
      identity::DefId definition) const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const VerifiedParsedModule& parsedModule() const noexcept;
  ZC_NODISCARD const FrozenDefinitionInventoryView& definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedExportSurfaceView> dependencySurfaces() const noexcept;
  ZC_NODISCARD zc::Maybe<const VerifiedExportSurfaceView&> preludeSurface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedImportEdge> resolvedImports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedModuleAlias> resolvedModuleAliases() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedLocalExportSpecifier> localExportSpecifiers()
      const noexcept;

private:
  struct Impl;
  explicit VerifiedBindingInput(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class BindingInputVerifier;
  friend class BindingVerifier;
};

using BindingInputVerificationResult =
    zc::OneOf<VerifiedBindingInput, BindingInputSourceRejected, ModuleGraphInvariantFact>;

/// \brief Recomputes graph and syntax inventories before publishing binder input.
class BindingInputVerifier final {
public:
  ZC_NODISCARD static BindingInputVerificationResult verify(const BindingInputCandidate& candidate);
};

}  // namespace zomlang::compiler::binder
