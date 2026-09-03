#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "compiler"
UTILS_ROOT = ROOT / "utils"
SESSION_HEADER = Path("compiler/driver/session/compiler-session.h")
SESSION_SOURCE = Path("compiler/driver/session/compiler-session.cc")
DRIVER_ROOT = Path("compiler/driver")
CORE_ROOT = DRIVER_ROOT / "core"
CRATE_GRAPH_HEADER = Path("compiler/driver/graph/crate-graph.h")
CRATE_GRAPH_SOURCE = Path("compiler/driver/graph/crate-graph.cc")
MODULE_DISCOVERY_HEADER = Path("compiler/driver/graph/module-discovery.h")
MODULE_DISCOVERY_SOURCE = Path("compiler/driver/graph/module-discovery.cc")
COHERENCE_BUILDER_HEADER = Path("compiler/driver/interface/coherence-builder.h")
COHERENCE_BUILDER_SOURCE = Path("compiler/driver/interface/coherence-builder.cc")
IMPORTED_VIEW_PROJECTOR_HEADER = Path(
    "compiler/driver/interface/imported-signature-view-projector.h"
)
IMPORTED_VIEW_PROJECTOR_SOURCE = Path(
    "compiler/driver/interface/imported-signature-view-projector.cc"
)
MODULE_INTERFACE_HEADER = Path("compiler/driver/interface/module-interface.h")
MODULE_INTERFACE_SOURCE = Path("compiler/driver/interface/module-interface.cc")
MODULE_INTERFACE_DIAGNOSTIC_HEADER = Path(
    "compiler/driver/interface/module-interface-diagnostic-adapter.h"
)
MODULE_INTERFACE_DIAGNOSTIC_SOURCE = Path(
    "compiler/driver/interface/module-interface-diagnostic-adapter.cc"
)
BORROW_EVIDENCE_HEADER = Path("compiler/driver/interface/borrow-evidence.h")
BORROW_EVIDENCE_SOURCE = Path("compiler/driver/interface/borrow-evidence.cc")
INTERFACE_SOURCE_HEADER = Path("compiler/driver/interface/interface-source.h")
ACTIVE_DEFINITION_AUTHORITY_QUERY_HEADER = Path(
    "compiler/driver/query/binding/active-definition-authority-query.h"
)
ACTIVE_DEFINITION_AUTHORITY_QUERY_SOURCE = Path(
    "compiler/driver/query/binding/active-definition-authority-query.cc"
)
ACTIVE_DEFINITION_AUTHORITY_SESSION_HEADER = Path(
    "compiler/driver/query/binding/active-definition-authority-session.h"
)
ACTIVE_DEFINITION_AUTHORITY_SESSION_SOURCE = Path(
    "compiler/driver/query/binding/active-definition-authority-session.cc"
)
ACTIVE_IDENTITY_MEMBERSHIP_QUERY_HEADER = Path(
    "compiler/driver/query/binding/active-identity-membership-query.h"
)
ACTIVE_IDENTITY_MEMBERSHIP_QUERY_SOURCE = Path(
    "compiler/driver/query/binding/active-identity-membership-query.cc"
)
CONTEXTUAL_BINDING_KEY_HEADER = Path(
    "compiler/driver/query/binding/contextual-binding-key.h"
)
CONTEXTUAL_BINDING_KEY_SOURCE = Path(
    "compiler/driver/query/binding/contextual-binding-key.cc"
)
INCREMENTAL_BINDING_QUERY_ADAPTER_HEADER = Path(
    "compiler/driver/query/binding/incremental-binding-query-adapter.h"
)
INCREMENTAL_BINDING_QUERY_ADAPTER_SOURCE = Path(
    "compiler/driver/query/binding/incremental-binding-query-adapter.cc"
)
INCREMENTAL_PACKAGE_GRAPH_INPUT_HEADER = Path(
    "compiler/driver/query/binding/incremental-package-graph-query-input.h"
)
INCREMENTAL_PACKAGE_GRAPH_INPUT_SOURCE = Path(
    "compiler/driver/query/binding/incremental-package-graph-query-input.cc"
)
INCREMENTAL_MODULE_RESOLUTION_QUERY_HEADER = Path(
    "compiler/driver/query/module-graph/incremental-module-resolution-query.h"
)
INCREMENTAL_MODULE_RESOLUTION_QUERY_SOURCE = Path(
    "compiler/driver/query/module-graph/incremental-module-resolution-query.cc"
)
NAMED_IDENTITY_INVENTORY_QUERY_HEADER = Path(
    "compiler/driver/query/binding/named-identity-inventory-query.h"
)
NAMED_IDENTITY_INVENTORY_QUERY_SOURCE = Path(
    "compiler/driver/query/binding/named-identity-inventory-query.cc"
)
NAMED_ITEM_QUERY_HEADER = Path("compiler/driver/query/binding/named-item-query.h")
NAMED_ITEM_QUERY_SOURCE = Path("compiler/driver/query/binding/named-item-query.cc")
OWNER_BODY_QUERY_HEADER = Path("compiler/driver/query/binding/owner-body-query.h")
OWNER_BODY_QUERY_SOURCE = Path("compiler/driver/query/binding/owner-body-query.cc")
CORE_QUERY_HEADER = Path(
    "compiler/driver/core/query.h"
)
CORE_QUERY_SOURCE = Path(
    "compiler/driver/core/query.cc"
)
CORE_VERIFIER_HEADER = Path(
    "compiler/driver/core/verifier.h"
)
CORE_VERIFIER_SOURCE = Path(
    "compiler/driver/core/verifier.cc"
)
CORE_FILES = frozenset(
    {
        CORE_QUERY_HEADER,
        CORE_QUERY_SOURCE,
        CORE_VERIFIER_HEADER,
        CORE_VERIFIER_SOURCE,
        Path("compiler/driver/core/library.h"),
        Path("compiler/driver/core/library.cc"),
        Path("compiler/driver/core/marker-authority.h"),
        Path("compiler/driver/core/marker-authority.cc"),
        Path("compiler/driver/core/revision.h"),
        Path("compiler/driver/core/role-seed-failure.h"),
        Path("compiler/driver/core/role-seed-failure.cc"),
        Path("compiler/driver/core/signature.h"),
        Path("compiler/driver/core/signature.cc"),
    }
)
MODULE_GRAPH_QUERY_INPUT_HEADER = Path(
    "compiler/driver/query/module-graph/module-graph-query-input.h"
)
MODULE_GRAPH_QUERY_INPUT_SOURCE = Path(
    "compiler/driver/query/module-graph/module-graph-query-input.cc"
)
MODULE_DEPENDENCY_PROVENANCE_QUERY_HEADER = Path(
    "compiler/driver/query/module-graph/module-dependency-provenance-query.h"
)
MODULE_DEPENDENCY_PROVENANCE_QUERY_SOURCE = Path(
    "compiler/driver/query/module-graph/module-dependency-provenance-query.cc"
)
MODULE_GRAPH_QUERY_HEADER = Path("compiler/driver/query/module-graph/module-graph-query.h")
MODULE_GRAPH_QUERY_SOURCE = Path("compiler/driver/query/module-graph/module-graph-query.cc")
MATERIALIZED_MODULE_GRAPH_QUERY_HEADER = Path(
    "compiler/driver/query/module-graph/materialized-module-graph-query.h"
)
MATERIALIZED_MODULE_GRAPH_QUERY_SOURCE = Path(
    "compiler/driver/query/module-graph/materialized-module-graph-query.cc"
)
QUERY_DATABASE_HEADER = Path("compiler/query/query-database.h")
QUERY_DATABASE_SOURCE = Path("compiler/query/query-database.cc")
DRIVER_CMAKE = Path("compiler/driver/CMakeLists.txt")
CLI_SOURCE = Path("utils/zomc/zomc.cc")
THREAD_POOL_HEADER = Path("compiler/basic/thread-pool.h")
THREAD_POOL_SOURCE = Path("compiler/basic/thread-pool.cc")
BRAND_SOURCE = Path("compiler/identity/brand.cc")
BRAND_HEADER = Path("compiler/identity/brand.h")
REGISTRY_SET_SOURCE = Path(
    "compiler/identity/semantic-identity-registry-set.cc"
)
BINDING_INPUT_SOURCE = Path("compiler/binder/binding-input.cc")
# The session-agnostic package-input verification authority. Build-plan derivation
# (VerifiedPreparatoryCrateGraph::buildPlan) moved here from CompilerSession so the
# CLI and the IDE workspace service share one verify-and-build entry point; the
# required-presence check below keeps that ownership anchored to this file.
VERIFIED_PACKAGE_INPUTS_SOURCE = Path("compiler/driver/package/verified-package-inputs.cc")
VERIFIED_VENDOR_ROOT = Path("compiler/driver/package/vendor")

