#!/usr/bin/env python3

"""Enforce the closed literal query descriptor architecture."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from importlib.machinery import SourceFileLoader


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "products/zomlang/compiler/query/query-descriptor-schema.def"
COMPILER_ROOT = ROOT / "products/zomlang/compiler"
QUERY_TYPES = ROOT / "products/zomlang/compiler/query/query-types.h"
QUERY_DATABASE_HEADER = ROOT / "products/zomlang/compiler/query/query-database.h"
QUERY_DATABASE_IMPLEMENTATION = ROOT / "products/zomlang/compiler/query/query-database.cc"
QUERY_TEST_SPECS = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/query/query-test-specs.h"
)
QUERY_DATABASE_TEST = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/query/query-database-test.cc"
)
QUERY_CAPABILITY_TEST = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/query/query-capability-test.cc"
)
TEST_CMAKE = ROOT / "products/zomlang/tests/CMakeLists.txt"
NEGATIVE_FIXTURE = (
    ROOT / "products/zomlang/tests/cmake/expect-compile-failure/CMakeLists.txt"
)
NEGATIVE_DIRECTORY = (
    ROOT / "products/zomlang/tests/compile-fail/query-runtime"
)
NEGATIVE_CASES = {
    "identity-token-construction": "QueryDatabaseIdentityToken",
    "request-result-copy": "QueryRequestResult",
    "request-result-clone": "clone",
    "memo-base-observer": "memoBase",
    "memo-base-cast": "memoAs",
    "capability-published-construction": "CapabilityPublished",
    "request-decoder-bridge": "QueryRuntimeTestAccess",
    "database-identity-constructor": "QueryDatabase",
    "database-allocator-constructor": "QueryDatabase",
    "database-gate-constructor": "QueryDatabase",
    "database-callback-constructor": "QueryDatabase",
    "database-verifier-constructor": "QueryDatabase",
    "memo-kind-mutation": "kind",
    "memo-database-mutation": "database",
    "memo-revision-mutation": "revision",
}
TEST_ACCESS_ALLOWED = {
    QUERY_TYPES,
    QUERY_DATABASE_HEADER,
    QUERY_TEST_SPECS,
    QUERY_DATABASE_TEST,
    QUERY_CAPABILITY_TEST,
}
FINAL_SEAL_GATE_ALLOWED = {
    QUERY_DATABASE_HEADER,
    QUERY_DATABASE_IMPLEMENTATION,
    QUERY_TEST_SPECS,
    QUERY_DATABASE_TEST,
}
GENERATOR = SourceFileLoader(
    "query_descriptor_generator",
    str(ROOT / "scripts/generate-query-descriptor-schema.py"),
).load_module()

OLD_API_PATTERNS = {
    "old query contract API": re.compile(
        r"\bQueryKindContract\b|static\s+[^;\n]*\bcontract\s*\(\s*\)"
    ),
    "old registration API": re.compile(
        r"\bregister(?:Input|Query|Capability|Derived|RevisionLocalCapability)Kind\b"
    ),
}
DESCRIPTOR_PATTERN = re.compile(
    r"static\s+constexpr\s+query::"
    r"(Input|Semantic|Capability)DescriptorMetadata\s+descriptor\s*\{(.*?)\};",
    re.DOTALL,
)
STRUCT_PATTERN = re.compile(r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)\s+final\s*\{")
NAMESPACE_PATTERN = re.compile(
    r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*\{"
)
RUNTIME_DOMAIN_PATTERN = re.compile(r"static\s+[^;\n]*\bdomain\s*\(\s*\)")
DIRECT_SLOT_PATTERN = re.compile(r"\bDescriptorSlot\s*\(")
LITERAL_PREFIX_PATTERN = re.compile(
    r'^"([A-Za-z_][A-Za-z0-9_]*)"_zcc,'
    r'"([a-z][a-z0-9]*(?:\.[a-z0-9][a-z0-9-]*)+)"_zcc,'
)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def matching_brace(text: str, opening: int) -> int | None:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return None


def namespace_at(text: str, position: int) -> str:
    containing: list[tuple[int, int, str]] = []
    for match in NAMESPACE_PATTERN.finditer(text):
        closing = matching_brace(text, match.end() - 1)
        if closing is not None and match.end() <= position < closing:
            containing.append((match.start(), closing, match.group(1)))
    containing.sort(key=lambda entry: entry[0])
    return "::".join(entry[2] for entry in containing)


def declared_descriptors(
    path: Path, text: str
) -> list[tuple[str, str, str, str, str, str, str]]:
    result: list[tuple[str, str, str, str, str, str, str]] = []
    for match in STRUCT_PATTERN.finditer(text):
        closing = matching_brace(text, match.end() - 1)
        if closing is None:
            continue
        body = text[match.end() : closing]
        descriptor = DESCRIPTOR_PATTERN.search(body)
        if descriptor is None:
            continue
        kind, initializer = descriptor.groups()
        normalized = re.sub(r"\s+", "", initializer)
        prefix = LITERAL_PREFIX_PATTERN.match(normalized)
        namespace = namespace_at(text, match.start())
        qualified_type = f"{namespace}::{match.group(1)}" if namespace else match.group(1)
        if prefix is None:
            result.append(
                (qualified_type, match.group(1), kind, "", "", normalized, body)
            )
            continue
        name, domain = prefix.groups()
        result.append(
            (qualified_type, match.group(1), kind, name, domain, normalized, body)
        )
    return result


def expected_initializer(row: object) -> str:
    prefix = f'"{row.name}"_zcc,"{row.domain}"_zcc,'
    if row.kind == "Input":
        return prefix + f"query::Durability::{row.durability}"
    if row.kind == "Semantic":
        return (
            prefix
            + f"query::ReuseClass::{row.reuse},"
            + f"query::RetentionClass::{row.retention},"
            + "query::QueryEqualityPolicy::CanonicalBytes,"
            + "query::QueryCyclePolicy::Reject,"
            + "query::QueryCostClass::Linear"
        )
    return (
        prefix
        + f"query::RetentionClass::{row.retention},"
        + "query::QueryCyclePolicy::Reject,"
        + "query::QueryCostClass::Linear,"
        + f"query::CapabilityAdmission::{row.admission}"
    )


def check_files(files: dict[Path, str], rows: list[object]) -> list[str]:
    errors: list[str] = []
    row_by_name = {row.name: row for row in rows}
    for path, text in files.items():
        path_text = relative(path)
        if path.name != "query-database.cc" and DIRECT_SLOT_PATTERN.search(text):
            errors.append(f"{path_text}: descriptor slot exists outside query runtime")
        for label, pattern in OLD_API_PATTERNS.items():
            if pattern.search(text):
                errors.append(f"{path_text}: {label} remains")
        if RUNTIME_DOMAIN_PATTERN.search(text):
            errors.append(f"{path_text}: runtime-computed descriptor domain remains")
        for (
            descriptor_type,
            struct_name,
            kind,
            literal_name,
            domain,
            initializer,
            body,
        ) in declared_descriptors(path, text):
            if struct_name != literal_name:
                errors.append(
                    f"{path_text}: descriptor/schema disagreement for {struct_name}"
                )
                continue
            row = row_by_name.get(struct_name)
            if row is None:
                errors.append(
                    f"{path_text}: descriptor {struct_name} is absent from inventory"
                )
                continue
            expected_kind = {
                "Input": "Input",
                "Semantic": "Semantic",
                "Capability": "RevisionLocalCapability",
            }[kind]
            if (
                row.descriptor_type != descriptor_type
                or
                row.kind != expected_kind
                or row.domain != domain
                or initializer != expected_initializer(row)
            ):
                errors.append(
                    f"{path_text}: descriptor/schema disagreement for {struct_name}"
                )
            owner = path_text.rsplit(".", 1)[0]
            if owner != row.owner:
                errors.append(
                    f"{path_text}: descriptor {struct_name} has wrong owner path family"
                )
    declared_names = {
        descriptor[1]
        for path, text in files.items()
        for descriptor in declared_descriptors(path, text)
    }
    for row in rows:
        if row.name in declared_names:
            continue
        # Future accepted descriptors remain inert until their owning task lands.
        if row.role == "CompleteContextAuthority":
            continue
        errors.append(f"{SCHEMA_PATH.relative_to(ROOT)}: row {row.name} has no descriptor")
    return errors


def check_runtime_test_boundaries(
    files: dict[Path, str],
    negative_cases: set[str],
    test_cmake: str,
    fixture: str,
) -> list[str]:
    errors: list[str] = []
    for path, text in files.items():
        if "QueryRuntimeTestAccess" in text and path not in TEST_ACCESS_ALLOWED:
            errors.append(
                f"{relative(path)}: query runtime test access escaped its owned files"
            )
        if "FinalSealPhaseTwoGate" in text and path not in FINAL_SEAL_GATE_ALLOWED:
            errors.append(
                f"{relative(path)}: final-seal test gate escaped its owned files"
            )
        if (
            path.parent == COMPILER_ROOT / "query"
            and "class QueryRuntimeTestAccess final" in text
        ):
            errors.append(
                f"{relative(path)}: query runtime test access is defined in production"
            )
    if "class QueryRuntimeTestAccess final" not in files.get(QUERY_TEST_SPECS, ""):
        errors.append(
            f"{relative(QUERY_TEST_SPECS)}: query runtime test access definition is missing"
        )
    if negative_cases != set(NEGATIVE_CASES):
        missing = sorted(set(NEGATIVE_CASES) - negative_cases)
        additional = sorted(negative_cases - set(NEGATIVE_CASES))
        errors.append(
            "query runtime compile-fail inventory drift: "
            f"missing={missing}, additional={additional}"
        )
    for name, symbol in NEGATIVE_CASES.items():
        if f"{name}:{symbol}" not in test_cmake:
            errors.append(
                f"{relative(TEST_CMAKE)}: negative case {name} is not registered"
            )
    fixture_markers = (
        "try_compile(",
        "SOURCE_FROM_CONTENT",
        "CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "ZOM_FORBIDDEN_SYMBOL",
    )
    for marker in fixture_markers:
        if marker not in fixture:
            errors.append(
                f"{relative(NEGATIVE_FIXTURE)}: missing fixture marker {marker}"
            )
    return errors


def production_files() -> dict[Path, str]:
    paths = sorted(
        path
        for path in COMPILER_ROOT.rglob("*")
        if path.suffix in {".h", ".cc"} and path.is_file()
    )
    return {path: path.read_text(encoding="utf-8") for path in paths}


def run_check() -> list[str]:
    rows = GENERATOR.load_schema(
        SCHEMA_PATH, require_complete_context=True
    )
    files = production_files()
    for path in (
        QUERY_TEST_SPECS,
        QUERY_DATABASE_TEST,
        QUERY_CAPABILITY_TEST,
    ):
        files[path] = path.read_text(encoding="utf-8")
    negative_cases = {
        path.stem for path in NEGATIVE_DIRECTORY.glob("*.cc") if path.is_file()
    }
    return [
        *check_files(production_files(), rows),
        *check_runtime_test_boundaries(
            files,
            negative_cases,
            TEST_CMAKE.read_text(encoding="utf-8"),
            NEGATIVE_FIXTURE.read_text(encoding="utf-8"),
        ),
    ]


def mutate_once(files: dict[Path, str], path: Path, marker: str) -> dict[Path, str]:
    result = dict(files)
    result[path] = result[path] + "\n" + marker + "\n"
    return result


def run_self_test() -> None:
    fixture_path = ROOT / "products/zomlang/compiler/example/query-fixture.h"
    rows = GENERATOR.parse_schema_text(
        "\n".join(
            [
                'ZOM_INPUT(0, zomlang::compiler::example::FixtureInput, '
                '"FixtureInput", "zom.query.fixture-input", Low, '
                '"products/zomlang/compiler/example/query-fixture")',
                'ZOM_COMPLETE_CONTEXT_INPUT(1, '
                'zomlang::compiler::driver::module_graph_query::'
                'CompleteCompilationContextAuthorityInput, '
                '"CompleteCompilationContextAuthorityInput", '
                '"zom.input.complete-compilation-context-authority", Frozen, '
                '"products/zomlang/compiler/driver/module-graph-query-input")',
            ]
        ),
        Path("fixture.def"),
    )
    baseline = {
        fixture_path: "\n".join(
            [
                "namespace zomlang::compiler::example {",
                "struct FixtureInput final {",
                "  static constexpr query::InputDescriptorMetadata descriptor{",
                '      "FixtureInput"_zcc, "zom.query.fixture-input"_zcc,',
                "      query::Durability::Low};",
                "};",
                "}  // namespace zomlang::compiler::example",
            ]
        )
    }
    baseline_errors = check_files(baseline, rows)
    if baseline_errors:
        raise RuntimeError(
            "self-test baseline failed: " + "; ".join(baseline_errors)
        )
    fixtures = [
        (
            "descriptor outside inventory",
            mutate_once(
                baseline,
                fixture_path,
                "struct MissingQuery final {\n"
                "  static constexpr query::SemanticDescriptorMetadata descriptor{\n"
                '      "MissingQuery"_zcc, "zom.query.missing"_zcc,\n'
                "      query::ReuseClass::Semantic, query::RetentionClass::Retained,\n"
                "      query::QueryEqualityPolicy::CanonicalBytes,\n"
                "      query::QueryCyclePolicy::Reject, query::QueryCostClass::Linear};\n"
                "};",
            ),
            "absent from inventory",
        ),
        (
            "old contract API",
            mutate_once(baseline, fixture_path, "query::QueryKindContract contract();"),
            "old query contract API remains",
        ),
        (
            "old registration API",
            mutate_once(
                baseline,
                fixture_path,
                "database.registerInputKind<FixtureInput>();",
            ),
            "old registration API remains",
        ),
        (
            "old derived registration API",
            mutate_once(
                baseline,
                fixture_path,
                "database.registerDerivedKind<FixtureInput>();",
            ),
            "old registration API remains",
        ),
        (
            "old capability registration API",
            mutate_once(
                baseline,
                fixture_path,
                "database.registerRevisionLocalCapabilityKind<FixtureInput>();",
            ),
            "old registration API remains",
        ),
        (
            "old registration declaration",
            mutate_once(
                baseline,
                fixture_path,
                "void registerDerivedKind();",
            ),
            "old registration API remains",
        ),
        (
            "old contract declaration",
            mutate_once(
                baseline,
                fixture_path,
                "static int contract();",
            ),
            "old query contract API remains",
        ),
        (
            "runtime domain",
            {
                fixture_path: baseline[fixture_path].replace(
                    "\n};",
                    "\n  static constexpr zc::StringPtr domain();\n};",
                    1,
                )
            },
            "runtime-computed descriptor domain remains",
        ),
        (
            "orphan runtime domain",
            mutate_once(
                baseline,
                fixture_path,
                "struct OrphanDescriptor final { static int domain(); };",
            ),
            "runtime-computed descriptor domain remains",
        ),
        (
            "metadata mismatch",
            {
                fixture_path: baseline[fixture_path].replace(
                    "query::Durability::Low",
                    "query::Durability::High",
                )
            },
            "descriptor/schema disagreement",
        ),
        (
            "wrong descriptor namespace",
            {
                fixture_path: baseline[fixture_path].replace(
                    "namespace zomlang::compiler::example",
                    "namespace zomlang::compiler::other",
                )
            },
            "descriptor/schema disagreement",
        ),
        (
            "direct slot",
            mutate_once(baseline, fixture_path, "DescriptorSlot(metadata);"),
            "descriptor slot exists outside query runtime",
        ),
    ]
    failures: list[str] = []
    for name, files, expected in fixtures:
        errors = check_files(files, rows)
        if not any(expected in error for error in errors):
            failures.append(name)
    boundary_baseline = {
        QUERY_TYPES: "namespace test { class QueryRuntimeTestAccess; }",
        QUERY_DATABASE_HEADER: "friend class test::QueryRuntimeTestAccess;",
        QUERY_DATABASE_IMPLEMENTATION: "void pauseAtFinalSealPhaseTwoGate();",
        QUERY_TEST_SPECS: (
            "class QueryRuntimeTestAccess final {};\n"
            "void armFinalSealPhaseTwoGate();"
        ),
        QUERY_DATABASE_TEST: (
            "QueryRuntimeTestAccess access;\n"
            "void waitForFinalSealPhaseTwoGate();"
        ),
        QUERY_CAPABILITY_TEST: "QueryRuntimeTestAccess access;",
    }
    boundary_cmake = "\n".join(
        f"{name}:{symbol}" for name, symbol in NEGATIVE_CASES.items()
    )
    boundary_fixture = "\n".join(
        (
            "try_compile(",
            "SOURCE_FROM_CONTENT",
            "CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "ZOM_FORBIDDEN_SYMBOL",
        )
    )
    if check_runtime_test_boundaries(
        boundary_baseline, set(NEGATIVE_CASES), boundary_cmake, boundary_fixture
    ):
        raise RuntimeError("query runtime boundary self-test baseline failed")
    escaped_access = dict(boundary_baseline)
    escaped_access[
        ROOT / "products/zomlang/compiler/example/escaped.cc"
    ] = "QueryRuntimeTestAccess"
    escaped_gate = dict(boundary_baseline)
    escaped_gate[
        ROOT / "products/zomlang/compiler/example/escaped.cc"
    ] = "FinalSealPhaseTwoGate"
    production_definition = dict(boundary_baseline)
    production_definition[
        QUERY_DATABASE_IMPLEMENTATION
    ] += "\nclass QueryRuntimeTestAccess final {};"
    boundary_fixtures = (
        (
            "escaped test access",
            escaped_access,
            set(NEGATIVE_CASES),
            boundary_cmake,
            boundary_fixture,
            "test access escaped",
        ),
        (
            "escaped final-seal gate",
            escaped_gate,
            set(NEGATIVE_CASES),
            boundary_cmake,
            boundary_fixture,
            "test gate escaped",
        ),
        (
            "production test access definition",
            production_definition,
            set(NEGATIVE_CASES),
            boundary_cmake,
            boundary_fixture,
            "defined in production",
        ),
        (
            "missing compile-fail case",
            boundary_baseline,
            set(NEGATIVE_CASES) - {"memo-revision-mutation"},
            boundary_cmake,
            boundary_fixture,
            "compile-fail inventory drift",
        ),
    )
    for name, files, cases, cmake, fixture, expected in boundary_fixtures:
        errors = check_runtime_test_boundaries(files, cases, cmake, fixture)
        if not any(expected in error for error in errors):
            failures.append(name)
    if failures:
        raise RuntimeError("self-test did not reject: " + ", ".join(failures))
    print(
        f"query descriptor architecture self-test passed "
        f"({len(fixtures) + len(boundary_fixtures)}/"
        f"{len(fixtures) + len(boundary_fixtures)})"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check the closed query descriptor architecture"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            run_self_test()
            return 0
        errors = run_check()
        if errors:
            print("query descriptor architecture check failed:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        print("query descriptor architecture check passed")
        return 0
    except (OSError, GENERATOR.SchemaError, RuntimeError) as error:
        print(f"query descriptor architecture failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
