#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "products" / "zomlang" / "compiler"
UTILS_ROOT = ROOT / "products" / "zomlang" / "utils"
SESSION_HEADER = Path("products/zomlang/compiler/driver/compiler-session.h")
SESSION_SOURCE = Path("products/zomlang/compiler/driver/compiler-session.cc")
CRATE_GRAPH_HEADER = Path("products/zomlang/compiler/driver/crate-graph.h")
CRATE_GRAPH_SOURCE = Path("products/zomlang/compiler/driver/crate-graph.cc")
MODULE_DISCOVERY_HEADER = Path("products/zomlang/compiler/driver/module-discovery.h")
MODULE_DISCOVERY_SOURCE = Path("products/zomlang/compiler/driver/module-discovery.cc")
COHERENCE_BUILDER_HEADER = Path("products/zomlang/compiler/driver/coherence-builder.h")
COHERENCE_BUILDER_SOURCE = Path("products/zomlang/compiler/driver/coherence-builder.cc")
IMPORTED_VIEW_PROJECTOR_HEADER = Path(
    "products/zomlang/compiler/driver/imported-signature-view-projector.h"
)
IMPORTED_VIEW_PROJECTOR_SOURCE = Path(
    "products/zomlang/compiler/driver/imported-signature-view-projector.cc"
)
MODULE_INTERFACE_HEADER = Path("products/zomlang/compiler/driver/module-interface.h")
MODULE_INTERFACE_SOURCE = Path("products/zomlang/compiler/driver/module-interface.cc")
MODULE_INTERFACE_DIAGNOSTIC_HEADER = Path(
    "products/zomlang/compiler/driver/module-interface-diagnostic-adapter.h"
)
MODULE_INTERFACE_DIAGNOSTIC_SOURCE = Path(
    "products/zomlang/compiler/driver/module-interface-diagnostic-adapter.cc"
)
BORROW_EVIDENCE_HEADER = Path("products/zomlang/compiler/driver/borrow-evidence.h")
BORROW_EVIDENCE_SOURCE = Path("products/zomlang/compiler/driver/borrow-evidence.cc")
ACTIVE_DEFINITION_AUTHORITY_QUERY_HEADER = Path(
    "products/zomlang/compiler/driver/active-definition-authority-query.h"
)
ACTIVE_DEFINITION_AUTHORITY_QUERY_SOURCE = Path(
    "products/zomlang/compiler/driver/active-definition-authority-query.cc"
)
ACTIVE_DEFINITION_AUTHORITY_SESSION_HEADER = Path(
    "products/zomlang/compiler/driver/active-definition-authority-session.h"
)
ACTIVE_DEFINITION_AUTHORITY_SESSION_SOURCE = Path(
    "products/zomlang/compiler/driver/active-definition-authority-session.cc"
)
INCREMENTAL_BINDING_QUERY_ADAPTER_HEADER = Path(
    "products/zomlang/compiler/driver/incremental-binding-query-adapter.h"
)
INCREMENTAL_BINDING_QUERY_ADAPTER_SOURCE = Path(
    "products/zomlang/compiler/driver/incremental-binding-query-adapter.cc"
)
INCREMENTAL_PACKAGE_GRAPH_INPUT_HEADER = Path(
    "products/zomlang/compiler/driver/incremental-package-graph-query-input.h"
)
INCREMENTAL_PACKAGE_GRAPH_INPUT_SOURCE = Path(
    "products/zomlang/compiler/driver/incremental-package-graph-query-input.cc"
)
INCREMENTAL_MODULE_RESOLUTION_QUERY_HEADER = Path(
    "products/zomlang/compiler/driver/incremental-module-resolution-query.h"
)
INCREMENTAL_MODULE_RESOLUTION_QUERY_SOURCE = Path(
    "products/zomlang/compiler/driver/incremental-module-resolution-query.cc"
)
NAMED_IDENTITY_INVENTORY_QUERY_HEADER = Path(
    "products/zomlang/compiler/driver/named-identity-inventory-query.h"
)
NAMED_IDENTITY_INVENTORY_QUERY_SOURCE = Path(
    "products/zomlang/compiler/driver/named-identity-inventory-query.cc"
)
NAMED_ITEM_QUERY_HEADER = Path("products/zomlang/compiler/driver/named-item-query.h")
NAMED_ITEM_QUERY_SOURCE = Path("products/zomlang/compiler/driver/named-item-query.cc")
OWNER_BODY_QUERY_HEADER = Path("products/zomlang/compiler/driver/owner-body-query.h")
OWNER_BODY_QUERY_SOURCE = Path("products/zomlang/compiler/driver/owner-body-query.cc")
CORE_LIBRARY_QUERY_PROVIDER_HEADER = Path(
    "products/zomlang/compiler/driver/core-library-query-provider.h"
)
CORE_LIBRARY_QUERY_PROVIDER_SOURCE = Path(
    "products/zomlang/compiler/driver/core-library-query-provider.cc"
)
CORE_LIBRARY_QUERY_VERIFIER_HEADER = Path(
    "products/zomlang/compiler/driver/core-library-query-verifier.h"
)
CORE_LIBRARY_QUERY_VERIFIER_SOURCE = Path(
    "products/zomlang/compiler/driver/core-library-query-verifier.cc"
)
MODULE_GRAPH_QUERY_INPUT_HEADER = Path(
    "products/zomlang/compiler/driver/module-graph-query-input.h"
)
MODULE_GRAPH_QUERY_INPUT_SOURCE = Path(
    "products/zomlang/compiler/driver/module-graph-query-input.cc"
)
MODULE_GRAPH_QUERY_HEADER = Path("products/zomlang/compiler/driver/module-graph-query.h")
MODULE_GRAPH_QUERY_SOURCE = Path("products/zomlang/compiler/driver/module-graph-query.cc")
QUERY_DATABASE_HEADER = Path("products/zomlang/compiler/query/query-database.h")
QUERY_DATABASE_SOURCE = Path("products/zomlang/compiler/query/query-database.cc")
DRIVER_CMAKE = Path("products/zomlang/compiler/driver/CMakeLists.txt")
CLI_SOURCE = Path("products/zomlang/utils/zomc/zomc.cc")
THREAD_POOL_HEADER = Path("products/zomlang/compiler/basic/thread-pool.h")
THREAD_POOL_SOURCE = Path("products/zomlang/compiler/basic/thread-pool.cc")
BRAND_SOURCE = Path("products/zomlang/compiler/identity/brand.cc")
BRAND_HEADER = Path("products/zomlang/compiler/identity/brand.h")
REGISTRY_SET_SOURCE = Path(
    "products/zomlang/compiler/identity/semantic-identity-registry-set.cc"
)
BINDING_INPUT_SOURCE = Path("products/zomlang/compiler/binder/binding-input.cc")
VERIFIED_VENDOR_ROOT = Path("products/zomlang/compiler/driver/package/vendor")

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
    ACTIVE_DEFINITION_AUTHORITY_QUERY_HEADER,
    ACTIVE_DEFINITION_AUTHORITY_QUERY_SOURCE,
    ACTIVE_DEFINITION_AUTHORITY_SESSION_HEADER,
    ACTIVE_DEFINITION_AUTHORITY_SESSION_SOURCE,
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
    CORE_LIBRARY_QUERY_PROVIDER_HEADER,
    CORE_LIBRARY_QUERY_PROVIDER_SOURCE,
    CORE_LIBRARY_QUERY_VERIFIER_HEADER,
    CORE_LIBRARY_QUERY_VERIFIER_SOURCE,
    MODULE_GRAPH_QUERY_INPUT_HEADER,
    MODULE_GRAPH_QUERY_INPUT_SOURCE,
    MODULE_GRAPH_QUERY_HEADER,
    MODULE_GRAPH_QUERY_SOURCE,
}

