#!/usr/bin/env python3

"""Validate query descriptor inventories and generate target-bound C++ headers."""

from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION_SCHEMA = (
    ROOT / "products/zomlang/compiler/query/query-descriptor-schema.def"
)
TEST_SCHEMA = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/query/query-test-descriptor-schema.def"
)
PRODUCTION_RELATIVE_OUTPUT = Path(
    "products/zomlang/compiler/query/query-descriptor-inventory.generated.h"
)
TEST_RELATIVE_OUTPUT = Path(
    "products/zomlang/tests/unittests/compiler/query/"
    "query-test-descriptor-inventory.generated.h"
)

ROW_PATTERN = re.compile(
    r"^(ZOM_INPUT|ZOM_COMPLETE_CONTEXT_INPUT|ZOM_SEMANTIC|ZOM_CAPABILITY)"
    r"\((.*)\)$"
)
TYPE_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+$")
NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DOMAIN_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:\.[a-z0-9][a-z0-9-]*)+$")
OWNER_PATTERN = re.compile(r"^[A-Za-z0-9_./-]+$")
INPUT_DURABILITIES = {"Low", "Medium", "High", "Frozen"}
SEMANTIC_REUSE = {"Semantic", "Persisted"}
RETENTION = {"Retained", "Evictable"}
ADMISSION = {"AnySnapshot", "FinalSealedSnapshot"}
COMPLETE_CONTEXT_TYPE = (
    "zomlang::compiler::driver::module_graph_query::"
    "CompleteCompilationContextAuthorityInput"
)
COMPLETE_CONTEXT_NAME = "CompleteCompilationContextAuthorityInput"
COMPLETE_CONTEXT_DOMAIN = "zom.input.complete-compilation-context-authority"
COMPLETE_CONTEXT_OWNER = "products/zomlang/compiler/driver/module-graph-query-input"
TRANSACTION_WITNESS_OWNER = (
    "products/zomlang/compiler/driver/module-graph-query-input"
)
TRANSACTION_WITNESS_ROWS = (
    (
        56,
        "zomlang::compiler::driver::module_graph_query::"
        "CoreDistributionTransactionWitnessInput",
        "CoreDistributionTransactionWitnessInput",
        "zom.query.core-distribution-transaction-witness",
    ),
    (
        57,
        "zomlang::compiler::driver::module_graph_query::"
        "ModuleStructureTransactionWitnessInput",
        "ModuleStructureTransactionWitnessInput",
        "zom.query.module-structure-transaction-witness",
    ),
    (
        58,
        "zomlang::compiler::driver::module_graph_query::"
        "ContextualIdentityAuthorityTransactionWitnessInput",
        "ContextualIdentityAuthorityTransactionWitnessInput",
        "zom.query.contextual-identity-authority-transaction-witness",
    ),
)
TEST_COMPLETE_CONTEXT_TYPE = "zomlang::compiler::query::test::TestCompleteContextInput"
TEST_COMPLETE_CONTEXT_NAME = "TestCompleteContextInput"
TEST_COMPLETE_CONTEXT_DOMAIN = "test.input.complete-context"
TEST_COMPLETE_CONTEXT_OWNER = (
    "products/zomlang/tests/unittests/compiler/query/query-test-specs"
)