EXPECTED_DRIVER_FILES = {
    DRIVER_CMAKE,
    SESSION_HEADER,
    SESSION_SOURCE,
    CRATE_GRAPH_HEADER,
    CRATE_GRAPH_SOURCE,
    MODULE_DISCOVERY_HEADER,
    MODULE_DISCOVERY_SOURCE,
    COHERENCE_BUILDER_HEADER,
    COHERENCE_BUILDER_SOURCE,
    IMPORTED_VIEW_PROJECTOR_HEADER,
    IMPORTED_VIEW_PROJECTOR_SOURCE,
    MODULE_INTERFACE_HEADER,
    MODULE_INTERFACE_SOURCE,
    MODULE_INTERFACE_DIAGNOSTIC_HEADER,
    MODULE_INTERFACE_DIAGNOSTIC_SOURCE,
    BORROW_EVIDENCE_HEADER,
    BORROW_EVIDENCE_SOURCE,
    INTERFACE_SOURCE_HEADER,
    ACTIVE_DEFINITION_AUTHORITY_QUERY_HEADER,
    ACTIVE_DEFINITION_AUTHORITY_QUERY_SOURCE,
    ACTIVE_DEFINITION_AUTHORITY_SESSION_HEADER,
    ACTIVE_DEFINITION_AUTHORITY_SESSION_SOURCE,
    ACTIVE_IDENTITY_MEMBERSHIP_QUERY_HEADER,
    ACTIVE_IDENTITY_MEMBERSHIP_QUERY_SOURCE,
    CONTEXTUAL_BINDING_KEY_HEADER,
    CONTEXTUAL_BINDING_KEY_SOURCE,
    INCREMENTAL_BINDING_QUERY_ADAPTER_HEADER,
    INCREMENTAL_BINDING_QUERY_ADAPTER_SOURCE,
    INCREMENTAL_PACKAGE_GRAPH_INPUT_HEADER,
    INCREMENTAL_PACKAGE_GRAPH_INPUT_SOURCE,
    INCREMENTAL_MODULE_RESOLUTION_QUERY_HEADER,
    INCREMENTAL_MODULE_RESOLUTION_QUERY_SOURCE,
    NAMED_IDENTITY_INVENTORY_QUERY_HEADER,
    NAMED_IDENTITY_INVENTORY_QUERY_SOURCE,
    NAMED_ITEM_QUERY_HEADER,
    NAMED_ITEM_QUERY_SOURCE,
    OWNER_BODY_QUERY_HEADER,
    OWNER_BODY_QUERY_SOURCE,
    *CORE_FILES,
    MODULE_GRAPH_QUERY_INPUT_HEADER,
    MODULE_GRAPH_QUERY_INPUT_SOURCE,
    MODULE_DEPENDENCY_PROVENANCE_QUERY_HEADER,
    MODULE_DEPENDENCY_PROVENANCE_QUERY_SOURCE,
    MODULE_GRAPH_QUERY_HEADER,
    MODULE_GRAPH_QUERY_SOURCE,
    MATERIALIZED_MODULE_GRAPH_QUERY_HEADER,
    MATERIALIZED_MODULE_GRAPH_QUERY_SOURCE,
}

DRIVER_BUILD_MARKER = (
    "set(DRIVER_SRC\n"
    "  session/compiler-session.cc\n"
    "  graph/crate-graph.cc\n"
    "  graph/module-discovery.cc\n"
    "  interface/borrow-evidence.cc\n"
    "  interface/coherence-builder.cc\n"
    "  interface/imported-signature-view-projector.cc\n"
    "  interface/module-interface.cc\n"
    "  interface/module-interface-diagnostic-adapter.cc\n"
    "  query/binding/active-definition-authority-query.cc\n"
    "  query/binding/active-definition-authority-session.cc\n"
    "  query/binding/active-identity-membership-query.cc\n"
    "  query/binding/contextual-binding-key.cc\n"
    "  query/binding/incremental-binding-query-adapter.cc\n"
    "  query/binding/incremental-package-graph-query-input.cc\n"
    "  query/binding/named-identity-inventory-query.cc\n"
    "  query/binding/named-item-query.cc\n"
    "  query/binding/owner-body-query.cc\n"
    "  query/module-graph/incremental-module-resolution-query.cc\n"
    "  query/module-graph/materialized-module-graph-query.cc\n"
    "  query/module-graph/module-dependency-provenance-query.cc\n"
    "  query/module-graph/module-graph-query.cc\n"
    "  query/module-graph/module-graph-query-input.cc\n"
    "  core/role-seed-failure.cc\n"
    "  core/library.cc\n"
    "  core/marker-authority.cc\n"
    "  core/signature.cc\n"
    "  core/query.cc\n"
    "  core/verifier.cc)"
)