DRIVER_BUILD_MARKER = (
    "set(DRIVER_SRC active-definition-authority-query.cc active-definition-authority-session.cc\n"
    "               borrow-evidence.cc coherence-builder.cc compiler-session.cc\n"
    "               core-library-query-provider.cc core-library-query-verifier.cc crate-graph.cc\n"
    "               imported-signature-view-projector.cc incremental-binding-query-adapter.cc\n"
    "               incremental-module-resolution-query.cc\n"
    "               incremental-package-graph-query-input.cc\n"
    "               module-discovery.cc module-graph-query-input.cc module-graph-query.cc\n"
    "               module-interface.cc\n"
    "               module-interface-diagnostic-adapter.cc\n"
    "               named-identity-inventory-query.cc named-item-query.cc owner-body-query.cc)"
)

SESSION_HEADER_MARKERS = (
    "class CompilerSession",
    "CompilerSession(identity::SemanticContextFactory& contextFactory,",
    "identity::SemanticContextBrand getSemanticContextBrand() const noexcept;",
    "zc::Maybe<const identity::SemanticIdentityRegistrySet&> getIdentityRegistries() const noexcept;",
    "addVerifiedPackageRoot(",
    "zc::ArrayPtr<const ParsedModuleRecord> getParsedModules() const noexcept;",
    "zc::Maybe<const VerifiedCrateGraph&> getVerifiedCrateGraph() const noexcept;",
    "getVerifiedPreparatoryCrateGraphs()\n      const noexcept;",
    "getSemanticContextFingerprint() const noexcept;",
    "getVerifiedModuleGraph()\n      const noexcept;",
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
    "zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;",
    "basic::ThreadPool queryScheduler;",
    "query::QueryDatabase queryDatabase;",
    "queryDatabase(queryScheduler, semanticContextCapabilityArena.addRef())",
    "zc::Own<basic::StringPool> stringPool;",
    "zc::Own<source::SourceManager> sourceManager;",
    "zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;",
    "zc::Vector<ParsedModuleRecord> parsedModules;",
    "snapshot.snapshot().readVerifiedFile(sourcePath)",
    "registerVerifiedSource(",
    "parseSnapshot.getCapability<parser::ParseSourceQuery>",
    "binder::ParsedModuleVerifier::verifyQueryResult(",
    "extractStructuralModuleDependencyRequests(parsed.value().capability().tree())",
    "incremental_binding_query::ModuleBodySyntaxQuery",
    "incremental_binding_query::ModuleBodyProvenanceQuery",
    "zc::Vector<ModuleBodyQueryBinding> moduleBodyQueryBindings;",
    "ActiveDefinitionAuthorityProjectionState",
    "activeDefinitionAuthority.refresh(",
    "incremental_binding_query::NamedItemSyntaxQuery",
    "incremental_binding_query::NamedItemProvenanceQuery",
    "zc::Vector<NamedItemQueryBinding> namedItemQueryBindings;",
    "!impl->freezeSourceIdentities()",
    "binder::DefinitionInventory::collect(tree);",
    "diagnostics::DiagID::IdentityBrandExhausted",
    "diagnostics::DiagID::IdentityDuplicateSingletonStore",
    "while (true) {",
    "zc::Maybe<VerifiedCrateGraph> crateGraph;",
    "zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryCrateGraphs;",
    "zc::Maybe<identity::SemanticContextFingerprint> semanticContextFingerprint;",
    "zc::Maybe<binder::VerifiedModuleGraph> moduleGraph;",
    "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
    "authorityStagingSnapshotValue.get<graph_query::ModuleGraphQuery>(",
    "authorityStagingSnapshotValue.get<graph_query::ModuleGraphSccQuery>(",
    "binder::VerifiedModuleGraphBuilder::build(",
    "!impl->freezeModuleGraph()",
    "identity::SemanticContextFingerprint::compute(",
    "collectSemanticContextInputs(toolchainInputs, crateEdges)",
    "registries, toolchainInputs.asPtr(), crates.packageEdges(), crateEdges.asPtr()",
    "VerifiedPreparatoryCrateGraph::build(request, node, resolution, plan, completed)",
    "VerifiedPreparatoryCrateGraph::buildPlan(request, graph)",
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
    "checker::dispatch::DispatchSiteInventoryBuilder::build(bound, inventory)",
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
    "ZC_ASSERT_NONNULL(crate).unit().kind() == identity::CompilationUnitKind::UserPackage",
    "ordinaryBoundModuleIndices.add(index);",
    "for (size_t ordinaryIndex = 0; ordinaryIndex < ordinaryBoundModuleIndices.size();",
    "const auto boundIndex = ordinaryBoundModuleIndices[ordinaryIndex];",
    "impl->boundModules[boundIndex], stagedSignatureFacts[boundIndex],",
    "stagedModuleInterfaces[boundIndex], stagedImportedSignatureViews[boundIndex],",
    "stagedModuleInterfaces.asPtr(), stagedCheckedEvidence[boundIndex],",
    "*stagedCheckedFactsRepository, stagedDispatchFacts[boundIndex],",
    "mir::BuiltMirBuilder::build(stagedHirModules[ordinaryIndex])",
    "stagedHirModules.size() != ordinaryBoundModuleIndices.size()",
    "stagedBuiltMirModules.size() != ordinaryBoundModuleIndices.size()",
    "stagedOwnershipEventOverlays.size() != ordinaryBoundModuleIndices.size()",
    "for (const auto index : ordinaryBoundModuleIndices)",
    "ordinarySignatureFacts.add(zc::mv(stagedSignatureFacts[index]));",
    "ordinaryImportedSignatureViews.add(zc::mv(stagedImportedSignatureViews[index]));",
    "ordinaryModuleInterfaces.add(zc::mv(stagedModuleInterfaces[index]));",
    "ordinaryCheckedEvidence.add(zc::mv(stagedCheckedEvidence[index]));",
    "ordinaryDispatchFacts.add(zc::mv(stagedDispatchFacts[index]));",
    "impl->signatureFacts = zc::mv(ordinarySignatureFacts);",
    "impl->importedSignatureViews = zc::mv(ordinaryImportedSignatureViews);",
    "impl->moduleInterfaces = zc::mv(ordinaryModuleInterfaces);",
    "impl->checkedEvidence = zc::mv(ordinaryCheckedEvidence);",
    "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
)


