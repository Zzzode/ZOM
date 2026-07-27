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
PRODUCT_ROOT = Path("products/zomlang")
COMPILER_CMAKE = COMPILER_ROOT / "CMakeLists.txt"
QUERY_CMAKE = QUERY_ROOT / "CMakeLists.txt"
QUERY_DATABASE_HEADER = QUERY_ROOT / "query-database.h"
QUERY_DATABASE_SOURCE = QUERY_ROOT / "query-database.cc"
DRIVER_SESSION = COMPILER_ROOT / "driver/compiler-session.cc"
DRIVER_TOPOLOGY_ADAPTER = COMPILER_ROOT / "driver/incremental-binding-query-adapter.cc"
DRIVER_AUTHORITY_QUERY = COMPILER_ROOT / "driver/active-definition-authority-query.cc"
DRIVER_AUTHORITY_SESSION = COMPILER_ROOT / "driver/active-definition-authority-session.cc"
DRIVER_NAMED_IDENTITY_QUERY = COMPILER_ROOT / "driver/named-identity-inventory-query.cc"
DRIVER_NAMED_ITEM_QUERY = COMPILER_ROOT / "driver/named-item-query.cc"
DRIVER_OWNER_BODY_QUERY = COMPILER_ROOT / "driver/owner-body-query.cc"
DRIVER_MODULE_RESOLUTION_QUERY = COMPILER_ROOT / "driver/incremental-module-resolution-query.cc"
DRIVER_MODULE_GRAPH_INPUT = COMPILER_ROOT / "driver/module-graph-query-input.cc"
DRIVER_MODULE_GRAPH_QUERY = COMPILER_ROOT / "driver/module-graph-query.cc"
BINDER_GRAPH_BRIDGE = COMPILER_ROOT / "binder/binding-input.cc"
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
DRIVER_MODULE_GRAPH_QUERY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/module-graph-query-input-test.cc"
)
QUERY_DATABASE_TEST = Path(
    "products/zomlang/tests/unittests/compiler/query/query-database-test.cc"
)
QUERY_CAPABILITY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/query/query-capability-test.cc"
)
ACTIVE_IDENTITY_MATERIALIZATION = (
    COMPILER_ROOT / "driver/active-identity-materialization.h"
)
CORE_LIBRARY_QUERY_PROVIDER_HEADER = (
    COMPILER_ROOT / "driver/core-library-query-provider.h"
)
CORE_LIBRARY_QUERY_PROVIDER_SOURCE = (
    COMPILER_ROOT / "driver/core-library-query-provider.cc"
)
DRIVER_AUTHORITY_SESSION_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/active-definition-authority-session-test.cc"
)
BINDER_MODULE_BODY_SYNTAX_TEST = Path(
    "products/zomlang/tests/unittests/compiler/binder/module-body-syntax-test.cc"
)
PERFORMANCE_RUNNER = Path("scripts/run-incremental-query-benchmarks.py")
PERFORMANCE_CORPUS = Path(
    "products/zomlang/tests/performance/incremental-query-corpus.json"
)
PERFORMANCE_BASELINE = Path(
    "products/zomlang/tests/performance/incremental-query-baseline.json"
)
MANIFEST = Path(".agents/subagents/manifest.yaml")
ROUTING = Path(".agents/subagents/README.md")
TASK_ROUTER = Path(".agents/subagents/task-router.md")
MODULE_OWNER = Path(".agents/subagents/module-system.md")
VERIFICATION_OWNER = Path(".agents/subagents/verification.md")
AGENTS = Path("AGENTS.md")

ROUTING_FILES = (MANIFEST, ROUTING, TASK_ROUTER, MODULE_OWNER, VERIFICATION_OWNER, AGENTS)