SESSION_HEADER_MARKERS = (
    "class CompilerSession",
    "CompilerSession(identity::SemanticContextFactory& contextFactory,",
    "identity::SemanticContextBrand getSemanticContextBrand() const noexcept;",
    "addVerifiedPackageRoot(",
    "zc::Maybe<zc::Vector<ParsedModuleRecord>> materializeParsedModules() const;",
    "zc::Maybe<const VerifiedCrateGraph&> getVerifiedCrateGraph() const noexcept;",
    "getVerifiedPreparatoryCrateGraphs()\n      const noexcept;",
    "zc::Maybe<MaterializedModuleGraphLease> materializeModuleGraph() const;",
    "zc::Maybe<checker::CheckerIdentityAuthority> materializeCheckerIdentityAuthority()",
    "getVerifiedSignatureFacts() const noexcept;",
    "getImportedSignatureViews() const noexcept;",
    "getVerifiedModuleInterfaces()\n      const noexcept;",
    "getFrozenCoherenceView()\n      const noexcept;",
    "getCheckedFactsRepository() const noexcept;",
    "getCheckedEvidenceLeases()\n      const noexcept;",
    "getVerifiedDispatchFacts() const noexcept;",
    "getBorrowEvidenceRepository() const noexcept;",
    "getVerifiedHirModules() const noexcept;",
    "getIrFailureGroups() const noexcept;",
    "getIrIdentityInvariantFailures() const noexcept;",
)

SESSION_SOURCE_MARKERS = (
    "identity::SemanticContextBrand contextBrand;",
    "public module_graph_query::ModuleGraphIdentityMaterializationResources",
    "identity::IdentityInternerSet& identityInterners() const override",
    "return ZC_ASSERT_NONNULL(identityInternerSet);",
    "identity::IdentityInternResult<identity::DefId> internDefinition(",
    "identity::IdentityInternResult<identity::ImplId> internImplementation(",
    "identity::IdentityInternResult<identity::GenericParameterId> internGenericParameter(",
    "identity::IdentityInternResult<identity::CallableParameterId> internCallableParameter(",
    "zc::Maybe<identity::DefinitionIdentityEntry> definition(identity::DefId handle) const override",
    "zc::Maybe<identity::ImplementationIdentityEntry> implementation(",
    "zc::Maybe<identity::GenericParameterIdentityEntry> genericParameter(",
    "zc::Maybe<identity::CallableParameterIdentityEntry> callableParameter(",
    "mutable zc::Maybe<identity::IdentityInternerSet> identityInternerSet;",
    "basic::ThreadPool queryScheduler;",
    "query::QueryDatabase queryDatabase;",
    "queryDatabase(queryScheduler, query::productionQueryDescriptorInventory(), "
    "semanticContextCapabilityArena.addRef())",
    "zc::Own<basic::StringPool> stringPool;",
    "zc::Own<source::SourceManager> sourceManager;",
    "zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;",
    "zc::Vector<ParsedModuleRecord> parsedModules;",
    "struct ModuleKeyBinding final",
    "zc::Vector<ModuleKeyBinding> moduleKeys;",
    "snapshot.snapshot().readVerifiedFile(sourcePath)",
    "registerVerifiedSource(",
    "parseSnapshot.getCapability<parser::ParseSourceQuery>",
    "binder::ParsedModuleVerifier::verifyQueryResult(",
    "extractStructuralModuleDependencyRequests(parsed.lease().capability().tree())",
    "incremental_binding_query::StableIdentityAdmissionQuery",
    "stagedCompilationRoots = zc::none;",
    "const auto& roots = snapshot.contextRoots();",
    "const auto& roots = finalSnapshot.contextRoots();",
    "incremental_binding_query::ContextualIdentityAuthorityInputTransaction::prepare(",
    "binder::DefinitionInventory::collect(tree);",
    "diagnostics::DiagID::IdentityBrandExhausted",
    "diagnostics::DiagID::IdentityDuplicateSingletonStore",
    "while (true) {",
    "zc::Maybe<VerifiedCrateGraph> crateGraph;",
    "zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryCrateGraphs;",
    "const auto& fingerprint = checkerAuthority.fingerprint();",
    "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
    "authorityStagingSnapshot.get<graph_query::ModuleGraph>(",
    "authorityStagingSnapshot.get<graph_query::ModuleGraphScc>(",
    "snapshot.getCapability<module_graph_query::MaterializeModuleGraph>(roots)",
    "finalSealedSnapshot = zc::mv(admitted).takeSnapshot();",
    "VerifiedPreparatoryCrateGraph::build(request, node, resolution, plan, completed)",
    "executor.execute(node, graph.get<VerifiedPreparatoryCrateGraph>(), completed)",
    "zc::Maybe<identity::RegistryBrandIssuer> factStoreBrands;",
    "zc::Own<checker::checked::CheckedFactsRepository> checkedFactsRepository;",
    "zc::Own<borrow_evidence::BorrowEvidenceRepository> borrowEvidenceRepository;",
    "zc::Vector<checker::signature::VerifiedSignatureFacts> signatureFacts;",
    "zc::Vector<checker::cross_module::ImportedSignatureView> importedSignatureViews;",
    "zc::Vector<VerifiedModuleInterface> moduleInterfaces;",
    "zc::Maybe<checker::coherence::FrozenCoherenceView> coherenceView;",
    "zc::Vector<checker::checked::CheckedEvidenceLease> checkedEvidence;",
    "zc::Vector<checker::dispatch::VerifiedDispatchFacts> dispatchFacts;",
    "zc::Vector<hir::VerifiedHirModule> hirModules;",
    "zc::Vector<ir::IrDiagnosticGroup> irFailureGroups;",
    "zc::Vector<identity::IdentityInvariant> irIdentityInvariantFailures;",
    "checker::dispatch::DispatchSiteInventoryBuilder::build(boundView.boundModule(), inventory)",
    "checker::dispatch::DispatchFactsBuilder::build(",
    "checker::dispatch::DispatchFactsVerifier::verify(",
    "borrow_evidence::BorrowEvidenceRepository::create(",
    "hir::CheckedModuleBuilder::build(",
    "hir::HirBuilder::build(",
    "hir::HirVerifier::verify(",
    "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
    "impl->borrowEvidenceRepository = zc::mv(stagedBorrowEvidenceRepository);",
    "impl->hirModules = zc::mv(stagedHirModules);",
    "bool verifiedCheckedSources = false;",
)

