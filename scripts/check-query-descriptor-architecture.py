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
PROVENANCE_QUERY_IMPLEMENTATION = (
    ROOT
    / "products/zomlang/compiler/driver/module-dependency-provenance-query.cc"
)
STABLE_BINDING_SCHEMA = (
    ROOT / "products/zomlang/compiler/binder/stable-binding-schema.def"
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


def without_cpp_comments(text: str) -> str:
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if current == "/" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "line-comment"
                continue
            if current == "/" and following == "*":
                result.extend((" ", " "))
                index += 2
                state = "block-comment"
                continue
            result.append(current)
            if current == '"':
                state = "string"
            elif current == "'":
                state = "character"
            index += 1
            continue
        if state == "line-comment":
            result.append("\n" if current == "\n" else " ")
            if current == "\n":
                state = "code"
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
            else:
                result.append("\n" if current == "\n" else " ")
                index += 1
            continue
        result.append(current)
        if current == "\\" and index + 1 < len(text):
            result.append(text[index + 1])
            index += 2
            continue
        if (state == "string" and current == '"') or (
            state == "character" and current == "'"
        ):
            state = "code"
        index += 1
    return "".join(result)


def splice_cpp_lines(text: str) -> str:
    spliced = re.sub(r"\\(?:\r\n|\n|\r)", "", text)
    return spliced.replace("\r\n", "\n").replace("\r", "\n")


def cpp_lexical_code_mask(text: str) -> str:
    result = list(text)

    def mask(begin: int, end: int) -> None:
        for position in range(begin, end):
            if result[position] not in ("\n", "\r"):
                result[position] = " "

    index = 0
    while index < len(text):
        following = text[index + 1] if index + 1 < len(text) else ""
        if text[index] == "/" and following == "/":
            end = text.find("\n", index + 2)
            if end == -1:
                end = len(text)
            mask(index, end)
            index = end
            continue
        if text[index] == "/" and following == "*":
            closing = text.find("*/", index + 2)
            end = len(text) if closing == -1 else closing + 2
            mask(index, end)
            index = end
            continue

        raw_match = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[index:])
        if raw_match is not None:
            closing_token = ")" + raw_match.group(1) + '"'
            closing = text.find(closing_token, index + raw_match.end())
            end = len(text) if closing == -1 else closing + len(closing_token)
            mask(index, end)
            index = end
            continue

        literal_match = re.match(r'(?:u8|u|U|L)?(["\'])', text[index:])
        if literal_match is not None:
            quote = literal_match.group(1)
            cursor = index + literal_match.end()
            while cursor < len(text):
                if text[cursor] == "\\" and cursor + 1 < len(text):
                    cursor += 2
                    continue
                cursor += 1
                if text[cursor - 1] == quote:
                    break
            mask(index, cursor)
            index = cursor
            continue
        index += 1
    return "".join(result)


def unconditional_cpp_code_mask(text: str) -> str:
    lexical = cpp_lexical_code_mask(splice_cpp_lines(text))
    result = list(lexical)
    conditional_depth = 0
    offset = 0
    directive_pattern = re.compile(
        r"^[ \t\v\f]*(?:#|%:)[ \t\v\f]*(if|ifdef|ifndef|elif|else|endif)\b"
    )
    lines = lexical.split("\n")
    for line_index, content in enumerate(lines):
        has_newline = line_index + 1 < len(lines)
        line = content + ("\n" if has_newline else "")
        directive = directive_pattern.match(line)
        directive_kind = directive.group(1) if directive is not None else ""
        line_is_conditional = conditional_depth != 0 or directive is not None
        if line_is_conditional:
            for position in range(offset, offset + len(line)):
                if result[position] not in ("\n", "\r"):
                    result[position] = " "
        if directive_kind in ("if", "ifdef", "ifndef"):
            conditional_depth += 1
        elif directive_kind == "endif" and conditional_depth != 0:
            conditional_depth -= 1
        offset += len(line)
    return "".join(result)