MATERIALIZATION_CAPABILITY_TOKENS = (
    "ActiveMaterialization<",
    "ActiveMembership<",
    "ActiveMaterializerPermission<",
    ".materializeActive(",
    "semanticContextResources()",
    "SnapshotCapabilityArena::context",
)

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
    for directory, child_directories, names in os.walk(ROOT / PRODUCT_ROOT):
        child_directories[:] = [name for name in child_directories if name != "vendor"]
        for name in names:
            path = Path(directory) / name
            if path.suffix not in {".cc", ".h"} or path.is_relative_to(ROOT / COMPILER_ROOT):
                continue
            text = path.read_text(encoding="utf-8")
            if any(token in text for token in MATERIALIZATION_CAPABILITY_TOKENS):
                files[relative(path)] = text
    for path in (
        DRIVER_TOPOLOGY_ADAPTER_TEST,
        DRIVER_SESSION_TEST,
        DRIVER_MODULE_RESOLUTION_QUERY_TEST,
        DRIVER_MODULE_GRAPH_QUERY_TEST,
        QUERY_DATABASE_TEST,
        QUERY_CAPABILITY_TEST,
        DRIVER_AUTHORITY_SESSION_TEST,
        BINDER_MODULE_BODY_SYNTAX_TEST,
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
    for path in (PERFORMANCE_RUNNER, PERFORMANCE_CORPUS, PERFORMANCE_BASELINE):
        if not (ROOT / path).is_file():
            errors.append(f"{path}: missing RFC 0017 performance infrastructure")


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


def check_input_probe_contract(files: dict[Path, str], errors: list[str]) -> None:
    query_header = files.get(QUERY_DATABASE_HEADER, "")
    query_source = files.get(QUERY_DATABASE_SOURCE, "")
    query_types = files.get(QUERY_ROOT / "query-types.h", "")
    query_test = files.get(QUERY_DATABASE_TEST, "")
    for text, path, marker, description in (
        (
            query_types,
            QUERY_ROOT / "query-types.h",
            "enum class InputProbeObservation",
            "closed input probe observation",
        ),
        (
            query_types,
            QUERY_ROOT / "query-types.h",
            "inputProbeObservation() const noexcept",
            "inspectable probe dependency metadata",
        ),
        (
            query_header,
            QUERY_DATABASE_HEADER,
            "TypedQueryResult<typename Spec::Value> probeInput",
            "typed input probe API",
        ),
        (
            query_source,
            QUERY_DATABASE_SOURCE,
            "dependency.inputProbeObservation()",
            "presence-aware dependency validation",
        ),
        (
            query_source,
            QUERY_DATABASE_SOURCE,
            "currentObservation != observation",
            "presence transition invalidation",
        ),
        (
            query_source,
            QUERY_DATABASE_SOURCE,
            "descriptor.contract.inputDurability()",
            "absent probe durability",
        ),
        (
            query_test,
            QUERY_DATABASE_TEST,
            "InputProbeTracksPresenceWithoutTombstonesOrContextPoisoning",
            "native input probe regression",
        ),
        (
            query_test,
            QUERY_DATABASE_TEST,
            "QueryRuntimeFailure::InvalidKeyEncoding",
            "malformed probe key regression",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")
    if "probeInputParallel" in query_header or "probeInputParallel" in query_source:
        errors.append(f"{QUERY_DATABASE_HEADER}: parallel optional input probing is forbidden")


def check_materialization_capability(files: dict[Path, str], errors: list[str]) -> None:
    specialization_allowlist = {QUERY_CAPABILITY_TEST}
    call_allowlist = {QUERY_CAPABILITY_TEST}

    for path, text in sorted(files.items()):
        if path.suffix not in {".cc", ".h"}:
            continue

        specializes_materialization = re.search(
            r"struct\s+ActiveMaterialization\s*<", text
        )
        specializes_membership = re.search(r"struct\s+ActiveMembership\s*<", text)
        specializes_permission = re.search(
            r"struct\s+ActiveMaterializerPermission\s*<", text
        )
        if (
            specializes_materialization
            or specializes_membership
            or specializes_permission
        ) and path not in specialization_allowlist:
            errors.append(
                f"{path}: active materialization specialization is outside the closed allowlist"
            )

        materialization_calls = len(re.findall(r"\.materializeActive\s*\(", text))
        if materialization_calls and path not in call_allowlist:
            errors.append(
                f"{path}: active materialization call is outside approved capability providers"
            )

        if (
            "semanticContextResources()" in text
            and path not in {QUERY_DATABASE_HEADER, QUERY_DATABASE_SOURCE}
        ):
            errors.append(
                f"{path}: semantic context capability resources escape the query runtime"
            )
        if "SnapshotCapabilityArena::context" in text:
            errors.append(f"{path}: snapshot capability arena must not expose query context")

    header = files.get(QUERY_DATABASE_HEADER, "")
    capability_test = files.get(QUERY_CAPABILITY_TEST, "")
    for path, text, marker, description in (
        (
            QUERY_DATABASE_HEADER,
            header,
            "ActiveMaterializerPermission<Spec>::allowed",
            "descriptor-bound materializer permission",
        ),
        (
            QUERY_DATABASE_HEADER,
            header,
            "context.activeMaterializationReady()",
            "final current sealed snapshot barrier",
        ),
        (
            QUERY_DATABASE_HEADER,
            header,
            "ActiveMembership<Key>::demand(context, key, authority...)",
            "tracked active-membership demand",
        ),
        (
            QUERY_DATABASE_HEADER,
            header,
            "ActiveMaterialization<Key>::materialize(context.semanticContextResources(), key)",
            "post-membership materialization",
        ),
        (
            QUERY_CAPABILITY_TEST,
            capability_test,
            "UnpermittedMaterializingCapabilityQuery",
            "unpermitted materializer regression",
        ),
        (
            QUERY_CAPABILITY_TEST,
            capability_test,
            "RejectsActiveMaterializationBeforeAndOutsideTheFinalBarrier",
            "final snapshot barrier regression",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")


def check_provider_registration(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if "registerQueryProvider(" not in text:
            continue
        allowed = path.parent == Path("products/zomlang/compiler/driver") or "query-adapter" in path.name
        if not allowed:
            errors.append(f"{path}: query provider registration must live in driver or an owner adapter")


def check_active_definition_authority(files: dict[Path, str], errors: list[str]) -> None:
    authority = files.get(DRIVER_AUTHORITY_QUERY, "")
    session = files.get(DRIVER_AUTHORITY_SESSION, "")
    named_item = files.get(DRIVER_NAMED_ITEM_QUERY, "")
    compiler_session = files.get(DRIVER_SESSION, "")
    test = files.get(DRIVER_AUTHORITY_SESSION_TEST, "")
    for text, path, marker, description in (
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            'return "zom.query.active-definition-authority"_zc;',
            "definition authority input domain",
        ),
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            'return "zom.query.active-definition-authority-ready"_zc;',
            "authority readiness input domain",
        ),
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            "DefinitionIdentityRecord::decodeCanonical(bytes)",
            "complete authority record decoding",
        ),
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            "query::Durability::Low",
            "low authority durability",
        ),
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            "registerInputKind<ActiveDefinitionAuthorityReadyInput>()",
            "readiness input registration",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "probeInput<ActiveDefinitionAuthorityReadyInput>",
            "readiness probe before base mutation",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "transaction.erase<ActiveDefinitionAuthorityReadyInput>",
            "readiness removal in the first base transaction",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "snapshot.get<module_graph_query::ModuleGraphQuery>",
            "complete stable graph reconstruction",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "snapshot.get<module_graph_query::ModuleGraphSccQuery>",
            "stable graph SCC closure comparison",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "snapshot.get<NamedDefinitionInventoryQuery>",
            "complete named inventory demand",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "transaction.erase<ActiveDefinitionAuthorityInput>",
            "stale authority erasure",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "transaction.set<ActiveDefinitionAuthorityReadyInput>",
            "atomic readiness restoration",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "keyLedgerField = zc::mv(nextKeyLedger);",
            "non-failing post-commit ledger publication",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            'return "zom.query.named-item-syntax"_zc;',
            "named-item syntax query domain",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            'return "zom.query.named-item-provenance"_zc;',
            "named-item provenance query domain",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "probeInput<ActiveDefinitionAuthorityInput>",
            "tracked definition authority recovery",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "containsAuthority",
            "exact owning inventory membership",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "providerRoot",
            "provider source-order authority occurrence selection",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "verifierRoot",
            "independent source-order authority occurrence selection",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "ModuleBodySyntaxVerifier::reconstructNamedItem",
            "independent named-item reconstruction",
        ),
        (
            compiler_session,
            DRIVER_SESSION,
            "activeDefinitionAuthority.beginBaseMutation(queryDatabase)",
            "authority-invalidating base transactions",
        ),
        (
            compiler_session,
            DRIVER_SESSION,
            "activeDefinitionAuthority.refresh(",
            "production authority refresh",
        ),
        (
            compiler_session,
            DRIVER_SESSION,
            "demandNamedItemQueries()",
            "ready-snapshot named-item demand",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "invalidates and atomically refreshes readiness",
            "atomic refresh regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "fails closed and erases stale keys on retry",
            "failed refresh and stale-ledger retry regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "readinessReads == 0",
            "conditional positive-path readiness regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "differential edits are deterministic across workers",
            "worker-count differential regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}})",
            "required differential worker matrix",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "isolates modules shrinks sets and tracks moves",
            "cross-module shrink and move regression",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")
    for path, text in ((DRIVER_AUTHORITY_SESSION, session), (DRIVER_NAMED_ITEM_QUERY, named_item)):
        for forbidden in ("identityRegistries", "CompilerSession", "moduleGraph"):
            if forbidden in text:
                errors.append(f"{path}: tracked authority path must not read {forbidden}")
    if named_item.count("probeInput<ActiveDefinitionAuthorityReadyInput>") != 4:
        errors.append(
            f"{DRIVER_NAMED_ITEM_QUERY}: readiness must be read only by absent or contradictory "
            "provider and verifier branches"
        )
    worker_matrix = "for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}})"
    if test.count(worker_matrix) != 2:
        errors.append(
            f"{DRIVER_AUTHORITY_SESSION_TEST}: must retain both required differential worker matrices"
        )


def check_owner_body_projection(files: dict[Path, str], errors: list[str]) -> None:
    owner_query = files.get(DRIVER_OWNER_BODY_QUERY, "")
    adapter = files.get(DRIVER_TOPOLOGY_ADAPTER, "")
    query_test = files.get(DRIVER_AUTHORITY_SESSION_TEST, "")
    codec_test = files.get(BINDER_MODULE_BODY_SYNTAX_TEST, "")
    for text, path, marker, description in (
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            'return "zom.query.module-body-owners"_zc;',
            "module-body owner inventory query domain",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            'return "zom.query.owner-body-syntax"_zc;',
            "owner-body syntax query domain",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            'return "zom.query.owner-body-provenance"_zc;',
            "owner-body provenance query domain",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.get<NamedDefinitionInventoryQuery>(ZC_ASSERT_NONNULL(stableModule))",
            "owner inventory dependency",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.getParallel<NamedItemSyntaxQuery>",
            "canonical parallel named-item dependency group",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.get<ModuleBodySyntaxQuery>",
            "module-owner syntax alternative",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.get<NamedItemSyntaxQuery>",
            "definition-owner syntax alternative",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.get<OwnerBodySyntaxQuery>(key)",
            "provenance syntax closure dependency",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.getCapability<ModuleBodyProvenanceQuery>",
            "module-owner provenance alternative",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "context.getCapability<NamedItemProvenanceQuery>",
            "definition-owner provenance alternative",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "providerExecutableRoot",
            "provider executable-root selection",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "verifierExecutableRoot",
            "independent verifier executable-root selection",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "providerProvenanceMatches",
            "provider provenance coverage reconstruction",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "verifierProvenanceMatches",
            "independent verifier provenance coverage reconstruction",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "return retainedSemanticContract(domain());",
            "retained semantic owner inventory contract",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "return evictableSemanticContract(domain());",
            "evictable semantic owner syntax contract",
        ),
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "return revisionLocalContract(domain());",
            "revision-local owner provenance contract",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerDerivedKind<ModuleBodyOwnersQuery>()",
            "module-body owners registration",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerDerivedKind<OwnerBodySyntaxQuery>()",
            "owner-body syntax registration",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerRevisionLocalCapabilityKind<OwnerBodyProvenanceQuery>()",
            "retained owner-body provenance capability registration",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body projection records exact alternative dependencies",
            "exact owner projection dependency regression",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body projections are deterministic across workers",
            "owner projection worker determinism regression",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "parallelNamedItemGroups == 2",
            "provider and verifier parallel dependency regression",
        ),
        (
            codec_test,
            BINDER_MODULE_BODY_SYNTAX_TEST,
            "Owner body codecs preserve canonical records and reject malformed inventories",
            "owner projection codec adversaries",
        ),
        (
            codec_test,
            BINDER_MODULE_BODY_SYNTAX_TEST,
            "duplicateModule",
            "duplicate module-owner rejection",
        ),
        (
            codec_test,
            BINDER_MODULE_BODY_SYNTAX_TEST,
            "missingModule",
            "missing module-owner rejection",
        ),
        (
            codec_test,
            BINDER_MODULE_BODY_SYNTAX_TEST,
            "foreignModule",
            "foreign module-owner rejection",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")
    if owner_query.count("context.getParallel<NamedItemSyntaxQuery>") != 2:
        errors.append(
            f"{DRIVER_OWNER_BODY_QUERY}: provider and verifier must each demand one parallel "
            "named-item group"
        )
    for forbidden in (
        "ParseSourceQuery",
        "SelectedModuleSourceInput",
        "UserPackageActiveSourcesInput",
        "ActiveSourcesQuery",
        "CompilerSession",
        "identityRegistries",
        "moduleGraph",
        "materializeActive(",
    ):
        if forbidden in owner_query:
            errors.append(f"{DRIVER_OWNER_BODY_QUERY}: owner projection must not read {forbidden}")


def check_production_topology_integration(files: dict[Path, str], errors: list[str]) -> None:
    session = files.get(DRIVER_SESSION, "")
    adapter = files.get(DRIVER_TOPOLOGY_ADAPTER, "")
    graph_input = files.get(DRIVER_MODULE_GRAPH_INPUT, "")
    graph_query = files.get(DRIVER_MODULE_GRAPH_QUERY, "")
    graph_bridge = files.get(BINDER_GRAPH_BRIDGE, "")
    resolution_query = files.get(DRIVER_MODULE_RESOLUTION_QUERY, "")
    source_query = files.get(IDENTITY_SOURCE_QUERY_INPUT, "")
    parse_query = files.get(PARSER_PARSE_SOURCE_QUERY, "")
    parse_verifier = files.get(PARSER_PARSE_SOURCE_QUERY_VERIFIER, "")
    named_identity = files.get(DRIVER_NAMED_IDENTITY_QUERY, "")
    named_item = files.get(DRIVER_NAMED_ITEM_QUERY, "")
    owner_body = files.get(DRIVER_OWNER_BODY_QUERY, "")
    package_graph = files.get(DRIVER_PACKAGE_GRAPH_INPUT, "")
    graph_test = files.get(DRIVER_MODULE_GRAPH_QUERY_TEST, "")

    for marker, description in (
        ("basic::ThreadPool queryScheduler;", "session-owned query scheduler"),
        ("query::QueryDatabase queryDatabase;", "session-owned query database"),
        ("registerIncrementalBindingQueryAdapter(queryDatabase)", "binding query registration"),
        ("module_graph_query::registerModuleGraphQueries(queryDatabase)", "graph input registration"),
        (
            "module_graph_query::registerStableModuleGraphQueries(queryDatabase)",
            "stable graph registration",
        ),
        (
            "activeDefinitionAuthority.beginBaseMutation(queryDatabase)",
            "authority-invalidating base transaction",
        ),
        (
            "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
            "atomic graph input transaction",
        ),
        (
            "graph_query::ModuleGraphInputTransactionAuthority authority",
            "authority-backed graph input verification",
        ),
        ("moduleGraphInputLedger", "complete graph input ledger"),
        ("stagedCompilationRoots", "complete stable root key"),
        (
            "authorityStagingSnapshotValue.get<graph_query::ModuleGraphQuery>",
            "final stable graph demand",
        ),
        (
            "authorityStagingSnapshotValue.get<graph_query::ModuleGraphSccQuery>",
            "final stable SCC demand",
        ),
        ("binder::VerifiedModuleGraphBuilder::build(", "final Binder graph materialization"),
        ("parsedByModule", "stable parsed-module index"),
        ("inventoryByModule", "stable frozen-inventory index"),
        ("bindingOutputsByModule", "stable dependency output index"),
    ):
        if marker not in session:
            errors.append(f"{DRIVER_SESSION}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.selected-module-catalog"_zc;', "selected catalog input domain"),
        ('return "zom.query.selected-module-source"_zc;', "selected source query domain"),
        ('return "zom.query.active-modules"_zc;', "active modules query domain"),
        ('return "zom.query.module-dependency-sites"_zc;', "dependency sites query domain"),
        ('return "zom.query.module-dependency-requests"_zc;', "dependency requests query domain"),
        ('return "zom.query.module-dependencies"_zc;', "module dependencies query domain"),
        ("VerifiedModuleGraphInputTransaction::prepare(", "verified graph input transaction"),
        (
            "ModuleGraphInputTransactionVerifier::verify(",
            "independent graph input transaction verifier",
        ),
        ("reconstructVerifierSites(", "independent parsed-site reconstruction"),
        ("authority.resolver.catalog()", "frozen resolver catalog enumeration"),
        ("authority.coreInputs.projections()", "verified core projection enumeration"),
        ("registerInputKind<SelectedModuleCatalogInput>()", "catalog input registration"),
        ("registerInputKind<ModuleDependencySiteInput>()", "dependency site input registration"),
        ("registerDerivedKind<SelectedModuleSourceQuery>()", "selected source registration"),
        ("registerDerivedKind<ActiveModulesQuery>()", "active modules registration"),
        (
            "registerDerivedKind<ModuleDependencyRequestsQuery>()",
            "dependency request registration",
        ),
        ("registerDerivedKind<ModuleDependenciesQuery>()", "dependency registration"),
        ("SelectedModuleSourceQuery::provide(", "selected source provider"),
        ("SelectedModuleSourceQuery::verify(", "selected source verifier"),
        ("ActiveModulesQuery::provide(", "active modules provider"),
        ("ActiveModulesQuery::verify(", "active modules verifier"),
        ("ModuleDependenciesQuery::provide(", "module dependencies provider"),
        ("ModuleDependenciesQuery::verify(", "module dependencies verifier"),
        ("rebuildVerifierRequests(", "independent dependency request reconstruction"),
        ("resolveVerifierDependencies(", "independent dependency resolution"),
    ):
        if marker not in graph_input:
            errors.append(f"{DRIVER_MODULE_GRAPH_INPUT}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.module-graph"_zc;', "stable module graph domain"),
        ('return "zom.query.module-graph-scc"_zc;', "stable SCC domain"),
        ("ModuleGraphQuery::provide(", "stable graph provider"),
        ("ModuleGraphQuery::verify(", "independent stable graph verifier"),
        ("evaluateVerifierGraph(", "independent stable graph reconstruction"),
        ("ModuleGraphSccQuery::provide(", "Tarjan SCC provider"),
        ("ModuleGraphSccQuery::verify(", "Kosaraju SCC verifier"),
        ("verifierOrderComponents(", "independent SCC ordering"),
        ("registerDerivedKind<ModuleGraphQuery>()", "stable graph registration"),
        ("registerDerivedKind<ModuleGraphSccQuery>()", "stable SCC registration"),
    ):
        if marker not in graph_query:
            errors.append(f"{DRIVER_MODULE_GRAPH_QUERY}: missing {description}: {marker}")

    for marker, description in (
        ("reconstructVerifierContextRoots(", "independent final root reconstruction"),
        ("demandVerifierActiveModules(", "independent final active-module demand"),
        ("rebuildVerifierSite(", "independent final syntax-site reconstruction"),
        ("verifierGraphProjectionMatches(", "independent final edge projection"),
        ("recomputeVerifierGraphRevision(", "independent final revision reconstruction"),
        ("input.registries.sourceFiles().find(", "independent final source registry demand"),
        (
            "input.finalSnapshot.getCapability<parser::ParseSourceQuery>",
            "independent final parser capability demand",
        ),
        ("consumedSites[", "independent final site-consumption proof"),
    ):
        if marker not in graph_bridge:
            errors.append(f"{BINDER_GRAPH_BRIDGE}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.requester-module-ancestry"_zc;', "requester ancestry input domain"),
        ('return "zom.query.module-catalog-path-bucket"_zc;', "catalog bucket input domain"),
        ('return "zom.query.resolve-module-request"_zc;', "resolution query domain"),
        ("context.getParallel<ModuleCatalogPathBucketInput>", "parallel exact bucket demand"),
        ("stageModuleResolutionQueryInputs(", "resolver input staging"),
        ("registerDerivedKind<ResolveModuleRequestQuery>()", "resolution query registration"),
    ):
        if marker not in resolution_query:
            errors.append(f"{DRIVER_MODULE_RESOLUTION_QUERY}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.active-crates"_zc;', "active crates query domain"),
        ('return "zom.query.active-sources"_zc;', "active sources query domain"),
        ("registerDerivedKind<ActiveCratesQuery>()", "active crates registration"),
        ("registerDerivedKind<ActiveSourcesQuery>()", "active sources registration"),
        ("registerDerivedKind<NamedDefinitionInventoryQuery>()", "named definition registration"),
        ("registerDerivedKind<ModuleBodySyntaxQuery>()", "module body registration"),
    ):
        if marker not in adapter:
            errors.append(f"{DRIVER_TOPOLOGY_ADAPTER}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.source-snapshot"_zc;', "source snapshot domain"),
        ("registerInputKind<SourceSnapshotInput>()", "source snapshot registration"),
        ('return "zom.query.compilation-options"_zc;', "compilation options domain"),
        ("registerInputKind<CompilationOptionsInput>()", "compilation options registration"),
        ("SourceFileKey::decodeCanonical(decoder)", "canonical source key admission"),
    ):
        if marker not in source_query:
            errors.append(f"{IDENTITY_SOURCE_QUERY_INPUT}: missing {description}: {marker}")

    for marker, description in (
        ('return "zom.query.parse-source"_zc;', "ParseSource domain"),
        ("CanonicalParsedSource::fromParsed", "query-safe parsed value admission"),
        (
            "registerRevisionLocalCapabilityKind<ParseSourceQuery>()",
            "parser capability registration",
        ),
    ):
        if marker not in parse_query:
            errors.append(f"{PARSER_PARSE_SOURCE_QUERY}: missing {description}: {marker}")
    if "zc::Maybe<zc::Array<uint8_t>> ParseSourceQuery::verify" not in parse_verifier:
        errors.append(f"{PARSER_PARSE_SOURCE_QUERY_VERIFIER}: missing independent parser verifier")

    for text, path, markers in (
        (
            named_identity,
            DRIVER_NAMED_IDENTITY_QUERY,
            (
                'return "zom.query.named-definition-inventory"_zc;',
                "StableIdentityCandidateVerifier::reconstruct",
            ),
        ),
        (named_item, DRIVER_NAMED_ITEM_QUERY, ("NamedItemProvenanceQuery::verify",)),
        (owner_body, DRIVER_OWNER_BODY_QUERY, ("OwnerBodyProvenanceQuery::verify",)),
        (
            package_graph,
            DRIVER_PACKAGE_GRAPH_INPUT,
            (
                'return "zom.query.package-graph"_zc;',
                "registerInputKind<PackageGraphInput>()",
                "validateGraphClosure",
            ),
        ),
    ):
        for marker in markers:
            if marker not in text:
                errors.append(f"{path}: missing required query architecture marker: {marker}")

    for marker, description in (
        (
            "Module graph input transaction commits its complete authority exactly once",
            "atomic graph transaction regression",
        ),
        (
            "Independent module graph input verifier rejects incomplete core roots",
            "independent graph transaction verifier regression",
        ),
        (
            "Stable graph and independent SCC queries cover the complete core root",
            "complete stable graph regression",
        ),
        (
            "Tarjan provider and Kosaraju verifier agree after a tracked cycle mutation",
            "independent SCC regression",
        ),
        (
            "Nested dependency failure globally precedes an earlier outside edge",
            "stable graph failure-precedence regression",
        ),
        (
            "Independent transaction verifier rejects sites not backed by parsed syntax",
            "parsed-site mutation regression",
        ),
    ):
        if marker not in graph_test:
            errors.append(f"{DRIVER_MODULE_GRAPH_QUERY_TEST}: missing {description}: {marker}")

    forbidden = (
        "SelectedModuleSourceInput",
        "ActiveModulesInput",
        "ModuleDependenciesInput",
        "ModuleBindingOrderQuery",
        "ModuleGraphCandidate",
        "ModuleGraphVerifier",
        "VerifiedStructuralResolutionReceipt",
        "materializeQueryResolution",
        "environmentRevision",
        "topologyByRequester",
    )
    for path, text in files.items():
        if not str(path).startswith("products/zomlang/"):
            continue
        for token in forbidden:
            if re.search(rf"\b{re.escape(token)}\b", text):
                errors.append(f"{path}: obsolete topology authority remains: {token}")

def check_files(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_routing(files, errors)
    check_query_leaf(files, errors)
    check_input_probe_contract(files, errors)
    check_materialization_capability(files, errors)
    check_provider_registration(files, errors)
    check_active_definition_authority(files, errors)
    check_owner_body_projection(files, errors)
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
    mutation[QUERY_ROOT / "forbidden-test.cc"] = (
        '#include "zomlang/compiler/driver/compiler-session.h"\n'
    )
    expect_failure(mutation, "forbidden semantic path", failures)

    mutation = dict(base)
    mutation[COMPILER_ROOT / "driver/unapproved-materialization.h"] = (
        "template <> struct ActiveMaterialization<UnapprovedKey> {};\n"
    )
    expect_failure(mutation, "outside the closed allowlist", failures)

    mutation = dict(base)
    mutation[COMPILER_ROOT / "driver/unapproved-materialization.cc"] = (
        "void provide(Context& context, const Key& key) { context.materializeActive(key); }\n"
    )
    expect_failure(mutation, "outside approved capability providers", failures)

    mutation = dict(base)
    mutation[ACTIVE_IDENTITY_MATERIALIZATION] = (
        "template <> struct ActiveMaterialization<ResurrectedKey> {};\n"
    )
    expect_failure(mutation, "outside the closed allowlist", failures)

    mutation = dict(base)
    mutation[CORE_LIBRARY_QUERY_PROVIDER_SOURCE] = (
        mutation.get(CORE_LIBRARY_QUERY_PROVIDER_SOURCE, "")
        + "\ntemplate <> struct ActiveMembership<PreauthorizedKey> {};\n"
    )
    expect_failure(mutation, "outside the closed allowlist", failures)

    mutation = dict(base)
    mutation[PRODUCT_ROOT / "utils/unapproved-materialization.cc"] = (
        "template <> struct ActiveMaterializerPermission<OutsideCompilerQuery> {};\n"
        "void provide(Context& context, const Key& key) { context.materializeActive(key); }\n"
    )
    expect_failure(mutation, "outside the closed allowlist", failures)
    expect_failure(mutation, "outside approved capability providers", failures)

    mutation = dict(base)
    mutation[QUERY_DATABASE_HEADER] = mutation[QUERY_DATABASE_HEADER].replace(
        "ActiveMembership<Key>::demand(context, key, authority...)",
        "REMOVED_ACTIVE_MEMBERSHIP_DEMAND",
        1,
    )
    expect_failure(mutation, "tracked active-membership demand", failures)

    for path, marker, description in (
        (
            DRIVER_SESSION,
            "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
            "atomic graph input transaction",
        ),
        (
            DRIVER_SESSION,
            "binder::VerifiedModuleGraphBuilder::build(",
            "final Binder graph materialization",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "registerInputKind<SelectedModuleCatalogInput>()",
            "catalog input registration",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "SelectedModuleSourceQuery::verify(",
            "selected source verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "ModuleGraphInputTransactionVerifier::verify(",
            "independent graph input transaction verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "reconstructVerifierSites(",
            "independent parsed-site reconstruction",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "rebuildVerifierRequests(",
            "independent dependency request reconstruction",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "resolveVerifierDependencies(",
            "independent dependency resolution",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "ModuleDependenciesQuery::verify(",
            "module dependencies verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY,
            "ModuleGraphQuery::verify(",
            "independent stable graph verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY,
            "evaluateVerifierGraph(",
            "independent stable graph reconstruction",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY,
            "ModuleGraphSccQuery::verify(",
            "Kosaraju SCC verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY,
            "verifierOrderComponents(",
            "independent SCC ordering",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "reconstructVerifierContextRoots(",
            "independent final root reconstruction",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "rebuildVerifierSite(",
            "independent final syntax-site reconstruction",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "input.finalSnapshot.getCapability<parser::ParseSourceQuery>",
            "independent final parser capability demand",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "verifierGraphProjectionMatches(",
            "independent final edge projection",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "recomputeVerifierGraphRevision(",
            "independent final revision reconstruction",
        ),
        (
            DRIVER_MODULE_RESOLUTION_QUERY,
            "context.getParallel<ModuleCatalogPathBucketInput>",
            "parallel exact bucket demand",
        ),
        (
            PARSER_PARSE_SOURCE_QUERY_VERIFIER,
            "zc::Maybe<zc::Array<uint8_t>> ParseSourceQuery::verify",
            "independent parser verifier",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY_TEST,
            "Tarjan provider and Kosaraju verifier agree after a tracked cycle mutation",
            "independent SCC regression",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY_TEST,
            "Independent module graph input verifier rejects incomplete core roots",
            "independent graph transaction verifier regression",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY_TEST,
            "Nested dependency failure globally precedes an earlier outside edge",
            "stable graph failure-precedence regression",
        ),
        (
            DRIVER_MODULE_GRAPH_QUERY_TEST,
            "Independent transaction verifier rejects sites not backed by parsed syntax",
            "parsed-site mutation regression",
        ),
    ):
        mutation = dict(base)
        mutation[path] = mutation[path].replace(marker, "REMOVED_ARCHITECTURE_MARKER")
        expect_failure(mutation, description, failures)

    mutation = dict(base)
    mutation[COMPILER_ROOT / "driver/obsolete-topology.cc"] = (
        "void obsolete() { ModuleBindingOrderQuery query; }\n"
    )
    expect_failure(mutation, "obsolete topology authority remains", failures)

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