ORDINARY_MODULE_PARTITION_MARKERS = (
    "zc::Vector<size_t> ordinaryBoundModuleIndices;",
    "ZC_ASSERT_NONNULL(crate).key().unit().kind() == identity::CompilationUnitKind::UserPackage",
    "ordinaryBoundModuleIndices.add(index);",
    "for (size_t ordinaryIndex = 0; ordinaryIndex < ordinaryBoundModuleIndices.size();",
    "const auto boundIndex = ordinaryBoundModuleIndices[ordinaryIndex];",
    "checkerBound, ordinarySignatureFacts[ordinaryIndex],",
    "ordinaryModuleInterfaces[ordinaryIndex], ordinaryImportedSignatureViews[ordinaryIndex],",
    "checkedModuleInterfaceSources.asPtr(), ordinaryCheckedEvidence[ordinaryIndex],",
    "*stagedCheckedFactsRepository, ordinaryDispatchFacts[ordinaryIndex],",
    "const mir::BuiltMirInput mirInput{stagedHirModules[ordinaryIndex], bodyInput};",
    "mir::BuiltMirBuilder::build(mirInput)",
    "stagedHirModules.size() != ordinaryBoundModuleIndices.size()",
    "stagedOwnershipCheckedMir.size() != ordinaryBoundModuleIndices.size()",
    "ownership::OwnershipFinalizer::finalizeOwnership(",
    "impl->ownershipCheckedMirModules = zc::mv(stagedOwnershipCheckedMir);",
    "for (const auto index : ordinaryBoundModuleIndices)",
    "const auto factIndex = checkerFactIndexByModule[index];",
    "ordinarySignatureFacts.add(zc::mv(stagedSignatureFacts[factIndex]));",
    "ordinaryImportedSignatureViews.add(zc::mv(stagedImportedSignatureViews[factIndex]));",
    "ordinaryBodyRequirements.add(zc::mv(stagedBodyRequirements[factIndex]));",
    "ordinaryModuleInterfaces.add(zc::mv(stagedModuleInterfaces[factIndex]));",
    "ordinaryCheckedEvidence.add(zc::mv(stagedCheckedEvidence[factIndex]));",
    "ordinaryDispatchFacts.add(zc::mv(stagedDispatchFacts[factIndex]));",
    "impl->signatureFacts = zc::mv(ordinarySignatureFacts);",
    "impl->importedSignatureViews = zc::mv(ordinaryImportedSignatureViews);",
    "impl->moduleInterfaces = zc::mv(ordinaryModuleInterfaces);",
    "impl->checkedEvidence = zc::mv(ordinaryCheckedEvidence);",
    "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
)


def contains_format_independent_marker(text: str, marker: str) -> bool:
    """Match C++ contract markers without depending on formatter line wrapping."""
    normalized_text = re.sub(r"\s+", "", text)
    normalized_marker = re.sub(r"\s+", "", marker)
    return normalized_marker in normalized_text

CLI_MARKERS = (
    "identity::SemanticContextFactory contextFactory;",
    "zc::Own<driver::CompilerSession> session;",
    "zc::SpaceFor<driver::CompilerSession> sessionSpace;",
    "session = sessionSpace.construct(contextFactory, langOpts, compilerOpts);",
)


def relative(path: Path) -> Path:
    return path.relative_to(ROOT)


def production_sources() -> dict[Path, str]:
    result: dict[Path, str] = {}
    for base in (COMPILER_ROOT, UTILS_ROOT):
        for suffix in ("*.h", "*.cc"):
            for path in base.rglob(suffix):
                relative_path = relative(path)
                if VERIFIED_VENDOR_ROOT in relative_path.parents:
                    continue
                result[relative_path] = path.read_text(encoding="utf-8")
    result[DRIVER_CMAKE] = (ROOT / DRIVER_CMAKE).read_text(encoding="utf-8")
    return result


def strip_cpp_comments_and_literals(text: str) -> str:
    output: list[str] = []
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if current == "/" and following == "/":
                output.extend("  ")
                index += 2
                state = "line-comment"
                continue
            if current == "/" and following == "*":
                output.extend("  ")
                index += 2
                state = "block-comment"
                continue
            if current in {'"', "'"}:
                output.append(" ")
                quote = current
                index += 1
                state = "literal"
                continue
            output.append(current)
            index += 1
            continue

        if state == "line-comment":
            if current == "\n":
                output.append("\n")
                state = "code"
            else:
                output.append(" ")
            index += 1
            continue

        if state == "block-comment":
            if current == "*" and following == "/":
                output.extend("  ")
                index += 2
                state = "code"
            else:
                output.append("\n" if current == "\n" else " ")
                index += 1
            continue

        if current == "\\" and following:
            output.extend("  ")
            index += 2
        elif current == quote:
            output.append(" ")
            index += 1
            state = "code"
        else:
            output.append("\n" if current == "\n" else " ")
            index += 1

    return "".join(output)


