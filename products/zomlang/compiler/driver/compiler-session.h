// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/parsed-module.h"
#include "zomlang/compiler/checker/checked-facts-repository.h"
#include "zomlang/compiler/checker/coherence-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/dispatch-facts.h"
#include "zomlang/compiler/checker/marker-proof.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/driver/borrow-evidence.h"
#include "zomlang/compiler/driver/core/query.h"
#include "zomlang/compiler/driver/core/library.h"
#include "zomlang/compiler/driver/crate-graph.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/module-interface.h"
#include "zomlang/compiler/driver/package/build-script-plan.h"
#include "zomlang/compiler/driver/package/build-script-runtime.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/driver/package/package-resolver.h"
#include "zomlang/compiler/driver/package/source-snapshot.h"
#include "zomlang/compiler/hir/hir-module.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang {
namespace compiler {

namespace source {
class BufferId;
class SourceManager;
namespace core {
class VerifiedCoreDistribution;
}
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics

namespace checker {
class CheckerIdentityAuthority;
}

namespace basic {
class StringPool;
}  // namespace basic

namespace driver {

namespace module_graph_query {
class CheckerBoundModuleView;
}

/// \brief Caller-owned verified parser result and its source-manager handle.
class ParsedModuleRecord final {
public:
  ParsedModuleRecord(ParsedModuleRecord&&) noexcept = default;
  ParsedModuleRecord& operator=(ParsedModuleRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ParsedModuleRecord);

  ZC_NODISCARD const source::BufferId& buffer() const noexcept;
  ZC_NODISCARD const binder::VerifiedParsedModule& parsedModule() const noexcept;

private:
  ParsedModuleRecord(const source::BufferId& buffer,
                     binder::VerifiedParsedModule&& parsedModule) noexcept;
  source::BufferId bufferValue;
  binder::VerifiedParsedModule parsedModuleValue;
  friend class CompilerSession;
};

/// \brief Atomic package-session input validated before session state changes.
class VerifiedPackageSessionInput final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedPackageSessionInput> from(
      package::VerifiedPackageCompilationRequest&& request,
      ir::VerifiedTargetSelection&& hostTarget, ir::VerifiedTargetSelection&& target,
      package::ResolutionOutput&& graph,
      zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots);

  ~VerifiedPackageSessionInput() noexcept(false);
  VerifiedPackageSessionInput(VerifiedPackageSessionInput&&) noexcept;
  VerifiedPackageSessionInput& operator=(VerifiedPackageSessionInput&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedPackageSessionInput);

private:
  struct Impl;
  zc::Own<Impl> impl;

  VerifiedPackageSessionInput(package::VerifiedPackageCompilationRequest&& request,
                              ir::VerifiedTargetSelection&& hostTarget,
                              ir::VerifiedTargetSelection&& target,
                              package::ResolutionOutput&& graph,
                              package::VerifiedBuildScriptPlan&& buildScriptPlan,
                              zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots);
  friend class CompilerSession;
};

class CompilerSession {
public:
  CompilerSession(identity::SemanticContextFactory& contextFactory,
                  const basic::LangOptions& langOpts, const basic::CompilerOptions& compilerOpts);
  ~CompilerSession() noexcept(false);
  ZC_DISALLOW_COPY_AND_MOVE(CompilerSession);

  /// \brief Admits one finalized root exclusively from the installed verified snapshot.
  ZC_NODISCARD zc::Maybe<source::BufferId> addVerifiedPackageRoot(
      const package::FinalizedCompilationRoot& root);

  /// Get the diagnostic engine used by the compiler.
  /// \return A reference to the diagnostic engine
  const diagnostics::DiagnosticEngine& getDiagnosticEngine() const;
  diagnostics::DiagnosticEngine& getDiagnosticEngine();

  /// Parses all added source files into ASTs.
  /// \return True if parsing succeeded without fatal errors, false otherwise.
  bool parseSources();

  /// Binds all parsed ASTs to create symbols and perform semantic analysis.
  /// \return True if binding succeeded without fatal errors, false otherwise.
  bool bindSources();

