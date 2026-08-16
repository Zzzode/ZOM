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
DRIVER_ACTIVE_IDENTITY_MEMBERSHIP = (
    COMPILER_ROOT / "driver/active-identity-membership-query.cc"
)
DRIVER_MATERIALIZED_MODULE_GRAPH = (
    COMPILER_ROOT / "driver/materialized-module-graph-query.cc"
)
DRIVER_NAMED_IDENTITY_QUERY = COMPILER_ROOT / "driver/named-identity-inventory-query.cc"
DRIVER_NAMED_ITEM_QUERY = COMPILER_ROOT / "driver/named-item-query.cc"
DRIVER_OWNER_BODY_QUERY = COMPILER_ROOT / "driver/owner-body-query.cc"
DRIVER_MODULE_RESOLUTION_QUERY = COMPILER_ROOT / "driver/incremental-module-resolution-query.cc"
DRIVER_MODULE_GRAPH_INPUT = COMPILER_ROOT / "driver/module-graph-query-input.cc"
DRIVER_MODULE_GRAPH_QUERY = COMPILER_ROOT / "driver/module-graph-query.cc"
BINDER_GRAPH_BRIDGE = DRIVER_MATERIALIZED_MODULE_GRAPH
DRIVER_PACKAGE_GRAPH_INPUT = COMPILER_ROOT / "driver/incremental-package-graph-query-input.cc"
IDENTITY_SOURCE_QUERY_INPUT = COMPILER_ROOT / "identity/source-query-input.cc"
PARSER_PARSE_SOURCE_QUERY = COMPILER_ROOT / "parser/query/parse-source-query.cc"
PARSER_PARSE_SOURCE_QUERY_VERIFIER = COMPILER_ROOT / "parser/query/parse-source-query-verifier.cc"
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
    COMPILER_ROOT / "driver/core/query.h"
)
CORE_LIBRARY_QUERY_PROVIDER_SOURCE = (
    COMPILER_ROOT / "driver/core/query.cc"
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
    query_links = re.findall(
        r"target_link_libraries\s*\(\s*query\b(.*?)\)",
        query_cmake,
        flags=re.DOTALL,
    )
    for link_block in query_links:
        for target in QUERY_FORBIDDEN_LINK_TARGETS:
            if re.search(rf"\b{re.escape(target)}\b", link_block):
                errors.append(
                    f"{QUERY_CMAKE}: query runtime must not link semantic target {target}"
                )

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
            "kind.descriptor.durability",
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


def check_final_seal_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(QUERY_DATABASE_HEADER, "")
    source = files.get(QUERY_DATABASE_SOURCE, "")
    database_test = files.get(QUERY_DATABASE_TEST, "")
    capability_test = files.get(QUERY_CAPABILITY_TEST, "")
    for path, text, marker, description in (
        (
            QUERY_DATABASE_HEADER,
            header,
            "FinalSealResult<typename CompleteContextInput::Key, FinalWitness> sealInputs",
            "typed final-seal transaction",
        ),
        (
            QUERY_DATABASE_HEADER,
            header,
            "admitFinalSnapshot(",
            "descriptor-bound sealed snapshot admission",
        ),
        (
            QUERY_DATABASE_SOURCE,
            source,
            "InputTransactionFailure::InputMutationAfterFinalSeal",
            "irreversible post-seal input barrier",
        ),
        (
            QUERY_DATABASE_SOURCE,
            source,
            "retainAdmission(",
            "nested capability admission propagation",
        ),
        (
            QUERY_DATABASE_SOURCE,
            source,
            "QueryRuntimeFailure::FinalSealRequired",
            "final-sealed capability rejection",
        ),
        (
            QUERY_DATABASE_TEST,
            database_test,
            "FinalSealIsOneShotRevisionNeutralAndIrreversible",
            "one-shot final-seal regression",
        ),
        (
            QUERY_DATABASE_TEST,
            database_test,
            "FinalSealRaceReturnsStaleSnapshotAfterWinningCommit",
            "final-seal precedence regression",
        ),
        (
            QUERY_CAPABILITY_TEST,
            capability_test,
            "FinalSealedParentCapabilityQuery",
            "nested final-sealed capability regression",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")

    for marker, description in (
        (
            "ActiveMaterializerPermission<Descriptor, GlobalIdentityKey, MembershipDescriptor>::"
            "allowed",
            "descriptor-bound active-materialization permission",
        ),
        (
            "context.inheritedFinalAdmissionFailure()",
            "final admission barrier before materialization",
        ),
        (
            "context.get<MembershipDescriptor>(membershipKey)",
            "tracked active-membership demand",
        ),
        (
            "ActiveMaterialization<GlobalIdentityKey>::materialize",
            "post-membership materialization",
        ),
    ):
        if marker not in header:
            errors.append(f"{QUERY_DATABASE_HEADER}: missing {description}: {marker}")

    for path, text in sorted(files.items()):
        if path.suffix not in {".cc", ".h"}:
            continue
        if path not in {QUERY_DATABASE_HEADER, QUERY_DATABASE_SOURCE} and (
            "semanticContextResources()" in text or "SnapshotCapabilityArena::context" in text
        ):
            errors.append(f"{path}: semantic context capability resources escape the query runtime")


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
            "registerDescriptor<ActiveDefinitionAuthorityInput>()",
            "definition authority input registration",
        ),
        (
            authority,
            DRIVER_AUTHORITY_QUERY,
            "registerDescriptor<ActiveDefinitionAuthorityReadyInput>()",
            "authority readiness input registration",
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
            "identity::DefinitionIdentityRecord::decodeCanonical",
            "complete authority record admission",
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
            "authorityStagingSnapshot.get<module_graph_query::ModuleGraphQuery>",
            "complete stable graph reconstruction",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "authorityStagingSnapshot.get<module_graph_query::ModuleGraphSccQuery>",
            "stable graph SCC closure comparison",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "authorityStagingSnapshot.get<NamedDefinitionInventoryQuery>",
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
            "transaction.set<CompleteRootIdentityReadinessInput>",
            "complete identity readiness restoration",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "transaction.set<module_graph_query::"
            "ContextualIdentityAuthorityTransactionWitnessInput>",
            "contextual identity transaction witness publication",
        ),
        (
            session,
            DRIVER_AUTHORITY_SESSION,
            "impl->committed = true;",
            "closed transaction publication",
        ),
        (
            named_item,
            DRIVER_NAMED_ITEM_QUERY,
            "NamedItemSyntaxQuery::provide(",
            "named-item syntax provider",
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
            "incremental_binding_query::ContextualIdentityAuthorityInputTransaction::prepare(",
            "production authority transaction preparation",
        ),
        (
            compiler_session,
            DRIVER_SESSION,
            "finalSealedSnapshot",
            "sealed snapshot publication",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Contextual identity authority transaction commits complete readiness atomically",
            "atomic authority transaction regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Contextual identity authority transaction rejects stale and malformed staging",
            "stale and malformed transaction regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body projection requires final admission",
            "pre-seal projection rejection regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body final-admission rejection is deterministic across workers",
            "worker-count final-admission regression",
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
            "Active definition authority session rejects modules outside their active crate",
            "cross-crate authority rejection regression",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")
    for path, text in ((DRIVER_AUTHORITY_SESSION, session), (DRIVER_NAMED_ITEM_QUERY, named_item)):
        for forbidden in ("identityRegistries", "CompilerSession", "moduleGraph"):
            if forbidden in text:
                errors.append(f"{path}: tracked authority path must not read {forbidden}")
    if "demandNamedItemQueries" in compiler_session:
        errors.append(
            f"{DRIVER_SESSION}: unauthorized complete-context named-item demand entered R29-14"
        )
    if named_item.count("probeInput<ActiveDefinitionAuthorityReadyInput>") != 4:
        errors.append(
            f"{DRIVER_NAMED_ITEM_QUERY}: readiness must be read only by absent or contradictory "
            "provider and verifier branches"
        )
    worker_matrix = "for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}})"
    if test.count(worker_matrix) != 1:
        errors.append(
            f"{DRIVER_AUTHORITY_SESSION_TEST}: must retain the required final-admission worker matrix"
        )


def check_active_identity_materialization(files: dict[Path, str], errors: list[str]) -> None:
    membership = files.get(DRIVER_ACTIVE_IDENTITY_MEMBERSHIP, "")
    materialized_graph = files.get(DRIVER_MATERIALIZED_MODULE_GRAPH, "")
    adapter = files.get(DRIVER_TOPOLOGY_ADAPTER, "")
    graph_input = files.get(DRIVER_MODULE_GRAPH_INPUT, "")
    test = files.get(DRIVER_AUTHORITY_SESSION_TEST, "")
    for text, path, marker, description in (
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCompilationUnitMembershipQuery::provide(",
            "compilation-unit membership provider",
        ),
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCompilationUnitMembershipQuery::verify(",
            "compilation-unit membership verifier",
        ),
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCallableParameterMembershipQuery::provide(",
            "callable-parameter membership provider",
        ),
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCallableParameterMembershipQuery::verify(",
            "callable-parameter membership verifier",
        ),
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "registerDescriptor<ActiveCompilationUnitMembershipQuery>()",
            "first active-membership registration",
        ),
        (
            membership,
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "registerDescriptor<ActiveCallableParameterMembershipQuery>()",
            "last active-membership registration",
        ),
        (
            materialized_graph,
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "MaterializeModuleGraphQuery::provide(",
            "materialized graph provider",
        ),
        (
            materialized_graph,
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "MaterializeModuleGraphQuery::verify(",
            "independent materialized graph verifier",
        ),
        (
            materialized_graph,
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "context.template materializeActive<GlobalKey, MembershipDescriptor>",
            "tracked active-identity materialization",
        ),
        (
            materialized_graph,
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "CompleteCompilationContextAuthorityInput",
            "complete-context authority demand",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerActiveIdentityMembershipQueries(database)",
            "active-membership adapter registration",
        ),
        (
            graph_input,
            DRIVER_MODULE_GRAPH_INPUT,
            "registerDescriptor<MaterializeModuleGraphQuery>()",
            "materialized graph registration",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Final-sealed identity and named-item capabilities publish verified values",
            "final-sealed materialization regression",
        ),
        (
            test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "sealed.getCapability<graph_query::MaterializeModuleGraphQuery>(roots)",
            "production materialized graph capability demand",
        ),
    ):
        if marker not in text:
            errors.append(f"{path}: missing {description}: {marker}")


def check_owner_body_projection(files: dict[Path, str], errors: list[str]) -> None:
    owner_query = files.get(DRIVER_OWNER_BODY_QUERY, "")
    adapter = files.get(DRIVER_TOPOLOGY_ADAPTER, "")
    query_test = files.get(DRIVER_AUTHORITY_SESSION_TEST, "")
    codec_test = files.get(BINDER_MODULE_BODY_SYNTAX_TEST, "")
    for text, path, marker, description in (
        (
            owner_query,
            DRIVER_OWNER_BODY_QUERY,
            "ModuleBodyOwnersQuery::provide(",
            "module-body owner inventory provider",
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
            "context.get<NamedItemSyntaxQuery>",
            "sequential named-item dependency demand",
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
            "context.get<ModuleBodySyntaxQuery>",
            "module-owner semantic syntax closure",
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
            "OwnerBodyProvenanceQuery::verify(",
            "independent owner provenance verifier",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerDescriptor<ModuleBodyOwnersQuery>()",
            "module-body owners registration",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerDescriptor<OwnerBodySyntaxQuery>()",
            "owner-body syntax registration",
        ),
        (
            adapter,
            DRIVER_TOPOLOGY_ADAPTER,
            "registerDescriptor<OwnerBodyProvenanceQuery>()",
            "retained owner-body provenance capability registration",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body projection requires final admission",
            "owner projection final-admission regression",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "Owner body final-admission rejection is deterministic across workers",
            "owner projection rejection worker determinism regression",
        ),
        (
            query_test,
            DRIVER_AUTHORITY_SESSION_TEST,
            "provenance.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired",
            "owner provenance final-admission regression",
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


def check_nested_parallel_demands(files: dict[Path, str], errors: list[str]) -> None:
    for path in (
        DRIVER_TOPOLOGY_ADAPTER,
        DRIVER_MODULE_RESOLUTION_QUERY,
        DRIVER_MATERIALIZED_MODULE_GRAPH,
        DRIVER_OWNER_BODY_QUERY,
    ):
        if "context.getParallel<" in files.get(path, ""):
            errors.append(
                f"{path}: production materialization must not issue nested parallel capability demands"
            )
    resolution = files.get(DRIVER_MODULE_RESOLUTION_QUERY, "")
    if resolution.count("context.get<ModuleCatalogPathBucketInput>") != 2:
        errors.append(
            f"{DRIVER_MODULE_RESOLUTION_QUERY}: provider and verifier must each issue one "
            "sequential catalog-bucket demand"
        )


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
            "queryDatabase.beginInputTransaction(queryDatabase.snapshot().revision())",
            "base input transaction",
        ),
        (
            "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
            "atomic graph input transaction",
        ),
        (
            "graph_query::ModuleGraphInputTransactionAuthority authority",
            "authority-backed graph input verification",
        ),
        (
            "graph_query::VerifiedModuleGraphInputLedger::empty()",
            "transaction-local graph input ledger",
        ),
        ("stagedCompilationRoots", "complete stable root key"),
        (
            "authorityStagingSnapshot.get<graph_query::ModuleGraphQuery>",
            "staging graph demand",
        ),
        (
            "authorityStagingSnapshot.get<graph_query::ModuleGraphSccQuery>",
            "staging SCC demand",
        ),
        (
            "getCapability<module_graph_query::MaterializeModuleGraphQuery>",
            "sealed graph materialization",
        ),
        ("checkerFactIndexByModule", "checker module index"),
    ):
        if marker not in session:
            errors.append(f"{DRIVER_SESSION}: missing {description}: {marker}")

    for marker, description in (
        ("VerifiedModuleGraphInputTransaction::prepare(", "verified graph input transaction"),
        (
            "ModuleGraphInputTransactionVerifier::verify(",
            "independent graph input transaction verifier",
        ),
        ("reconstructVerifierSites(", "independent parsed-site reconstruction"),
        ("authority.resolver.catalog()", "frozen resolver catalog enumeration"),
        ("authority.coreInputs.projections()", "verified core projection enumeration"),
        ("registerDescriptor<SelectedModuleCatalogInput>()", "catalog input registration"),
        ("registerDescriptor<ModuleDependencySiteInput>()", "dependency site input registration"),
        ("registerDescriptor<SelectedModuleSourceQuery>()", "selected source registration"),
        ("registerDescriptor<ActiveModulesQuery>()", "active modules registration"),
        (
            "registerDescriptor<ModuleDependencyRequestsQuery>()",
            "dependency request registration",
        ),
        ("registerDescriptor<ModuleDependenciesQuery>()", "dependency registration"),
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
        ("ModuleGraphQuery::provide(", "stable graph provider"),
        ("ModuleGraphQuery::verify(", "independent stable graph verifier"),
        ("evaluateVerifierGraph(", "independent stable graph reconstruction"),
        ("ModuleGraphSccQuery::provide(", "Tarjan SCC provider"),
        ("ModuleGraphSccQuery::verify(", "Kosaraju SCC verifier"),
        ("verifierOrderComponents(", "independent SCC ordering"),
        ("registerDescriptor<ModuleGraphQuery>()", "stable graph registration"),
        ("registerDescriptor<ModuleGraphSccQuery>()", "stable SCC registration"),
    ):
        if marker not in graph_query:
            errors.append(f"{DRIVER_MODULE_GRAPH_QUERY}: missing {description}: {marker}")

    for marker, description in (
        ("acquireVerifierContext(", "independent final root reconstruction"),
        ("acquireVerifierModules(", "independent final module demand"),
        ("VerifierSourceContent", "independent final source reconstruction"),
        ("stableEdgesMatchGraph(", "independent final edge projection"),
        ("computeVerifierGraphRevision(", "independent final revision reconstruction"),
        (
            "identity::source_query::StableSourceQueryKey::fromVerified(",
            "independent final source-key demand",
        ),
        (
            "context.getCapability<parser::ParseSourceQuery>",
            "independent final parser capability demand",
        ),
        ("rootsMatchMaterializedEntries(", "independent final root membership proof"),
    ):
        if marker not in graph_bridge:
            errors.append(f"{BINDER_GRAPH_BRIDGE}: missing {description}: {marker}")

    for marker, description in (
        ("stageModuleResolutionQueryInputs(", "resolver input staging"),
        ("registerDescriptor<ResolveModuleRequestQuery>()", "resolution query registration"),
    ):
        if marker not in resolution_query:
            errors.append(f"{DRIVER_MODULE_RESOLUTION_QUERY}: missing {description}: {marker}")

    for marker, description in (
        ("registerDescriptor<ActiveCratesQuery>()", "active crates registration"),
        ("registerDescriptor<ActiveSourcesQuery>()", "active sources registration"),
        ("registerDescriptor<NamedDefinitionInventoryQuery>()", "named definition registration"),
        ("registerDescriptor<ModuleBodySyntaxQuery>()", "module body registration"),
    ):
        if marker not in adapter:
            errors.append(f"{DRIVER_TOPOLOGY_ADAPTER}: missing {description}: {marker}")

    for marker, description in (
        ("registerDescriptor<SourceSnapshotInput>()", "source snapshot registration"),
        ("registerDescriptor<CompilationOptionsInput>()", "compilation options registration"),
        ("SourceFileKey::decodeCanonical(decoder)", "canonical source key admission"),
    ):
        if marker not in source_query:
            errors.append(f"{IDENTITY_SOURCE_QUERY_INPUT}: missing {description}: {marker}")

    for marker, description in (
        ("CanonicalParsedSource::fromParsed", "query-safe parsed value admission"),
        (
            "registerDescriptor<ParseSourceQuery>()",
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
                "IdentitySyntaxSiteInventoryQuery::verify",
                "StableIdentityAdmissionQuery::verify",
            ),
        ),
        (named_item, DRIVER_NAMED_ITEM_QUERY, ("NamedItemProvenanceQuery::verify",)),
        (owner_body, DRIVER_OWNER_BODY_QUERY, ("OwnerBodyProvenanceQuery::verify",)),
        (
            package_graph,
            DRIVER_PACKAGE_GRAPH_INPUT,
            (
                "registerDescriptor<PackageGraphInput>()",
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
    check_final_seal_contract(files, errors)
    check_provider_registration(files, errors)
    check_active_definition_authority(files, errors)
    check_active_identity_materialization(files, errors)
    check_owner_body_projection(files, errors)
    check_nested_parallel_demands(files, errors)
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
    mutation[PRODUCT_ROOT / "utils/escaped-semantic-context.cc"] = (
        "void escape(Context& context) { context.semanticContextResources(); }\n"
    )
    expect_failure(mutation, "semantic context capability resources escape", failures)

    mutation = dict(base)
    mutation[QUERY_DATABASE_HEADER] = mutation[QUERY_DATABASE_HEADER].replace(
        "context.get<MembershipDescriptor>(membershipKey)",
        "REMOVED_TRACKED_MEMBERSHIP_DEMAND",
        1,
    )
    expect_failure(mutation, "tracked active-membership demand", failures)

    mutation = dict(base)
    mutation[QUERY_DATABASE_HEADER] = mutation[QUERY_DATABASE_HEADER].replace(
        "FinalSealResult<typename CompleteContextInput::Key, FinalWitness> sealInputs",
        "REMOVED_FINAL_SEAL_TRANSACTION",
        1,
    )
    expect_failure(mutation, "typed final-seal transaction", failures)

    for path, marker, description in (
        (
            DRIVER_SESSION,
            "graph_query::VerifiedModuleGraphInputTransaction::prepare(",
            "atomic graph input transaction",
        ),
        (
            DRIVER_SESSION,
            "getCapability<module_graph_query::MaterializeModuleGraphQuery>",
            "sealed graph materialization",
        ),
        (
            DRIVER_MODULE_GRAPH_INPUT,
            "registerDescriptor<SelectedModuleCatalogInput>()",
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
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCompilationUnitMembershipQuery::provide(",
            "compilation-unit membership provider",
        ),
        (
            DRIVER_ACTIVE_IDENTITY_MEMBERSHIP,
            "ActiveCallableParameterMembershipQuery::verify(",
            "callable-parameter membership verifier",
        ),
        (
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "MaterializeModuleGraphQuery::provide(",
            "materialized graph provider",
        ),
        (
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "MaterializeModuleGraphQuery::verify(",
            "independent materialized graph verifier",
        ),
        (
            DRIVER_MATERIALIZED_MODULE_GRAPH,
            "context.template materializeActive<GlobalKey, MembershipDescriptor>",
            "tracked active-identity materialization",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "acquireVerifierContext(",
            "independent final root reconstruction",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "VerifierSourceContent",
            "independent final source reconstruction",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "context.getCapability<parser::ParseSourceQuery>",
            "independent final parser capability demand",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "stableEdgesMatchGraph(",
            "independent final edge projection",
        ),
        (
            BINDER_GRAPH_BRIDGE,
            "computeVerifierGraphRevision(",
            "independent final revision reconstruction",
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

    mutation = dict(base)
    mutation[DRIVER_MATERIALIZED_MODULE_GRAPH] += "\ncontext.getParallel<InjectedDemand>();\n"
    expect_failure(mutation, "nested parallel capability demand", failures)

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