def check_driver_surface(
    files: dict[Path, str], stripped_sources: dict[Path, str], errors: list[str],
    scan_paths: set[Path] | None
) -> None:
    driver_files = {
        path
        for path in files
        if DRIVER_ROOT in path.parents and "package" not in path.relative_to(DRIVER_ROOT).parts
    }
    unexpected = sorted(driver_files - EXPECTED_DRIVER_FILES)
    missing = sorted(EXPECTED_DRIVER_FILES - driver_files)
    for path in unexpected:
        errors.append(f"{path}: unexpected compiler driver surface")
    for path in missing:
        errors.append(f"{path}: required CompilerSession surface is missing")

    for path, text in sorted(stripped_sources.items()):
        if scan_paths is not None and path not in scan_paths:
            continue
        if re.search(r"\bCompilerDriver\b", text):
            errors.append(f"{path}: forbidden CompilerDriver identifier remains")
        if "compiler/driver/driver.h" in files[path]:
            errors.append(f"{path}: forbidden driver.h include remains")
        if (DRIVER_ROOT in path.parents and "package" not in path.relative_to(DRIVER_ROOT).parts) and re.search(
            r"\b(?:ZC_IREQUIRE|ZC_FAIL(?:_ASSERT)?)\b", text
        ):
            errors.append(f"{path}: raw session invariant assertion is forbidden")

    header_text = strip_cpp_comments_and_literals(files.get(SESSION_HEADER, ""))
    class_surface = re.sub(r"\bfriend\s+class\s+CompilerSession\s*;", "", header_text)
    if len(re.findall(r"\bclass\s+CompilerSession\b", class_surface)) != 1:
        errors.append(f"{SESSION_HEADER}: must declare exactly one CompilerSession class")
    if re.search(r"\busing\s+\w+\s*=\s*(?:driver::)?CompilerSession\b", class_surface):
        errors.append(f"{SESSION_HEADER}: CompilerSession compatibility alias is forbidden")
    if re.search(r"\btypedef\b[^;]*\bCompilerSession\b", class_surface):
        errors.append(f"{SESSION_HEADER}: CompilerSession typedef wrapper is forbidden")
    for match in re.finditer(
        r"\b(?:class|struct)\s+([A-Za-z_]\w*)[^;{]*\{(.*?)\};", class_surface, re.S
    ):
        if match.group(1) != "CompilerSession" and re.search(
            r"\bCompilerSession\b", match.group(2)
        ):
            errors.append(f"{SESSION_HEADER}: CompilerSession wrapper class is forbidden")


def check_session_ownership(
    files: dict[Path, str], stripped_sources: dict[Path, str], errors: list[str],
    scan_paths: set[Path] | None
) -> None:
    header = files.get(SESSION_HEADER, "")
    source = files.get(SESSION_SOURCE, "")
    for marker in SESSION_HEADER_MARKERS:
        if not contains_format_independent_marker(header, marker):
            errors.append(f"{SESSION_HEADER}: missing session contract marker: {marker}")
    for marker in SESSION_SOURCE_MARKERS:
        if not contains_format_independent_marker(source, marker):
            errors.append(f"{SESSION_SOURCE}: missing session ownership marker: {marker}")

    forbidden_source_authority = (
        "getFileSystemSourceBufferID(",
        "HashMap<source::BufferId, ast::Tree>",
        "basic::performParse(",
    )
    for forbidden in forbidden_source_authority:
        if forbidden in source:
            errors.append(f"{SESSION_SOURCE}: forbidden raw source authority remains: {forbidden}")
    for forbidden in (
        "NamedItemQueryBinding",
        "namedItemQueryBindings",
        "demandNamedItemQueries",
    ):
        if forbidden in source:
            errors.append(
                f"{SESSION_SOURCE}: unauthorized complete-context demand remains: {forbidden}"
            )
    for forbidden in ("addSourceFile(", "addPackageSourceFile(", "getASTs("):
        if forbidden in header:
            errors.append(f"{SESSION_HEADER}: forbidden raw source API remains: {forbidden}")
    if "getParsedModules(" in header:
        errors.append(f"{SESSION_HEADER}: obsolete retained parser-result API remains")
    for forbidden in (
        "FrozenInventoryBinding",
        "FrozenInventoryInputBinding",
        "frozenInventories",
        "frozenInventoryInputs",
        "ModuleBodyQueryBinding",
        "moduleBodyQueryBindings",
    ):
        if forbidden in source:
            errors.append(f"{SESSION_SOURCE}: obsolete session ledger remains: {forbidden}")

    if source.count("while (true) {") != 1:
        errors.append(f"{SESSION_SOURCE}: must own exactly one discovery fixed-point scheduler")
    if (
        source.count(
            "extractStructuralModuleDependencyRequests(parsed.lease().capability().tree())"
        )
        != 1
    ):
        errors.append(f"{SESSION_SOURCE}: discovery scheduler must have exactly one request site")
    if "binder::VerifiedModuleGraphBuilder::build(" in source:
        errors.append(f"{SESSION_SOURCE}: session-owned module graph publication is forbidden")
    if source.count("getCapability<module_graph_query::MaterializeModuleGraph>") != 3:
        errors.append(f"{SESSION_SOURCE}: sealed materialized graph demand is missing")
    if source.count("getCapability<core_library_query::MaterializeCoreAuthority>") != 2:
        errors.append(f"{SESSION_SOURCE}: final core authority demand is missing")
    if source.count("FinalizeCoreModuleInterface") != 2:
        errors.append(f"{SESSION_SOURCE}: source-backed final core-interface demand is missing")
    for forbidden in (
        "MaterializeCoreRoleSeed",
        "MaterializeCoreBootstrapModuleInterface",
        "CoreExportSurface",
    ):
        if forbidden in source:
            errors.append(f"{SESSION_SOURCE}: bootstrap-only core query escapes finalization: {forbidden}")
    prelude_surface_preflight = (
        "auto preludeSurface = finalSnapshot.get<core_library_query::CorePreludeSurface>("
    )
    if prelude_surface_preflight not in source:
        errors.append(f"{SESSION_SOURCE}: source-backed core prelude-surface preflight is missing")
    if "resolver.resolve(zc::mv(request))" in source:
        errors.append(f"{SESSION_SOURCE}: batch module resolution authority is forbidden")
    if "identity::ModuleId identity;" in source:
        errors.append(
            f"{SESSION_SOURCE}: session-owned persistent module handle cache is forbidden"
        )

    for path, text in stripped_sources.items():
        if path in {SESSION_SOURCE, BINDING_INPUT_SOURCE} or (
            scan_paths is not None and path not in scan_paths
        ):
            continue
        if "VerifiedModuleGraphBuilder::build(" in text:
            errors.append(f"{path}: global module graph publication bypasses CompilerSession")


def check_ordinary_module_partition(files: dict[Path, str], errors: list[str]) -> None:
    source = files.get(SESSION_SOURCE, "")
    for marker in ORDINARY_MODULE_PARTITION_MARKERS:
        if not contains_format_independent_marker(source, marker):
            errors.append(f"{SESSION_SOURCE}: missing ordinary-module partition marker: {marker}")

    forbidden_publications = (
        "impl->signatureFacts = zc::mv(stagedSignatureFacts);",
        "impl->importedSignatureViews = zc::mv(stagedImportedSignatureViews);",
        "impl->moduleInterfaces = zc::mv(stagedModuleInterfaces);",
        "impl->checkedEvidence = zc::mv(stagedCheckedEvidence);",
        "impl->dispatchFacts = zc::mv(stagedDispatchFacts);",
    )
    for publication in forbidden_publications:
        if contains_format_independent_marker(source, publication):
            errors.append(
                f"{SESSION_SOURCE}: core module facts must not enter ordinary publication: "
                f"{publication}"
            )

    if (contains_format_independent_marker(source, "checkerBound, stagedSignatureFacts[") or
            contains_format_independent_marker(
                source, "checkerBound, ordinarySignatureFacts[factIndex],")):
        errors.append(
            f"{SESSION_SOURCE}: ordinary HIR lowering must use the stable ordinary-module container"
        )