class SchemaError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class Row:
    macro: str
    ordinal: int
    descriptor_type: str
    name: str
    domain: str
    first_policy: str
    second_policy: str | None
    owner: str

    @property
    def kind(self) -> str:
        if self.macro in {"ZOM_INPUT", "ZOM_COMPLETE_CONTEXT_INPUT"}:
            return "Input"
        if self.macro == "ZOM_SEMANTIC":
            return "Semantic"
        return "RevisionLocalCapability"

    @property
    def role(self) -> str:
        if self.macro == "ZOM_COMPLETE_CONTEXT_INPUT":
            return "CompleteContextAuthority"
        return "Ordinary"

    @property
    def durability(self) -> str:
        if self.kind == "Input":
            return self.first_policy
        return "Frozen"

    @property
    def reuse(self) -> str:
        if self.kind == "Input":
            return "Input"
        if self.kind == "Semantic":
            return self.first_policy
        return "RevisionLocal"

    @property
    def retention(self) -> str:
        if self.kind == "Input":
            return "Retained"
        if self.kind == "Semantic":
            assert self.second_policy is not None
            return self.second_policy
        return self.first_policy

    @property
    def admission(self) -> str:
        if self.kind != "RevisionLocalCapability":
            return "AnySnapshot"
        assert self.second_policy is not None
        return self.second_policy


def split_arguments(arguments: str) -> list[str]:
    fields: list[str] = []
    start = 0
    quoted = False
    escaped = False
    for index, character in enumerate(arguments):
        if escaped:
            escaped = False
            continue
        if character == "\\" and quoted:
            escaped = True
            continue
        if character == '"':
            quoted = not quoted
            continue
        if character == "," and not quoted:
            fields.append(arguments[start:index].strip())
            start = index + 1
    if quoted:
        raise SchemaError("unterminated string literal")
    fields.append(arguments[start:].strip())
    return fields


def unquote(value: str, field: str, path: Path, line: int) -> str:
    if len(value) < 2 or value[0] != '"' or value[-1] != '"':
        raise SchemaError(f"{path}:{line}: {field} must be a string literal")
    result = value[1:-1]
    if '"' in result or "\\" in result:
        raise SchemaError(f"{path}:{line}: {field} must not contain escapes")
    return result


def parse_schema_text(text: str, path: Path) -> list[Row]:
    rows: list[Row] = []
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("//"):
            continue
        match = ROW_PATTERN.fullmatch(line)
        if match is None:
            raise SchemaError(f"{path}:{line_number}: invalid schema row")
        macro, arguments = match.groups()
        fields = split_arguments(arguments)
        expected = 7 if macro in {"ZOM_SEMANTIC", "ZOM_CAPABILITY"} else 6
        if len(fields) != expected:
            raise SchemaError(
                f"{path}:{line_number}: {macro} expects {expected} fields"
            )
        try:
            ordinal = int(fields[0], 10)
        except ValueError as error:
            raise SchemaError(
                f"{path}:{line_number}: ordinal must be a decimal uint32"
            ) from error
        if ordinal < 0 or ordinal > 0xFFFFFFFF:
            raise SchemaError(f"{path}:{line_number}: ordinal is outside uint32")
        descriptor_type = fields[1]
        name = unquote(fields[2], "name", path, line_number)
        domain = unquote(fields[3], "domain", path, line_number)
        first_policy = fields[4]
        if expected == 7:
            second_policy = fields[5]
            owner_field = fields[6]
        else:
            second_policy = None
            owner_field = fields[5]
        owner = unquote(owner_field, "owner path family", path, line_number)
        rows.append(
            Row(
                macro,
                ordinal,
                descriptor_type,
                name,
                domain,
                first_policy,
                second_policy,
                owner,
            )
        )
    return rows