def macro_invocations(text: str, name: str) -> list[str]:
    phase_two_source = splice_cpp_lines(text)
    source = without_cpp_comments(phase_two_source)
    active_mask = unconditional_cpp_code_mask(phase_two_source)
    start_pattern = re.compile(rf"(?m)^[ \t]*{re.escape(name)}[ \t]*\(")
    invocations: list[str] = []
    for match in start_pattern.finditer(active_mask):
        opening = source.find("(", match.start(), match.end())
        depth = 0
        quote = ""
        index = opening
        while index < len(source):
            current = source[index]
            if quote:
                if current == "\\" and index + 1 < len(source):
                    index += 2
                    continue
                if current == quote:
                    quote = ""
            elif current in ('"', "'"):
                quote = current
            elif current == "(":
                depth += 1
            elif current == ")":
                depth -= 1
                if depth == 0:
                    invocations.append(source[opening + 1 : index])
                    break
            index += 1
    return invocations


def macro_arguments(body: str) -> list[str]:
    result: list[str] = []
    start = 0
    angle_depth = 0
    parenthesis_depth = 0
    quote = ""
    index = 0
    while index < len(body):
        current = body[index]
        if quote:
            if current == "\\" and index + 1 < len(body):
                index += 2
                continue
            if current == quote:
                quote = ""
        elif current in ('"', "'"):
            quote = current
        elif current == "<":
            angle_depth += 1
        elif current == ">":
            angle_depth = max(0, angle_depth - 1)
        elif current == "(":
            parenthesis_depth += 1
        elif current == ")":
            parenthesis_depth -= 1
        elif current == "," and angle_depth == 0 and parenthesis_depth == 0:
            result.append(re.sub(r"\s+", "", body[start:index]))
            start = index + 1
        index += 1
    result.append(re.sub(r"\s+", "", body[start:]))
    return result


def macro_definition(text: str, name: str) -> str | None:
    source = unconditional_cpp_code_mask(text)
    lines = source.split("\n")
    pattern = re.compile(
        rf"^[ \t]*#[ \t]*define[ \t]+{re.escape(name)}"
        rf"(?![A-Za-z0-9_])(?:\([^)]*\))?[ \t]*(.*)$"
    )
    definitions: list[str] = []
    line_index = 0
    while line_index < len(lines):
        logical_parts = [lines[line_index].rstrip()]
        while logical_parts[-1].endswith("\\") and line_index + 1 < len(lines):
            logical_parts[-1] = logical_parts[-1][:-1]
            line_index += 1
            logical_parts.append(lines[line_index].rstrip())
        logical_line = " ".join(logical_parts)
        match = pattern.match(logical_line)
        if match is None:
            line_index += 1
            continue
        definitions.append(match.group(1))
        line_index += 1
    if len(definitions) != 1:
        return None
    return re.sub(r"\s+", "", definitions[0])


def lexical_preprocessor_events(text: str) -> list[tuple[str, str]]:
    phase_two_source = splice_cpp_lines(text)
    lexical_mask = cpp_lexical_code_mask(phase_two_source)
    uncommented = without_cpp_comments(phase_two_source)
    directive_pattern = re.compile(
        r"(?m)^[ \t\v\f]*(?:#|%:)[ \t\v\f]*([A-Za-z_][A-Za-z0-9_]*)\b"
    )
    events: list[tuple[str, str]] = []
    for match in directive_pattern.finditer(lexical_mask):
        line_end = lexical_mask.find("\n", match.start())
        if line_end == -1:
            line_end = len(lexical_mask)
        masked_line = lexical_mask[match.start() : line_end]
        source_line = uncommented[match.start() : line_end]
        kind = match.group(1)
        if kind in ("define", "undef"):
            name_match = re.search(
                rf"\b{kind}\b[ \t\v\f]+([A-Za-z_][A-Za-z0-9_]*)",
                masked_line,
            )
            if name_match is not None:
                events.append((kind, name_match.group(1)))
            continue
        include_match = re.search(
            r'\binclude\b[ \t\v\f]+"([^"]+)"',
            source_line,
        )
        if include_match is not None:
            events.append((kind, include_match.group(1)))
        else:
            events.append((kind, ""))
    return events


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
        + f"query::CapabilityAdmission::{row.admission},"
        + f"query::FinalFailureProjection::{row.failure_projection}"
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