def stripped_cpp_sources(files: dict[Path, str]) -> dict[Path, str]:
    return {
        path: strip_cpp_comments_and_literals(text)
        for path, text in files.items()
        if path.suffix in {".h", ".cc"}
    }


def check_single_scheduler(
    files: dict[Path, str], stripped_sources: dict[Path, str], errors: list[str],
    scan_paths: set[Path] | None
) -> None:
    infrastructure = {THREAD_POOL_HEADER, THREAD_POOL_SOURCE}
    thread_pool_declaration = re.compile(r"\b(?:basic::)?ThreadPool\s+[A-Za-z_]\w*\s*[;({]")
    raw_scheduler = re.compile(r"\b(?:std::jthread|std::async|pthread_create|dispatch_async)\b")

    for path, text in sorted(stripped_sources.items()):
        if path in infrastructure or (scan_paths is not None and path not in scan_paths):
            continue
        if (
            path != SESSION_SOURCE
            and "ThreadPool" in text
            and thread_pool_declaration.search(text)
        ):
            errors.append(f"{path}: secondary ThreadPool scheduler is forbidden")
        if (
            path != SESSION_SOURCE
            and "threadPool" in text
            and re.search(r"\bthreadPool\s*\.\s*enqueue\s*\(", text)
        ):
            errors.append(f"{path}: secondary frontend enqueue site is forbidden")
        if (
            any(marker in text for marker in ("jthread", "async", "pthread_create", "dispatch_async"))
            and raw_scheduler.search(text)
        ):
            errors.append(f"{path}: raw secondary scheduler primitive is forbidden")

    query_header = files.get(QUERY_DATABASE_HEADER, "")
    query_source = files.get(QUERY_DATABASE_SOURCE, "")
    for owner, marker in (
        (
            query_header,
            "QueryDatabase(basic::ThreadPool& scheduler, QueryDescriptorInventoryRef inventory);",
        ),
        (query_source, "basic::ThreadPool& scheduler;"),
        (
            query_source,
            "QueryDatabase::QueryDatabase(basic::ThreadPool& scheduler, "
            "QueryDescriptorInventoryRef descriptorInventory)",
        ),
    ):
        if not contains_format_independent_marker(owner, marker):
            errors.append(f"{QUERY_DATABASE_SOURCE}: missing borrowed scheduler marker: {marker}")
    if contains_format_independent_marker(query_source, "basic::ThreadPool parallelWork{4};"):
        errors.append(f"{QUERY_DATABASE_SOURCE}: query database must not own worker threads")


def check_cli_root(
    files: dict[Path, str], stripped_sources: dict[Path, str], errors: list[str],
    scan_paths: set[Path] | None
) -> None:
    source = files.get(CLI_SOURCE, "")
    for marker in CLI_MARKERS:
        if marker not in source:
            errors.append(f"{CLI_SOURCE}: missing process-root session marker: {marker}")

    factory_position = source.find("identity::SemanticContextFactory contextFactory;")
    session_position = source.find("zc::Own<driver::CompilerSession> session;")
    if factory_position < 0 or session_position < 0 or factory_position > session_position:
        errors.append(f"{CLI_SOURCE}: context factory must outlive the CompilerSession")

    factory_declaration = re.compile(
        r"\b(?:identity::)?SemanticContextFactory\s+[A-Za-z_]\w*\s*(?:[;{]|\()"
    )
    for path, text in sorted(stripped_sources.items()):
        if path in {
            CLI_SOURCE,
            BRAND_HEADER,
            BRAND_SOURCE,
        } or (scan_paths is not None and path not in scan_paths):
            continue
        if "SemanticContextFactory" in text and factory_declaration.search(text):
            errors.append(f"{path}: secondary process-root SemanticContextFactory is forbidden")

    for path, text in sorted(stripped_sources.items()):
        if path in {
            SESSION_SOURCE,
            REGISTRY_SET_SOURCE,
        } or (scan_paths is not None and path not in scan_paths):
            continue
        if "SemanticIdentityRegistrySet::create(" in text:
            errors.append(f"{path}: identity registry family must be claimed by CompilerSession")


def check_build_wiring(files: dict[Path, str], errors: list[str]) -> None:
    cmake = files.get(DRIVER_CMAKE, "")
    for marker in (DRIVER_BUILD_MARKER,):
        if marker not in cmake:
            errors.append(f"{DRIVER_CMAKE}: missing direct driver build marker: {marker}")
    if re.search(r"\bdriver\.cc\b", cmake):
        errors.append(f"{DRIVER_CMAKE}: forbidden driver.cc build input remains")