def validate_rows(
    rows: list[Row],
    path: Path,
    *,
    require_complete_context: bool,
    expected_start: int = 0,
) -> None:
    if not rows:
        raise SchemaError(f"{path}: inventory must not be empty")
    descriptor_types: set[str] = set()
    names: set[str] = set()
    domains: set[str] = set()
    complete_context_rows: list[Row] = []
    for index, row in enumerate(rows):
        expected_ordinal = expected_start + index
        if row.ordinal != expected_ordinal:
            raise SchemaError(
                f"{path}: ordinal {row.ordinal} must be contiguous at "
                f"{expected_ordinal}"
            )
        if not TYPE_PATTERN.fullmatch(row.descriptor_type):
            raise SchemaError(
                f"{path}: invalid descriptor type {row.descriptor_type!r}"
            )
        if not NAME_PATTERN.fullmatch(row.name):
            raise SchemaError(f"{path}: invalid descriptor name {row.name!r}")
        if row.descriptor_type.rsplit("::", 1)[1] != row.name:
            raise SchemaError(
                f"{path}: descriptor type and literal name disagree for {row.name}"
            )
        if not DOMAIN_PATTERN.fullmatch(row.domain):
            raise SchemaError(f"{path}: invalid literal domain {row.domain!r}")
        if not OWNER_PATTERN.fullmatch(row.owner) or not row.owner.startswith(
            "products/zomlang/"
        ):
            raise SchemaError(f"{path}: invalid owner path family {row.owner!r}")
        if row.descriptor_type in descriptor_types:
            raise SchemaError(
                f"{path}: duplicate descriptor type {row.descriptor_type}"
            )
        if row.name in names:
            raise SchemaError(f"{path}: duplicate descriptor name {row.name}")
        if row.domain in domains:
            raise SchemaError(f"{path}: duplicate descriptor domain {row.domain}")
        descriptor_types.add(row.descriptor_type)
        names.add(row.name)
        domains.add(row.domain)
        if row.kind == "Input":
            if row.first_policy not in INPUT_DURABILITIES:
                raise SchemaError(
                    f"{path}: invalid input durability {row.first_policy}"
                )
            if row.second_policy is not None:
                raise SchemaError(f"{path}: input row has extra policy")
        elif row.kind == "Semantic":
            if row.first_policy not in SEMANTIC_REUSE:
                raise SchemaError(
                    f"{path}: invalid semantic reuse {row.first_policy}"
                )
            if row.second_policy not in RETENTION:
                raise SchemaError(
                    f"{path}: invalid semantic retention {row.second_policy}"
                )
        else:
            if row.first_policy != "Retained":
                raise SchemaError(
                    f"{path}: capability retention must be Retained"
                )
            if row.second_policy not in ADMISSION:
                raise SchemaError(
                    f"{path}: invalid capability admission {row.second_policy}"
                )
        if row.role == "CompleteContextAuthority":
            complete_context_rows.append(row)
    if require_complete_context:
        if len(complete_context_rows) != 1:
            raise SchemaError(
                f"{path}: production inventory requires exactly one complete-context row"
            )
        complete = complete_context_rows[0]
        if (
            complete.descriptor_type != COMPLETE_CONTEXT_TYPE
            or complete.name != COMPLETE_CONTEXT_NAME
            or complete.domain != COMPLETE_CONTEXT_DOMAIN
            or complete.owner != COMPLETE_CONTEXT_OWNER
            or complete.first_policy != "Frozen"
        ):
            raise SchemaError(
                f"{path}: complete-context row does not name the exact production authority"
            )
        rows_by_ordinal = {row.ordinal: row for row in rows}
        for ordinal, descriptor_type, name, domain in TRANSACTION_WITNESS_ROWS:
            witness = rows_by_ordinal.get(ordinal)
            if (
                witness is None
                or witness.macro != "ZOM_INPUT"
                or witness.descriptor_type != descriptor_type
                or witness.name != name
                or witness.domain != domain
                or witness.first_policy != "Frozen"
                or witness.second_policy is not None
                or witness.owner != TRANSACTION_WITNESS_OWNER
            ):
                raise SchemaError(
                    f"{path}: transaction-witness row {ordinal} is not exact"
                )


def load_schema(path: Path, *, require_complete_context: bool) -> list[Row]:
    if not path.is_file():
        raise SchemaError(f"{path}: schema file is missing")
    rows = parse_schema_text(path.read_text(encoding="utf-8"), path)
    validate_rows(
        rows,
        path,
        require_complete_context=require_complete_context,
    )
    return rows