  /// Type-checks all bound ASTs.
  /// \return True if checking succeeded without fatal errors, false otherwise.
  bool checkSources();

  /// \brief Reconstructs parser results from the final sealed snapshot in canonical order.
  ZC_NODISCARD zc::Maybe<zc::Vector<ParsedModuleRecord>> materializeParsedModules() const;
  /// \brief Returns the canonical parser records retained for AST-only emission.
  ZC_NODISCARD zc::ArrayPtr<const ParsedModuleRecord> retainedParsedModules() const noexcept;
  /// \brief Returns whether all discovered sources published immutable parser results.
  ZC_NODISCARD bool hasVerifiedParsedSyntax() const noexcept;

  using MaterializedModuleGraphLease =
      query::QueryCapabilityLease<const module_graph_query::MaterializedModuleGraph>;
  /// \brief Demands the final-sealed retained module graph for this session.
  ZC_NODISCARD zc::Maybe<MaterializedModuleGraphLease> materializeModuleGraph() const;
  /// \brief Assembles one source-backed core library from final interface and authority leases.
  ZC_NODISCARD zc::Maybe<core::VerifiedCoreLibrary> materializeCoreLibrary(
      const identity::CrateKey& coreCrate) const;
  /// \brief Materializes retained Checker identity authority from the sealed binding snapshot.
  ZC_NODISCARD zc::Maybe<checker::CheckerIdentityAuthority> materializeCheckerIdentityAuthority()
      const;
  /// \brief Returns the closed invariant rejection from the most recent Checker run.
  ZC_NODISCARD zc::ArrayPtr<const checker::signature::CheckerVerificationFailure>
  getCheckerInvariantFailures() const noexcept;
  /// \brief Returns verified local signature publications in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const checker::signature::VerifiedSignatureFacts>
  getVerifiedSignatureFacts() const noexcept;
  /// \brief Returns exact requester-filtered imported views in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const checker::cross_module::ImportedSignatureView>
  getImportedSignatureViews() const noexcept;
  /// \brief Returns immutable verified module interfaces in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const VerifiedModuleInterface> getVerifiedModuleInterfaces()
      const noexcept;
  /// \brief Returns the context-complete coherence authority after successful checking.
  ZC_NODISCARD zc::Maybe<const checker::coherence::FrozenCoherenceView&> getFrozenCoherenceView()
      const noexcept;
  /// \brief Resolves one demand-driven marker query through the verified session authority.
  ZC_NODISCARD zc::Maybe<checker::marker::MarkerProofResult> proveMarker(
      identity::ModuleId requester, identity::DefId marker, identity::SemanticTypeId subject);
  /// \brief Returns the append-only checked-evidence repository after successful checking.
  ZC_NODISCARD zc::Maybe<const checker::checked::CheckedFactsRepository&>
  getCheckedFactsRepository() const noexcept;
  /// \brief Returns one opaque evidence lease per dependency-ordered module.
  ZC_NODISCARD zc::ArrayPtr<const checker::checked::CheckedEvidenceLease> getCheckedEvidenceLeases()
      const noexcept;
  /// \brief Returns verified dispatch publications in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFacts>
  getVerifiedDispatchFacts() const noexcept;
  /// \brief Returns the session-owned append-only RFC 0013 borrow-evidence authority.
  ZC_NODISCARD zc::Maybe<const borrow_evidence::BorrowEvidenceRepository&>
  getBorrowEvidenceRepository() const noexcept;
  /// \brief Returns immutable verified HIR modules in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const hir::VerifiedHirModule> getVerifiedHirModules() const noexcept;
  /// \brief Returns immutable revision-checked Built MIR modules in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const mir::VerifiedBuiltMir> getVerifiedBuiltMirModules()
      const noexcept;
  /// \brief Returns immutable revision-checked RFC 0007 ownership event overlays in dependency
  /// order.
  ZC_NODISCARD zc::ArrayPtr<const ownership::VerifiedOwnershipEventOverlay>
  getVerifiedOwnershipEventOverlays() const noexcept;
  /// \brief Returns the exact retained checker-to-MIR handoff for one ownership overlay.
  ZC_NODISCARD zc::Maybe<ownership::OwnershipEventOverlayInput> getOwnershipEventOverlayInput(
      identity::ModuleId module) const noexcept;
  /// \brief Returns atomic current-subset ownership-analysis inputs in dependency order.
  ZC_NODISCARD zc::ArrayPtr<const ownership::facts::VerifiedOwnershipInputs>
  getVerifiedOwnershipInputs() const noexcept;
  /// \brief Returns complete grouped IR failures retained after rejected lowering.
  ZC_NODISCARD zc::ArrayPtr<const ir::IrDiagnosticGroup> getIrFailureGroups() const noexcept;
  /// \brief Returns complete identity failures retained from rejected IR operations.
  ZC_NODISCARD zc::ArrayPtr<const identity::IdentityInvariant> getIrIdentityInvariantFailures()
      const noexcept;