def contains_format_independent_marker(text: str, marker: str) -> bool:
    """Match C++ contract markers without depending on formatter line wrapping."""
    normalized_text = re.sub(r"\s+", " ", text)
    normalized_marker = re.sub(r"\s+", " ", marker)
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


def check_driver_surface(files: dict[Path, str], errors: list[str]) -> None:
    driver_files = {
        path for path in files if path.parent == SESSION_HEADER.parent
    }
    unexpected = sorted(driver_files - EXPECTED_DRIVER_FILES)
    missing = sorted(EXPECTED_DRIVER_FILES - driver_files)
    for path in unexpected:
        errors.append(f"{path}: unexpected compiler driver surface")
    for path in missing:
        errors.append(f"{path}: required CompilerSession surface is missing")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"}:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if re.search(r"\bCompilerDriver\b", text):
            errors.append(f"{path}: forbidden CompilerDriver identifier remains")
        if "zomlang/compiler/driver/driver.h" in raw_text:
            errors.append(f"{path}: forbidden driver.h include remains")
        if path.parent == SESSION_HEADER.parent and re.search(
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


def check_session_ownership(files: dict[Path, str], errors: list[str]) -> None:
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
    for forbidden in ("addSourceFile(", "addPackageSourceFile(", "getASTs("):
        if forbidden in header:
            errors.append(f"{SESSION_HEADER}: forbidden raw source API remains: {forbidden}")

    if source.count("while (true) {") != 1:
        errors.append(f"{SESSION_SOURCE}: must own exactly one discovery fixed-point scheduler")
    if (
        source.count(
            "extractStructuralModuleDependencyRequests(parsed.value().capability().tree())"
        )
        != 1
    ):
        errors.append(f"{SESSION_SOURCE}: discovery scheduler must have exactly one request site")
    if source.count("binder::VerifiedModuleGraphBuilder::build(") != 1:
        errors.append(f"{SESSION_SOURCE}: must publish exactly one verified module graph")
    if "resolver.resolve(zc::mv(request))" in source:
        errors.append(f"{SESSION_SOURCE}: batch module resolution authority is forbidden")

    for path, raw_text in files.items():
        if path in {SESSION_SOURCE, BINDING_INPUT_SOURCE} or path.suffix not in {".h", ".cc"}:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
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

    if contains_format_independent_marker(
        source, "impl->boundModules[ordinaryIndex], stagedSignatureFacts[ordinaryIndex],"
    ):
        errors.append(
            f"{SESSION_SOURCE}: ordinary HIR lowering must map through the bound-module index"
        )


def check_single_scheduler(files: dict[Path, str], errors: list[str]) -> None:
    infrastructure = {THREAD_POOL_HEADER, THREAD_POOL_SOURCE}
    thread_pool_declaration = re.compile(r"\b(?:basic::)?ThreadPool\s+[A-Za-z_]\w*\s*[;({]")
    raw_scheduler = re.compile(r"\b(?:std::jthread|std::async|pthread_create|dispatch_async)\b")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in infrastructure:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if path != SESSION_SOURCE and thread_pool_declaration.search(text):
            errors.append(f"{path}: secondary ThreadPool scheduler is forbidden")
        if path != SESSION_SOURCE and re.search(r"\bthreadPool\s*\.\s*enqueue\s*\(", text):
            errors.append(f"{path}: secondary frontend enqueue site is forbidden")
        if raw_scheduler.search(text):
            errors.append(f"{path}: raw secondary scheduler primitive is forbidden")

    query_header = files.get(QUERY_DATABASE_HEADER, "")
    query_source = files.get(QUERY_DATABASE_SOURCE, "")
    for marker in (
        "explicit QueryDatabase(basic::ThreadPool& scheduler);",
        "basic::ThreadPool& scheduler;",
        "QueryDatabase::QueryDatabase(basic::ThreadPool& scheduler)",
    ):
        owner = query_header if marker.startswith("explicit") else query_source
        if not contains_format_independent_marker(owner, marker):
            errors.append(f"{QUERY_DATABASE_SOURCE}: missing borrowed scheduler marker: {marker}")
    if contains_format_independent_marker(query_source, "basic::ThreadPool parallelWork{4};"):
        errors.append(f"{QUERY_DATABASE_SOURCE}: query database must not own worker threads")


def check_cli_root(files: dict[Path, str], errors: list[str]) -> None:
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
    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in {
            CLI_SOURCE,
            BRAND_HEADER,
            BRAND_SOURCE,
        }:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if factory_declaration.search(text):
            errors.append(f"{path}: secondary process-root SemanticContextFactory is forbidden")

    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"} or path in {
            SESSION_SOURCE,
            REGISTRY_SET_SOURCE,
        }:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        if "SemanticIdentityRegistrySet::create(" in text:
            errors.append(f"{path}: identity registry family must be claimed by CompilerSession")