def validate_test_prefix(production: list[Row], combined: list[Row], path: Path) -> None:
    if len(combined) <= len(production):
        raise SchemaError(f"{path}: test inventory requires a nonempty test-only tail")
    if combined[: len(production)] != production:
        raise SchemaError(f"{path}: test inventory does not preserve the production prefix")
    tail = combined[len(production) :]
    validate_rows(
        tail,
        path,
        require_complete_context=False,
        expected_start=len(production),
    )
    complete_context_rows = [
        row for row in tail if row.role == "CompleteContextAuthority"
    ]
    first = tail[0]
    if (
        len(production) != 59
        or len(complete_context_rows) != 1
        or first != complete_context_rows[0]
        or first.ordinal != 59
        or first.descriptor_type != TEST_COMPLETE_CONTEXT_TYPE
        or first.name != TEST_COMPLETE_CONTEXT_NAME
        or first.domain != TEST_COMPLETE_CONTEXT_DOMAIN
        or first.first_policy != "Frozen"
        or first.second_policy is not None
        or first.owner != TEST_COMPLETE_CONTEXT_OWNER
    ):
        raise SchemaError(
            f"{path}: test tail must begin with the exact slot-59 complete-context fixture"
        )
    production_domains = {row.domain for row in production}
    production_names = {row.name for row in production}
    production_types = {row.descriptor_type for row in production}
    for row in tail:
        if row.domain in production_domains:
            raise SchemaError(f"{path}: test domain duplicates the production inventory")
        if row.name in production_names:
            raise SchemaError(f"{path}: test name duplicates the production inventory")
        if row.descriptor_type in production_types:
            raise SchemaError(f"{path}: test type duplicates the production inventory")


def cpp_forward_declarations(rows: list[Row]) -> list[str]:
    result: list[str] = []
    for row in rows:
        namespace, name = row.descriptor_type.rsplit("::", 1)
        result.append(f"namespace {namespace} {{ struct {name}; }}")
    return result


def cpp_row(row: Row) -> str:
    return (
        "    {"
        f"{row.ordinal}, \"{row.descriptor_type}\"_zcc, \"{row.name}\"_zcc, "
        f"\"{row.domain}\"_zcc, QueryDescriptorKind::{row.kind}, "
        f"QueryDescriptorRole::{row.role}, ReuseClass::{row.reuse}, "
        f"RetentionClass::{row.retention}, Durability::{row.durability}, "
        "QueryEqualityPolicy::CanonicalBytes, QueryCyclePolicy::Reject, "
        f"QueryCostClass::Linear, CapabilityAdmission::{row.admission}, "
        f"\"{row.owner}\"_zcc"
        "},"
    )


def cpp_binding(row: Row, identity: str) -> list[str]:
    return [
        "template <>",
        f"struct QueryDescriptorInventoryBinding<::{row.descriptor_type}> final {{",
        f'  static constexpr auto inventoryIdentity = "{identity}"_zcc;',
        f"  static constexpr uint32_t ordinal = {row.ordinal};",
        f'  static constexpr auto descriptorType = "{row.descriptor_type}"_zcc;',
        f"  static constexpr auto role = QueryDescriptorRole::{row.role};",
        f'  static constexpr auto ownerPathFamily = "{row.owner}"_zcc;',
        "};",
        "",
    ]