  /// Get the string pool used by the compiler.
  /// \return A reference to the string pool
  basic::StringPool& getStringPool();
  const basic::StringPool& getStringPool() const;

  /// Get the compiler options used by the session.
  /// \return A reference to the compiler options
  const basic::CompilerOptions& getCompilerOptions() const;

  /// Get the source manager used by the session.
  /// \return A reference to the source manager
  const source::SourceManager& getSourceManager() const;

  /// \brief Returns the process-unique brand owned by this compilation session.
  identity::SemanticContextBrand getSemanticContextBrand() const noexcept;

  /// \brief Returns the sole RFC 0005 semantic type store owned by this session.
  zc::Maybe<const type::SemanticTypeStore&> getSemanticTypeStore() const noexcept;

  /// \brief Returns the session-owned issuer for Checker fact and recovery identities.
  zc::Maybe<const identity::RegistryBrandIssuer&> getFactStoreBrandIssuer() const noexcept;

  /// \brief Installs one fully verified package input before parsing begins.
  ZC_NODISCARD bool installVerifiedPackageInput(VerifiedPackageSessionInput&& input);
  /// \brief Atomically installs the verified source-backed core distribution for this context.
  ZC_NODISCARD bool installVerifiedCoreDistribution(
      const source::core::VerifiedCoreDistribution& distribution);

  /// \brief Returns session-owned storage for package resolution inputs and outputs.
  /// \return A resource that outlives the installed package graph.
  ZC_NODISCARD zc::MemoryResource& getPackageResolutionMemoryResource() noexcept;

  /// \brief Returns the installed package request, if this is a package compilation.
  ZC_NODISCARD zc::Maybe<const package::VerifiedPackageCompilationRequest&>
  getPackageCompilationRequest() const noexcept;

  /// \brief Returns post-build roots whose complete CrateKey values may enter identity freeze.
  ZC_NODISCARD zc::ArrayPtr<const package::FinalizedCompilationRoot> getFinalizedCompilationRoots()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const VerifiedCrateGraph&> getVerifiedCrateGraph() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedPreparatoryCrateGraph> getVerifiedPreparatoryCrateGraphs()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const ir::VerifiedTargetSelection&> getVerifiedHostTarget() const noexcept;
  ZC_NODISCARD zc::Maybe<const ir::VerifiedTargetSelection&> getVerifiedTarget() const noexcept;

  ZC_NODISCARD zc::Maybe<const package::ResolutionOutput&> getResolvedPackageGraph() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot>
  getResolvedPackageSnapshots() const noexcept;

  /// \brief Explicitly removes private source snapshots before process quick-exit.
  ZC_NODISCARD zc::Maybe<package::MaterializationIssue> finishResolvedPackageSnapshots();

  /// \brief Derives and executes the authoritative build plan once, then freezes its result map.
  ZC_NODISCARD zc::Maybe<package::BuildScriptIssue> executeBuildScripts(
      package::BuildScriptPlanExecutor& executor);
  /// \brief Returns the retained verified build plan, if execution completed.
  ZC_NODISCARD zc::Maybe<const package::VerifiedBuildScriptPlan&> getBuildScriptPlan()
      const noexcept;
  /// \brief Returns the final verified build-script results, if installed.
  ZC_NODISCARD zc::Maybe<const package::VerifiedBuildScriptResultSet&> getBuildScriptResults()
      const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
