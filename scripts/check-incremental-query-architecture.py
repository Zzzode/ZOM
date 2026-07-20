#!/usr/bin/env python3
"""Enforce RFC 0017 query routing, dependency, and capability boundaries."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
QUERY_ROOT = Path("products/zomlang/compiler/query")
COMPILER_ROOT = Path("products/zomlang/compiler")
COMPILER_CMAKE = COMPILER_ROOT / "CMakeLists.txt"
QUERY_CMAKE = QUERY_ROOT / "CMakeLists.txt"
QUERY_DATABASE_HEADER = QUERY_ROOT / "query-database.h"
QUERY_DATABASE_SOURCE = QUERY_ROOT / "query-database.cc"
DRIVER_SESSION = COMPILER_ROOT / "driver/compiler-session.cc"
DRIVER_TOPOLOGY_ADAPTER = COMPILER_ROOT / "driver/incremental-binding-query-adapter.cc"
DRIVER_NAMED_IDENTITY_QUERY = COMPILER_ROOT / "driver/named-identity-inventory-query.cc"
DRIVER_MODULE_RESOLUTION_QUERY = COMPILER_ROOT / "driver/incremental-module-resolution-query.cc"
DRIVER_PACKAGE_GRAPH_INPUT = COMPILER_ROOT / "driver/incremental-package-graph-query-input.cc"
IDENTITY_SOURCE_QUERY_INPUT = COMPILER_ROOT / "identity/source-query-input.cc"
PARSER_PARSE_SOURCE_QUERY = COMPILER_ROOT / "parser/parse-source-query.cc"
PARSER_PARSE_SOURCE_QUERY_VERIFIER = COMPILER_ROOT / "parser/parse-source-query-verifier.cc"
DRIVER_TOPOLOGY_ADAPTER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc"
)
DRIVER_SESSION_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc"
)
DRIVER_MODULE_RESOLUTION_QUERY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/incremental-module-resolution-query-test.cc"
)
MANIFEST = Path(".agents/subagents/manifest.yaml")
ROUTING = Path(".agents/subagents/README.md")
TASK_ROUTER = Path(".agents/subagents/task-router.md")
MODULE_OWNER = Path(".agents/subagents/module-system.md")
VERIFICATION_OWNER = Path(".agents/subagents/verification.md")
AGENTS = Path("AGENTS.md")

ROUTING_FILES = (MANIFEST, ROUTING, TASK_ROUTER, MODULE_OWNER, VERIFICATION_OWNER, AGENTS)

QUERY_FORBIDDEN_INCLUDES = (
    "zomlang/compiler/ast/",
    "zomlang/compiler/binder/",
    "zomlang/compiler/checker/",
    "zomlang/compiler/diagnostics/",
    "zomlang/compiler/driver/",
    "zomlang/compiler/hir/",
    "zomlang/compiler/identity/",
    "zomlang/compiler/ir/",
    "zomlang/compiler/lexer/",
    "zomlang/compiler/mir/",
    "zomlang/compiler/parser/",
    "zomlang/compiler/source/",
    "zomlang/compiler/type/",
)

QUERY_FORBIDDEN_LINK_TARGETS = (
    "ast",
    "binder",
    "checker",
    "diagnostics",
    "driver",
    "hir",
    "identity",
    "ir",
    "lexer",
    "mir",
    "parser",
    "source",
    "trace",
    "type",
)


def relative(path: Path) -> Path:
    return path.relative_to(ROOT)


def source_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for path in ROUTING_FILES:
        absolute = ROOT / path
        if absolute.exists():
            files[path] = absolute.read_text(encoding="utf-8")
    for directory, child_directories, names in os.walk(ROOT / COMPILER_ROOT):
        child_directories[:] = [name for name in child_directories if name != "vendor"]
        for name in names:
            path = Path(directory) / name
            if path.suffix not in {".cc", ".h"} and name != "CMakeLists.txt":
                continue
            files[relative(path)] = path.read_text(encoding="utf-8")
    for path in (
        DRIVER_TOPOLOGY_ADAPTER_TEST,
        DRIVER_SESSION_TEST,
        DRIVER_MODULE_RESOLUTION_QUERY_TEST,
    ):
        absolute = ROOT / path
        if absolute.exists():
            files[path] = absolute.read_text(encoding="utf-8")
    return files


def require_marker(
    files: dict[Path, str], path: Path, marker: str, description: str, errors: list[str]
) -> None:
    if marker not in files.get(path, ""):
        errors.append(f"{path}: missing {description}: {marker}")


def check_routing(files: dict[Path, str], errors: list[str]) -> None:
    require_marker(
        files,
        MANIFEST,
        '"products/zomlang/compiler/query/**"',
        "module-system query ownership",
        errors,
    )
    require_marker(
        files,
        MANIFEST,
        '"scripts/check-incremental-query-architecture.py"',
        "verification gate ownership",
        errors,
    )
    require_marker(
        files,
        MANIFEST,
        '"scripts/run-incremental-query-benchmarks.py"',
        "verification benchmark ownership",
        errors,
    )
    require_marker(
        files,
        ROUTING,
        "Incremental query runtime, red-green reuse, or projection shielding",
        "incremental query routing row",
        errors,
    )
    require_marker(
        files,
        TASK_ROUTER,
        "Query runtime, memo, red-green, and incremental identity work routes to",
        "task-router query rule",
        errors,
    )
    require_marker(
        files,
        MODULE_OWNER,
        "products/zomlang/compiler/query/**",
        "module-system query path",
        errors,
    )
    require_marker(
        files,
        VERIFICATION_OWNER,
        "scripts/check-incremental-query-architecture.py",
        "verification query gate",
        errors,
    )
    require_marker(
        files,
        AGENTS,
        "Query database, identity, import/export",
        "repository query owner summary",
        errors,
    )


def query_sources(files: dict[Path, str]) -> list[tuple[Path, str]]:
    return sorted(
        (path, text)
        for path, text in files.items()
        if QUERY_ROOT in path.parents and path.suffix in {".cc", ".h"}
    )


def check_query_leaf(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in query_sources(files):
        for include in QUERY_FORBIDDEN_INCLUDES:
            if include in text:
                errors.append(f"{path}: query runtime includes forbidden semantic path {include}")

    query_cmake = files.get(QUERY_CMAKE, "")
    if not query_cmake:
        return
    if not re.search(r"\badd_library\s*\(\s*query\b", query_cmake):
        errors.append(f"{QUERY_CMAKE}: query runtime target must be named query")
    for target in QUERY_FORBIDDEN_LINK_TARGETS:
        if re.search(rf"\b{re.escape(target)}\b", query_cmake):
            errors.append(f"{QUERY_CMAKE}: query runtime must not link semantic target {target}")

    compiler_cmake = files.get(COMPILER_CMAKE, "")
    if "add_subdirectory(query)" not in compiler_cmake:
        errors.append(f"{COMPILER_CMAKE}: query subdirectory is missing from compiler composition")
    else:
        query_position = compiler_cmake.find("add_subdirectory(query)")
        semantic_positions = [
            compiler_cmake.find(f"add_subdirectory({name})")
            for name in ("diagnostics", "driver", "identity", "parser", "binder", "checker")
        ]
        semantic_positions = [position for position in semantic_positions if position >= 0]
        if semantic_positions and query_position > min(semantic_positions):
            errors.append(f"{COMPILER_CMAKE}: query must be configured before semantic providers")


def check_materialization_capability(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if path.suffix not in {".cc", ".h"}:
            continue
        declares_semantic = "ReuseClass::Semantic" in text or "ReuseClass::Persisted" in text
        if declares_semantic and "materializeActive(" in text:
            errors.append(
                f"{path}: Semantic or Persisted provider must not access active-handle materialization"
            )


def check_provider_registration(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if "registerQueryProvider(" not in text:
            continue
        allowed = path.parent == Path("products/zomlang/compiler/driver") or "query-adapter" in path.name
        if not allowed:
            errors.append(f"{path}: query provider registration must live in driver or an owner adapter")


def check_production_topology_integration(files: dict[Path, str], errors: list[str]) -> None:
    session = files.get(DRIVER_SESSION, "")
    query_header = files.get(QUERY_DATABASE_HEADER, "")
    query_source = files.get(QUERY_DATABASE_SOURCE, "")
    adapter = files.get(DRIVER_TOPOLOGY_ADAPTER, "")
    named_identity_query = files.get(DRIVER_NAMED_IDENTITY_QUERY, "")
    module_resolution_query = files.get(DRIVER_MODULE_RESOLUTION_QUERY, "")
    package_graph_input = files.get(DRIVER_PACKAGE_GRAPH_INPUT, "")
    source_query_input = files.get(IDENTITY_SOURCE_QUERY_INPUT, "")
    parse_source_query = files.get(PARSER_PARSE_SOURCE_QUERY, "")
    parse_source_query_verifier = files.get(PARSER_PARSE_SOURCE_QUERY_VERIFIER, "")
    adapter_test = files.get(DRIVER_TOPOLOGY_ADAPTER_TEST, "")
    session_test = files.get(DRIVER_SESSION_TEST, "")
    module_resolution_query_test = files.get(DRIVER_MODULE_RESOLUTION_QUERY_TEST, "")
    for marker, description in (
        ("basic::ThreadPool queryScheduler;", "session-owned query scheduler"),
        ("query::QueryDatabase queryDatabase;", "session-owned query database"),
        ("queryDatabase(queryScheduler)", "borrowed query scheduler injection"),
        (
            "registerIncrementalBindingQueryAdapter(queryDatabase)",
            "production query registration",
        ),
        ("beginInputTransaction()", "atomic topology input transaction"),
        (
            "transaction.set<incremental::SelectedModuleSourceInput>",
            "selected-source authority staging",
        ),
        (
            "transaction.erase<incremental::SelectedModuleSourceInput>",
            "stale selected-source authority removal",
        ),
        (
            "transaction.set<source_query::SourceSnapshotInput>",
            "source snapshot staging",
        ),
        (
            "transaction.set<source_query::CompilationOptionsInput>",
            "compilation options staging",
        ),
        (
            "transaction.set<incremental::ActiveCratesInput>",
            "active crate root staging",
        ),
        (
            "transaction.set<incremental::PackageGraphInput>",
            "package graph root staging",
        ),
        (
            "transaction.erase<incremental::PackageGraphInput>",
            "stale package graph root removal",
        ),
        (
            "transaction.set<incremental::ActiveSourcesInput>",
            "per-crate active source staging",
        ),
        (
            "transaction.set<incremental::ActiveModulesInput>",
            "per-crate active module staging",
        ),
        (
            "transaction.erase<source_query::SourceSnapshotInput>",
            "stale source snapshot removal",
        ),
        ("registries.sourceSnapshots()", "registry-owned source snapshot projection"),
        ("stagedSourceSnapshots", "complete source snapshot root state"),
        ("stagedActiveCrates", "complete per-crate membership root state"),
        ("verifySourceSnapshotInputs", "independent source snapshot root verification"),
        ("verifyCompilationOptionsInput", "independent compilation options verification"),
        ("verifyActiveCratesInput", "independent active crate root verification"),
        ("verifyPackageGraphInput", "independent package graph verification"),
        ("verifyCrateMembershipInputs", "independent per-crate membership verification"),
        (
            "verifySelectedSourceSnapshotClosure",
            "selected-source to snapshot-root closure verification",
        ),
        ("graph.sourceFile(handle)", "direct verified selected-source projection"),
        ("verifySelectedModuleSourceInputs", "independent selected-source input verification"),
        ("snapshot.get<incremental::ModuleBindingOrderQuery>", "demanded topology query"),
        (
            "incremental_binding_query::ModuleBodySyntaxQuery",
            "demanded module-body syntax query",
        ),
        (
            "incremental_binding_query::ModuleBodyProvenanceQuery",
            "demanded module-body provenance query",
        ),
        (
            "incremental_module_resolution_query::stageModuleResolutionQueryInputs(",
            "module resolution input staging",
        ),
        (
            "resolutionSnapshot.get<incremental_module_resolution_query::ResolveModuleRequestQuery>",
            "module resolution query demand",
        ),
        ("resolver.materializeQueryResolution(", "query result receipt materialization"),
        ("topologyByRequester", "single-pass topology adjacency index"),
        ("parsedByModule", "stable parsed-module index"),
        ("inventoryByModule", "stable frozen-inventory index"),
        ("bindingOutputsByModule", "stable dependency output index"),
    ):
        if marker not in session:
            errors.append(f"{DRIVER_SESSION}: missing {description}: {marker}")
    for text, path, marker, description in (
        (
            query_header,
            QUERY_DATABASE_HEADER,
            "explicit QueryDatabase(basic::ThreadPool& scheduler);",
            "mandatory scheduler injection",
        ),
        (
            query_source,
            QUERY_DATABASE_SOURCE,
            "basic::ThreadPool& scheduler;",
            "borrowed query scheduler reference",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")
    if re.search(r"\bbasic::ThreadPool\s+[A-Za-z_]\w*\s*[({]", query_source):
        errors.append(f"{QUERY_DATABASE_SOURCE}: query runtime must not own a scheduler")
    for forbidden, description in (
        ("while (completed", "batch readiness scheduler"),
        ("resolver.resolve(zc::mv(request))", "batch module resolution authority"),
        ("for (const auto& requesterKey : graph.modules())", "per-module graph edge rescan"),
        ("graph.view(handle)", "per-module complete graph-view cloning"),
        (
            "CompilationUnitQueryKey::fixed(), activeValue",
            "compilation-wide active module input authority",
        ),
    ):
        if forbidden in session:
            errors.append(f"{DRIVER_SESSION}: production binding retains {description}")
    if "context.getParallel<ModuleDependenciesInput>" not in adapter:
        errors.append(
            f"{DRIVER_TOPOLOGY_ADAPTER}: topology provider must demand dependency inputs in parallel"
        )
    if '"zom.driver.active-modules"' in adapter:
        errors.append(f"{DRIVER_TOPOLOGY_ADAPTER}: obsolete global active-module domain remains")
    for marker, description in (
        (
            'return "zom.query.requester-module-ancestry.v1"_zc;',
            "requester ancestry input domain",
        ),
        (
            'return "zom.query.module-catalog-path-bucket.v1"_zc;',
            "catalog bucket input domain",
        ),
        ('return "zom.query.resolve-module-request.v1"_zc;', "module request query domain"),
        ("context.getParallel<ModuleCatalogPathBucketInput>", "parallel exact bucket demand"),
        ("stageModuleResolutionQueryInputs(", "verified input closure staging"),
        ("transaction.set<RequesterModuleAncestryInput>", "requester ancestry staging"),
        ("transaction.set<ModuleCatalogPathBucketInput>", "catalog bucket staging"),
        ("transaction.set<DependencyAliasRootInput>", "dependency alias staging"),
        ("transaction.set<ConfiguredPreludeInput>", "configured prelude staging"),
        ("registerDerivedKind<ResolveModuleRequestQuery>()", "module request registration"),
    ):
        if marker not in module_resolution_query:
            errors.append(
                f"{DRIVER_MODULE_RESOLUTION_QUERY}: missing {description}: {marker}"
            )
    for marker, description in (
        (
            'return "zom.query.selected-module-source.v1"_zc;',
            "versioned selected-source input domain",
        ),
        (
            "registerInputKind<SelectedModuleSourceInput>()",
            "selected-source input registration",
        ),
        ("source.belongsTo(module.crate())", "selected-source crate verification"),
        ("occurrences != 1", "exact selected-source snapshot closure"),
        ('return "zom.query.active-crates.v1"_zc;', "versioned active crate input domain"),
        ("registerInputKind<ActiveCratesInput>()", "active crate input registration"),
        ('return "zom.query.active-sources.v1"_zc;', "versioned active source domain"),
        ('return "zom.query.active-modules.v1"_zc;', "versioned active module domain"),
        ("registerInputKind<ActiveSourcesInput>()", "active source input registration"),
        ("registerInputKind<ActiveModulesInput>()", "active module input registration"),
        ("return CanonicalSourceSet::decodeCanonical(bytes);", "canonical active-source set"),
        (
            "ZC_ASSERT_NONNULL(modules).modules().size() == 0",
            "non-empty active-module input admission",
        ),
        ("context.get<ActiveCratesInput>(key)", "active crate demand from module order"),
        (
            "context.getParallel<ActiveModulesInput>(activeCrates.crates())",
            "parallel per-crate active module demand",
        ),
        ("moduleBelongsToCrate", "module-to-crate membership admission"),
        (
            "return PackageRootSetQueryKey::decodeCanonical(bytes);",
            "canonical package-root-set admission",
        ),
        (
            "return CanonicalCrateSet::decodeCanonical(bytes);",
            "canonical active-crate-set admission",
        ),
        ("identity::PackageKey::decodeCanonical(decoder)", "canonical package-key admission"),
        ("identity::CrateKey::decodeCanonical(decoder)", "canonical crate-key admission"),
        (
            "ActiveCratesInput::contract() {\n"
            "  return inputContract(domain(), query::Durability::Medium);",
            "medium active crate durability",
        ),
        ("identity::ModuleKey::decodeCanonical(decoder)", "canonical module-key admission"),
        ("!decoder.finished()", "exact identity envelope consumption"),
        (
            "registerIncrementalPackageGraphQueryInput(database)",
            "package graph input registration composition",
        ),
    ):
        if marker not in adapter:
            errors.append(f"{DRIVER_TOPOLOGY_ADAPTER}: missing {description}: {marker}")
    for marker, description in (
        ('return "zom.query.source-snapshot.v1"_zc;', "versioned source snapshot input domain"),
        ("registerInputKind<SourceSnapshotInput>()", "source snapshot input registration"),
        ("kMaximumSourceSnapshotBytes = 64 * 1024 * 1024", "bounded source snapshot bytes"),
        ("auto computed = sha256(", "decoded source digest recomputation"),
        ("ZC_ASSERT_NONNULL(computed) != ZC_ASSERT_NONNULL(digest)", "source digest mismatch rejection"),
        (
            'return "zom.query.compilation-options.v1"_zc;',
            "versioned compilation options input domain",
        ),
        ("registerInputKind<CompilationOptionsInput>()", "compilation options input registration"),
        (
            "CompilationOptionsInput::contract() {\n"
            "  return inputContract(domain(), query::Durability::Medium);",
            "medium compilation options durability",
        ),
        ("validateTargetSelection", "independent target selection admission"),
        (
            "static_cast<uint8_t>(driver::package::PackagePanicStrategy::Abort)",
            "closed panic strategy admission",
        ),
        ("SourceFileKey::decodeCanonical(decoder)", "canonical source-key admission"),
    ):
        if marker not in source_query_input:
            errors.append(f"{IDENTITY_SOURCE_QUERY_INPUT}: missing {description}: {marker}")
    if (
        adapter.count("identity::SourceFileKey::decodeCanonical(decoder)")
        + source_query_input.count("SourceFileKey::decodeCanonical(decoder)")
        < 2
    ):
        errors.append(
            "canonical source-key admission must guard both "
            "source input keys and selected-source values"
        )
    if adapter.count("identity::ModuleKey::decodeCanonical(decoder)") < 2:
        errors.append(
            f"{DRIVER_TOPOLOGY_ADAPTER}: canonical module-key admission must guard both "
            "query keys and crate membership"
        )
    if adapter.count("return PackageRootSetQueryKey::decodeCanonical(bytes);") < 2:
        errors.append(
            f"{DRIVER_TOPOLOGY_ADAPTER}: package-root-set admission must guard both "
            "active-crate input keys and module-order query keys"
        )

    def verifies_before_transaction(marker: str) -> bool:
        verification_position = session.find(marker)
        if verification_position < 0:
            return False
        transaction_position = session.find(
            "auto pending = queryDatabase.beginInputTransaction()", verification_position
        )
        return transaction_position >= 0

    if not verifies_before_transaction("verifySelectedSourceSnapshotClosure"):
        errors.append(
            f"{DRIVER_SESSION}: selected-source snapshot closure must verify before transaction staging"
        )
    if not verifies_before_transaction("verifyCompilationOptionsInput"):
        errors.append(
            f"{DRIVER_SESSION}: compilation options must verify before transaction staging"
        )
    if not verifies_before_transaction("verifyActiveCratesInput"):
        errors.append(f"{DRIVER_SESSION}: active crates must verify before transaction staging")
    if not verifies_before_transaction("verifyCrateMembershipInputs"):
        errors.append(
            f"{DRIVER_SESSION}: per-crate memberships must verify before transaction staging"
        )
    if not verifies_before_transaction("verifyPackageGraphInput"):
        errors.append(f"{DRIVER_SESSION}: package graph must verify before transaction staging")
    for marker, description in (
        ('return "zom.query.parse-source.v1"_zc;', "versioned ParseSource domain"),
        ("CanonicalParsedSource::fromParsed", "query-safe parsed value admission"),
    ):
        if marker not in parse_source_query:
            errors.append(f"{PARSER_PARSE_SOURCE_QUERY}: missing {description}: {marker}")
    if "bool ParseSourceQuery::verify" not in parse_source_query_verifier:
        errors.append(
            f"{PARSER_PARSE_SOURCE_QUERY_VERIFIER}: missing independent ParseSource verification"
        )
    for marker, description in (
        ('return "zom.query.named-definition-inventory.v1"_zc;', "named definition query domain"),
        ('return "zom.query.named-implementation-inventory.v1"_zc;', "named implementation query domain"),
        ('return "zom.query.module-body-syntax.v1"_zc;', "module-body syntax query domain"),
        ('return "zom.query.module-body-provenance.v1"_zc;', "module-body provenance query domain"),
        ("StableIdentityCandidateVerifier::reconstruct", "independent stable identity reconstruction"),
        ("ModuleBodySyntaxVerifier::reconstruct", "independent module-body reconstruction"),
    ):
        if marker not in named_identity_query:
            errors.append(f"{DRIVER_NAMED_IDENTITY_QUERY}: missing {description}: {marker}")
    for marker, description in (
        ("registerDerivedKind<NamedDefinitionInventoryQuery>()", "named definition registration"),
        ("registerDerivedKind<NamedImplementationInventoryQuery>()", "named implementation registration"),
        ("registerDerivedKind<ModuleBodySyntaxQuery>()", "module-body syntax registration"),
        ("registerDerivedKind<ModuleBodyProvenanceQuery>()", "module-body provenance registration"),
    ):
        if marker not in adapter:
            errors.append(f"{DRIVER_TOPOLOGY_ADAPTER}: missing {description}: {marker}")
    for marker, description in (
        ('return "zom.query.package-graph.v1"_zc;', "versioned package graph domain"),
        ("registerInputKind<PackageGraphInput>()", "package graph input registration"),
        (
            "query::Durability::Medium",
            "medium package graph durability",
        ),
        (
            "identity::PackageDependencyEdgeKey::decodeCanonical(decoder)",
            "compositional package-edge admission",
        ),
        (
            "identity::CrateDependencyEdgeKey::decodeCanonical(decoder)",
            "compositional crate-edge admission",
        ),
        ("validateGraphClosure", "closed graph validation"),
        ("containsPackage(resolvedPackages", "package endpoint closure"),
        ("containsPackageEdge(resolvedEdges", "selected package-edge subset closure"),
        ("containsPackageEdge(selectedEdges", "crate-edge projection closure"),
        (
            "projectedPackageEdges[index] != selectedEdges[selectedIndex]",
            "complete selected-edge projection",
        ),
        ("remainingDependencies", "crate dependency cycle rejection"),
        ("kMaximumEncodedGraphBytes", "bounded package graph value"),
        ("!decoder.finished()", "exact package graph envelope consumption"),
    ):
        if marker not in package_graph_input:
            errors.append(f"{DRIVER_PACKAGE_GRAPH_INPUT}: missing {description}: {marker}")
    for marker, description in (
        (
            "Incremental binding query source snapshot codec is fixed bounded and self checking",
            "fixed and adversarial source snapshot codec regression",
        ),
        ("missingSnapshots", "missing selected-source snapshot regression"),
        ("replacedSnapshots", "replaced selected-source snapshot regression"),
        ("duplicateSnapshots", "duplicate selected-source snapshot regression"),
        (
            "source snapshots preserve equals replace changes and erase stale",
            "source snapshot replacement transaction regression",
        ),
        (
            "compilation options codec is exact bounded and closed",
            "compilation options codec regression",
        ),
        (
            "compilation options backdate equals and replace changes",
            "compilation options replacement regression",
        ),
        (
            "active crates use a canonical package root set",
            "active crate root and closed codec regression",
        ),
        (
            "active crates backdate equals and replace changes",
            "active crate replacement regression",
        ),
        (
            "per crate source and module roots are strict and replaceable",
            "per-crate membership codec and replacement regression",
        ),
        (
            "Incremental package graph input admits only a closed canonical graph",
            "closed package graph codec regression",
        ),
        (
            "Incremental package graph input backdates equals and replaces changes",
            "package graph replacement regression",
        ),
        ("outsideResolution", "selected edge outside resolution regression"),
        ("unprojected", "unprojected selected edge regression"),
        ("cycle", "package graph cycle regression"),
        ("malformedModule", "malformed canonical module-key rejection"),
        ("trailingModule", "trailing canonical module-key rejection"),
        ("malformedSource", "malformed canonical source-key rejection"),
        ("trailingSource", "trailing canonical source-value rejection"),
        ("trailingKey", "trailing canonical source-key rejection"),
    ):
        if marker not in adapter_test:
            errors.append(f"{DRIVER_TOPOLOGY_ADAPTER_TEST}: missing {description}: {marker}")
    require_marker(
        files,
        DRIVER_SESSION_TEST,
        "CompilerSession stages the complete source snapshot root with module topology",
        "driver source snapshot transaction regression",
        errors,
    )
    for marker, description in (
        (
            "ResolveModuleRequestQuery stages and demands exact candidates",
            "exact module candidate regression",
        ),
        (
            "ResolveModuleRequestQuery fails closed when exact inputs are absent",
            "missing module input regression",
        ),
        (
            "ResolveModuleRequestQuery shields unrelated catalog bucket changes",
            "unrelated bucket shielding regression",
        ),
    ):
        require_marker(
            files,
            DRIVER_MODULE_RESOLUTION_QUERY_TEST,
            marker,
            description,
            errors,
        )


def check_files(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_routing(files, errors)
    check_query_leaf(files, errors)
    check_materialization_capability(files, errors)
    check_provider_registration(files, errors)
    check_production_topology_integration(files, errors)
    return errors


def expect_failure(files: dict[Path, str], expected: str, failures: list[str]) -> None:
    errors = check_files(files)
    if not any(expected in error for error in errors):
        failures.append(f"self-test did not reject {expected}")


def self_test() -> list[str]:
    base = source_files()
    failures: list[str] = []

    mutation = dict(base)
    mutation[MANIFEST] = mutation[MANIFEST].replace(
        '      - "products/zomlang/compiler/query/**"\n', "", 1
    )
    expect_failure(mutation, "module-system query ownership", failures)

    mutation = dict(base)
    mutation[MANIFEST] = mutation[MANIFEST].replace(
        '      - "scripts/check-incremental-query-architecture.py"\n', "", 1
    )
    expect_failure(mutation, "verification gate ownership", failures)

    mutation = dict(base)
    mutation[QUERY_ROOT / "forbidden-test.cc"] = (
        '#include "zomlang/compiler/driver/compiler-session.h"\n'
    )
    expect_failure(mutation, "forbidden semantic path", failures)

    mutation = dict(base)
    mutation[COMPILER_ROOT / "fake-semantic-query-adapter.cc"] = (
        "auto reuse = ReuseClass::Semantic;\nvoid test() { materializeActive(); }\n"
    )
    expect_failure(mutation, "must not access active-handle materialization", failures)

    mutation = dict(base)
    mutation[QUERY_CMAKE] = "add_library(query STATIC query-runtime.cc)\ntarget_link_libraries(query PUBLIC zc basic driver)\n"
    expect_failure(mutation, "must not link semantic target driver", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "query::QueryDatabase queryDatabase;", "query::QueryDatabase removedDatabase;", 1
    )
    expect_failure(mutation, "session-owned query database", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "queryDatabase(queryScheduler)", "queryDatabase()", 1
    )
    expect_failure(mutation, "borrowed query scheduler injection", failures)

    mutation = dict(base)
    mutation[QUERY_DATABASE_SOURCE] = mutation[QUERY_DATABASE_SOURCE].replace(
        "basic::ThreadPool& scheduler;", "basic::ThreadPool scheduler{4};", 1
    )
    expect_failure(mutation, "query runtime must not own a scheduler", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] += "\nvoid restored_batch_scheduler() { while (completed) {} }\n"
    expect_failure(mutation, "batch readiness scheduler", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "incremental_module_resolution_query::stageModuleResolutionQueryInputs(",
        "removedModuleResolutionInputStaging(",
        1,
    )
    expect_failure(mutation, "module resolution input staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "resolutionSnapshot.get<incremental_module_resolution_query::ResolveModuleRequestQuery>",
        "resolutionSnapshot.get<RemovedModuleResolutionQuery>",
        1,
    )
    expect_failure(mutation, "module resolution query demand", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] += (
        "\nvoid restored_module_resolution_batch() { "
        "auto result = resolver.resolve(zc::mv(request)); }\n"
    )
    expect_failure(mutation, "batch module resolution authority", failures)

    mutation = dict(base)
    mutation[DRIVER_MODULE_RESOLUTION_QUERY] = mutation[DRIVER_MODULE_RESOLUTION_QUERY].replace(
        "context.getParallel<ModuleCatalogPathBucketInput>",
        "context.getSequentially<ModuleCatalogPathBucketInput>",
        1,
    )
    expect_failure(mutation, "parallel exact bucket demand", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "context.getParallel<ModuleDependenciesInput>",
        "context.getSequentially<ModuleDependenciesInput>",
    )
    expect_failure(mutation, "must demand dependency inputs in parallel", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<incremental::SelectedModuleSourceInput>",
        "transaction.set<incremental::RemovedSelectedModuleSourceInput>",
    )
    expect_failure(mutation, "selected-source authority staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifySelectedModuleSourceInputs", "removedSelectedModuleSourceInputVerifier"
    )
    expect_failure(mutation, "independent selected-source input verification", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] += "\nvoid clone_graph_per_module() { graph.view(handle); }\n"
    expect_failure(mutation, "per-module complete graph-view cloning", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "source.belongsTo(module.crate())", "true", 1
    )
    expect_failure(mutation, "selected-source crate verification", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<source_query::SourceSnapshotInput>",
        "transaction.set<source_query::RemovedSourceSnapshotInput>",
    )
    expect_failure(mutation, "source snapshot staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<source_query::CompilationOptionsInput>",
        "transaction.set<source_query::RemovedCompilationOptionsInput>",
    )
    expect_failure(mutation, "compilation options staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifyCompilationOptionsInput", "removedCompilationOptionsInputVerifier"
    )
    expect_failure(mutation, "independent compilation options verification", failures)

    mutation = dict(base)
    mutation[IDENTITY_SOURCE_QUERY_INPUT] = mutation[IDENTITY_SOURCE_QUERY_INPUT].replace(
        "CompilationOptionsInput::contract() {\n"
        "  return inputContract(domain(), query::Durability::Medium);",
        "CompilationOptionsInput::contract() {\n"
        "  return inputContract(domain(), query::Durability::Low);",
        1,
    )
    expect_failure(mutation, "medium compilation options durability", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<incremental::ActiveCratesInput>",
        "transaction.set<incremental::RemovedActiveCratesInput>",
        1,
    )
    expect_failure(mutation, "active crate root staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<incremental::PackageGraphInput>",
        "transaction.set<incremental::RemovedPackageGraphInput>",
        1,
    )
    expect_failure(mutation, "package graph root staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifyPackageGraphInput", "removedPackageGraphInputVerifier"
    )
    expect_failure(mutation, "independent package graph verification", failures)

    mutation = dict(base)
    mutation[DRIVER_PACKAGE_GRAPH_INPUT] = mutation[DRIVER_PACKAGE_GRAPH_INPUT].replace(
        "validateGraphClosure", "removedGraphClosure"
    )
    expect_failure(mutation, "closed graph validation", failures)

    mutation = dict(base)
    mutation[DRIVER_PACKAGE_GRAPH_INPUT] = mutation[DRIVER_PACKAGE_GRAPH_INPUT].replace(
        "identity::CrateDependencyEdgeKey::decodeCanonical(decoder)", "zc::none"
    )
    expect_failure(mutation, "compositional crate-edge admission", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifyActiveCratesInput", "removedActiveCratesInputVerifier"
    )
    expect_failure(mutation, "independent active crate root verification", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "return PackageRootSetQueryKey::decodeCanonical(bytes);",
        "return RemovedPackageRootSetDecoder(bytes);",
    )
    expect_failure(mutation, "canonical package-root-set admission", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "return CanonicalCrateSet::decodeCanonical(bytes);",
        "return RemovedCanonicalCrateSetDecoder(bytes);",
        1,
    )
    expect_failure(mutation, "canonical active-crate-set admission", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "ActiveCratesInput::contract() {\n"
        "  return inputContract(domain(), query::Durability::Medium);",
        "ActiveCratesInput::contract() {\n"
        "  return inputContract(domain(), query::Durability::Low);",
        1,
    )
    expect_failure(mutation, "medium active crate durability", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "transaction.set<incremental::ActiveSourcesInput>",
        "transaction.set<incremental::RemovedActiveSourcesInput>",
        1,
    )
    expect_failure(mutation, "per-crate active source staging", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifyCrateMembershipInputs", "removedCrateMembershipVerifier"
    )
    expect_failure(mutation, "independent per-crate membership verification", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "context.getParallel<ActiveModulesInput>(activeCrates.crates())",
        "removedActiveModuleGroup(activeCrates.crates())",
    )
    expect_failure(mutation, "parallel per-crate active module demand", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "ZC_ASSERT_NONNULL(modules).modules().size() == 0",
        "ZC_ASSERT_NONNULL(modules).modules().size() > kMaximumActiveModules",
        1,
    )
    expect_failure(mutation, "non-empty active-module input admission", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "identity::ModuleKey::decodeCanonical(decoder)", "zc::none"
    )
    expect_failure(mutation, "canonical module-key admission", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "identity::SourceFileKey::decodeCanonical(decoder)", "zc::none"
    )
    expect_failure(mutation, "canonical source-key admission", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifySourceSnapshotInputs", "removedSourceSnapshotInputVerifier"
    )
    expect_failure(mutation, "independent source snapshot root verification", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "verifySelectedSourceSnapshotClosure", "removedSelectedSourceSnapshotClosure"
    )
    expect_failure(mutation, "selected-source to snapshot-root closure verification", failures)

    mutation = dict(base)
    mutation[IDENTITY_SOURCE_QUERY_INPUT] = mutation[IDENTITY_SOURCE_QUERY_INPUT].replace(
        "ZC_ASSERT_NONNULL(computed) != ZC_ASSERT_NONNULL(digest)",
        "ZC_ASSERT_NONNULL(computed) == ZC_ASSERT_NONNULL(digest)",
        1,
    )
    expect_failure(mutation, "source digest mismatch rejection", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER] = mutation[DRIVER_TOPOLOGY_ADAPTER].replace(
        "registerDerivedKind<ModuleBodySyntaxQuery>()",
        "registerDerivedKind<RemovedModuleBodySyntaxQuery>()",
        1,
    )
    expect_failure(mutation, "module-body syntax registration", failures)

    mutation = dict(base)
    mutation[DRIVER_SESSION] = mutation[DRIVER_SESSION].replace(
        "incremental_binding_query::ModuleBodyProvenanceQuery",
        "incremental_binding_query::RemovedModuleBodyProvenanceQuery",
        1,
    )
    expect_failure(mutation, "demanded module-body provenance query", failures)

    mutation = dict(base)
    mutation[DRIVER_NAMED_IDENTITY_QUERY] = mutation[DRIVER_NAMED_IDENTITY_QUERY].replace(
        "StableIdentityCandidateVerifier::reconstruct",
        "StableIdentityCandidateProducer::produce",
    )
    expect_failure(mutation, "independent stable identity reconstruction", failures)

    mutation = dict(base)
    mutation[DRIVER_TOPOLOGY_ADAPTER_TEST] = mutation[DRIVER_TOPOLOGY_ADAPTER_TEST].replace(
        "replacedSnapshots", "removedReplacedSnapshots"
    )
    expect_failure(mutation, "replaced selected-source snapshot regression", failures)

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="check the live repository")
    mode.add_argument("--self-test", action="store_true", help="run adversarial gate tests")
    args = parser.parse_args()

    errors = self_test() if args.self_test else check_files(source_files())
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    if args.self_test:
        print("Incremental query architecture self-test passed")
    else:
        print("Incremental query architecture check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