def check_build_wiring(files: dict[Path, str], errors: list[str]) -> None:
    cmake = files.get(DRIVER_CMAKE, "")
    for marker in (DRIVER_BUILD_MARKER,):
        if marker not in cmake:
            errors.append(f"{DRIVER_CMAKE}: missing direct driver build marker: {marker}")
    if re.search(r"\bdriver\.cc\b", cmake):
        errors.append(f"{DRIVER_CMAKE}: forbidden driver.cc build input remains")


def check_crate_graph_authority(files: dict[Path, str], errors: list[str]) -> None:
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
        "identity::SemanticContextFingerprint::compute(",
    ):
        if marker not in source:
            errors.append(f"{CRATE_GRAPH_SOURCE}: missing crate expansion marker: {marker}")
    for path, text in files.items():
        if path in {
            CRATE_GRAPH_SOURCE,
            CRATE_GRAPH_HEADER,
            Path("products/zomlang/compiler/driver/package/package-compilation-request.h"),
            Path("products/zomlang/compiler/driver/package/package-compilation-request.cc"),
        }:
            continue
        if "finalizeRoots(" in text and path.suffix in {".h", ".cc"}:
            errors.append(f"{path}: final crate identity bypasses VerifiedCrateGraph")
    if "collectCrate(root.crateKey().clone()" in files.get(SESSION_SOURCE, ""):
        errors.append(f"{SESSION_SOURCE}: root-only crate freeze bypasses VerifiedCrateGraph")
    session = files.get(SESSION_SOURCE, "")
    if session.count("registries.freezeCompilationUnits()") != 1:
        errors.append(
            f"{SESSION_SOURCE}: final crate graph must freeze exactly one compilation-unit set"
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
        if "executeBuildScriptPlan" in text:
            errors.append(f"{path}: caller-supplied build-script plan is forbidden")


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_driver_surface(files, errors)
    check_session_ownership(files, errors)
    check_ordinary_module_partition(files, errors)
    check_single_scheduler(files, errors)
    check_cli_root(files, errors)
    check_build_wiring(files, errors)
    check_crate_graph_authority(files, errors)
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
    baseline: dict[Path, str], name: str, mutate, expected_fragment: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    errors = analyze(fixture)
    if any(expected_fragment in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected_fragment!r}"]


def run_self_test() -> int:
    baseline = production_sources()
    errors = analyze(baseline)
    if errors:
        print("CompilerSession architecture self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    failures += expect_rejection(
        baseline,
        "old driver",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/driver/driver.h"), "class CompilerDriver {};"
        ),
        "forbidden CompilerDriver identifier remains",
    )
    failures += expect_rejection(
        baseline,
        "compatibility alias",
        lambda files: files.__setitem__(
            SESSION_HEADER, files[SESSION_HEADER] + "\nusing Driver = CompilerSession;\n"
        ),
        "CompilerSession compatibility alias is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "wrapper surface",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER] + "\nclass SessionWrapper { CompilerSession session; };\n",
        ),
        "CompilerSession wrapper class is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "second scheduler",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/checker/parallel-checker.cc"),
            "void run() { basic::ThreadPool checkerPool; }",
        ),
        "secondary ThreadPool scheduler is forbidden",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "missing scheduler injection",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "queryDatabase(queryScheduler, semanticContextCapabilityArena.addRef())",
                "queryDatabase(querySchedulerRemoved, semanticContextCapabilityArena.addRef())",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
        "missing registry owner",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "second context factory",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/checker/checker-context.cc"),
            "void run() { identity::SemanticContextFactory checkerFactory; }",
        ),
        "secondary process-root SemanticContextFactory is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "raw session assertion",
        lambda files: files.__setitem__(
            SESSION_SOURCE, files[SESSION_SOURCE] + "\nZC_IREQUIRE(false, \"bad\");\n"
        ),
        "raw session invariant assertion is forbidden",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "root-only crate freeze",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid bypass() { collectCrate(root.crateKey().clone()); }\n",
        ),
        "root-only crate freeze bypasses VerifiedCrateGraph",
    )
    failures += expect_rejection(
        baseline,
        "missing semantic fingerprint",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "identity::SemanticContextFingerprint::compute(",
                "missingSemanticContextFingerprint(",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
        "resolution edges in final context",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "registries, toolchainInputs.asPtr(), crates.packageEdges(), crateEdges.asPtr()",
                "registries, toolchainInputs.asPtr(), packages.edges(), crateEdges.asPtr()",
                1,
            ),
        ),
        "resolution edges must not enter final semantic context",
    )
    failures += expect_rejection(
        baseline,
        "empty toolchain semantic context",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid bypass() { auto noToolchainInputs = makeEmptyInputs(); }\n",
        ),
        "empty toolchain semantic context bypass is forbidden",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "caller supplied build plan",
        lambda files: files.__setitem__(
            SESSION_HEADER,
            files[SESSION_HEADER]
            + "\nvoid executeBuildScriptPlan(package::VerifiedBuildScriptPlan&& plan);\n",
        ),
        "caller-supplied build-script plan is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "package path reread",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid reread() { getFileSystemSourceBufferID(path); }\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline,
        "raw AST store",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nzc::HashMap<source::BufferId, ast::Tree> rawTrees;\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline,
        "parse wrapper",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE] + "\nvoid parse() { basic::performParse(input); }\n",
        ),
        "forbidden raw source authority remains",
    )
    failures += expect_rejection(
        baseline,
        "missing module graph owner",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "zc::Maybe<binder::VerifiedModuleGraph> moduleGraph;", ""
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
        "module graph bypass",
        lambda files: files.__setitem__(
            Path("products/zomlang/compiler/checker/module-graph.cc"),
            "void run() { VerifiedModuleGraphBuilder::build(candidate); }",
        ),
        "global module graph publication bypasses CompilerSession",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "missing stable module graph demand",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "authorityStagingSnapshotValue.get<graph_query::ModuleGraphQuery>",
                "authorityStagingSnapshotValue.get<RemovedModuleGraphQuery>",
                1,
            ),
        ),
        "missing session ownership marker",
    )
    failures += expect_rejection(
        baseline,
        "batch module resolution authority",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE]
            + "\nvoid resolve_batch() { auto resolved = resolver.resolve(zc::mv(request)); }\n",
        ),
        "batch module resolution authority is forbidden",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "missing ordinary module classification",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace("ordinaryBoundModuleIndices.add(index);", "", 1),
        ),
        "missing ordinary-module partition marker",
    )
    failures += expect_rejection(
        baseline,
        "ordinary HIR uses the compact index for bound facts",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "impl->boundModules[boundIndex], stagedSignatureFacts[boundIndex],",
                "impl->boundModules[ordinaryIndex], stagedSignatureFacts[ordinaryIndex],",
                1,
            ),
        ),
        "ordinary HIR lowering must map through the bound-module index",
    )
    failures += expect_rejection(
        baseline,
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
        baseline,
        "missing atomic borrow evidence publication",
        lambda files: files.__setitem__(
            SESSION_SOURCE,
            files[SESSION_SOURCE].replace(
                "impl->borrowEvidenceRepository = zc::mv(stagedBorrowEvidenceRepository);", ""
            ),
        ),
        "missing session ownership marker",
    )

    if failures:
        print("CompilerSession architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("CompilerSession architecture negative fixtures passed (29/29).")
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
