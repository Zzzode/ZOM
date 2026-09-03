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

#include "compiler/ast/tree.h"
#include "compiler/basic/compiler-opts.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/binder/graph/parsed-module.h"
#include "compiler/checker/body/marker-proof.h"
#include "compiler/checker/facts/checked-facts-repository.h"
#include "compiler/checker/facts/coherence-facts.h"
#include "compiler/checker/facts/cross-module-facts.h"
#include "compiler/checker/facts/dispatch-facts.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/driver/core/library.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/graph/crate-graph.h"
#include "compiler/driver/interface/borrow-evidence.h"
#include "compiler/driver/interface/module-interface.h"
#include "compiler/driver/package/build-script-plan.h"
#include "compiler/driver/package/build-script-runtime.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/driver/package/package-input-installer.h"
#include "compiler/driver/package/package-resolver.h"
#include "compiler/driver/package/source-snapshot.h"
#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "compiler/hir/hir-module.h"
#include "compiler/identity/brand.h"
#include "compiler/ir/ir-diagnostic-adapter.h"
#include "compiler/ir/target-registry.h"
#include "compiler/mir/built-mir.h"
#include "compiler/ownership/drop-elaborated-mir.h"
#include "compiler/ownership/facts/inputs.h"
#include "compiler/ownership/ownership-checked-mir.h"
#include "compiler/ownership/ownership-event-overlay.h"
#include "compiler/ownership/ownership-proof-validation.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"

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

/// \brief Test-only view of Built MIR staged before a borrow-source rejection.
///
/// Populated only when checkSources() rejects a module at the borrow-source
/// stage (for example a returned function-local borrow, ZOM4061). It is never
/// read by any production query accessor and never populated for a committed
/// module. It lets ownership fact-derivation tests reconstruct fact families
/// from verified MIR that was built and verified before the rejection but never
/// reached publication.
struct StagedOwnershipMirForTesting final {
  const mir::VerifiedBuiltMir& builtMir;
  const ownership::VerifiedOwnershipEventOverlay& eventOverlay;
  const borrow_evidence::BorrowEvidenceRepository& borrowEvidence;
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

  explicit VerifiedPackageSessionInput(package::InstalledPackageInputs&& inputs);
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
  /// \brief Returns immutable RFC 0007 ownership-checked MIR wrappers in dependency order.
  ///
  /// Each wrapper owns its Built MIR, verified event overlay, and verified
  /// ownership facts; access the payload through OwnershipCheckedMir::builtMir,
  /// eventOverlay, and facts.
  ZC_NODISCARD zc::ArrayPtr<const ownership::OwnershipCheckedMir> getOwnershipCheckedMirModules()
      const noexcept;
  /// \brief Returns immutable RFC 0013 validated ownership proofs in dependency order.
  ///
  /// Each validated proof owns the escape proofs, region memberships, capture
  /// facts, and the validation report for one module.
  ZC_NODISCARD zc::ArrayPtr<const ownership::ValidatedOwnershipProofs> getValidatedOwnershipProofs()
      const noexcept;
  /// \brief Returns immutable RFC 0007 verified executable MIR wrappers in dependency order.
  ///
  /// Each terminal wrapper owns the full successor chain (drop-elaborated,
  /// coroutine-elaborated) and retains the recorded drop-discharge inventory;
  /// the owned OwnershipCheckedMir payload is published separately through
  /// getOwnershipCheckedMirModules after VerifiedExecutableMir::takeCheckedMir.
  ZC_NODISCARD zc::ArrayPtr<const ownership::VerifiedExecutableMir>
  getVerifiedExecutableMirModules() const noexcept;
  /// \brief Returns the exact retained checker-to-MIR handoff for one ownership overlay.
  ZC_NODISCARD zc::Maybe<ownership::OwnershipEventOverlayInput> getOwnershipEventOverlayInput(
      identity::ModuleId module) const noexcept;
  /// \brief Test-only: Built MIR, overlay, and borrow evidence staged before a
  /// borrow-source rejection.
  ///
  /// Returns none unless the most recent checkSources() rejected exactly one
  /// module at the borrow-source stage (ZOM4061). The staged products are never
  /// committed to any production accessor and never read by production
  /// consumers; this exists so ownership fact-derivation tests can drive the
  /// fact builders against verified MIR for sources the borrow checker rejects.
  ZC_NODISCARD zc::Maybe<StagedOwnershipMirForTesting> firstStagedBorrowSourceRejectionForTesting()
      const noexcept;
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
  /// \brief Installs one already-verified package inputs bundle before parsing begins.
  ///
  /// Shares the exact move-and-install path of the `VerifiedPackageSessionInput`
  /// overload; both funnel to one internal implementation, so neither expands the
  /// crate graph a second time. Used by callers that hold an
  /// `InstalledPackageInputs` bundle directly (the CLI and the IDE workspace
  /// service).
  ZC_NODISCARD bool installVerifiedPackageInput(package::InstalledPackageInputs&& inputs);
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