def check_crate_graph_authority(
    files: dict[Path, str], errors: list[str], scan_paths: set[Path] | None
) -> None:
    header = files.get(CRATE_GRAPH_HEADER, "")
    source = files.get(CRATE_GRAPH_SOURCE, "")
    for marker in (
        "class VerifiedCrateGraph final",
        "class VerifiedPreparatoryCrateGraph final",
        "static CrateGraphBuildResult buildFinal(",
        "static PreparatoryCrateGraphBuildResult build(",
        "static BuildScriptPlanBuildResult buildPlan(",
        "zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges() const noexcept;",
        "zc::ArrayPtr<const identity::CrateDependencyEdgeKey> edges() const noexcept;",
        "explicit VerifiedCrateGraph(zc::Own<Impl>&& impl) noexcept;",
    ):
        if marker not in header:
            errors.append(f"{CRATE_GRAPH_HEADER}: missing verified crate graph marker: {marker}")
    for marker in (
        "CrateGraphBuildResult VerifiedCrateGraph::buildFinal(",
        "PreparatoryCrateGraphBuildResult VerifiedPreparatoryCrateGraph::build(",
        "BuildScriptPlanBuildResult VerifiedPreparatoryCrateGraph::buildPlan(",
        "request.finalizeRoots(buildPlan)",
        "identity::CrateDependencyEdgeKey::from(",
        "hasCycle(crates.asPtr(), edges.asPtr())",
        "identity::ContextFingerprint::compute(",
    ):
        if marker not in source:
            errors.append(f"{CRATE_GRAPH_SOURCE}: missing crate expansion marker: {marker}")
    for path, text in files.items():
        if path in {
            CRATE_GRAPH_SOURCE,
            CRATE_GRAPH_HEADER,
            Path("compiler/driver/package/package-compilation-request.h"),
            Path("compiler/driver/package/package-compilation-request.cc"),
        } or (scan_paths is not None and path not in scan_paths):
            continue
        if "finalizeRoots(" in text and path.suffix in {".h", ".cc"}:
            errors.append(f"{path}: final crate identity bypasses VerifiedCrateGraph")
    if "collectCrate(root.crateKey().clone()" in files.get(SESSION_SOURCE, ""):
        errors.append(f"{SESSION_SOURCE}: root-only crate freeze bypasses VerifiedCrateGraph")
    session = files.get(SESSION_SOURCE, "")
    if session.count("finalSealedSnapshot = zc::mv(admitted).takeSnapshot();") != 1:
        errors.append(
            f"{SESSION_SOURCE}: final query snapshot must have exactly one sealed publication"
        )
    if "noToolchainInputs" in session:
        errors.append(f"{SESSION_SOURCE}: empty toolchain semantic context bypass is forbidden")
    if (
        "registries, toolchainInputs.asPtr(), packages.edges(), crateEdges.asPtr()"
        in session
    ):
        errors.append(f"{SESSION_SOURCE}: resolution edges must not enter final semantic context")
    if session.count("executor.execute(") != 1:
        errors.append(f"{SESSION_SOURCE}: build scripts must have exactly one verified execute site")
    for path, text in files.items():
        if scan_paths is not None and path not in scan_paths:
            continue
        if "executeBuildScriptPlan" in text:
            errors.append(f"{path}: caller-supplied build-script plan is forbidden")


def check_verified_package_inputs_authority(
    files: dict[Path, str], errors: list[str], scan_paths: set[Path] | None
) -> None:
    # Build-plan derivation is the shared verify-and-build authority, not a session
    # duty: the marker must live in verified-package-inputs.cc and nowhere else, so
    # neither CompilerSession nor any other file re-runs VerifiedPreparatoryCrateGraph
    # ::buildPlan(request, graph) and forks a second authority.
    marker = "VerifiedPreparatoryCrateGraph::buildPlan(request, graph)"
    source = files.get(VERIFIED_PACKAGE_INPUTS_SOURCE, "")
    if marker not in source:
        errors.append(
            f"{VERIFIED_PACKAGE_INPUTS_SOURCE}: missing verify-and-build authority marker: {marker}"
        )
    for path, text in files.items():
        if path == VERIFIED_PACKAGE_INPUTS_SOURCE or path.suffix not in {".h", ".cc"} or (
            scan_paths is not None and path not in scan_paths
        ):
            continue
        if marker in text:
            errors.append(f"{path}: build-plan authority bypasses verified-package-inputs.cc")


def analyze(
    files: dict[Path, str], stripped_sources: dict[Path, str] | None = None,
    scan_paths: set[Path] | None = None
) -> list[str]:
    if stripped_sources is None:
        stripped_sources = stripped_cpp_sources(files)
    errors: list[str] = []
    check_driver_surface(files, stripped_sources, errors, scan_paths)
    check_session_ownership(files, stripped_sources, errors, scan_paths)
    check_ordinary_module_partition(files, errors)
    check_single_scheduler(files, stripped_sources, errors, scan_paths)
    check_cli_root(files, stripped_sources, errors, scan_paths)
    check_build_wiring(files, errors)
    check_crate_graph_authority(files, errors, scan_paths)
    check_verified_package_inputs_authority(files, errors, scan_paths)
    return errors