def generate_header(
    rows: list[Row],
    *,
    inventory_name: str,
    identity: str,
    production: bool,
) -> str:
    guard_open = ["#if !defined(ZOM_QUERY_TEST_DESCRIPTOR_INVENTORY)", ""] if production else []
    guard_close = ["#endif", ""] if production else []
    lines = [
        "// Generated by scripts/generate-query-descriptor-schema.py.",
        "// Do not edit.",
        "",
        "#pragma once",
        "",
        '#include "zomlang/compiler/query/query-types.h"',
        "",
        *guard_open,
        *cpp_forward_declarations(rows),
        "",
        "namespace zomlang::compiler::query {",
        "",
    ]
    for row in rows:
        lines.extend(cpp_binding(row, identity))
    lines.extend(
        [
            "namespace _query_descriptor_generated {",
            "",
            "inline constexpr QueryDescriptorInventoryRow rows[] = {",
            *(cpp_row(row) for row in rows),
            "};",
            "",
            "}  // namespace _query_descriptor_generated",
            "",
            f"struct {inventory_name} final {{",
            f'  static constexpr auto identity = "{identity}"_zcc;',
            "  static constexpr zc::ArrayPtr<const QueryDescriptorInventoryRow> rows() {",
            "    return zc::arrayPtr(_query_descriptor_generated::rows);",
            "  }",
            "};",
            "",
            "ZC_NODISCARD inline constexpr QueryDescriptorInventoryRef",
            (
                "productionQueryDescriptorInventory()"
                if production
                else "queryTestDescriptorInventory()"
            ),
            "{",
            f"  return generatedQueryDescriptorInventory<{inventory_name}>();",
            "}",
            "",
            "}  // namespace zomlang::compiler::query",
            "",
            *guard_close,
        ]
    )
    return "\n".join(lines)