def check_provenance_owned_aliases(
    implementation: str, stable_binding_schema: str
) -> list[str]:
    errors: list[str] = []
    for path, source in (
        (PROVENANCE_QUERY_IMPLEMENTATION, implementation),
        (STABLE_BINDING_SCHEMA, stable_binding_schema),
    ):
        if 'R"' in splice_cpp_lines(source):
            errors.append(
                f"{relative(path)}: raw string token is forbidden in provenance architecture input"
            )
    expected_schema_arguments = (
        "ModuleDependencyProvenance",
        '"zom.query.module-dependency-provenance"',
        "ModuleKey",
        "CapabilityDemandResult<ModuleDependencyProvenance>",
        "ModuleDependencyProvenanceMap",
        "ModuleDependencyProvenanceProvider",
        "ModuleDependencyProvenanceVerifier",
        "SourceRejection<DiagnosticFact>|KeyRejection<BinderKeyFailure>",
        "R28_16A",
        "R28_16A",
        "R28_16A",
        "R28_16B",
        '"domain|key|dependency|cycle|provenance|witness|lineage"',
        '"MaterializedModuleGraphCapabilityTest.DependencyWitnessRejectsEdgeMutations"',
    )
    rows = [
        macro_arguments(invocation)
        for invocation in macro_invocations(
            stable_binding_schema, "ZOM_STABLE_BINDING_CAPABILITY_QUERY"
        )
    ]
    provenance_rows = [
        row for row in rows if row and row[0] == "ModuleDependencyProvenance"
    ]
    if len(provenance_rows) != 1 or len(provenance_rows[0]) != len(
        expected_schema_arguments
    ):
        errors.append(
            f"{relative(STABLE_BINDING_SCHEMA)}: provenance capability row drift"
        )
    else:
        row = provenance_rows[0]
        if row[3] != expected_schema_arguments[3]:
            errors.append(
                f"{relative(STABLE_BINDING_SCHEMA)}: provenance result type drift"
            )
        if "SourceRejection<DiagnosticFact>" not in row[7]:
            errors.append(
                f"{relative(STABLE_BINDING_SCHEMA)}: provenance source rejection drift"
            )
        if "KeyRejection<BinderKeyFailure>" not in row[7]:
            errors.append(
                f"{relative(STABLE_BINDING_SCHEMA)}: provenance key rejection drift"
            )
        if tuple(row) != expected_schema_arguments and not any(
            marker in error
            for error in errors
            for marker in (
                "provenance result type drift",
                "provenance source rejection drift",
                "provenance key rejection drift",
            )
        ):
            errors.append(
                f"{relative(STABLE_BINDING_SCHEMA)}: provenance capability row drift"
            )

    owned_assertions = macro_definition(
        implementation, "ZOM_R28_16A_SELECT_R28_16A"
    )
    expected_owned_assertions = (
        "static_assert("
        "zc::isSameType<"
        "zomlang::compiler::driver::module_graph_query::name##Query::Capability,"
        "zomlang::compiler::driver::module_graph_query::capabilityType>());"
        "static_assert("
        "zc::isSameType<"
        "zomlang::compiler::driver::module_graph_query::name##Query::FailureAlternatives,"
        "zomlang::compiler::query::CapabilityFailureList<"
        "zomlang::compiler::query::SourceRejection<"
        "zomlang::compiler::diagnostics::DiagnosticFact>,"
        "zomlang::compiler::query::KeyRejection<"
        "zomlang::compiler::binder::BinderKeyFailure>>>())"
    )
    if owned_assertions != expected_owned_assertions:
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance owned assertions drift"
        )

    macro_router = macro_definition(
        implementation, "ZOM_STABLE_BINDING_CAPABILITY_QUERY"
    )
    if macro_router != "ZOM_R28_16A_SELECT(descriptorTask,name,capabilityType);":
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance row router drift"
        )

    selector = macro_definition(implementation, "ZOM_R28_16A_SELECT")
    selector_expansion = macro_definition(
        implementation, "ZOM_R28_16A_SELECT_EXPAND"
    )
    if selector != "ZOM_R28_16A_SELECT_EXPAND(task,name,capabilityType)":
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance selector drift"
        )
    if selector_expansion != "ZOM_R28_16A_SELECT_##task(name,capabilityType)":
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance selector expansion drift"
        )

    inactive_selectors = (
        "ZOM_R28_16A_SELECT_M1",
        "ZOM_R28_16A_SELECT_M2",
        "ZOM_R28_16A_SELECT_M3",
        "ZOM_R28_16A_SELECT_M5",
    )
    for name in inactive_selectors:
        if macro_definition(implementation, name) != "":
            errors.append(
                f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance inactive selector drift"
            )

    schema_path = "zomlang/compiler/binder/stable-binding-schema.def"
    relevant_macros = (
        "ZOM_R28_16A_SELECT_R28_16A",
        *inactive_selectors,
        "ZOM_R28_16A_SELECT",
        "ZOM_R28_16A_SELECT_EXPAND",
        "ZOM_STABLE_BINDING_CAPABILITY_QUERY",
    )
    expected_transaction = [
        *(("define", name) for name in relevant_macros),
        ("include", schema_path),
        *(("undef", name) for name in reversed(relevant_macros)),
    ]
    events = lexical_preprocessor_events(implementation)
    unique_transaction_events = (
        all(events.count(("define", name)) == 1 for name in relevant_macros)
        and all(events.count(("undef", name)) == 1 for name in relevant_macros)
        and events.count(("include", schema_path)) == 1
    )
    if unique_transaction_events:
        try:
            transaction_begin = events.index(("define", relevant_macros[0]))
            transaction_end = events.index(
                ("undef", relevant_macros[0]), transaction_begin + 1
            )
            transaction = events[transaction_begin : transaction_end + 1]
        except ValueError:
            transaction = []
    else:
        transaction = []
    if transaction != expected_transaction:
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance directive transaction drift"
        )

    phase_two_implementation = splice_cpp_lines(implementation)
    lexical_mask = cpp_lexical_code_mask(phase_two_implementation)
    lines = lexical_mask.split("\n")
    first_define_line = next(
        (
            index
            for index, line in enumerate(lines)
            if re.match(
                r"^[ \t\v\f]*(?:#|%:)[ \t\v\f]*define[ \t\v\f]+"
                + re.escape(relevant_macros[0])
                + r"\b",
                line,
            )
        ),
        None,
    )
    final_undef_line = next(
        (
            index
            for index in range(len(lines) - 1, -1, -1)
            if re.match(
                r"^[ \t\v\f]*(?:#|%:)[ \t\v\f]*undef[ \t\v\f]+"
                + re.escape(relevant_macros[0])
                + r"\b",
                lines[index],
            )
        ),
        None,
    )
    directive_line = re.compile(r"^[ \t\v\f]*(?:#|%:)")
    if (
        first_define_line is None
        or final_undef_line is None
        or first_define_line > final_undef_line
        or any(
            line.strip(" \t\v\f") and directive_line.match(line) is None
            for line in lines[first_define_line : final_undef_line + 1]
        )
    ):
        errors.append(
            f"{relative(PROVENANCE_QUERY_IMPLEMENTATION)}: provenance directive block is not closed"
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
        *check_provenance_owned_aliases(
            files[PROVENANCE_QUERY_IMPLEMENTATION],
            STABLE_BINDING_SCHEMA.read_text(encoding="utf-8"),
        ),
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
                'ZOM_CAPABILITY(2, zomlang::compiler::example::FixtureCapability, '
                '"FixtureCapability", "zom.query.fixture-capability", Retained, '
                'FinalSealedSnapshot, SourceOrKey, '
                '"products/zomlang/compiler/example/query-fixture")',
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
                "struct FixtureCapability final {",
                "  static constexpr query::CapabilityDescriptorMetadata descriptor{",
                '      "FixtureCapability"_zcc, "zom.query.fixture-capability"_zcc,',
                "      query::RetentionClass::Retained, query::QueryCyclePolicy::Reject,",
                "      query::QueryCostClass::Linear,",
                "      query::CapabilityAdmission::FinalSealedSnapshot,",
                "      query::FinalFailureProjection::SourceOrKey};",
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
            "failure projection mismatch",
            {
                fixture_path: baseline[fixture_path].replace(
                    "query::FinalFailureProjection::SourceOrKey",
                    "query::FinalFailureProjection::None",
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
    provenance_schema = (
        "ZOM_STABLE_BINDING_CAPABILITY_QUERY("
        "ModuleDependencyProvenance, "
        '"zom.query.module-dependency-provenance", '
        "ModuleKey, CapabilityDemandResult<ModuleDependencyProvenance>, "
        "ModuleDependencyProvenanceMap, ModuleDependencyProvenanceProvider, "
        "ModuleDependencyProvenanceVerifier, "
        "SourceRejection<DiagnosticFact> | KeyRejection<BinderKeyFailure>, "
        "R28_16A, R28_16A, R28_16A, R28_16B, "
        '"domain|key|dependency|cycle|provenance|witness|lineage", '
        '"MaterializedModuleGraphCapabilityTest.DependencyWitnessRejectsEdgeMutations")'
    )
    owned_macro = (
        "#define ZOM_R28_16A_SELECT_R28_16A(name, capabilityType) \\\n"
        "  static_assert(zc::isSameType<"
        "zomlang::compiler::driver::module_graph_query::name##Query::Capability, "
        "zomlang::compiler::driver::module_graph_query::capabilityType>()); \\\n"
        "  static_assert(zc::isSameType<"
        "zomlang::compiler::driver::module_graph_query::name##Query::FailureAlternatives, "
        "zomlang::compiler::query::CapabilityFailureList<"
        "zomlang::compiler::query::SourceRejection<"
        "zomlang::compiler::diagnostics::DiagnosticFact>, "
        "zomlang::compiler::query::KeyRejection<"
        "zomlang::compiler::binder::BinderKeyFailure>>>() )\n"
    )
    inactive_selector_macros = (
        "#define ZOM_R28_16A_SELECT_M1(name, capabilityType)\n"
        "#define ZOM_R28_16A_SELECT_M2(name, capabilityType)\n"
        "#define ZOM_R28_16A_SELECT_M3(name, capabilityType)\n"
        "#define ZOM_R28_16A_SELECT_M5(name, capabilityType)\n"
    )
    selector_macro = (
        "#define ZOM_R28_16A_SELECT(task, name, capabilityType) \\\n"
        "  ZOM_R28_16A_SELECT_EXPAND(task, name, capabilityType)\n"
    )
    selector_expansion_macro = (
        "#define ZOM_R28_16A_SELECT_EXPAND(task, name, capabilityType) \\\n"
        "  ZOM_R28_16A_SELECT_##task(name, capabilityType)\n"
    )
    router_macro = (
        "#define ZOM_STABLE_BINDING_CAPABILITY_QUERY("
        "name, domain, keyType, resultType, capabilityType, producer, verifier, "
        "failureAlternatives, descriptorTask, providerTask, verifierTask, testTask, "
        "mutations, test) \\\n"
        "  ZOM_R28_16A_SELECT(descriptorTask, name, capabilityType);\n"
    )
    schema_include = '#include "zomlang/compiler/binder/stable-binding-schema.def"\n'
    cleanup_directives = (
        "#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY\n"
        "#undef ZOM_R28_16A_SELECT_EXPAND\n"
        "#undef ZOM_R28_16A_SELECT\n"
        "#undef ZOM_R28_16A_SELECT_M5\n"
        "#undef ZOM_R28_16A_SELECT_M3\n"
        "#undef ZOM_R28_16A_SELECT_M2\n"
        "#undef ZOM_R28_16A_SELECT_M1\n"
        "#undef ZOM_R28_16A_SELECT_R28_16A\n"
    )
    provenance_implementation = (
        owned_macro
        + inactive_selector_macros
        + selector_macro
        + selector_expansion_macro
        + router_macro
        + schema_include
        + cleanup_directives
    )

    def raw_string_decoy(value: str) -> str:
        return 'const char* decoy = R"ZOM(\n' + value + ')ZOM";\n'

    def conditionally_inactive(value: str) -> str:
        return "#if 0\n" + value + "#endif\n"

    def splice_conditionally_inactive(value: str) -> str:
        return "#i\\\nf 0\n" + value + "#endif\n"

    def splice_formed_comment(value: str) -> str:
        return "/\\\n*\n" + value + "*\\\n/\n"

    def digraph_conditionally_inactive(value: str) -> str:
        return "%:if 0\n" + value + "%:endif\n"

    def vertical_tab_conditionally_inactive(value: str) -> str:
        return "#\vif 0\n" + value + "#endif\n"

    def form_feed_conditionally_inactive(value: str) -> str:
        return "%:\fif 0\n" + value + "%:endif\n"

    def raw_string_splice_false_close(value: str) -> str:
        return (
            'const char* decoy = R"ZOM(\n'
            ")ZO\\\nM\"\n"
            + value
            + ')ZOM";\n'
        )

    if check_provenance_owned_aliases(
        provenance_implementation, provenance_schema
    ):
        raise RuntimeError("provenance alias self-test baseline failed")
    provenance_cases = (
        (
            "schema result type",
            provenance_implementation,
            provenance_schema.replace(
                "CapabilityDemandResult<ModuleDependencyProvenance>",
                "CapabilityDemandResult<OtherProvenance>",
                1,
            ),
            "provenance result type drift",
        ),
        (
            "schema source rejection",
            provenance_implementation,
            provenance_schema.replace(
                "SourceRejection<DiagnosticFact>",
                "SourceRejection<OtherFact>",
                1,
            ),
            "provenance source rejection drift",
        ),
        (
            "schema key rejection",
            provenance_implementation,
            provenance_schema.replace(
                "KeyRejection<BinderKeyFailure>",
                "KeyRejection<OtherKeyFailure>",
                1,
            ),
            "provenance key rejection drift",
        ),
        (
            "schema task",
            provenance_implementation,
            provenance_schema.replace("R28_16B", "M1", 1),
            "provenance capability row drift",
        ),
        (
            "schema comment decoy",
            provenance_implementation,
            provenance_schema.replace(
                "CapabilityDemandResult<ModuleDependencyProvenance>",
                "CapabilityDemandResult<OtherProvenance>",
                1,
            )
            + "\n/* "
            + provenance_schema
            + " */",
            "provenance result type drift",
        ),
        (
            "schema raw string only",
            provenance_implementation,
            raw_string_decoy(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema raw string splice false close",
            provenance_implementation,
            raw_string_splice_false_close(provenance_schema),
            "raw string token is forbidden",
        ),
        (
            "schema inactive row only",
            provenance_implementation,
            conditionally_inactive(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema splice-inactive row only",
            provenance_implementation,
            splice_conditionally_inactive(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema splice-comment row only",
            provenance_implementation,
            splice_formed_comment(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema digraph-inactive row only",
            provenance_implementation,
            digraph_conditionally_inactive(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema vertical-tab-inactive row only",
            provenance_implementation,
            vertical_tab_conditionally_inactive(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "schema form-feed-inactive row only",
            provenance_implementation,
            form_feed_conditionally_inactive(provenance_schema),
            "provenance capability row drift",
        ),
        (
            "owned assertion",
            provenance_implementation.replace(
                "name##Query::Capability", "name##Query::OtherCapability", 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned assertion comment decoy",
            provenance_implementation.replace(
                "name##Query::FailureAlternatives",
                "name##Query::OtherFailureAlternatives",
                1,
            )
            + "\n/* "
            + provenance_implementation
            + " */",
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned assertion string decoy",
            provenance_implementation.replace(
                "BinderKeyFailure", "OtherKeyFailure", 1
            )
            + '\nconst char* decoy = R"('
            + provenance_implementation
            + ')";',
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro raw string only",
            provenance_implementation.replace(
                owned_macro, raw_string_decoy(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro raw string splice false close",
            provenance_implementation.replace(
                owned_macro,
                raw_string_splice_false_close(owned_macro),
                1,
            ),
            provenance_schema,
            "raw string token is forbidden",
        ),
        (
            "owned macro inactive only",
            provenance_implementation.replace(
                owned_macro, conditionally_inactive(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro splice-inactive only",
            provenance_implementation.replace(
                owned_macro, splice_conditionally_inactive(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro splice-comment only",
            provenance_implementation.replace(
                owned_macro, splice_formed_comment(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro digraph-inactive only",
            provenance_implementation.replace(
                owned_macro, digraph_conditionally_inactive(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro vertical-tab-inactive only",
            provenance_implementation.replace(
                owned_macro,
                vertical_tab_conditionally_inactive(owned_macro),
                1,
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "owned macro form-feed-inactive only",
            provenance_implementation.replace(
                owned_macro, form_feed_conditionally_inactive(owned_macro), 1
            ),
            provenance_schema,
            "provenance owned assertions drift",
        ),
        (
            "row router",
            provenance_implementation.replace(
                "ZOM_R28_16A_SELECT(descriptorTask, name, capabilityType);",
                "ZOM_R28_16A_SELECT(providerTask, name, capabilityType);",
                1,
            ),
            provenance_schema,
            "provenance row router drift",
        ),
        (
            "row router raw string only",
            provenance_implementation.replace(
                router_macro, raw_string_decoy(router_macro), 1
            ),
            provenance_schema,
            "provenance row router drift",
        ),
        (
            "row router inactive only",
            provenance_implementation.replace(
                router_macro, conditionally_inactive(router_macro), 1
            ),
            provenance_schema,
            "provenance row router drift",
        ),
        (
            "selector",
            provenance_implementation.replace(
                "ZOM_R28_16A_SELECT_EXPAND(task, name, capabilityType)",
                "ZOM_R28_16A_SELECT_EXPAND(providerTask, name, capabilityType)",
                1,
            ),
            provenance_schema,
            "provenance selector drift",
        ),
        (
            "selector raw string only",
            provenance_implementation.replace(
                selector_macro, raw_string_decoy(selector_macro), 1
            ),
            provenance_schema,
            "provenance selector drift",
        ),
        (
            "selector inactive only",
            provenance_implementation.replace(
                selector_macro, conditionally_inactive(selector_macro), 1
            ),
            provenance_schema,
            "provenance selector drift",
        ),
        (
            "selector expansion raw string only",
            provenance_implementation.replace(
                selector_expansion_macro,
                raw_string_decoy(selector_expansion_macro),
                1,
            ),
            provenance_schema,
            "provenance selector expansion drift",
        ),
        (
            "selector expansion inactive only",
            provenance_implementation.replace(
                selector_expansion_macro,
                conditionally_inactive(selector_expansion_macro),
                1,
            ),
            provenance_schema,
            "provenance selector expansion drift",
        ),
        (
            "schema include comment decoy",
            provenance_implementation.replace(
                '#include "zomlang/compiler/binder/stable-binding-schema.def"',
                '/* #include "zomlang/compiler/binder/stable-binding-schema.def" */',
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include before definitions",
            schema_include
            + provenance_implementation.replace(schema_include, "", 1),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "inert transaction with disconnected live pieces",
            conditionally_inactive(provenance_implementation)
            + schema_include
            + provenance_implementation.replace(schema_include, "", 1),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "router undef before schema include",
            provenance_implementation.replace(
                schema_include,
                "#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY\n"
                + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "router undef in active conditional",
            provenance_implementation.replace(
                schema_include,
                "#if 1\n"
                "#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY\n"
                "#endif\n"
                + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "router undef in active else branch",
            provenance_implementation.replace(
                schema_include,
                "#if 0\n"
                "#else\n"
                "#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY\n"
                "#endif\n"
                + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "intervening include",
            provenance_implementation.replace(
                schema_include,
                '#include "mutates-provenance-router.h"\n' + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "intervening pragma",
            provenance_implementation.replace(
                schema_include,
                '#pragma pop_macro("ZOM_STABLE_BINDING_CAPABILITY_QUERY")\n'
                + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "intervening pragma operator",
            provenance_implementation.replace(
                schema_include,
                '_Pragma("pop_macro(\\\"ZOM_STABLE_BINDING_CAPABILITY_QUERY\\\")")\n'
                + schema_include,
                1,
            ),
            provenance_schema,
            "provenance directive block is not closed",
        ),
        (
            "schema include raw string only",
            provenance_implementation.replace(
                schema_include, raw_string_decoy(schema_include), 1
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include raw string splice false close",
            provenance_implementation.replace(
                schema_include,
                raw_string_splice_false_close(schema_include),
                1,
            ),
            provenance_schema,
            "raw string token is forbidden",
        ),
        (
            "schema include inactive only",
            provenance_implementation.replace(
                schema_include, conditionally_inactive(schema_include), 1
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include splice-inactive only",
            provenance_implementation.replace(
                schema_include,
                splice_conditionally_inactive(schema_include),
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include splice-comment only",
            provenance_implementation.replace(
                schema_include, splice_formed_comment(schema_include), 1
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include digraph-inactive only",
            provenance_implementation.replace(
                schema_include,
                digraph_conditionally_inactive(schema_include),
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include vertical-tab-inactive only",
            provenance_implementation.replace(
                schema_include,
                vertical_tab_conditionally_inactive(schema_include),
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
        (
            "schema include form-feed-inactive only",
            provenance_implementation.replace(
                schema_include,
                form_feed_conditionally_inactive(schema_include),
                1,
            ),
            provenance_schema,
            "provenance directive transaction drift",
        ),
    )
    for name, implementation, schema, expected in provenance_cases:
        errors = check_provenance_owned_aliases(
            implementation, schema
        )
        if not any(expected in error for error in errors):
            failures.append("provenance " + name)
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
        f"({len(fixtures) + len(provenance_cases) + len(boundary_fixtures)}/"
        f"{len(fixtures) + len(provenance_cases) + len(boundary_fixtures)})"
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