def run_check() -> int:
    errors = analyze(production_sources())
    if errors:
        print("CompilerSession architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("CompilerSession architecture check passed (one root, one context, one scheduler).")
    return 0


def expect_rejection(
    baseline: dict[Path, str], baseline_stripped_sources: dict[Path, str], name: str, mutate,
    expected_fragment: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    fixture_stripped_sources = dict(baseline_stripped_sources)
    changed_paths = set[Path]()
    for path, text in fixture.items():
        if baseline.get(path) != text:
            changed_paths.add(path)
        if baseline.get(path) != text and path.suffix in {".h", ".cc"}:
            fixture_stripped_sources[path] = strip_cpp_comments_and_literals(text)
    errors = analyze(fixture, fixture_stripped_sources, changed_paths)
    if any(expected_fragment in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected_fragment!r}"]


def run_self_test() -> int:
    baseline = production_sources()
    baseline_stripped_sources = stripped_cpp_sources(baseline)
    errors = analyze(baseline, baseline_stripped_sources)
    if errors:
        print("CompilerSession architecture self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "old driver",
        lambda files: files.__setitem__(
            Path("compiler/driver/driver.h"), "class CompilerDriver {};"
        ),
        "forbidden CompilerDriver identifier remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "compatibility alias",
        lambda files: files.__setitem__(
            SESSION_HEADER, files[SESSION_HEADER] + "\nusing Driver = CompilerSession;\n"
        ),
        "CompilerSession compatibility alias is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "wrapper surface",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER] + "\nclass SessionWrapper { CompilerSession session; };\n",
        ),
        "CompilerSession wrapper class is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "second scheduler",
        lambda files: files.__setitem__(
            Path("compiler/checker/parallel-checker.cc"),
            "void run() { basic::ThreadPool checkerPool; }",
        ),
        "secondary ThreadPool scheduler is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "query-owned scheduler",
        lambda files: files.__setitem__(
            QUERY_DATABASE_SOURCE,
            files[QUERY_DATABASE_SOURCE].replace(
                "basic::ThreadPool& scheduler;", "basic::ThreadPool scheduler{4};", 1
            ),
        ),
        "secondary ThreadPool scheduler is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing scheduler injection",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "queryDatabase(queryScheduler,",
                "queryDatabase(querySchedulerRemoved,",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing canonical interner owner",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "mutable zc::Maybe<identity::IdentityInternerSet> identityInternerSet;", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "session module handle cache",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "identity::ModuleKey key;", "identity::ModuleId identity;", 1
            ),
        ),
        "session-owned persistent module handle cache is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing retained IR identity failures",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER].replace(
                "getIrIdentityInvariantFailures()", "getDroppedIrIdentityInvariantFailures()"
            ),
        ),
        "missing session contract marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "retained parser-result API",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER]
            + "\nzc::ArrayPtr<const ParsedModuleRecord> getParsedModules() const noexcept;\n",
        ),
        "obsolete retained parser-result API remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "module-body session ledger",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nzc::Vector<ModuleBodyQueryBinding> moduleBodyQueryBindings;\n",
        ),
        "obsolete session ledger remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "frozen inventory session ledger",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nzc::Vector<FrozenInventoryBinding> frozenInventories;\n",
        ),
        "obsolete session ledger remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "frozen inventory input session ledger",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nzc::Vector<FrozenInventoryInputBinding> frozenInventoryInputs;\n",
        ),
        "obsolete session ledger remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "second context factory",
        lambda files: files.__setitem__(
            Path("compiler/checker/checker-context.cc"),
            "void run() { identity::SemanticContextFactory checkerFactory; }",
        ),
        "secondary process-root SemanticContextFactory is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "raw session assertion",
        lambda files: files.__setitem__(
            SESSION_SOURCE, files[SESSION_SOURCE] + "\nZC_IREQUIRE(false, \"bad\");\n"
        ),
        "raw session invariant assertion is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing crate graph wiring",
        lambda files: files.__setitem__(
            DRIVER_CMAKE,
            files[DRIVER_CMAKE].replace(
                DRIVER_BUILD_MARKER,
                "set(DRIVER_SRC compiler-session.cc)",
            ),
        ),
        "missing direct driver build marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "root-only crate freeze",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid bypass() { collectCrate(root.crateKey().clone()); }\n",
        ),
        "root-only crate freeze bypasses VerifiedCrateGraph",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing semantic fingerprint",
        lambda files: files.__setitem__(
            CRATE_GRAPH_SOURCE,
            files[CRATE_GRAPH_SOURCE].replace(
                "identity::ContextFingerprint::compute(",
                "missingSemanticContextFingerprint(",
                1,
            ),
        ),
        "missing crate expansion marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "resolution edges in final context",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid bypass() { registries, toolchainInputs.asPtr(), packages.edges(), crateEdges.asPtr(); }\n",
        ),
        "resolution edges must not enter final semantic context",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "empty toolchain semantic context",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid bypass() { auto noToolchainInputs = makeEmptyInputs(); }\n",
        ),
        "empty toolchain semantic context bypass is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing preparatory crate graph",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "VerifiedPreparatoryCrateGraph::build(request, node, resolution, plan, completed)",
                "bypassPreparatoryCrateGraph(request, node, resolution, plan, completed)",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "caller supplied build plan",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER]
            + "\nvoid executeBuildScriptPlan(package::VerifiedBuildScriptPlan&& plan);\n",
        ),
        "caller-supplied build-script plan is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "package path reread",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid reread() { getFileSystemSourceBufferID(path); }\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "raw AST store",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nzc::HashMap<source::BufferId, ast::Tree> rawTrees;\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "parse wrapper",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid parse() { basic::performParse(input); }\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "session-owned module graph publication",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid bypass() { binder::VerifiedModuleGraphBuilder::build(candidate); }\n",
        ),
        "session-owned module graph publication is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "module graph bypass",
        lambda files: files.__setitem__(
            Path("compiler/checker/module-graph.cc"),
            "void run() { VerifiedModuleGraphBuilder::build(candidate); }",
        ),
        "global module graph publication bypasses CompilerSession",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing stable module graph input staging",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
                "removedModuleGraphInputTransaction(",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing stable module graph demand",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "snapshot.getCapability<module_graph_query::MaterializeModuleGraph>",
                "snapshot.getCapability<module_graph_query::RemovedModuleGraphQuery>",
                1,
            ),
        ),
        "sealed materialized graph demand is missing",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing final core authority demand",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "getCapability<core_library_query::MaterializeCoreAuthority>",
                "getCapability<core_library_query::RemovedCoreAuthorityQuery>",
                1,
            ),
        ),
        "final core authority demand is missing",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing final core-interface demand",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "FinalizeCoreModuleInterface", "RemovedFinalCoreModuleInterfaceQuery", 1
            ),
        ),
        "source-backed final core-interface demand is missing",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "bootstrap-only core query escape",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "FinalizeCoreModuleInterface",
                "FinalizeCoreModuleInterface\nMaterializeCoreBootstrapModuleInterfaceQuery",
                1,
            ),
        ),
        "bootstrap-only core query escapes finalization",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing core prelude-surface preflight",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "auto preludeSurface = finalSnapshot.get<core_library_query::CorePreludeSurface>(",
                "auto preludeSurface = finalSnapshot.get<core_library_query::RemovedPreludeSurfaceQuery>(",
                1,
            ),
        ),
        "source-backed core prelude-surface preflight is missing",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "batch module resolution authority",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid resolve_batch() { auto resolved = resolver.resolve(zc::mv(request)); }\n",
        ),
        "batch module resolution authority is forbidden",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing atomic dispatch publication",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing ordinary module classification",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace("ordinaryBoundModuleIndices.add(index);", "", 1),
        ),
        "missing ordinary-module partition marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "ordinary HIR uses a staging index for bound facts",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "checkerBound, ordinarySignatureFacts[ordinaryIndex],",
                "checkerBound, ordinarySignatureFacts[factIndex],",
                1,
            ),
        ),
        "ordinary HIR lowering must use the stable ordinary-module container",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "core dispatch facts enter ordinary publication",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
                "impl->dispatchFacts = zc::mv(stagedDispatchFacts);",
                1,
            ),
        ),
        "core module facts must not enter ordinary publication",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing atomic borrow evidence publication",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "impl->borrowEvidenceRepository = zc::mv(stagedBorrowEvidenceRepository);", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "missing verify-and-build authority marker",
        lambda files: files.__setitem__(
            VERIFIED_PACKAGE_INPUTS_SOURCE,
            files[VERIFIED_PACKAGE_INPUTS_SOURCE].replace(
                "VerifiedPreparatoryCrateGraph::buildPlan(request, graph)",
                "bypassBuildPlan(request, graph)",
                1,
            ),
        ),
        "missing verify-and-build authority marker",
    )
    failures += expect_rejection(
        baseline, baseline_stripped_sources,
        "build-plan authority bypasses the owner file",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid bypass() { VerifiedPreparatoryCrateGraph::buildPlan(request, graph); }\n",
        ),
        "build-plan authority bypasses verified-package-inputs.cc",
    )

    if failures:
        print("CompilerSession architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("CompilerSession architecture negative fixtures passed (35/35).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return run_check() if args.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