def write_if_different(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def compare_output(path: Path, expected: str) -> None:
    if not path.is_file():
        raise SchemaError(f"{path}: generated output is missing")
    if path.read_text(encoding="utf-8") != expected:
        raise SchemaError(f"{path}: generated output is stale")


def generated_outputs_in_build_trees(relative: Path) -> list[Path]:
    return sorted(
        path
        for path in ROOT.glob("build-*")
        if path.is_dir()
        for path in [path / relative]
        if path.exists()
    )


def production_content(rows: list[Row]) -> str:
    return generate_header(
        rows,
        inventory_name="ProductionQueryDescriptorInventory",
        identity="zom.query-descriptor-inventory.production",
        production=True,
    )


def combined_test_rows(production: list[Row], test_path: Path) -> list[Row]:
    text = test_path.read_text(encoding="utf-8")
    combined = parse_schema_text(text, test_path)
    validate_test_prefix(production, combined, test_path)
    return combined


def run_self_test() -> None:
    production_text = PRODUCTION_SCHEMA.read_text(encoding="utf-8")
    test_text = TEST_SCHEMA.read_text(encoding="utf-8")
    path = Path("production-fixture.def")
    test_path = Path("test-fixture.def")
    baseline = parse_schema_text(production_text, path)
    validate_rows(baseline, path, require_complete_context=True)
    combined = parse_schema_text(test_text, test_path)
    validate_test_prefix(baseline, combined, test_path)
    witness_lines = [
        line
        for line in production_text.splitlines()
        if "TransactionWitnessInput" in line
    ]
    if len(witness_lines) != 3:
        raise SchemaError("self-test requires the exact three witness rows")
    test_complete_line = next(
        line
        for line in test_text.splitlines()
        if line.startswith("ZOM_COMPLETE_CONTEXT_INPUT(59,")
    )
    first_production_line = next(
        line for line in test_text.splitlines() if line.startswith("ZOM_")
    )
    fixtures = [
        (
            "missing witness",
            production_text.replace(witness_lines[0] + "\n", ""),
        ),
        (
            "duplicate witness ordinal",
            production_text.replace("ZOM_INPUT(57,", "ZOM_INPUT(56,", 1),
        ),
        (
            "reordered witnesses",
            production_text.replace(
                witness_lines[0] + "\n" + witness_lines[1],
                witness_lines[1] + "\n" + witness_lines[0],
            ),
        ),
        (
            "renamed witness",
            production_text.replace(
                '"CoreDistributionTransactionWitnessInput"',
                '"OtherTransactionWitnessInput"',
                1,
            ),
        ),
        (
            "wrong witness domain",
            production_text.replace(
                "zom.query.core-distribution-transaction-witness",
                "zom.query.other-transaction-witness",
                1,
            ),
        ),
        (
            "wrong witness type",
            production_text.replace(
                "::CoreDistributionTransactionWitnessInput,",
                "::OtherTransactionWitnessInput,",
                1,
            ),
        ),
        (
            "mutable witness",
            production_text.replace(
                witness_lines[0],
                witness_lines[0].replace(", Frozen,", ", Low,"),
            ),
        ),
    ]
    test_fixtures = [
        (
            "restarted test ordinal",
            test_text.replace(
                "ZOM_COMPLETE_CONTEXT_INPUT(59,",
                "ZOM_COMPLETE_CONTEXT_INPUT(60,",
                1,
            ),
        ),
        (
            "test tail witness row",
            test_text.replace(
                test_complete_line,
                witness_lines[0].replace("ZOM_INPUT(56,", "ZOM_INPUT(59,"),
                1,
            ),
        ),
        (
            "missing production prefix",
            test_text.replace(first_production_line + "\n", "", 1),
        ),
    ]
    failures: list[str] = []
    for name, fixture in fixtures:
        try:
            rows = parse_schema_text(fixture, path)
            validate_rows(rows, path, require_complete_context=True)
        except SchemaError:
            continue
        failures.append(name)
    for name, fixture in test_fixtures:
        try:
            rows = parse_schema_text(fixture, test_path)
            validate_test_prefix(baseline, rows, test_path)
        except SchemaError:
            continue
        failures.append(name)
    if failures:
        raise SchemaError(
            "self-test did not reject: " + ", ".join(failures)
        )
    total = len(fixtures) + len(test_fixtures)
    print(f"query descriptor schema self-test passed ({total}/{total})")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate immutable query descriptor inventories"
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--production-schema", type=Path, default=PRODUCTION_SCHEMA)
    parser.add_argument("--test-schema", type=Path)
    parser.add_argument("--production-output", type=Path)
    parser.add_argument("--test-output", type=Path)
    parser.add_argument(
        "--production-only",
        action="store_true",
        help="validate or generate only the production inventory",
    )
    args = parser.parse_args()

    try:
        if args.self_test:
            run_self_test()
            return 0
        production = load_schema(
            args.production_schema, require_complete_context=True
        )
        generated_production = production_content(production)
        generated_test: str | None = None
        test_schema = args.test_schema
        if args.production_only and (
            args.test_schema is not None or args.test_output is not None
        ):
            parser.error("--production-only does not accept test inventory arguments")
        if args.check and test_schema is None and not args.production_only:
            test_schema = TEST_SCHEMA
        if test_schema is not None:
            combined = combined_test_rows(production, test_schema)
            generated_test = generate_header(
                combined,
                inventory_name="QueryTestDescriptorInventory",
                identity="zom.query-descriptor-inventory.query-tests",
                production=False,
            )
        if args.check:
            production_outputs = (
                [args.production_output]
                if args.production_output is not None
                else generated_outputs_in_build_trees(PRODUCTION_RELATIVE_OUTPUT)
            )
            if not production_outputs:
                raise SchemaError("no generated production inventory was found")
            for output in production_outputs:
                compare_output(output, generated_production)
            if args.test_output is not None:
                if generated_test is None:
                    raise SchemaError("--test-output requires --test-schema")
                compare_output(args.test_output, generated_test)
            elif generated_test is not None:
                test_outputs = generated_outputs_in_build_trees(TEST_RELATIVE_OUTPUT)
                if not test_outputs:
                    raise SchemaError("no generated query-test inventory was found")
                for output in test_outputs:
                    compare_output(output, generated_test)
            print("query descriptor schema check passed")
            return 0
        if args.production_output is None:
            parser.error("--production-output is required when generating")
        write_if_different(args.production_output, generated_production)
        if args.test_output is not None:
            if generated_test is None:
                parser.error("--test-output requires --test-schema")
            write_if_different(args.test_output, generated_test)
        print("query descriptor inventory generated")
        return 0
    except (OSError, SchemaError) as error:
        print(f"query descriptor schema failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
